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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include "m.h"

struct file_device {
	int fd_in, fd_out;
};

static int file_read(struct modem *m, void *buf, unsigned count)
{
	struct file_device *f = m->device_data;
	int ret, fd = f->fd_in;
	//trace("%d", count);
	ret = read(fd, buf, count * 2);
	if (ret == 0)
		return -1;	/* eof simulation */
	return ret / 2;
}

static int file_write(struct modem *m, void *buf, unsigned count)
{
	struct file_device *f = m->device_data;
	int ret, fd = f->fd_out;
	//trace("%d", count);
	ret = write(fd, buf, count * 2);
	return ret / 2;
}

static int file_start(struct modem *m)
{
	trace();
	return 0;
}

static int file_stop(struct modem *m)
{
	trace();
	return 0;
}

static int file_ctrl(struct modem *m, unsigned cmd, unsigned long arg)
{
	trace("cmd=%u, arg=%lu", cmd, arg);
	return 0;
}

static int file_open(struct modem *m, const char *dev_name)
{
	char path[PATH_MAX];
	struct file_device *f;
	const char *file_name;
	char *p;

	trace();

	file_name = dev_name;
	if (!file_name)
		file_name = modem_device_name;
	f = malloc(sizeof(*f));
	if (!f) {
		err("no mem: %s\n", strerror(errno));
		return -1;
	}
	if (!strcmp(file_name, "-")) {
		f->fd_in = STDIN_FILENO;
		f->fd_out = STDOUT_FILENO;
	} else {
		f->fd_in = open(file_name, O_RDONLY);
		if (f->fd_in < 0) {
			err("cannot open \'%s\': %s\n", file_name, strerror(errno));
			free(f);
			return -1;
		}

		strncpy(path, file_name, sizeof(path));
		if ((p = strrchr(path, '.')))
			*p = '\0';
		snprintf(path + strlen(path), sizeof(path) - strlen(path), ".out");

		file_name = path;
		f->fd_out = creat(file_name, 0644);
		if (f->fd_out < 0) {
			err("cannot creat \'%s\': %s\n", file_name, strerror(errno));
			close(f->fd_in);
			free(f);
			return -1;
		}
	}
	m->device_data = f;
	return f->fd_in;
}

static int file_close(struct modem *m)
{
	struct file_device *f = m->device_data;
	trace();
	m->device_data = NULL;
	close(f->fd_in);
	close(f->fd_out);
	free(f);
	return 0;
}

const struct modem_driver file_driver = {
	.name = "file",
	.open = file_open,
	.close = file_close,
	.start = file_start,
	.stop = file_stop,
	.read = file_read,
	.write = file_write,
	.ctrl = file_ctrl,
};
