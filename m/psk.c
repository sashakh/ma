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

/*
 *    V22 symbols encoding:
 * 
 *    00  0    +90
 *    01  -      0
 *    11  1   +270
 *    10  -   +180
 */
const unsigned int qpsk_symbols_A[] = { 1, 0, 2, 3 };
const unsigned int qpsk_phases_A[] = { 1, 0, 2, 3 };

const unsigned int qpsk_symbols_C[] = { 2, 3, 1, 0 };
const unsigned int qpsk_phases_C[] = { 3, 2, 0, 1 };

#define qpsk_symbols qpsk_symbols_A
#define qpsk_phases qpsk_phases_A


int psk_demodulator_init(struct psk_demodulator *p, struct modem *m,
		unsigned freq, unsigned symbol_rate)
{
	int i;

	memset(p, 0, sizeof(*p));

	p->modem = m;
	p->filter_len = SAMPLE_RATE*2/symbol_rate < PSK_FILTER_LEN ?
		SAMPLE_RATE*2/symbol_rate : PSK_FILTER_LEN ;
	p->phinc = freq * COSTAB_SIZE / SAMPLE_RATE;
	p->hist_index = 0;

	i = p->filter_len/2;
	while(i) {
		p->shift++;
		i >>= 1;
	}
	p->shift = (15-COSTAB_SHIFT > p->shift) ? 0 : p->shift - (15-COSTAB_SHIFT);
	p->symbol_rate = symbol_rate;
	return 0;
}


int psk_demodulate(struct psk_demodulator *p, int16_t *buf, unsigned count)
{
	const unsigned int phinc = p->phinc;
	const unsigned len = p->filter_len;
	const unsigned shift = p->shift;
	unsigned idx = p->hist_index;
	unsigned int symbol;
	int i, j;
	
	for(i = 0 ; i < count ; i++) {
		int16_t d0; // debug
		int32_t x0,x1,y0,y1,x,y;
		unsigned ph, ph0, ph1;
		unsigned idx0, idx1;

		p->history[idx%len] = buf[i] >> shift;
		idx = (idx + 1)%len;

		ph0 = 0;
		ph1 = phinc*len/2;
		idx0 = idx;
		idx1 = (idx + len/2)%len;
		x0 = x1 = y0 = y1 = 0;
		for ( j = 0 ; j < len/2 ; j++) {
			x0 += m_cos(ph0) * p->history[idx0];
			y0 -= m_sin(ph0) * p->history[idx0];
			x1 += m_cos(ph1) * p->history[idx1];
			y1 -= m_sin(ph1) * p->history[idx1];
			ph0 += phinc;
			ph1 += phinc;
			idx0 = (idx0 + 1)%len;
			idx1 = (idx1 + 1)%len;
		}
		x0 >>= COSTAB_SHIFT;
		x1 >>= COSTAB_SHIFT;
		y0 >>= COSTAB_SHIFT;
		y1 >>= COSTAB_SHIFT;
		
		x = x0*x1 + y0*y1;
		y = x0*y1 - y0*x1;

		// FIXME: when no signal - result is mess
#if 1
		x = x - y;
		y = x + 2*y;
		x >>= 16; //COSTAB_SHIFT;
		y >>= 16; //COSTAB_SHIFT;
		//d0 = x<<3; log_data(16, &d0, sizeof(d0));
		//d0 = y<<3; log_data(17, &d0, sizeof(d0));
		if (x*y > 0)
			ph = (x > 0) ? 0 : 2;
		else
			ph = (y > 0) ? 1 : 3;
#else
		if (x && (y/x) == 0)
			ph = (x>0) ? 0 : 2 ;
		else
			ph = (y>0) ? 1 : 3 ;
#endif

		//d0 = ph<<13 ; log_data(22, &d0, sizeof(d0));
		//dbg("%u: x = %d, y = %d ; phase = %d\n",
		//	p->modem->samples_count + i, x, y, ph);

		symbol = qpsk_symbols[ph];
#define READY_TO_DECODE_BITS 1
#ifdef READY_TO_DECODE_BITS
		p->symbol_count += p->symbol_rate;
		if (p->symbol != symbol) {
			if (p->symbol_count > SAMPLE_RATE/2 && p->put_symbol) {
				p->put_symbol(p->modem, p->symbol);
				//dbg("%u: put_symbol: %01x\n",
				//	p->modem->samples_count, p->symbol&0x3);
			}
			p->symbol = symbol;
			p->symbol_count = 0;
		}
		else if (p->symbol_count >= SAMPLE_RATE) {
			if(p->put_symbol) {
				p->put_symbol(p->modem, p->symbol);
				//dbg("%u: put_symbol: %01x\n",
				//	p->modem->samples_count, p->symbol&0x3);
			}
			p->symbol_count -= SAMPLE_RATE;
		}
#endif
	}
	
	p->hist_index = idx % len;
	return i;
}


int psk_modulator_init(struct psk_modulator *p, struct modem *m,
		unsigned freq, unsigned symbol_rate)
{
	memset(p, 0, sizeof(*p));
	p->modem = m;
	p->symbol_rate = symbol_rate;
	p->phinc = freq*COSTAB_SIZE/SAMPLE_RATE;
	return 0;
}


int psk_modulate(struct psk_modulator *p, int16_t *buf, unsigned count)
{
	unsigned int phinc = p->phinc;
	unsigned int phase = p->phase;
	unsigned int symbol_count = p->symbol_count;
	unsigned int symbol_rate = p->symbol_rate;
	int i;
	for (i = 0 ; i < count ; i++) {
		buf[i] = m_cos(phase) >> 1; // FIXME: >>1
		phase += phinc;
		symbol_count += symbol_rate;
		if (symbol_count >= SAMPLE_RATE) {
			unsigned int symbol = 0xf;
			if(p->get_symbol)
				symbol = p->get_symbol(p->modem);
			//dbg("%d: getsymbol() = %d...\n",symbol_count,symbol);
			symbol_count -= SAMPLE_RATE;
			phase += qpsk_phases[symbol&3]*COSTAB_SIZE/4;
		}
	}
	p->phase = phase%COSTAB_SIZE;
	p->phinc = phinc;
	p->symbol_count = symbol_count;
	return count;
}
