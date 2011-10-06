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
 *  mtest.c - m test application
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "m.h"

const static char *known_modulation_tests[] = {
	[DP_DETECTOR] = "detector",
	[DP_V21] = "v21",
	[DP_V22] = "v22",
	[DP_LAST] = NULL,
};

static int find_modulation_test(const char *name)
{
	int i;
	if (*name == '?') {
		info("Known modulation tests:");
		for (i = 0; i < DP_LAST; i++)
			if (known_modulation_tests[i])
				info(" %s", known_modulation_tests[i]);
		info("\n");
		return -1;
	}
	for (i = 0; i < DP_LAST; i++)
		if (known_modulation_tests[i] &&
		    !strcmp(known_modulation_tests[i], name))
			return i;
	err("unknown modulation test: '%s'\n", modulation_test);
	return -1;
}

static int mtest(unsigned dp_id)
{
	struct modem *m;
	int ret;

	m = modem_create(0, modem_driver_name);
	if (!m)
		return -1;

	m->caller = 1;
	m->signals_to_detect |= MASK(SIGNAL_2100) | MASK(SIGNAL_ANSAM) |
	    MASK(SIGNAL_2225) | MASK(SIGNAL_2245);

	ret = modem_go(m, dp_id);
	if (ret < 0) {
		dbg("cannot go with modem.\n");
		return ret;
	}

	ret = modem_run(m);

	modem_delete(m);
	return ret;
}

int main(int argc, char *argv[])
{
	unsigned dp_id = 0;
	int ret;
	modulation_test = "detector";
	modem_driver_name = "file";
	modem_device_name = "samples.in";
	debug_level = 1;
	log_level = 1;
	ret = parse_cmdline(argc, argv);
	ret = find_modulation_test(modulation_test);
	if (ret < 0) {
		return ret;
	}
	dp_id = ret;
	ret = mtest(dp_id);
	return ret;
}
