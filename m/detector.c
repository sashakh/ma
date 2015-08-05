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
#include <stdint.h>
#include <string.h>

#include "m.h"
#include "m_dsp.h"

#define DETECTOR_WAIT_TIME 10	/* in secs */

#define BLOCK_SHIFT 8
#define BLOCK_SIZE  1<<BLOCK_SHIFT

struct tonedet_state {
	const char *name;
	unsigned mask;
	unsigned int phase;
	unsigned int phinc;
	int32_t x, y;
};

struct detector_struct {
	struct modem *modem;
	unsigned int timeout;
	unsigned int num_samples;
	int32_t energy;
	struct tonedet_state det[8];
};

static inline void tonedet_reset(struct tonedet_state *d)
{
	d->phase = d->x = d->y = 0;
}

static inline void tonedet_init(struct tonedet_state *d, const char *name,
				unsigned mask, unsigned freq)
{
	d->name = name;
	d->mask = mask;
	d->phinc = freq * COSTAB_SIZE / SAMPLE_RATE;
	tonedet_reset(d);
}

static inline void tonedet_update(struct tonedet_state *d, int16_t sample)
{
	d->x += sample * m_cos(d->phase);
	d->y += sample * m_sin(d->phase);
	d->phase += d->phinc;
}

static inline int tonedet_evaluate(struct tonedet_state *d, int32_t energy)
{
	int32_t x = d->x >> COSTAB_SHIFT;
	int32_t y = d->y >> COSTAB_SHIFT;
	int32_t te = x * x + y * y;
	return (energy > 10000 && te > 20 * energy);
}

static inline void tonedet_debug_print(struct tonedet_state *d,
				       struct detector_struct *s)
{
	int32_t x, y;
	x = d->x >> COSTAB_SHIFT;
	y = d->y >> COSTAB_SHIFT;
	dbg("%s: %d: x = %d, y = %d, toneE = %d, E = %d\n",
	    d->name ? d->name : "",
	    s->modem->samples_count + s->num_samples,
	    x, y, x * x + y * y, s->energy);
}

static int detector_process(struct modem *m, int16_t * in, int16_t * out,
			    unsigned int count)
{
	struct detector_struct *s = (struct detector_struct *)m->datapump.dp;
	unsigned int i, j;
	for (i = 0; i < count; i++) {
		int32_t sample = in[i] >> BLOCK_SHIFT;
		s->energy += sample * sample;
		for (j = 0; j < arrsize(s->det) && s->det[j].name; j++)
			tonedet_update(&s->det[j], sample);
		if (s->num_samples++ >= BLOCK_SIZE) {
			unsigned detected = 0;
			for (j = 0; j < arrsize(s->det) && s->det[j].name; j++) {
				if (tonedet_evaluate(&s->det[j], s->energy))
					detected |= s->det[j].mask;
				//tonedet_debug_print(&s->det[j], s);
				tonedet_reset(&s->det[j]);
			}
			if (detected)
				modem_update_signals(s->modem, detected);
			s->num_samples = 0;
			s->energy = 0;
		}
		if (--s->timeout == 0) {
			modem_update_status(m, STATUS_DP_TIMEOUT);
			break;
		}
	}
	memset(out, 0, count * sizeof(int16_t));
	//trace("rest = %d, ret = %d", s->timeout, i);
	return count;
}

#if 0
static inline void add_signal_detector(struct detector_struct *s,
				       enum SIGNAL_ID n)
{
	struct signal_desc *const desc = &signal_descs[n];
	int i;
	for (i = 0; i < arrsize(s->det); n++) {
		if (s->det[i].name && s->det[i].freq == desc->freq) {
			s->det[i].mask |= MASK(n);
			break;
		}
	}
	if (i < arrsize(s->det) && !s->det[i].name)
		tonedet_init(&s->det[i], desc->name, MASK(n), desc->freq);
}
#endif

static void *detector_create(struct modem *m)
{
	struct detector_struct *s;
	unsigned int n, i;
	s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	memset(s, 0, sizeof(*s));
	s->modem = m;
	s->timeout = samples_in_sec(DETECTOR_WAIT_TIME);
	// FIXME: something more intelligent here: don't need duplicate the
	// same frequencies detection for different signals
	for (n = 0, i = 0; n < arrsize(signal_descs); n++) {
		if (m->signals_to_detect & MASK(n) && signal_descs[n].name) {
			tonedet_init(&s->det[i], signal_descs[n].name,
				     MASK(n), signal_descs[n].freq);
			if (++i >= arrsize(s->det))
				break;
		}
	}
	//tonedet_init(&s->det[0], "ansam", 2100);
	//tonedet_init(&s->det[1], "2245", 2245);
	return s;
}

static void detector_delete(void *data)
{
	struct detector_struct *s = (struct detector_struct *)data;
	free(s);
}

const struct dp_operations detector_ops = {
	.create = detector_create,
	.delete = detector_delete,
	.process = detector_process,
};
