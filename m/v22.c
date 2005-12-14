/*
 *   M - yet another soft modem
 *
 *   Copyright (c) 2005 Sasha Khapyorsky <sashak@alsa-project.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *   MA 02110-1301, USA
 *
 */

#include <stdlib.h>
#include <string.h>

#include "m.h"
#include "m_dsp.h"


#define V22_FRAG 256


struct scrambler {
	uint32_t data;
	unsigned int one_count;
};


struct v22_struct {
	struct modem *modem;
	unsigned count1, count2; /* for negotiation flow */
	unsigned samples_count;
	struct psk_demodulator dem;
	struct psk_modulator   mod;
	struct scrambler scram, descr;
	void (*run_func)(struct v22_struct *s, int16_t *in, int16_t *out, unsigned cnt);
	struct fbuf {	
		const int16_t *filter;
		unsigned size;
		unsigned index;
		int16_t *history;
	} rx_fbuf, tx_fbuf;
	int16_t rx_samples[V22_FRAG];
	int16_t tx_samples[V22_FRAG];
};


static void v22_run_dem(struct v22_struct *s, int16_t *in, int16_t *out, unsigned cnt);
static void v22_run_mod(struct v22_struct *s, int16_t *in, int16_t *out, unsigned cnt);
static void v22_run_both(struct v22_struct *s, int16_t *in, int16_t *out, unsigned cnt);


/* scrambler */

static unsigned scramble_bit(struct scrambler *s, unsigned bit)
{
	bit = bit^(s->data>>(14-1))^(s->data>>(17-1));
	if(s->one_count == 64) {
		bit ^= 1;
		s->one_count = 0;
	}
	if(bit&1)
		s->one_count++;
	else
		s->one_count = 0;
	s->data <<= 1;
	s->data |= bit&1;
	return bit&1;
}

static unsigned descramble_bit(struct scrambler *s, unsigned bit)
{
	if(bit&1)
		s->one_count++;
	else
		s->one_count = 0;
	s->data <<= 1;
	s->data |= bit&1;
	bit ^= (s->data>>14)^(s->data>>17);
	if (s->one_count == 64+1) {
		bit ^= 1;
		s->one_count = 1;
	}
	return bit&1;
}


/* get/put symbol stuff - negotiation flow is here too */

static unsigned v22_get_data_symbol(struct modem *m)
{
	struct scrambler *s = &((struct v22_struct *)(m->datapump.dp))->scram;
	return (scramble_bit(s, modem_get_bits(m, 1)) << 1) |
		scramble_bit(s, modem_get_bits(m, 1));
}

static void v22_put_data_symbol(struct modem *m, unsigned symbol)
{
	struct scrambler *d = &((struct v22_struct *)(m->datapump.dp))->descr;
	modem_put_bits(m, descramble_bit(d, (symbol>>1)&1), 1);
	modem_put_bits(m, descramble_bit(d, symbol&1), 1);
}

static unsigned v22_get_scram_symbol(struct modem *m)
{
	struct scrambler *s = &((struct v22_struct *)(m->datapump.dp))->scram;
	return (scramble_bit(s, 1) << 1)|scramble_bit(s, 1);
}

static void v22_put_scram_symbol(struct modem *m, unsigned symbol)
{
	struct v22_struct *s = (struct v22_struct *)m->datapump.dp;
	struct scrambler *d = &s->descr;
	unsigned bits;
	unsigned mask;
	
	bits = (descramble_bit(d, (symbol>>1)&1) << 1)|
		descramble_bit(d, symbol&1);
	mask = (1 << 2) - 1;

	if (bits == mask || s->count1 > 162) { /* bits: 600*0.270 sec */
		s->count1++;
		s->count2 += (bits != mask);
	}
	else
		s->count1 = s->count2 = 0;
	
	if (!s->modem->caller && s->count1 == 162)
		s->mod.get_symbol = v22_get_scram_symbol;

	if (s->count1 > 621) {	/* bits: 600*(0.270 + 0.765)sec */
		if(s->count2 < 4) { /* up to 3 errors */
			dbg("%d: v22 enters data state (err=%d).\n",
				s->samples_count, s->count2);
			s->dem.put_symbol = v22_put_data_symbol;
			s->mod.get_symbol = v22_get_data_symbol;
			modem_update_status(s->modem, STATUS_DP_CONNECT);
			s->count1 = 0;
		}
		else /* another chance */
			s->count1 = 164;
		s->count2 = 0;
	}
}

static unsigned v22_get_raw_symbol(struct modem *m)
{
	return 0x3;
}

static void v22_put_raw_symbol(struct modem *m, unsigned symbol)
{
	struct v22_struct *s = (struct v22_struct *)m->datapump.dp;
	unsigned mask = (1 << 2) - 1;

	if ((symbol&mask) == mask || s->count1 > 93) { /* bits: 600*0.155 sec */
		s->count1 ++;
		s->count2 += ((symbol&mask) != mask);
	}
	else
		s->count1 = s->count2 = 0;
	
	if (s->count1 > 366) {	/* bits: 600*(0.155 + 0.456)sec */
		if(s->count2 < 4) { /* up to 3 errors */
			dbg("%d: v22 enters scrambled state (err=%d).\n",
				s->samples_count, s->count2);
			s->dem.put_symbol = v22_put_scram_symbol;
			s->mod.get_symbol = v22_get_scram_symbol;
			s->run_func = v22_run_both;
			s->count1 = 0;
		}
		else /* another chance */
			s->count1 = 93;
		s->count2 = 0;
	}
}


