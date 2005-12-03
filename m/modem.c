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
#include <stdint.h>
#include <errno.h>
#include <termios.h>
#include <sys/poll.h>

#include "m.h"


extern const struct dp_operations dialer_ops;
extern const struct dp_operations detector_ops;
extern const struct dp_operations v21_ops;
extern const struct dp_operations v22_ops;

static int modem_start(struct modem *m);
static int modem_stop(struct modem *m);
static void modem_reset(struct modem *m);

const static char *dp_names[] = {
	[DP_NONE] = "none",
	[DP_DIALER] = "dialer",
	[DP_DETECTOR] = "detector",
	[DP_V21] = "v21",
	[DP_V22] = "v22",
};

const static struct dp_operations *dp_ops [] = {
	[DP_DIALER] = &dialer_ops,
	[DP_DETECTOR] = &detector_ops,
	[DP_V21] = &v21_ops,
	[DP_V22] = &v22_ops,
};

#define get_dp_name(id) (((id) < arrsize(dp_names) && dp_names[(id)]) ? \
		dp_names[(id)] : "unknown" )
#define get_dp_operations(id) ((id) < arrsize(dp_ops) ? dp_ops[(id)] : NULL )


/* FIXME: rx,tx fifos are needed */
static int modem_get_chars(struct modem *m, uint8_t *buf, unsigned count)
{
	//dbg("modem_get_chars: %d\n", count);
	return fifo_get(&m->tx_fifo, buf, count);
}

static int modem_put_chars(struct modem *m, uint8_t *buf, unsigned count)
{
	//dbg("modem_put_chars: %d\n", count);
	//return fifo_put(&m->rx_fifo, buf, count);
	return write(m->tty, buf, count);
}

static void drop_all(struct modem *m)
{
	trace();
	modem_stop(m);
	modem_reset(m);
}


static int modem_switch_datapump(struct modem *m, unsigned int dp_id)
{
	struct datapump old_datapump, new_datapump;
	void *dp;
	const struct dp_operations *dp_op;

	trace();

	old_datapump = m->datapump;

	if ( !dp_id ||
		!(dp_op = get_dp_operations(dp_id)) ||
		!(dp = dp_op->create(m))) {
		dbg("failed to create new dp \'%u\', drop all...\n", dp_id);
		return -1;
	}

	new_datapump.id = dp_id;
	new_datapump.name = get_dp_name(dp_id);
	new_datapump.dp = dp;
	new_datapump.op = dp_op;

	dbg("%u: switch datapamp: %s (%u) -> %s (%u)...\n", m->samples_count,
			old_datapump.name ? old_datapump.name : "none", old_datapump.id,
			new_datapump.name, new_datapump.id);

	if (old_datapump.id) {
		old_datapump.op->delete(old_datapump.dp);
	}
	m->datapump = new_datapump;
	m->process = m->datapump.op->process;
	return 0;
}

static void switch_datapump(struct modem *m)
{
	unsigned dp_id = m->next_dp_id;
	m->next_dp_id = 0;
	if(modem_switch_datapump(m, dp_id) < 0)
		drop_all(m);
}


static inline void samples_timer_update(struct modem *m, unsigned int count)
{
	m->samples_count += count;
#if 0 /* unused yet */
	if (m->samples_timer && m->samples_count >= m->samples_timer) {
		m->samples_timer = 0;
		m->samples_timer_func(m);
	}
#endif
}

static int modem_null_process(struct modem *m,
		int16_t *in, int16_t *out, unsigned count)
{
	memset(out, 0, count*sizeof(*out));
	return count;
}

static int modem_dev_process(struct modem *m)
{
	static int16_t buf_in[1024], buf_out[1024];
	int (*process)(struct modem *m,
			int16_t *in, int16_t *out, unsigned count);
	unsigned need_to_switch = 0;
	int ret, count;

	trace("%d:", m->samples_count);

	ret = m->driver->read(m, buf_in, arrsize(buf_in));
	if (ret <= 0) {
		dbg("device read = %d\n", ret);
		goto _error;
	}

	count = ret;
	process = m->process ? m->process : modem_null_process;
	if ((ret = process(m, buf_in, buf_out, count)) < 0) {
		err("process failed\n");
		goto _error;
	}

	if (ret < count) {
		memset(buf_out + ret, 0, (count - ret)*sizeof(int16_t));
		need_to_switch = 1;
	}

	ret = m->driver->write(m, buf_out, count);
	if (ret < 0) {
		err("device write failed.\n");
		goto _error;
	}

	samples_timer_update(m, count);

	log_rx_samples(buf_in,  count);
	log_tx_samples(buf_out, count);

	if (need_to_switch) {
		ret = modem_switch_datapump(m, m->next_dp_id);
		m->next_dp_id = 0;
	}

 _error:
	return ret;
}

int modem_run(struct modem *m)
{
	struct pollfd pollfd[2];
	unsigned int nfds;
	unsigned closed_tty_count = 0;
	struct pollfd *devfd, *ttyfd;
	int ret;

	trace();
	
	while(!m->killed) {
		nfds = 0;
		memset(pollfd, 0, sizeof(pollfd));
		pollfd[0].events = pollfd[1].events = POLLIN;
		devfd = ttyfd = NULL;
		if (m->started) {
			devfd = &pollfd[nfds++];
			devfd->fd = m->dev;
		}
		if (closed_tty_count)
			closed_tty_count--;
		else {
			ttyfd = &pollfd[nfds++];
			ttyfd->fd = m->tty;
		}

		ret = poll(pollfd, nfds, 1000);
		if (ret < 0) {
			if(errno == EINTR)
				continue;
			perror("poll");
			return ret;
		}
		if (ret == 0) { /* timeout */
			closed_tty_count = 0;
			continue;
		}
		if (devfd && (devfd->revents & POLLIN) &&
			(ret = modem_dev_process(m)) < 0)
				return ret;
		if (ttyfd && (ttyfd->revents & POLLIN) ) {
			static char tty_buf[4096];
			dbg("poll: ttyfd...\n");
			ret = read(ttyfd->fd, tty_buf, sizeof(tty_buf));
			if (ret < 0) {
				if(errno == -EIO) {
					dbg("closed tty - suspend poll.\n");
					closed_tty_count = 100;
					continue;
				}
				return ret;
			}
			dbg("got %d chars from tty.\n", ret);
		}
	}

	return 0;
}

