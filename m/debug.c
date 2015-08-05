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

#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "m.h"

#ifdef MODEM_DEBUG

const char *log_names[] = {
	[LOG_MESSAGES] = "messages.log",
	[LOG_RX_SAMPLES] = "rxsamples.data",
	[LOG_TX_SAMPLES] = "txsamples.data",
	[LOG_FSK_DATA] = "fsk.data",
	[LOG_PSK_DATA] = "psk.data",
};

static int log_fds[32] = { };

static char log_dir_name[64];

static int create_log_file(unsigned id)
{
	char file_name[256];
	int fd = -1;
	if (!log_dir_name[0]) {
		sprintf(log_dir_name, "logs.%d", getpid());
		if (mkdir(log_dir_name, 0755) < 0) {
			err("mkdir: %s\n", strerror(errno));
			return -1;
		}
	}
	sprintf(file_name, "%s/%02u-%s", log_dir_name, id,
		(id >= arrsize(log_names) || !log_names[id]) ?
		"misc.data" : log_names[id]);
	if ((fd = creat(file_name, 0644)) < 0) {
		err("creat: %s\n", strerror(errno));
		return fd;
	}
	return fd;
}

static int get_log_fd(unsigned id)
{
	if (id > arrsize(log_fds) || log_fds[id] < 0 ||
	    (!log_fds[id] && (log_fds[id] = create_log_file(id)) < 0))
		return -1;
	return log_fds[id];
}

int log_data(unsigned id, void *buf, unsigned size)
{
	int fd;
	if (!log_level)
		return 0;
	if ((fd = get_log_fd(id)) > 0)
		return write(fd, buf, size);
	return 0;
}

int log_samples(unsigned id, int16_t * buf, unsigned size)
{
	if (!log_level)
		return 0;
	return log_data(id, buf, size * sizeof(int16_t));
}

int log_printf(unsigned level, const char *fmt, ...)
{
	if (log_level || level <= debug_level) {
		char log_buf[4096];
		va_list args;
		int ret = 0;
		va_start(args, fmt);
		ret = vsnprintf(log_buf, sizeof(log_buf), fmt, args);
		va_end(args);
		if (log_level)
			log_data(LOG_MESSAGES, log_buf, ret);
		if (level <= debug_level)
			ret = fprintf(stderr, "%s", log_buf);
		return ret;
	}
	return 0;
}

#endif
