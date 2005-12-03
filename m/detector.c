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

#define DETECTOR_WAIT_TIME 10 /* in secs */

#define BLOCK_SHIFT 8
#define BLOCK_SIZE  1<<BLOCK_SHIFT


struct detector_struct;

struct tonedet_state {
	const char *name;
	unsigned int phase;
	unsigned int phinc;
	int32_t x, y;
};

struct detector_struct {
	struct modem *modem;
	unsigned int timeout;
	unsigned int num_samples;
	int32_t energy;
	struct tonedet_state det[2];
};


static inline void tonedet_init(struct tonedet_state *s, const char *name, unsigned freq)
{
	s->name = name;
	s->phinc = freq*COSTAB_SIZE/SAMPLE_RATE;
	s->phase = s->x = s->y = 0;
}

static inline void tonedet_reset(struct tonedet_state *s)
{
	s->phase = s->x = s->y = 0;
}

static inline void tonedet_update(struct tonedet_state *s, int16_t sample)
{
	s->x += sample*m_cos(s->phase);
	s->y += sample*m_sin(s->phase);
	s->phase += s->phinc;
}

static inline int tonedet_evaluate(struct tonedet_state *s, int32_t energy)
{
	int32_t x = s->x >> COSTAB_SHIFT;
	int32_t y = s->y >> COSTAB_SHIFT;
	int32_t te = x*x + y*y;
	return (energy > 10000 && te > 20*energy);
}


static inline void detectors_evaluate(struct detector_struct *s)
{
	int i, ret;
	for (i = 0 ; i < arrsize(s->det) ; i++) {
		struct tonedet_state *d = &s->det[i];
		ret = tonedet_evaluate(d, s->energy);
		{ int32_t x,y;
		x = d->x >> COSTAB_SHIFT;
		y = d->y >> COSTAB_SHIFT;
		dbg("%s: %d: x = %d, y = %d, toneE = %d, E = %d%s\n",
			d->name ? d->name : "",
			s->modem->samples_count + s->num_samples,
			x, y, x*x+y*y, s->energy, (ret) ? ". Detected!" : "");
		}
		if (ret) /* update detected mask */ ;
		tonedet_reset(&s->det[i]);
	}
	s->energy = 0;
}
	
static int detector_process(struct modem *m, int16_t *in, int16_t *out, unsigned int count)
{
	struct detector_struct *s = (struct detector_struct *)m->datapump.dp;
	int i, j;
	for ( i = 0 ; i < count ; i++ ) {
		int32_t sample = in[i] >> BLOCK_SHIFT;
		if(s->timeout == 0)
			break;
		s->energy += sample*sample;
		for (j = 0 ; j < arrsize(s->det) ; j++)
			tonedet_update(&s->det[j], sample);
		if(s->num_samples++ >= BLOCK_SIZE) {
			detectors_evaluate(s);
			s->num_samples = 0;
		}
		s->timeout--;
	}
	memset(out, 0, count*sizeof(int16_t));
	trace("rest = %d, ret = %d", s->timeout, i);
#if 1
	if (i < count)
		m->next_dp_id = DP_V21;
#endif
	return i;
}

static void *detector_create(struct modem *m)
{
	struct detector_struct *s;
	s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	memset(s, 0, sizeof(*s));
	s->modem = m;
	s->timeout = samples_in_sec(DETECTOR_WAIT_TIME);
	tonedet_init(&s->det[0], "ansam", 2100);
	tonedet_init(&s->det[1], "2245", 2245);
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
