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
#include <errno.h>

#include <alsa/asoundlib.h>

#include "m.h"

struct alsa_device {
	int fd;
	snd_pcm_t *ppcm, *cpcm;
	snd_pcm_uframes_t period_size;
	snd_pcm_uframes_t buffer_size;
	unsigned sample_rate;
	snd_mixer_t *mixer;
	snd_mixer_elem_t *hook_off_elem, *cid_elem, *speaker_elem;
	snd_output_t *log;
};

static int alsa_xrun_recovery(struct alsa_device *dev)
{
	int ret;
	dbg("xrun, try to recover...\n");
	ret = snd_pcm_prepare(dev->ppcm);
	if (ret < 0) {
		err("cannot prepare playback: %s\n", snd_strerror(ret));
		return ret;
	}
#if 0
	int len = dev->delay - INTERNAL_DELAY;
	snd_pcm_format_set_silence(SND_PCM_FORMAT_S16_LE, outbuf, len);
	ret = snd_pcm_writei(dev->ppcm, outbuf, len);
	if (ret < 0) {
		err("write error: %s\n", snd_strerror(ret));
		return ret;
	}
	ret = snd_pcm_start(dev->cpcm);
	if (ret < 0) {
		err("snd_pcm_start error: %s\n", snd_strerror(ret));
		return ret;
	}
#endif
	dbg("alsa xrun: recovered.\n");
	return 0;
}

static int alsa_read(struct modem *m, void *buf, unsigned count)
{
	struct alsa_device *dev = m->device_data;
	int ret;
	trace("%d", count);
	do {
		ret = snd_pcm_readi(dev->cpcm, buf, count);
		if (ret == -EPIPE) {
			ret = alsa_xrun_recovery(dev);
			break;
		}
	} while (ret == -EAGAIN);
	return ret;
}

static int alsa_write(struct modem *m, void *buf, unsigned count)
{
	struct alsa_device *dev = m->device_data;
	int written = 0;
	trace("%d", count);
	while (count > 0) {
		int ret = snd_pcm_writei(dev->ppcm, buf, count);
		if (ret < 0) {
			if (ret == -EAGAIN)
				continue;
			if (ret == -EPIPE) {
				ret = alsa_xrun_recovery(dev);
			}
			written = ret;
			break;
		}
		count -= ret;
		buf += ret;
		written += ret;
	}
	return written;
}

