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

/*
 *   m.c - 'm' means both "main" and "misc"
 */

#include <string.h>

#include "m.h"

/* global parameters */
unsigned int verbose_level = 1;
unsigned int debug_level = 0;
unsigned int log_level = 0;
const char *modem_driver_name = "alsa";
const char *modem_device_name = "modem:1";
const char *modem_phone_number = "0123456789";
const char *modulation_test = "v21";

/* drivers stuff */
extern const struct modem_driver alsa_driver;
extern const struct modem_driver file_driver;

const static struct modem_driver *drivers[] = {
	&alsa_driver,
	&file_driver,
};

const struct modem_driver *find_modem_driver(const char *name)
{
	int i;
	if (!name)
		return drivers[0];
	for (i = 0; i < arrsize(drivers); i++)
		if (!strcmp(drivers[i]->name, name))
			return drivers[i];
	return NULL;
}

/* fifo stuff */
unsigned int fifo_get(struct fifo *f, unsigned char *buf, unsigned int count)
{
	unsigned int cnt;
	if (count > f->head - f->tail)
		count = f->head - f->tail;
	cnt = count;
	if (cnt > sizeof(f->buf) - f->tail % sizeof(f->buf))
		cnt = sizeof(f->buf) - f->tail % sizeof(f->buf);
	memcpy(buf, f->buf + f->tail % sizeof(f->buf), cnt);
	memcpy(buf + cnt, f->buf, count - cnt);
	f->tail += count;
	return count;
}

unsigned int fifo_put(struct fifo *f, unsigned char *buf, unsigned int count)
{
	unsigned int cnt;
	if (count > sizeof(f->buf) - (f->head - f->tail))
		count = sizeof(f->buf) - (f->head - f->tail);
	cnt = count;
	if (cnt > sizeof(f->buf) - f->head % sizeof(f->buf))
		cnt = sizeof(f->buf) - f->head % sizeof(f->buf);
	memcpy(f->buf + f->head % sizeof(f->buf), buf, cnt);
	memcpy(f->buf, buf + cnt, count - cnt);
	f->head += count;
	return count;
}