/* fbuf (filter buffer) stuff */

static void fbuf_filter_samples(struct fbuf *f, int16_t *in, int16_t *out, unsigned count)
{
	int i;
	for (i = 0 ; i < count ; i++) {
		unsigned j, idx;
		int32_t sum = 0;
		f->history[f->index] = in[i];
		f->index = (f->index + 1)%f->size;
		idx = f->index;
		for( j = 0 ; j < f->size ; j++) {
			sum += f->filter[j]*f->history[idx];
			idx = (idx + 1)%f->size;
		}
		out[i] = sum >> COSTAB_SHIFT;
	}
}

static int fbuf_init(struct fbuf *f, const int16_t *filter, unsigned size)
{
	memset(f, 0, sizeof(*f));
	f->filter = filter;
	f->size = size;
	f->history = malloc(size*sizeof(*f->history));
	if (!f->history)
		return -1;
	return 0;
}

static void fbuf_free(struct fbuf *f)
{
	free(f->history);
}


/* v22 processors */

static void v22_run_dem(struct v22_struct *s, int16_t *in, int16_t *out, unsigned cnt)
{
	fbuf_filter_samples(&s->rx_fbuf, in, s->rx_samples, cnt);
	//log_data(27, s->rx_samples, cnt*sizeof(*s->rx_samples));
	psk_demodulate(&s->dem, s->rx_samples, cnt);
	memset(out, 0, cnt*sizeof(*out));
}

static void v22_run_mod(struct v22_struct *s, int16_t *in, int16_t *out, unsigned cnt)
{
	psk_modulate(&s->mod, s->tx_samples, cnt);
	fbuf_filter_samples(&s->tx_fbuf, s->tx_samples, out, cnt);
}

static void v22_run_both(struct v22_struct *s, int16_t *in, int16_t *out, unsigned cnt)
{
	fbuf_filter_samples(&s->rx_fbuf, in, s->rx_samples, cnt);
	psk_demodulate(&s->dem, s->rx_samples, cnt);
	psk_modulate(&s->mod, s->tx_samples, cnt);
	fbuf_filter_samples(&s->tx_fbuf, s->tx_samples, out, cnt);
}

static int v22_process(struct modem *m, int16_t *in, int16_t *out, unsigned int count)
{
	struct v22_struct *s = (struct v22_struct *)m->datapump.dp;
	void (*run_func)(struct v22_struct *, int16_t *, int16_t *, unsigned);
	int cnt;
	int ret = 0;

	trace("%d", count);
	while (ret < count) {
		cnt = V22_FRAG;
		if (cnt > count - ret)
			cnt = count - ret;
		run_func = s->run_func;
		run_func(s, in, out, cnt);
		ret += cnt;
		in  += cnt;
		out += cnt;
		s->samples_count += cnt;
	}

	return ret;
}


struct channel_config {
	unsigned fc;
	const int16_t *filter;
	unsigned filter_size;
	const int16_t *rx_fir; // FIXME: remove it
	unsigned rx_fir_size;
};

const static struct channel_config v22_high_ch = {
	2400, v22_rrc_2400, arrsize(v22_rrc_2400),
	v22_bp_2400, arrsize(v22_bp_2400)
};
const static struct channel_config v22_low_ch = {
	1200, v22_rrc_1200, arrsize(v22_rrc_1200),
	v22_bp_1200, arrsize(v22_bp_1200)
};

static void *v22_create(struct modem *m)
{
	struct v22_struct *s;
	const struct channel_config *rx, *tx;
	
	s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	memset(s, 0, sizeof(*s));
	s->modem = m;
	s->samples_count = m->samples_count;

	rx = m->caller ? &v22_high_ch : &v22_low_ch;
	tx = m->caller ? &v22_low_ch : &v22_high_ch;

	psk_demodulator_init(&s->dem, m, rx->fc, 600);
	psk_modulator_init(&s->mod, m, tx->fc, 600);
	if (fbuf_init(&s->rx_fbuf, rx->rx_fir, rx->rx_fir_size) < 0 ||
		fbuf_init(&s->tx_fbuf, tx->filter, tx->filter_size) < 0) {
		free(s);
		return NULL;
	}

	if (m->caller) {
		s->run_func = v22_run_dem;
		s->dem.put_symbol = v22_put_raw_symbol;
		s->mod.get_symbol = v22_get_scram_symbol;
	}
	else {
		s->run_func = v22_run_mod;
		s->dem.put_symbol = v22_put_scram_symbol;
		s->mod.get_symbol = v22_get_raw_symbol;
	}

	return s;
}

static void v22_delete(void *data)
{
	struct v22_struct *s = (struct v22_struct *)data;
	fbuf_free(&s->rx_fbuf);
	fbuf_free(&s->tx_fbuf);
	free(s);
}

const struct dp_operations v22_ops = {
	.create = v22_create,
	.delete = v22_delete,
	.process = v22_process,
};
