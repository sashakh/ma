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
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <math.h>

#include "gen_tables.h"

/* config parameters */
unsigned use_stdout = 0;
unsigned debug_level = 0;

static const char *prog_name;

/*
 * Generators
 */

void gen_costab(FILE * h, FILE * f)
{
	int i;

	fprintf(h, "\n/* costab definitions */\n");
	fprintf(h, "#define COSTAB_SHIFT %d\n", COSTAB_SHIFT);
	fprintf(h, "#define COSTAB_BASE %d\n", COSTAB_BASE);
	fprintf(h, "#define COSTAB_SIZE %d\n", COSTAB_SIZE);
	fprintf(h, "extern const int16_t costab[COSTAB_SIZE];\n");

	fprintf(f, "\nconst int16_t costab [%d] = {", COSTAB_SIZE);
	for (i = 0; i < COSTAB_SIZE; i++) {
		double c = cos(M_PI * 2 * i / COSTAB_SIZE) * COSTAB_BASE;
		fprintf(f, "%s%d,", i % 8 ? " " : "\n\t", (int16_t) c);
	}
	fprintf(f, "\n};\n");

}

#define V21_FILTER_SIZE 101

void gen_v21_filters(FILE * h, FILE * f)
{
	double bp_fir[V21_FILTER_SIZE];
	double roff = 4. / V21_FILTER_SIZE;

	fprintf(h, "\n/* v21 filters definitions */\n");
	fprintf(h, "#define V21_FILTER_SIZE %d\n", V21_FILTER_SIZE);

	hwsinc_bp_filter(FREQ(1650) - roff / 2, FREQ(1850) + roff / 2,
			 bp_fir, V21_FILTER_SIZE);
	TABLE_PROTO(h, "v21_bandpass_1750", int16_t, V21_FILTER_SIZE);
	TABLE_PRINT(f, "v21_bandpass_1750", int16_t, bp_fir, V21_FILTER_SIZE);

	hwsinc_bp_filter(FREQ(980) - roff / 2, FREQ(1180) + roff / 2,
			 bp_fir, V21_FILTER_SIZE);
	TABLE_PROTO(h, "v21_bandpass_1080", int16_t, V21_FILTER_SIZE);
	TABLE_PRINT(f, "v21_bandpass_1080", int16_t, bp_fir, V21_FILTER_SIZE);
}

/* v22 attempts */

/* stupid brick-wall non-linear BPF, works with receiver for uknown reasons */
static void build_v22_bp_filter(double f1, double f2, double *filter,
				unsigned taps, unsigned size)
{
	double mag[size], phase[size];
	double temp[size];
	int i, j;

	for (i = 0; i < size / 2; i++) {
		double f = (double)i / (double)size;
		if (f >= f1 && f <= f2)
			mag[i] = 1.;
		else
			mag[i] = 0.;
		//phase[i] = 2.*M_PI*(taps-1)*i/size;
		phase[i] = 0.;
	}
	for (i = 0; i < size / 2; i++) {
		mag[size - i - 1] = mag[i];
		phase[size - i - 1] = phase[i];
	}
	//dump_freq_for_plot("v22f.dat", mag, phase, size);

	for (i = 0; i < size; i++) {
		temp[i] = 0.;
		for (j = 0; j < size; j++) {
			temp[i] +=
			    mag[j] * cos(phase[j]) * cos(M_PI * 2 * i * j /
							 size);
			temp[i] -=
			    mag[j] * sin(phase[j]) * sin(M_PI * 2 * i * j /
							 size);
		}
		temp[i] = temp[i] / (double)size;
	}
	dump_time_for_plot("v22t.dat", temp, size);

	/* truncate and don't shift */
	for (i = 0; i < taps; i++)
		filter[i] = temp[i];

	/* windowing */
	// (maybe - from one test it breaks demodulator)

	//dump_filter_for_plot("v22bp", filter, taps, size);
}

/* (square) root raised cosine filter */
static void build_rrc(unsigned symbol_rate, double beta, double *filter,
		      unsigned taps, unsigned size)
{
	double mag[size], phase[size];
	double temp[size];
	double f1, f2;
	double T;
	double sum = 0.;
	int i;

	T = (double)SAMPLE_RATE / (double)symbol_rate;
	f1 = (1. - beta) / (2. * T);
	f2 = (1. + beta) / (2. * T);
	for (i = 0; i < size / 2; i++) {
		double f = (double)i / (double)size;
		if (f <= f1)
			mag[i] = T;
		else if (f >= f2)
			mag[i] = 0.;
		else {
			mag[i] =
			    T * (1. + cos(M_PI * T * (f - f1) / beta)) / 2.;
		}
		mag[i] = sqrt(mag[i]);
		phase[i] = 0.;
	}
	for (i = 0; i < size / 2; i++) {
		mag[size - i - 1] = mag[i];
		phase[size - i - 1] = phase[i];
	}
	//dump_freq_for_plot("rrc3f.dat", mag, phase, size);

	/* inverse dft */
	for (i = 0; i < size; i++) {
		int j;
		temp[i] = 0.;
		for (j = 0; j < size; j++) {
			temp[i] +=
			    mag[j] * cos(phase[j]) * cos(M_PI * 2 * i * j /
							 size);
			temp[i] -=
			    mag[j] * sin(phase[j]) * sin(M_PI * 2 * i * j /
							 size);
		}
		//temp[i] = temp[i]/(double)size;
	}
	//dump_time_for_plot("rrc3t.dat", temp, size);

	/* truncate and shift */
	for (i = 0; i < taps; i++) {
		filter[i] = temp[(size - (taps - 1) / 2 + i) % size];
	}

	/* windowing */
	for (i = 0; i < taps; i++) {
		filter[i] *=
		    0.54 -
		    0.46 * cos(2. * M_PI * (double)i / (double)(size - 1));
		sum += filter[i];
	}

	/* normalize */
	for (i = 0; i < taps; i++) {
		filter[i] = filter[i] / sum;
	}

	//dump_filter_for_plot("rrc", filter, taps, 2048);
}

