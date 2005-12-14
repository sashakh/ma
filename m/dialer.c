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
#include <ctype.h>

#include "m.h"
#include "m_dsp.h"

/*
 *      1209 1336 1477 1633
 *  697   1    2    3    A
 *  770   4    5    6    B
 *  852   7    8    9    C
 *  941   *    0    #    D
 */

#define PHINC(f) ((f)*COSTAB_SIZE/SAMPLE_RATE)

static const int16_t dtmf_phinc_low[]  = { PHINC(697), PHINC(770), PHINC(852), PHINC(941) };
static const int16_t dtmf_phinc_high[] = { PHINC(1209), PHINC(1336), PHINC(1477), PHINC(1633) };
static const char dtmf_trans[] = "123A456B789C*0#D";

/* dtmf stuff */
struct dtmfgen_state {
	const char *p;
	unsigned int duration;
	unsigned int pause_duration;
	unsigned int count;
	unsigned int phinc_low, phinc_high;
	unsigned int phase_low, phase_high;
};

void dtmfgen_init(struct dtmfgen_state *s, const char *dial_string)
{
	memset(s, 0, sizeof(*s));
	s->p = dial_string;
	s->duration = samples_in_msec(200);
	s->pause_duration = samples_in_msec(100);
}

static int dtmfgen_process(struct dtmfgen_state *s, int16_t *buf, unsigned count)
{
	int i;
	for(i = 0 ; i < count ; i++) {
		if (s->count == 0) {
			unsigned idx;
			const char *p = strchr(dtmf_trans, *s->p);
			if (!p || !*p)
				break;
			s->p++;
			dbg("dtmfgen: %c...\n", *p);
			idx = p - dtmf_trans;
			s->phase_low = s->phase_high = 0;
			s->phinc_low = dtmf_phinc_low[idx/4];
			s->phinc_high = dtmf_phinc_high[idx%4];
			s->count = s->duration;
		}
		else if (s->count == s->pause_duration) {
			s->phase_low = s->phase_high = 0;
			s->phinc_low = s->phinc_high = 0;
		}
		buf[i] = (m_sin(s->phase_low) + m_sin(s->phase_high));
		s->phase_low += s->phinc_low;
		s->phase_high += s->phinc_high;
		s->count--;
	}
	return i;
}

/*
 * detector part (empty yet)
 */

struct detector_state {
	void *foo;
	int count;
};

static void detector_init(struct detector_state *s, int count)
{
	s->count = count;
}

static int detector_process(struct detector_state *s, int16_t *buf, unsigned int count)
{
	int ret = s->count > count ? count : s->count;
	s->count -= count;
	if (s->count < 0)
		s->count = 0;
	return ret;
}


/*
 * Dialer stuff
 *
 */

enum dialer_states {
	STATE_WAIT, STATE_DIAL, STATE_FINISHED
};

#ifdef MODEM_DEBUG
static const char *dialer_state_names[] = {
	"WAIT","DIAL","FINISHED"
};
#define STATE_NAME(name) dialer_state_names[name]
#endif

struct dialer_struct {
	enum dialer_states state;
	const char *d_ptr;
	struct dtmfgen_state dtmfgen;
	struct detector_state detector ;
};


static int dialer_process(struct modem *m, int16_t *in, int16_t *out, unsigned int count)
{
	struct dialer_struct *s = (struct dialer_struct *)m->datapump.dp;
	enum dialer_states new_state;
	const char *p;
	int ret = 0;
	
	switch (s->state) {
	case STATE_WAIT:
		ret = detector_process(&s->detector, in, count);
		memset(out, 0, ret*sizeof(int16_t));
		break;
	case STATE_DIAL:
		ret = dtmfgen_process(&s->dtmfgen, out, count);
		break;
	default:
		return -1;
	}
	if (ret == count)
		return count;
	if (ret < 0)
		return ret;

	memset(out + ret, 0, (count-ret)*sizeof(int16_t));
	if (s->state == STATE_DIAL)
		p = s->d_ptr = s->dtmfgen.p;
	else
		p = s->d_ptr++;
	if (*p == '\0') {
		dbg("dialer finished\n");
		m->next_dp_id = DP_DETECTOR;
		new_state = STATE_FINISHED;
	}
	else if ( tolower(*p) == 'w' || *p == ',') {
		unsigned int pause_time = m->sregs[8] > 0 ? m->sregs[8] : 2;
		new_state = STATE_WAIT;
		detector_init(&s->detector, samples_in_sec(pause_time));
	}
	else {
		new_state = STATE_DIAL;
		dtmfgen_init(&s->dtmfgen, p);
	}
	dbg("dialer state: %s -> %s\n",
		STATE_NAME(s->state), STATE_NAME(new_state));
	s->state = new_state;
	return count;
}

static void *dialer_create(struct modem *m)
{
	struct dialer_struct *s;
	unsigned int wait_before_dial;
	s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	memset(s, 0, sizeof(*s));
	s->d_ptr = m->dial_string;
	s->state = STATE_WAIT;
	wait_before_dial = m->sregs[6] > 0 ? m->sregs[6] : 2;
	detector_init(&s->detector, samples_in_sec(wait_before_dial));
	dtmfgen_init(&s->dtmfgen, m->dial_string);
	return s;
}

static void dialer_delete(void *data)
{
	struct dialer_struct *s = (struct dialer_struct *)data;
	free(s);
}

const struct dp_operations dialer_ops = {
	.create = dialer_create,
	.delete = dialer_delete,
	.process = dialer_process,
};
