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

#ifndef __M_H__
#define __M_H__

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <termios.h>

#define SAMPLE_RATE 8000

/* types */

enum MDRV_CTRL_CMD {
	MDRV_CTRL_NONE = 0,
	MDRV_CTRL_HOOK,
	MDRV_CTRL_CID,
	MDRV_CTRL_SPEAKER,
};

enum DP_ID {
	DP_NONE = 0,
	DP_DIALER,
	DP_DETECTOR,
	DP_V21,
	DP_V22,
	DP_LAST,
	DP_FAIL = 255
};

enum SIGNAL_ID {
	SIGNAL_NONE = 0,
	SIGNAL_2100,
	SIGNAL_ANSAM,
	SIGNAL_2245,
	SIGNAL_2225,
	SIGNAL_V21,
	SIGNAL_V22,
	SIGNAL_LAST,
};

enum MODEM_STATUS {
	STATUS_NONE = 0,
	STATUS_CONNECTING,
	STATUS_DP_TIMEOUT,
	STATUS_DP_CONNECT,
};

struct modem;

struct signal_desc {
	const char *name;
	unsigned freq;
	/* add more stuff here */
};

struct modem_driver {
	const char *name;
	int (*open) (struct modem * m, const char *dev_name);
	int (*close) (struct modem * m);
	int (*start) (struct modem * m);
	int (*stop) (struct modem * m);
	int (*read) (struct modem * m, void *buf, unsigned int count);
	int (*write) (struct modem * m, void *buf, unsigned int count);
	int (*ctrl) (struct modem * m, unsigned int cmd, unsigned long arg);
};

struct dp_operations {
	void *(*create) (struct modem * m);
	int (*process) (struct modem * m, int16_t * in, int16_t * out,
			unsigned count);
	void (*hangup) (struct modem * m);
	void (*delete) (void *dp_data);
} *dp_op;

struct async_bitque {
	unsigned int bits;
	unsigned long data;
};

struct fifo {
	unsigned int head;
	unsigned int tail;
	unsigned char buf[4096];
};

struct modem {
	const char *name;
	int tty, dev;
	unsigned is_tty;
	const char *tty_name, *dev_name, *tty_link_name;
	const struct modem_driver *driver;
	void *device_data;
	struct termios termios;
	unsigned int samples_count;
	unsigned int killed;
	unsigned int caller;
	unsigned int hook_state;
	unsigned int started;
	/* states for write/get,put chars */
	unsigned int command;
	unsigned int data;
	/* for detector */
	unsigned int signals_to_detect;
	unsigned int signals_detected;
	/* main process proc */
	int (*process) (struct modem * m, int16_t * in, int16_t * out,
			unsigned int count);
	unsigned int samples_timer;
	void (*samples_timer_func) (struct modem * m);
	unsigned int (*get_bits) (struct modem * m, unsigned num);
	void (*put_bits) (struct modem * m, unsigned int bit, unsigned num);
	int (*put_chars) (struct modem * m, uint8_t * buf, unsigned count);
	int (*get_chars) (struct modem * m, uint8_t * buf, unsigned count);
	unsigned int next_dp_id;
	struct datapump {
		unsigned int id;
		const char *name;
		void *dp;
		const struct dp_operations *op;
	} datapump;
	struct async_bitque rx_bitque, tx_bitque;
	struct fifo rx_fifo, tx_fifo;
	unsigned char sregs[16];
	char dial_string[128];
};

/*
 * prototypes
 */

static inline void fifo_reset(struct fifo *f)
{
	f->head = f->tail = 0;
}

static inline unsigned fifo_room(struct fifo *f)
{
	return sizeof(f->buf) - (f->head - f->tail);
}

extern unsigned fifo_get(struct fifo *f, unsigned char *buf, unsigned count);
extern unsigned fifo_put(struct fifo *f, unsigned char *buf, unsigned count);

static inline void async_bitque_reset(struct async_bitque *q)
{
	q->bits = 0;
}

extern void async_bitque_put_bits(struct modem *m, unsigned bits, unsigned num);
extern unsigned async_bitque_get_bits(struct modem *m, unsigned num);

extern struct modem *modem_create(const char *tty_name, const char *drv_name);
extern void modem_delete(struct modem *m);
extern int modem_go(struct modem *m, enum DP_ID dp_id);
extern int modem_dial(struct modem *m, const char *dial_string);
extern int modem_run(struct modem *m);
extern int modem_process(struct modem *m, int16_t * in, int16_t * out,
			 unsigned int count);
extern int modem_set_hook(struct modem *m, unsigned int hook_off);

extern void modem_update_status(struct modem *m, enum MODEM_STATUS status);
extern void modem_update_signals(struct modem *m, unsigned int signals);

static inline unsigned modem_get_bits(struct modem *m, unsigned num)
{
	return m->get_bits ? m->get_bits(m, num) : ((1 << num) - 1);
}

static inline void modem_put_bits(struct modem *m, unsigned bits, unsigned num)
{
	if (m->put_bits)
		m->put_bits(m, bits, num);
}

/* modem drivers interface */
extern const struct modem_driver *find_modem_driver(const char *name);
extern const struct dp_operations *find_dp_operations(unsigned int id);

/* command line parser */
extern int parse_cmdline(int argc, char **argv);

/*
 * global stuff
 */

extern const struct signal_desc signal_descs[SIGNAL_LAST];

extern unsigned int verbose_level;
extern unsigned int debug_level;
extern unsigned int log_level;
extern const char *modem_driver_name;
extern const char *modem_device_name;
extern const char *modem_tty_name;
extern const char *modem_phone_number;
extern const char *modulation_test;

/*
 * misc helpers
 */

#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
#define samples_in_sec(sec) ((SAMPLE_RATE)*(sec))
#define samples_in_msec(msec) (((SAMPLE_RATE)*(msec))/1000)

#define MASK(bit) (1 << (bit))

#define info(fmt, arg...) fprintf(stderr, fmt, ##arg )

/*
 * debug stuff
 */

#ifdef MODEM_DEBUG

enum { LOG_MESSAGES, LOG_RX_SAMPLES, LOG_TX_SAMPLES,
	LOG_FSK_DATA, LOG_PSK_DATA
};

extern int log_data(unsigned id, void *buf, unsigned size);
extern int log_samples(unsigned id, int16_t * buf, unsigned size);
extern int log_printf(unsigned level, const char *fmt, ...);

#define dbg(fmt, ...) log_printf(1, fmt, ## __VA_ARGS__)
#define trace(fmt, ...) dbg(__FILE__ ":%s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__)

#undef info
#define info(fmt, ...) log_printf(0, fmt, ##__VA_ARGS__)

#else
#define log_data(id,buf,size)
#define log_samples(id,buf,size)
#define dbg(fmt...)
#define trace(fmt...)
#endif

#define err(fmt, ...) info("err: " __FILE__ ":%d : %s(): " fmt , __LINE__ , __func__, ## __VA_ARGS__)

#define log_rx_samples(buf,size) log_samples(LOG_RX_SAMPLES,buf,size)
#define log_tx_samples(buf,size) log_samples(LOG_TX_SAMPLES,buf,size)

#endif /* __M_H__ */
