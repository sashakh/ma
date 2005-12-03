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
 *  mdial.c - m dialer application
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "m.h"

static int mdial(void)
{
	struct modem *m;
	int ret;

	m = modem_create(0, modem_driver_name);
	if (!m)
		return -1;

	ret = modem_dial(m, modem_phone_number);
	if (ret < 0) {
		dbg("cannot dial.\n");
		return ret;
	}

	ret = modem_run(m);

	modem_delete(m);
	return ret;
}

int main(int argc, char *argv[])
{
	int ret;
	modem_driver_name = "alsa";
	modem_phone_number = "8479999";
	log_level = 1;
	ret = parse_cmdline(argc, argv);
	ret = mdial();
	return ret;
}