static void shift_filter(double freq, double *filter, double *template,
			 unsigned taps)
{
	int i;
	for (i = 0; i < taps; i++)
		filter[i] = template[i] * cos(M_PI * 2 * i * freq);
}

#define V22_BP_LENGTH  41
#define V22_RRC_LENGTH 41

#define V22_SYMBOL_RATE 600
#define V22_RRC_ROLLOFF 0.75

void gen_v22_filters(FILE * h, FILE * f)
{
	double bp_fir[V22_BP_LENGTH];
	double rrc_temp[V22_RRC_LENGTH];
	double rrc_fir[V22_RRC_LENGTH];

	fprintf(h, "\n/* v22 filters definitions */\n");
	fprintf(h, "#define V22_BP_LENGTH %d\n", V22_BP_LENGTH);

	build_v22_bp_filter(FREQ(2400 - 550), FREQ(2400 + 550), bp_fir,
			    V22_BP_LENGTH, 2048);
	TABLE_PROTO(h, "v22_bp_2400", int16_t, V22_BP_LENGTH);
	TABLE_PRINT(f, "v22_bp_2400", int16_t, bp_fir, V22_BP_LENGTH);

	build_v22_bp_filter(FREQ(1200 - 550), FREQ(1200 + 550), bp_fir,
			    V22_BP_LENGTH, 2048);
	TABLE_PROTO(h, "v22_bp_1200", int16_t, V22_BP_LENGTH);
	TABLE_PRINT(f, "v22_bp_1200", int16_t, bp_fir, V22_BP_LENGTH);

	fprintf(h, "#define V22_RRC_LENGTH %d\n", V22_RRC_LENGTH);

	build_rrc(V22_SYMBOL_RATE, V22_RRC_ROLLOFF,
		  rrc_temp, V22_RRC_LENGTH, 2048);

	shift_filter(FREQ(1200), rrc_fir, rrc_temp, V22_RRC_LENGTH);
	TABLE_PROTO(h, "v22_rrc_1200", int16_t, V22_RRC_LENGTH);
	TABLE_PRINT(f, "v22_rrc_1200", int16_t, rrc_fir, V22_RRC_LENGTH);
	//dump_filter_for_plot("rrc", rrc_fir, V22_RRC_LENGTH, 2048);

	shift_filter(FREQ(2400), rrc_fir, rrc_temp, V22_RRC_LENGTH);
	TABLE_PROTO(h, "v22_rrc_2400", int16_t, V22_RRC_LENGTH);
	TABLE_PRINT(f, "v22_rrc_2400", int16_t, rrc_fir, V22_RRC_LENGTH);
}

/*
 *
 */

static void common_print(FILE * f, const char *name)
{
	fprintf(f, "/* %s - generated by %s at %s */\n",
		name, prog_name, __DATE__);
	fprintf(f, "\n#include <stdint.h>\n\n");
}

static int gen_table(FILE * header, const char *name,
		     void (*generator) (FILE * header, FILE * source))
{
	FILE *f;

	fprintf(stderr, "generating %s...\n", name);

	f = (use_stdout) ? stdout : fopen(name, "w");
	if (!f) {
		perror("fopen");
		return -1;
	}

	common_print(f, name);
	generator(header, f);

	if (!use_stdout)
		fclose(f);
	return 0;
}

static void build_macro_name(const char *name, char *macro, int len)
{
	const char *q;
	char *p = macro;
	snprintf(p, len, "__");
	p += 2;
	len -= 2;
	for (q = name; *q && len > 0; q++, p++, len--)
		*p = (*q == '.' ? '_' : toupper(*q));
	snprintf(p, len, "__");
}

static void gen_tables(char *header_name)
{
	char macro_name[256];
	FILE *f;

	build_macro_name(header_name, macro_name, sizeof(macro_name));

	if (!(f = fopen(header_name, "w"))) {
		perror("fopen");
		exit(-1);
	}

	fprintf(f, "/* %s - generated by %s at %s */\n",
		header_name, prog_name, __DATE__);
	fprintf(f, "\n#ifndef %s\n", macro_name);
	fprintf(f, "#define %s\n", macro_name);

	gen_table(f, "cos_table.c", gen_costab);
	//gen_table(f, "v21_filters.c", gen_v21_filters);
	gen_table(f, "v22_tables.c", gen_v22_filters);

	fprintf(f, "\n#endif /* %s */\n", macro_name);
	fclose(f);
}

#if 0
static struct option long_options[] = {
	{"verbose", 0, 0, 'v'},
	{"debug", 0, 0, 'd'},
	{}
};
#endif

static int parse_cmd_line(int argc, char *const argv[])
{
	return 0;
}

int main(int argc, char *argv[])
{
	prog_name = argv[0];
	if (argc > 1 && parse_cmd_line(argc, argv) < 0)
		exit(-1);
	gen_tables("m_tables.h");
	return 0;
}