int modem_set_hook(struct modem *m, unsigned hook_off)
{
	int ret;
	trace("hook_%s...", hook_off ? "off" : "on");
	if (m->hook_state == hook_off)
		return 0;
	if((ret = m->driver->ctrl(m, MDRV_CTRL_HOOK, hook_off)) < 0);
		return ret;
	m->hook_state = hook_off;
	return 0;
}

static int modem_start(struct modem *m)
{
	int ret;
	trace();
	if((ret = m->driver->start(m)) < 0 )
		return ret;
	m->samples_count = 0;
	m->started = 1;
	return 0;
}

static int modem_stop(struct modem *m)
{
	int ret;
	trace();
	if((ret = m->driver->stop(m)) < 0 )
		return ret;
	m->started = 0;
	return 0;
}

int modem_go(struct modem *m, enum DP_ID dp_id)
{
	int ret;
	trace("..");
	if((ret = modem_set_hook(m, 1)) < 0) {
		err("cannot set hook\n");
		goto _err;
	}
	if((ret = modem_start(m)) < 0) {
		err("cannot start modem\n");
		goto _err;
	}
	if((ret = modem_switch_datapump(m, dp_id)) < 0) {
		err("cannot switch dp\n");
		goto _err;
	}
	// FIXME: bit coder should be dependent on DP and DP state (CONNECT)
	async_bitque_reset(&m->rx_bitque);
	async_bitque_reset(&m->tx_bitque);
	m->get_bits = async_bitque_get_bits;
	m->put_bits = async_bitque_put_bits;
	fifo_reset(&m->rx_fifo);
	fifo_reset(&m->tx_fifo);
	m->get_chars = modem_get_chars;
	m->put_chars = modem_put_chars;
	return 0;
_err:
	modem_set_hook(m, 0);
	modem_stop(m);
	return ret;
}

int modem_dial(struct modem *m, const char *dial_string)
{
	int ret;
	trace("%s...", dial_string);
	m->caller = 1;
	strncpy(m->dial_string, dial_string, sizeof(m->dial_string));
	ret = modem_go(m, DP_DIALER);
	m->next_dp_id = DP_DETECTOR;
	return ret;
}


static void sregs_reset(struct modem *m)
{
	m->sregs[0] = 0; /* autoanswer rings count */
	m->sregs[1] = 0; /* rings count */
	m->sregs[2] = '+'; /* escape char */
	m->sregs[3] = '\r'; /* carriage return char */
	m->sregs[4] = '\n'; /* line feed char */
	m->sregs[5] = '\b'; /* backspace char */
	m->sregs[6] = 2; /* wait before dialing  */
	m->sregs[7] = 60; /* wait for carrier */
	m->sregs[8] = 2; /* pause for comma when dialing */
	m->sregs[9] = 6; /* carrier detect time 0.1s */
	m->sregs[10] = 14; /* carrier lost time 0.1s */
	m->sregs[11] = 100; /* dtmf dialing speed ms */
}

static void modem_reset(struct modem *m)
{
	trace();
	m->samples_count = 0;
	m->samples_timer = 0;
	m->samples_timer_func = NULL;
	m->next_dp_id = 0;
	if (m->datapump.id) {
		m->datapump.op->delete(m->datapump.dp);
		m->datapump = (struct datapump) {};
	}
	fifo_reset(&m->rx_fifo);
	fifo_reset(&m->tx_fifo);
	m->caller = 0;
	modem_set_hook(m, 0);
}

#define MODEM_NAME "big-m"
#define MODEM_DESC "Sasha'k softmodem"
#define MODEM_VERSION "0.000002 (or less)"

struct modem *modem_create(int tty, const char *drv_name)
{
	struct modem *m;
	const struct modem_driver *drv;

	if(!isatty(tty)) {
		err("file descriptor %d is not tty\n", tty);
		return NULL;
	}

	drv = find_modem_driver(drv_name);
	if(!drv) {
		err("no driver \'%s\' is found\n", drv_name);
		return NULL;
	}

	m = malloc(sizeof(*m));
	if(!m)
		return NULL;
	memset(m, 0, sizeof(*m));

	m->name = MODEM_NAME;
	m->tty = tty;
	m->tty_name = ttyname(tty);
	m->driver = drv;

	sregs_reset(m);

	tcgetattr(tty, &m->termios);

	m->dev = m->driver->open(m, modem_device_name);
	if (m->dev < 0) {
		err("cannot open device.\n");
		goto error;
	}

	info("%s - %s, version %s\ndriver is \'%s\', tty is \'%s\'\n",
			m->name, MODEM_DESC, MODEM_VERSION,
			m->driver->name, m->tty_name);

	return m;
  error:
	free(m);
	return NULL;
}

void modem_delete(struct modem *m)
{
	if(m->dev)
		m->driver->close(m);
	free(m);
}
