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

#include "m.h"

const static uint8_t _reversed_bits[] = {
	0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
	0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf,
};

#define reverse_bits(a) (_reversed_bits[(a)&0xf] << 4|_reversed_bits[(a)>>4])

void async_bitque_put_bits(struct modem *m, unsigned bits, unsigned num)
{
	struct async_bitque *q = &m->rx_bitque;
	q->data <<= num;
	q->data |= bits & ((1 << num) - 1);
	q->bits += num;
	while (q->bits && ((q->data >> (q->bits - 1)) & 1))
		q->bits--;
	if (q->bits >= 10) {
		uint8_t ch = (q->data >> (q->bits - 9)) & 0xff;
		if (!(q->data >> (q->bits - 10) & 1))
			dbg("async: no stop bit\n");
		q->bits -= 10;
		ch = reverse_bits(ch);
		dbg("put_char = %02x '%c'\n", ch, ch);
		if (m->put_chars)
			m->put_chars(m, &ch, 1);
	}
}

unsigned async_bitque_get_bits(struct modem *m, unsigned num)
{
	struct async_bitque *q = &m->tx_bitque;
	if (q->bits < num) {
		uint8_t ch;
		if (m->get_chars && m->get_chars(m, &ch, 1) > 0) {
			ch = reverse_bits(ch);
			q->data <<= 10;
			q->data |= ((ch << 1) | 1) & 0x1ff;
			q->bits += 10;
		}
		else {
			q->data <<= num - q->bits;
			q->data |= ((1 << (num - q->bits)) - 1);
			q->bits = num;
		}
	}
	q->bits -= num;
	return (q->data >> q->bits) & ((1 << num) - 1);
}
