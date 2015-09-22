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

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/poll.h>

#include "m.h"

extern const struct dp_operations dialer_ops;
extern const struct dp_operations detector_ops;
extern const struct dp_operations v21_ops;
extern const struct dp_operations v22_ops;

static int modem_start(struct modem *m);
static int modem_stop(struct modem *m);
static void modem_reset(struct modem *m);

const static char *modem_status_names[] = {
	[STATUS_NONE] = "none",
	[STATUS_CONNECTING] = "connecting",
	[STATUS_DP_CONNECT] = "dp connect",
	[STATUS_DP_TIMEOUT] = "dp timeout",
};

const static char *dp_names[] = {
	[DP_NONE] = "none",
	[DP_DIALER] = "dialer",
	[DP_DETECTOR] = "detector",
	[DP_V21] = "v21",
	[DP_V22] = "v22",
};

const static struct dp_operations *dp_ops[] = {
	[DP_DIALER] = &dialer_ops,
	[DP_DETECTOR] = &detector_ops,
	[DP_V21] = &v21_ops,
	[DP_V22] = &v22_ops,
};

#define modem_status_name(stat) (((stat) < arrsize(modem_status_names) && \
		modem_status_names[(stat)]) ? modem_status_names[(stat)] : \
		"unknown" )
#define get_dp_name(id) (((id) < arrsize(dp_names) && dp_names[(id)]) ? \
		dp_names[(id)] : "unknown" )
#define get_dp_operations(id) ((id) < arrsize(dp_ops) ? dp_ops[(id)] : NULL )

/*
 *  get/put chars
 */

static int modem_get_chars(struct modem *m, uint8_t * buf, unsigned count)
{
	return fifo_get(&m->tx_fifo, buf, count);
}

static int modem_put_chars(struct modem *m, uint8_t * buf, unsigned count)
{
	//return fifo_put(&m->rx_fifo, buf, count);
	return write(m->tty, buf, count);
}

/*
 * status updates
 */

void modem_update_status(struct modem *m, enum MODEM_STATUS status)
{
	dbg("modem_update_status: status = %s\n", modem_status_name(status));
	switch (status) {
	case STATUS_NONE:
		break;
	case STATUS_DP_TIMEOUT:
		m->next_dp_id = DP_FAIL;
	case STATUS_CONNECTING:
		m->get_bits = NULL;
		m->put_bits = NULL;
		m->get_chars = m->put_chars = NULL;
		m->data = m->command = 0;
		break;
	case STATUS_DP_CONNECT:
		dbg("dp reports CONNECT\n");
		info("\nCONNECT\n");
		m->get_bits = async_bitque_get_bits;
		m->put_bits = async_bitque_put_bits;
		m->get_chars = modem_get_chars;
		m->put_chars = modem_put_chars;
		m->data = 1;
		m->command = 0;
		break;
	}
}

void modem_update_signals(struct modem *m, unsigned int signals)
{
	unsigned n;
#if 1
	unsigned changed = m->signals_detected ^ signals;
	for (n = 0; n < arrsize(signal_descs); n++) {
		if (changed & MASK(n))
			dbg("update_signals: signal %s d%sed\n",
			    signal_descs[n].name ? signal_descs[n].
			    name : "unknown",
			    signals & MASK(n) ? "etect" : "isappear");
	}
#endif
	if (signals & (MASK(SIGNAL_ANSAM)))
		/* nothing yet */ ;
	else if (signals & (MASK(SIGNAL_2225) | MASK(SIGNAL_2245)))
		m->next_dp_id = DP_V22;
	m->signals_detected = signals;
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
	const struct dp_operations *dp_op;
	void *dp;

	trace();

	old_datapump = m->datapump;
	m->datapump.id = 0;
	if (old_datapump.id)
		old_datapump.op->delete(old_datapump.dp);

	if (!dp_id ||
	    !(dp_op = get_dp_operations(dp_id)) || !(dp = dp_op->create(m))) {
		dbg("failed to create new dp \'%u\', drop all...\n", dp_id);
		return -1;
	}

	new_datapump.id = dp_id;
	new_datapump.name = get_dp_name(dp_id);
	new_datapump.dp = dp;
	new_datapump.op = dp_op;

	dbg("%u: switch datapump: %s (%u) -> %s (%u)...\n", m->samples_count,
	    get_dp_name(old_datapump.id), old_datapump.id,
	    new_datapump.name, new_datapump.id);

	m->datapump = new_datapump;
	m->process = m->datapump.op->process;

	return 0;
}

static void switch_datapump(struct modem *m)
{
	unsigned dp_id = m->next_dp_id;
	m->next_dp_id = 0;
	if (modem_switch_datapump(m, dp_id) < 0)
		drop_all(m);
}

