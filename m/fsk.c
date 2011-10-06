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

#include <string.h>

#include "m_dsp.h"

int fsk_demodulator_init(struct fsk_demodulator *f, struct modem *m,
			 unsigned freq0, unsigned freq1, unsigned bit_rate)
{
	memset(f, 0, sizeof(*f));
	f->modem = m;
	f->filter_len = SAMPLE_RATE / bit_rate < FSK_FILTER_LEN ?
	    SAMPLE_RATE / bit_rate : FSK_FILTER_LEN;
	f->shift = 0;
	while (f->filter_len >> f->shift)
		f->shift++;
	f->shift = (15 - COSTAB_SHIFT > f->shift) ?
	    0 : f->shift - (15 - COSTAB_SHIFT);
	f->phinc0 = freq0 * COSTAB_SIZE / SAMPLE_RATE;
	f->phinc1 = freq1 * COSTAB_SIZE / SAMPLE_RATE;
	f->bit_rate = bit_rate;
	f->bit_count = 0;
	f->bit = 0;
#ifdef FAST_FSK
	f->phase0 = f->phase1 = 0;
	f->x0 = f->y0 = f->x1 = f->y1 = 0;
#endif
	f->hist_index = 0;
	return 0;
}

int fsk_demodulate(struct fsk_demodulator *f, int16_t * buf, unsigned count)
{
	const unsigned len = f->filter_len;
	const unsigned shift = f->shift;
	const unsigned phinc0 = f->phinc0;
	const unsigned phinc1 = f->phinc1;
	unsigned idx = f->hist_index;
#ifdef FAST_FSK
	unsigned ph0 = f->phase0;
	unsigned ph1 = f->phase1;
#endif
	int32_t x0, y0, x1, y1, diff_energy;
	unsigned bit;
	int i;

	for (i = 0; i < count; i++) {
#ifdef FAST_FSK
		int16_t sample = f->history[idx % len];
		f->x0 -= sample * m_cos(ph0 - phinc0 * len);
		f->y0 -= sample * m_sin(ph0 - phinc0 * len);
		f->x1 -= sample * m_cos(ph1 - phinc1 * len);
		f->y1 -= sample * m_sin(ph1 - phinc1 * len);
		sample = buf[i] >> shift;
		f->x0 += sample * m_cos(ph0);
		f->y0 += sample * m_sin(ph0);
		f->x1 += sample * m_cos(ph1);
		f->y1 += sample * m_sin(ph1);
		f->history[idx % len] = sample;
		idx++;
		x0 = f->x0;
		y0 = f->y0;
		x1 = f->x1;
		y1 = f->y1;
		ph0 += phinc0;
		ph1 += phinc1;
#else
		unsigned ph0, ph1;
		int j;
		f->history[idx % len] = buf[i] >> shift;
		idx++;
		ph0 = ph1 = 0;
		x0 = y0 = x1 = y1 = 0;
		for (j = 0; j < len; j++) {
			unsigned n = (idx + j) % len;
			x0 += m_cos(ph0) * f->history[n];
			y0 += m_sin(ph0) * f->history[n];
			x1 += m_cos(ph1) * f->history[n];
			y1 += m_sin(ph1) * f->history[n];
			ph0 += phinc0;
			ph1 += phinc1;
		}
#endif
		x0 >>= COSTAB_SHIFT;
		y0 >>= COSTAB_SHIFT;
		x1 >>= COSTAB_SHIFT;
		y1 >>= COSTAB_SHIFT;

		diff_energy = x1 * x1 + y1 * y1 - x0 * x0 - y0 * y0;

#ifdef CHECK_SIGNAL
#define FSK_THRESHOLD 100000
		if (m_abs(diff_energy) < FSK_THRESHOLD) {
			no_signal_count++;
			/* no signal */ ;
			dbg();
		} else
			no_signal_count = 0;
#endif

		bit = (diff_energy > 0);
		f->bit_count += f->bit_rate;
		if (f->bit != bit) {
			/* 1: send bits: num = bit_count*bit_rate/SAMPLE_RATE */
			if (f->bit_count > SAMPLE_RATE / 2) {
				modem_put_bits(f->modem, f->bit, 1);
			}
			f->bit = bit;
			f->bit_count = 0;
		} else if (f->bit_count >= SAMPLE_RATE) {
			modem_put_bits(f->modem, f->bit, 1);
			f->bit_count -= SAMPLE_RATE;
		}
#define BIG_LOG 1
#ifdef BIG_LOG
		enum { id_s1 = 16, id_s2, id_s3, id_s4 };
#define LOG_VAL(s) { val = (s) ; log_data(id_##s, &(val), sizeof(val)); }
		//dbg("%u: (mark %d.%d, space %d.%d) diff_energy = %d\n",
		//              f->modem->samples_count+i,
		//              x1,y1, x0,y0, diff_energy);
		//{int16_t val;LOG_VAL(s1);LOG_VAL(s2);LOG_VAL(s3);LOG_VAL(s4);}
		log_data(LOG_FSK_DATA, &diff_energy, sizeof(diff_energy));
#endif
	}
#ifdef FAST_FSK
	f->phase0 = ph0 % COSTAB_SIZE;
	f->phase1 = ph1 % COSTAB_SIZE;
#endif
	f->hist_index = idx % len;
	return i;
}

int fsk_modulator_init(struct fsk_modulator *f, struct modem *m,
		       unsigned freq0, unsigned freq1, unsigned bit_rate)
{
	memset(f, 0, sizeof(*f));
	f->modem = m;
	f->bit_rate = bit_rate;
	f->phinc0 = freq0 * COSTAB_SIZE / SAMPLE_RATE;
	f->phinc1 = freq1 * COSTAB_SIZE / SAMPLE_RATE;
	f->phinc = f->phinc1;
	return 0;
}

int fsk_modulate(struct fsk_modulator *f, int16_t * buf, unsigned count)
{
	unsigned int phinc = f->phinc;
	unsigned int phase = f->phase;
	unsigned int bit_count = f->bit_count;
	unsigned int bit_rate = f->bit_rate;
	int i;
	for (i = 0; i < count; i++) {
		buf[i] = m_sin(phase);
		bit_count += bit_rate;
		if (bit_count >= SAMPLE_RATE) {
			unsigned bit = modem_get_bits(f->modem, 1);
			//dbg("%d: getbit()...\n", bit_count);
			bit_count -= SAMPLE_RATE;
			phinc = bit ? f->phinc1 : f->phinc0;
		}
		phase += phinc;
	}
	f->phase = phase % COSTAB_SIZE;
	f->phinc = phinc;
	f->bit_count = bit_count;
	return count;
}
