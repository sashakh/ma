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

#ifndef __GEN_TABLES_H__
#define __GEN_TABLES_H__

#include <m.h>

#define COSTAB_SHIFT 14
#define COSTAB_BASE  ((1<<COSTAB_SHIFT) - 1)
#define COSTAB_SIZE  (1024)

/* filters */
#define FREQ(x) ((double)(x)/(double)(SAMPLE_RATE))

extern void hwsinc_lp_filter(double fc, double *buf, unsigned size);
extern void hwsinc_hp_filter(double fc, double *buf, unsigned size);
extern void hwsinc_bp_filter(double f1, double f2, double *buf, unsigned size);

/* common helpers */

#define TABLE_PROTO(f,name,type,size) fprintf((f), "extern const "#type" " name "["#size"];\n")
#define TABLE_PRINT(f,name,type,buf,size) { int i; \
	fprintf((f), "const "#type" %s [] = {", (name)); \
	for( i = 0 ; i < (size) ; i++ ) \
		fprintf((f), "%s%d,", i%8 ? " " : "\n\t", \
			(type) ((buf)[i] * COSTAB_BASE)); \
	fprintf((f), "\n};\n"); }

#define arrsize(a) (sizeof(a)/sizeof((a)[0]))

/* misc debug */
extern void show_freq_domain(const char *name, const double *buf,
			     unsigned size);

extern void dump_time_for_plot(const char *name, double *filter, unsigned size);
extern void dump_freq_for_plot(const char *name, double *mag, double *ph,
			       unsigned size);
extern void dump_filter_for_plot(const char *name, double *filter,
				 unsigned taps, unsigned size);

#endif	/* __GEN_TABLES_H__ */