static inline void samples_timer_update(struct modem *m, unsigned int count)
{
	m->samples_count += count;
#if 0				/* unused yet */
	if (m->samples_timer && m->samples_count >= m->samples_timer) {
		m->samples_timer = 0;
		m->samples_timer_func(m);
	}
#endif
}

static int modem_null_process(struct modem *m,
			      int16_t * in, int16_t * out, unsigned count)
{
	memset(out, 0, count * sizeof(*out));
	return count;
}

static int modem_dev_process(struct modem *m)
{
	static int16_t buf_in[1024], buf_out[1024];
	int (*process) (struct modem * m,
			int16_t * in, int16_t * out, unsigned count);
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
		memset(buf_out + ret, 0, (count - ret) * sizeof(int16_t));
		if (!m->next_dp_id)
			m->next_dp_id = DP_FAIL;
	}

	ret = m->driver->write(m, buf_out, count);
	if (ret < 0) {
		err("device write failed.\n");
		goto _error;
	}

	samples_timer_update(m, count);

	log_rx_samples(buf_in, count);
	log_tx_samples(buf_out, count);

	if (m->next_dp_id) {
		ret = modem_switch_datapump(m, m->next_dp_id);
		m->next_dp_id = 0;
	}

_error:
	return ret;
}

int modem_tty_process(struct modem *m)
{
	static unsigned char tty_buf[4096];
	int cnt;
	dbg("poll: ttyfd...\n");
	cnt = fifo_room(&m->tx_fifo);
	if (cnt > sizeof(tty_buf))
		cnt = sizeof(tty_buf);
	cnt = read(m->tty, tty_buf, cnt);
	if (cnt < 0) {
		if (errno == -EIO) {
			dbg("closed tty - suspend poll.\n");
			return 100;	/* closed_tty_count = 100; */
		}
		return cnt;
	}
	dbg("got %d chars from tty.\n", cnt);
	fifo_put(&m->tx_fifo, tty_buf, cnt);
	return 0;
}

int modem_run(struct modem *m)
{
	struct pollfd pollfd[2];
	unsigned int nfds;
	unsigned closed_tty_count = 0;
	struct pollfd *devfd, *ttyfd;
	int ret = 0;

	trace();

	while (!m->killed) {
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
		else if (fifo_room(&m->tx_fifo)) {
			ttyfd = &pollfd[nfds++];
			ttyfd->fd = m->tty;
		}

		ret = poll(pollfd, nfds, 1000);
		if (ret < 0) {
			if (errno == EINTR) {
				ret = 0;
				continue;
			}
			err("poll error: %s\n", strerror(errno));
			break;
		}
		if (ret == 0) {	/* timeout */
			closed_tty_count = 0;
			continue;
		}
		if (devfd && (devfd->revents & POLLIN)) {
			ret = modem_dev_process(m);
			if (ret < 0)
				break;
		}
		if (ttyfd && (ttyfd->revents & POLLIN)) {
			ret = modem_tty_process(m);
			if (ret < 0)
				break;
			closed_tty_count = ret;
		}
	}

	return ret;
}

int modem_set_hook(struct modem *m, unsigned hook_off)
{
	int ret;
	trace("hook_%s...", hook_off ? "off" : "on");
	if (m->hook_state == hook_off)
		return 0;
	if ((ret = m->driver->ctrl(m, MDRV_CTRL_HOOK, hook_off)) < 0) ;
	return ret;
	m->hook_state = hook_off;
	return 0;
}

static int modem_start(struct modem *m)
{
	int ret;
	trace();
	if ((ret = m->driver->start(m)) < 0)
		return ret;
	m->samples_count = 0;
	m->started = 1;
	return 0;
}

static int modem_stop(struct modem *m)
{
	int ret;
	trace();
	if ((ret = m->driver->stop(m)) < 0)
		return ret;
	m->started = 0;
	return 0;
}

int modem_go(struct modem *m, enum DP_ID dp_id)
{
	int ret;
	trace("..");
	m->data = 0;
	m->command = 0;
	if ((ret = modem_set_hook(m, 1)) < 0) {
		err("cannot set hook\n");
		goto _error;
	}
	if ((ret = modem_start(m)) < 0) {
		err("cannot start modem\n");
		goto _error;
	}
	if ((ret = modem_switch_datapump(m, dp_id)) < 0) {
		err("cannot switch dp\n");
		goto _error;
	}
	async_bitque_reset(&m->rx_bitque);
	async_bitque_reset(&m->tx_bitque);
	fifo_reset(&m->rx_fifo);
	fifo_reset(&m->tx_fifo);
	modem_update_status(m, STATUS_CONNECTING);
	return 0;
_error:
	modem_set_hook(m, 0);
	modem_stop(m);
	return ret;
}

