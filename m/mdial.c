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

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "m.h"

int make_terminal(void)
{
	int pty;
	if((pty = posix_openpt(O_RDWR)) < 0 ||
			grantpt(pty) < 0 || unlockpt(pty) < 0) {
		err("openpt failed: %s\n", strerror(errno));
		return -1;
	}
	return pty;
}

int setup_terminal(int tty)
{
	struct termios termios;
	int ret;

	fcntl(tty, F_SETFL, O_NONBLOCK);

	if (!isatty(tty))
		return 0;
	if ((ret = tcgetattr(tty, &termios)) < 0) {
		err("tcsetattr failed: %s\n", strerror(errno));
		return ret;
	}

	cfmakeraw(&termios);
	cfsetispeed(&termios, B115200);
	cfsetospeed(&termios, B115200);

	if ((ret = tcsetattr(tty, TCSANOW, &termios)) < 0)
		err("tcsetattr failed: %s\n", strerror(errno));
	return ret;
}

static struct modem *mdial_modem;

static void mark_killed(int signum)
{
	dbg("mark_killed: %d...\n", signum);
	mdial_modem->killed = signum;
}

static int mdial(void)
{
	struct termios termios;
	struct modem *m;
	int tty = 0;
	int ret;

	tcgetattr(tty, &termios);
	
	m = modem_create(tty, modem_driver_name);
	if (!m)
		return -1;

	mdial_modem = m;

	setup_terminal(tty);

	signal(SIGINT, mark_killed);
	signal(SIGTERM, mark_killed);
	
	ret = modem_dial(m, modem_phone_number);
	if (ret < 0) {
		dbg("cannot dial.\n");
		return ret;
	}
	
	ret = modem_run(m);

	modem_delete(m);

	tcsetattr(tty, TCSANOW, &termios);
	
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
