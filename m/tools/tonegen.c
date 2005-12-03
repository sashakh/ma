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

/* simle tone generator */

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define SAMP_RATE (8000)
#define AMP (1<<14)

//#define FREQ 2225
#define FREQ (1200*2)

struct tonegen_state {
	unsigned count;
	double phase;
};

int tone_generate(struct tonegen_state *s, int16_t * buf, unsigned count)
{
	unsigned i;
	double phase = s->phase;
	for (i = 0; i < count; i++) {
#if 0
		if (!(s->count % 160))
			phase += M_PI / 2.;
		if (!(s->count % 320))
			phase += M_PI / 2.;
		if (!(s->count % 640))
			phase += M_PI / 2.;
#endif
#if 0
		if (!(s->count % 13))
			phase += M_PI / 2.;
#endif
		fprintf(stderr, "%d: phase = %f\n", s->count, phase);
		buf[i] = (int16_t) (AMP * sin(phase));
		s->count++;
		phase += M_PI * 2 * FREQ / SAMP_RATE;
	}
	//s->phase = phase/(M_PI*2);
	s->phase = phase;
	return count;
}

int main()
{
	int n = 64;
	struct tonegen_state tonegen;
	memset(&tonegen, 0, sizeof(tonegen));
	while (n--) {
		int16_t buf[256];
		tone_generate(&tonegen, buf, 256);
		write(1, buf, sizeof(buf));
	}
	return 0;
}