int modem_dial(struct modem *m, const char *dial_string)
{
	int ret;
	trace("%s...", dial_string);
	m->caller = 1;
	m->signals_to_detect = MASK(SIGNAL_2100) | MASK(SIGNAL_ANSAM) |
	    MASK(SIGNAL_2225) | MASK(SIGNAL_2245);
	m->signals_detected = 0;
	strncpy(m->dial_string, dial_string, sizeof(m->dial_string));
	ret = modem_go(m, DP_DIALER);
	return ret;
}

static void sregs_reset(struct modem *m)
{
	m->sregs[0] = 0;	/* autoanswer rings count */
	m->sregs[1] = 0;	/* rings count */
	m->sregs[2] = '+';	/* escape char */
	m->sregs[3] = '\r';	/* carriage return char */
	m->sregs[4] = '\n';	/* line feed char */
	m->sregs[5] = '\b';	/* backspace char */
	m->sregs[6] = 2;	/* wait before dialing  */
	m->sregs[7] = 60;	/* wait for carrier */
	m->sregs[8] = 2;	/* pause for comma when dialing */
	m->sregs[9] = 6;	/* carrier detect time 0.1s */
	m->sregs[10] = 14;	/* carrier lost time 0.1s */
	m->sregs[11] = 100;	/* dtmf dialing speed ms */
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
		m->datapump.id = 0;
	}
	fifo_reset(&m->rx_fifo);
	fifo_reset(&m->tx_fifo);
	m->caller = 0;
	modem_set_hook(m, 0);
}

#define MODEM_NAME "MA (modem again)"
#define MODEM_DESC "SashaK's softmodem attempt"
#define MODEM_VERSION "0.000003"

/* hack */
static struct modem *__modem_last;

static void mark_killed(int signum)
{
	struct modem *m = __modem_last;
	dbg("mark_killed: %d...\n", signum);
	m->killed = signum;
}

static int make_terminal(const char *link_name)
{
	int pty;

	if ((pty = posix_openpt(O_RDWR)) < 0 ||
	    grantpt(pty) < 0 || unlockpt(pty) < 0) {
		err("openpt failed: %s\n", strerror(errno));
		return -1;
	}

	unlink(link_name);
	if (symlink(ptsname(pty), link_name) < 0) {
#if 0
		struct stat st;
		int saved_errno = errno;
		if (errno == EXIST && stat(ptsname(pty), &stat) < 0
		    && errno == ENOENT && !unlink(link_name)
		    symlink(ptsname(pty), link_name) < 0)
			;
#endif
		err("cannot symlink %s -> %s: %s\n", link_name, ptsname(pty), strerror(errno));

		return -1;
	}

	return pty;
}

int setup_terminal(int tty)
{
	struct termios termios;
	int ret;

	fcntl(tty, F_SETFL, O_NONBLOCK);

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

struct modem *modem_create(const char *tty_name, const char *drv_name)
{
	struct modem *m;
	const struct modem_driver *drv;
	int tty = 0;

	drv = find_modem_driver(drv_name);
	if (!drv) {
		err("no driver \'%s\' is found\n", drv_name);
		return NULL;
	}

	m = malloc(sizeof(*m));
	if (!m)
		return NULL;
	memset(m, 0, sizeof(*m));

	if (tty_name && (tty = make_terminal(tty_name)) < 0) {
		err("cannot make terminal for \'%s\'\n", tty_name);
		return NULL;
	}

	m->name = MODEM_NAME;
	m->driver = drv;
	m->tty = tty;
	m->is_tty = isatty(tty);
	m->tty_link_name = tty_name;

	if (m->is_tty) {
		m->tty_name = ttyname(tty);
		tcgetattr(tty, &m->termios);
		if (tty_name)
			setup_terminal(tty);
	} else {
		m->tty_name = "nottty";
		dbg("warn: %d (%s) is not a tty\n", m->tty, m->tty_name);
	}

	sregs_reset(m);

	m->dev = m->driver->open(m, modem_device_name);
	if (m->dev < 0) {
		err("cannot open device.\n");
		goto _error;
	}

	__modem_last = m;
	signal(SIGINT, mark_killed);
	signal(SIGTERM, mark_killed);

	info("%s - %s, version %s\ndriver is \'%s\', tty is \'%s\'\n",
	     m->name, MODEM_DESC, MODEM_VERSION, m->driver->name, m->tty_name);

	return m;
_error:
	free(m);
	return NULL;
}

void modem_delete(struct modem *m)
{
	trace();
	if (m->started)
		modem_stop(m);
	modem_reset(m);
	if (m->dev)
		m->driver->close(m);
	if (m->is_tty)
		tcsetattr(m->tty, TCSANOW, &m->termios);
	if (m->tty_link_name)
		unlink(m->tty_link_name);

	free(m);
}
