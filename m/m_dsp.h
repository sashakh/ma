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

#ifndef __M_DSP_H__
#define __M_DSP_H__

#include <stdint.h>

#include "m.h"
#include "m_tables.h"

#define m_cos(x) ((costab)[(x)%(COSTAB_SIZE)])
#define m_sin(x) (m_cos((x)+(COSTAB_SIZE)*3/4))

#define m_abs(x) ((x) < 0 ? -(x) : (x))

/*
 * FSK stuff
 */

#define FSK_FILTER_LEN 40

#define FAST_FSK 1

struct fsk_demodulator {
	struct modem *modem;
	unsigned filter_len;
	unsigned int shift;
	unsigned int phinc0;
	unsigned int phinc1;
	unsigned int bit_rate;
	unsigned int bit;
	unsigned int bit_count;
#ifdef FAST_FSK
	unsigned int phase0;
	unsigned int phase1;
	int32_t x0, y0, x1, y1;
#endif
	unsigned hist_index;
	int16_t history[FSK_FILTER_LEN];
};

struct fsk_modulator {
	unsigned int phase;
	unsigned int phinc;
	unsigned int bit_rate;
	unsigned int bit_count;
	unsigned int phinc0, phinc1;
	struct modem *modem;
};

extern int fsk_demodulator_init(struct fsk_demodulator *f, struct modem *m,
				unsigned freq0, unsigned freq1,
				unsigned bit_rate);
extern int fsk_demodulate(struct fsk_demodulator *f, int16_t * buf,
			  unsigned count);
extern int fsk_modulator_init(struct fsk_modulator *f, struct modem *m,
			      unsigned freq0, unsigned freq1,
			      unsigned bit_rate);
extern int fsk_modulate(struct fsk_modulator *f, int16_t * buf, unsigned count);

/*
 * PSK stuff
 */

#define PSK_FILTER_LEN 256

struct psk_demodulator {
	unsigned int shift;
	unsigned filter_len;
	unsigned int phinc;
	unsigned hist_index;
	int16_t history[PSK_FILTER_LEN];
	unsigned int symbol;
	unsigned int symbol_rate;
	unsigned int symbol_count;
	struct modem *modem;
	void (*put_symbol) (struct modem * m, unsigned symbol);
};

struct psk_modulator {
	unsigned int phase;
	unsigned int phinc;
	unsigned int symbol_rate;
	unsigned int symbol_count;
	struct modem *modem;
	unsigned int (*get_symbol) (struct modem * m);
};

extern int psk_demodulator_init(struct psk_demodulator *p, struct modem *m,
				unsigned freq, unsigned symbol_rate);
extern int psk_demodulate(struct psk_demodulator *p, int16_t * buf,
			  unsigned int count);
extern int psk_modulator_init(struct psk_modulator *p, struct modem *m,
			      unsigned freq, unsigned symbol_rate);
extern int psk_modulate(struct psk_modulator *p, int16_t * buf,
			unsigned int count);

#endif /* __M_DSP_H__ */
