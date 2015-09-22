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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include "m.h"

#ifndef info
#define info(fmt, arg...) fprintf(stderr, fmt, ##arg )
#endif
#define local_dbg(fmt, arg...)
//#define local_dbg(fmt, arg...) dbg("cmdline: " fmt, ##arg )

static void show_usage();

#define OPTARG_STR 0
#define OPTARG_INT 1

static struct cmdline_option {
	const char *name;
	int val;
	const char *description;
	void (*action) (void);
	int has_arg;
	int arg_type;
	void *arg_val;
} cmdline_options[] = {
	{ "usage", 'u', "this message", show_usage}, {
	"help", 'h', "this help message", show_usage},
	    //{"verbose", 'v', "verbose level", NULL, 2, OPTARG_INT, &verbose_level},
	    //{"verbose", 'v', "verbose mode"},
	{
	"debug", 'd', "debug level", NULL, 2, OPTARG_INT, &debug_level}, {
	"log", 'l', "logging mode", NULL, 2, OPTARG_INT, &log_level}, {
	"modem", 'm', "modem driver", NULL, 1, OPTARG_STR, &modem_driver_name},
	{
	"device", 'D', "device name", NULL, 1, OPTARG_STR, &modem_device_name},
	{"tty", 'T', "tty name", NULL, 1, OPTARG_STR, &modem_tty_name},
	{
	"number", 'n', "preset phone number", NULL, 1, OPTARG_STR,
		    &modem_phone_number}, {
	"test", 't', "test modulation (mtest only)", NULL, 1,
		    OPTARG_STR, &modulation_test}, {
	"jopa", 0, "jopa kakaya-to", NULL, 1},
#if 0
	{
	"group", 'g', "group"}, {
	"perm", 'p', "port node permission"},
#endif
	{
	0, 0, 0, 0}
};

const char *prog_name = "thisprog";

static void show_usage()
{
	struct cmdline_option *opt = cmdline_options;
	info("Usage: %s [options] [device]\n", prog_name);
	while (opt->name) {
		int n = opt->val ? info(" -%c,", opt->val) : info("    ");
		n += info(" --%s", opt->name);
		if (opt->has_arg)
			n += info("%s=VAL%s",
				  opt->has_arg == 2 ? "[" : "",
				  opt->has_arg == 2 ? "]" : "");
		n += info("%*s", 24 - n, "");
		n += info("%s", opt->description);
		if (opt->has_arg && opt->arg_val) {
			n += info("; default: ");
			if (opt->arg_type == OPTARG_INT)
				n += info("%d", *(int *)opt->arg_val);
			else
				n += info("%s", *(char **)opt->arg_val);
		}
		info("\n");
		opt++;
	}
	exit(2);
}

static int is_optarg_int(char *arg)
{
	char *end;
	strtol(arg, &end, 0);
	return (*end) ? 0 : 1;
}

int parse_cmdline(int argc, char *argv[])
{
	struct option long_options[arrsize(cmdline_options)];
	char opt_string[256];
	struct cmdline_option *opt = cmdline_options;
	int c, n;
	int index = 0;

	prog_name = argv[0];

	memset(long_options, 0, sizeof(long_options));
	n = sprintf(opt_string, ":");
	while (opt->name) {
		if (opt->val)
			n += snprintf(opt_string + n, sizeof(opt_string) - n,
				      "%c%s%s", opt->val,
				      opt->has_arg ? ":" : "",
				      opt->has_arg > 1 ? ":" : "");
		long_options[index].name = opt->name;
		long_options[index].val = opt->val;
		long_options[index].has_arg = opt->has_arg;
		index++;
		opt++;
	}

	local_dbg("generated opt_string: %s\n", opt_string);

	while (1) {
		index = 0;

		c = getopt_long_only(argc, argv, opt_string,
				     long_options, &index);
		if (c == -1)
			break;

		local_dbg("** "
			  "getopt: ret = %d '%c', index = %d,"
			  " optind = %d optopt = %d optarg = %p %s\n",
			  c, c, index, optind, optopt,
			  optarg, optarg ? optarg : "");
		opt = NULL;
		if (c == '?') {
			info("unknown option: %d '%c'\n", optopt, optopt);
			goto _error;
		} else if (c == ':')
			c = optopt;
		if (index)
			opt = &cmdline_options[index];
		else
			for (opt = cmdline_options; opt->name; opt++)
				if (opt->val == c)
					break;
		if (!opt || !opt->name)
			goto _error;

		if (opt->has_arg == 1 && !optarg) {
			info("option '%s' requeres argument\n", opt->name);
			goto _error;
		}
		if (opt->has_arg == 2 && !optarg && argv[optind]
		    && *argv[optind] != '-' && (opt->arg_type == OPTARG_STR
						|| is_optarg_int(argv[optind])))
		{
			optarg = argv[optind++];
		}
		if (opt->has_arg && optarg && opt->arg_type == OPTARG_INT &&
		    !is_optarg_int(optarg)) {
			info("option '%s' requeres integer argument\n",
			     opt->name);
			goto _error;
		}

		local_dbg("++ opt %d '%c' \"%s\" is found\n", c, c, opt->name);

		if (opt->action)
			opt->action();

		if (!opt->has_arg || !optarg)
			continue;

		if (opt->arg_type == OPTARG_INT) {
			int val = strtol(optarg, NULL, 0);
			*((int *)opt->arg_val) = val;
		} else
			*((char **)opt->arg_val) = optarg;
	}

	if (optind < argc) {
		local_dbg("non-option ARGV-elements: ");
		while (optind < argc)
			local_dbg("%s ", argv[optind++]);
		local_dbg("\n");
	}

	return 0;
_error:
	show_usage();
	exit(2);
}