static int setup_stream(struct alsa_device *dev, snd_pcm_t * pcm,
			const char *stream_name)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	unsigned int rrate;
	snd_pcm_uframes_t rsize;
	int ret;

	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_sw_params_alloca(&sw_params);

	ret = snd_pcm_hw_params_any(pcm, hw_params);
	if (ret < 0) {
		err("cannot init hw params for %s: %s\n",
		    stream_name, snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_access(pcm, hw_params,
					   SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		err("cannot set access for %s: %s\n",
		    stream_name, snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_format(pcm, hw_params,
					   SND_PCM_FORMAT_S16_LE);
	if (ret < 0) {
		err("cannot set format for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_channels(pcm, hw_params, 1);
	if (ret < 0) {
		err("cannot set channels for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}

	rrate = dev->sample_rate;
	ret = snd_pcm_hw_params_set_rate_near(pcm, hw_params, &rrate, 0);
	if (ret < 0) {
		err("cannot set rate for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
	if (rrate != dev->sample_rate) {
		err("rate %d is not supported by %s (%d).\n",
		    dev->sample_rate, stream_name, rrate);
		return -1;
	}

	rsize = dev->period_size;
	ret =
	    snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &rsize,
						   NULL);
	if (ret < 0) {
		err("cannot set period size for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
	if (rsize != dev->period_size) {
		dbg("period size for %s was changed %u -> %lu\n",
		    stream_name, dev->period_size, rsize);
		dev->period_size = rsize;
	}

	rsize = dev->buffer_size;
	ret = snd_pcm_hw_params_set_buffer_size_near(pcm, hw_params, &rsize);
	if (ret < 0) {
		err("cannot set buffer size for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
	if (rsize != dev->buffer_size) {
		dbg("buffer size for %s was changed %lu -> %lu\n",
		    stream_name, dev->buffer_size, rsize);
		dev->buffer_size = rsize;
	}

	ret = snd_pcm_hw_params(pcm, hw_params);
	if (ret < 0) {
		err("cannot setup hw params for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_prepare(pcm);
	if (ret < 0) {
		err("cannot prepare %s: %s\n", stream_name, snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_sw_params_current(pcm, sw_params);
	if (ret < 0) {
		err("cannot get sw params for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
#if 0				/* if we want to disable autostart */
	ret = snd_pcm_sw_params_set_start_threshold(pcm, sw_params, INT_MAX);
	if (ret < 0) {
		err("cannot set start threshold for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
#endif
#if 0				/* to prevent xrun at set stop threshold equal to boundary */
	ret = snd_pcm_sw_params_get_boundary(sw_params, &rsize);
	if (ret < 0) {
		err("cannot get boundary for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
	ret = snd_pcm_sw_params_set_stop_threshold(pcm, sw_params, rsize);
	if (ret < 0) {
		err("cannot set stop threshold for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
#endif
#if 0				/* don't know why */
	ret = snd_pcm_sw_params_set_avail_min(pcm, sw_params, 4);
	if (ret < 0) {
		err("cannot set avail min for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
	ret = snd_pcm_sw_params_set_xfer_align(pcm, sw_params, 4);
	if (ret < 0) {
		err("cannot set align for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}
#endif
	ret = snd_pcm_sw_params(pcm, sw_params);
	if (ret < 0) {
		err("cannot set sw params for %s: %s\n", stream_name,
		    snd_strerror(ret));
		return ret;
	}

	return 0;
}

static int alsa_start(struct modem *m)
{
	struct alsa_device *dev = m->device_data;
	void *buf;
	unsigned len;
	int ret;

	trace();

#ifdef USE_PCM_LINK
	ret = snd_pcm_link(dev->cpcm, dev->ppcm);
	if (ret < 0) {
		err("snd_pcm_link error: %s\n", snd_strerror(ret));
		return ret;
	}
#endif

	len = dev->buffer_size;
	buf = alloca(snd_pcm_frames_to_bytes(dev->ppcm, len));
	if (!buf) {
		err("cannot alloca %d frames\n", len);
		return -1;
	}

	ret = snd_pcm_format_set_silence(SND_PCM_FORMAT_S16_LE, buf, len);
	if (ret < 0) {
		err("silence error\n");
		return ret;
	}

	dbg("startup write: %d...\n", len);
	ret = snd_pcm_writei(dev->ppcm, buf, len);
	if (ret < 0) {
		err("startup write error\n");
		return ret;
	}
#ifndef USE_PCM_LINK			/* autostart is used */
	ret = snd_pcm_start(dev->cpcm);
	if (ret < 0) {
		err("snd_pcm_start error: %s\n", snd_strerror(ret));
		return ret;
	}
#endif
	return 0;
}

static int alsa_stop(struct modem *m)
{
	struct alsa_device *dev = m->device_data;
	trace();
	snd_pcm_drop(dev->cpcm);
	snd_pcm_nonblock(dev->ppcm, 0);
	snd_pcm_drain(dev->ppcm);
	snd_pcm_nonblock(dev->ppcm, 1);
	snd_pcm_unlink(dev->cpcm);
	snd_pcm_hw_free(dev->ppcm);
	snd_pcm_hw_free(dev->cpcm);
	return 0;
}

static int alsa_ctrl(struct modem *m, unsigned int cmd, unsigned long arg)
{
	struct alsa_device *dev = m->device_data;
	trace("cmd %u, arg %lu", cmd, arg);
	switch (cmd) {
	case MDRV_CTRL_HOOK:
		return (dev->hook_off_elem) ?
		    snd_mixer_selem_set_playback_switch_all(dev->hook_off_elem,
							    (arg != 0)) : 0;
	case MDRV_CTRL_CID:
		return (dev->cid_elem) ?
		    snd_mixer_selem_set_playback_switch_all(dev->cid_elem,
							    (arg != 0)) : 0;
	case MDRV_CTRL_SPEAKER:
		return (dev->speaker_elem) ?
		    snd_mixer_selem_set_playback_volume_all(dev->speaker_elem,
							    arg) : 0;
	}
	return -EINVAL;
}

static int alsa_mixer_setup(struct alsa_device *dev, const char *dev_name)
{
	char card_name[32];
	snd_mixer_elem_t *elem;
	int card_num = 0;
	char *p;
	int ret;

	if ((p = strchr(dev_name, ':')))
		card_num = strtoul(p + 1, NULL, 0);
	sprintf(card_name, "hw:%d", card_num);

	ret = snd_mixer_open(&dev->mixer, 0);
	if (ret < 0) {
		dbg("cannot open: %s\n", snd_strerror(ret));
		return ret;
	}
	ret = snd_mixer_attach(dev->mixer, card_name);
	if (ret < 0) {
		err("attach %s error: %s\n", card_name, snd_strerror(ret));
		goto _error;
	}
	ret = snd_mixer_selem_register(dev->mixer, NULL, NULL);
	if (ret < 0) {
		err("register %s error: %s\n", card_name, snd_strerror(ret));
		goto _error;
	}
	ret = snd_mixer_load(dev->mixer);
	if (ret < 0) {
		err("load %s error: %s\n", card_name, snd_strerror(ret));
		goto _error;
	}

	for (elem = snd_mixer_first_elem(dev->mixer); elem;
	     elem = snd_mixer_elem_next(elem)) {
		if (strcmp(snd_mixer_selem_get_name(elem), "Off-hook") == 0)
			dev->hook_off_elem = elem;
		else if (strcmp(snd_mixer_selem_get_name(elem), "Caller ID") ==
			 0)
			dev->cid_elem = elem;
		else if (strcmp(snd_mixer_selem_get_name(elem), "Modem Speaker")
			 == 0)
			dev->speaker_elem = elem;
	}

	if (dev->hook_off_elem)
		return 0;

_error:
	if (dev->mixer)
		snd_mixer_close(dev->mixer);
	dev->mixer = NULL;
	if (!ret) {
		err("Off-hook switch not found for card %s\n", card_name);
		ret = -ENODEV;
	}
	return ret;
}

static int alsa_open(struct modem *m, const char *dev_name)
{
	struct pollfd pollfd;
	struct alsa_device *dev;
	const char *alsa_name;
	int ret;

	trace();

	alsa_name = dev_name;
	if (!alsa_name)
		alsa_name = modem_device_name;
	dev = malloc(sizeof(*dev));
	if (!dev) {
		err("no mem: %s\n", strerror(errno));
		return -1;
	}
	memset(dev, 0, sizeof(*dev));

	ret = alsa_mixer_setup(dev, alsa_name);
	if (ret < 0)
		dbg("cannot setup mixer: %s\n", snd_strerror(ret));

	ret = snd_pcm_open(&dev->ppcm, alsa_name,
			   SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if (ret < 0) {
		err("cannot open playback '%s': %s\n",
		    alsa_name, snd_strerror(ret));
		goto _error;
	}

	ret = snd_pcm_open(&dev->cpcm, alsa_name,
			   SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
	if (ret < 0) {
		err("cannot open capture '%s': %s\n",
		    alsa_name, snd_strerror(ret));
		goto _error;
	}

	dev->period_size = SAMPLE_RATE / 100;
	dev->buffer_size = dev->period_size * 16;
	dev->sample_rate = SAMPLE_RATE;

	ret = setup_stream(dev, dev->ppcm, "playback");
	if (ret < 0)
		return ret;

	ret = setup_stream(dev, dev->cpcm, "capture");
	if (ret < 0)
		return ret;

	ret = snd_pcm_poll_descriptors(dev->cpcm, &pollfd, 1);
	if (ret <= 0) {
		err("cannot get poll descriptor for '%s': %s\n",
		    alsa_name, snd_strerror(ret));
		goto _error;
	}

	snd_output_stdio_attach(&dev->log, stderr, 0);

	if (debug_level > 0)
		snd_pcm_dump(dev->ppcm, dev->log);

	m->device_data = dev;
	return pollfd.fd;

_error:
	if (dev->ppcm)
		snd_pcm_close(dev->ppcm);
	if (dev->cpcm)
		snd_pcm_close(dev->cpcm);
	if (dev->mixer)
		snd_mixer_close(dev->mixer);
	free(dev);
	return ret;
}

static int alsa_close(struct modem *m)
{
	struct alsa_device *dev = m->device_data;
	trace();
	m->device_data = NULL;
	snd_pcm_close(dev->ppcm);
	snd_pcm_close(dev->cpcm);
	if (dev->mixer) {
		if (dev->hook_off_elem)
			snd_mixer_selem_set_playback_switch_all(dev->
								hook_off_elem,
								0);
		if (dev->cid_elem)
			snd_mixer_selem_set_playback_switch_all(dev->cid_elem,
								0);
		if (dev->speaker_elem)
			snd_mixer_selem_set_playback_switch_all(dev->
								speaker_elem,
								0);
		snd_mixer_close(dev->mixer);
	}
	free(dev);
	return 0;
}

const struct modem_driver alsa_driver = {
	.name = "alsa",
	.open = alsa_open,
	.close = alsa_close,
	.start = alsa_start,
	.stop = alsa_stop,
	.read = alsa_read,
	.write = alsa_write,
	.ctrl = alsa_ctrl,
};
