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

struct v21_struct {
	struct modem *modem;
	struct fsk_demodulator dem;
	struct fsk_modulator mod;
};

static int v21_process(struct modem *m, int16_t * in, int16_t * out,
		       unsigned int count)
{
	struct v21_struct *s = (struct v21_struct *)m->datapump.dp;

	trace("%d", count);

	fsk_demodulate(&s->dem, in, count);
	fsk_modulate(&s->mod, out, count);

	return count;
}

#define V21_CHAN1_FREQ 1080	/* +/- 100 caller -> answer */
#define V21_CHAN2_FREQ 1750	/* +/- 100 answer -> caller */

static void *v21_create(struct modem *m)
{
	struct v21_struct *s;
	s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	memset(s, 0, sizeof(*s));
	s->modem = m;
	if (m->caller) {
		fsk_demodulator_init(&s->dem, m, 1850, 1650, 300);
		fsk_modulator_init(&s->mod, m, 1180, 980, 300);
	} else {
		fsk_demodulator_init(&s->dem, m, 1180, 980, 300);
		fsk_modulator_init(&s->mod, m, 1850, 1650, 300);
	}
	return s;
}

static void v21_delete(void *data)
{
	struct v21_struct *s = (struct v21_struct *)data;
	free(s);
}

const struct dp_operations v21_ops = {
	.create = v21_create,
	.delete = v21_delete,
	.process = v21_process,
};
