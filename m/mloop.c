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
 *  mloop.c - modem loop tester main
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "m.h"

static int two_modem_test(struct modem *m1, struct modem *m2)
{
#if 0
	static int16_t ibuf[4096], obuf[4096];
	int count = 1;
#endif
	m1->caller = 1;
	m2->caller = 0;
	while (1) {
#if 0
		modem_process(m1, ibuf, obuf, count);
		modem_process(m2, obuf, ibuf, count);
#endif
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct modem *ma, *mb;

	modem_driver_name = "file";
	modem_device_name = "/dev/null";
	parse_cmdline(argc, argv);

	ma = modem_create(NULL, modem_driver_name);
	mb = modem_create(NULL, modem_driver_name);
	if (!ma || !mb)
		exit(1);

	two_modem_test(ma, mb);

	modem_delete(ma);
	modem_delete(mb);
	return 0;
}
