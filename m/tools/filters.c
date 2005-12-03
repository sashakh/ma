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
#include <math.h>

#include "gen_tables.h"

static void inverse_filter(double *buf, unsigned size)
{
	int i;
	for (i = 0; i < size; i++)
		buf[i] = -buf[i];
	buf[size / 2] += 1.;
}

/*
 *  hamming windowed sinc filter
 */

void hwsinc_lp_filter(double fc, double *buf, unsigned size)
{
	double sum = 0;
	int i;
	for (i = 0; i < size; i++) {
		int k = i - size / 2;
		buf[i] = (k == 0) ? 2 * M_PI * fc : sin(M_PI * 2. * fc * k) / k;
		buf[i] *= 0.54 - 0.46 * cos(2 * M_PI * i / (size - 1));
		sum += buf[i];
	}
	for (i = 0; i < size; i++)
		buf[i] /= sum;
}

void hwsinc_hp_filter(double fc, double *buf, unsigned size)
{
	hwsinc_lp_filter(fc, buf, size);
	inverse_filter(buf, size);
}

void hwsinc_bp_filter(double fc_from, double fc_to, double *buf, unsigned size)
{
	double lowpass[size];
	double highpass[size];
	int i;
	hwsinc_lp_filter(fc_from, lowpass, arrsize(lowpass));
	hwsinc_hp_filter(fc_to, highpass, arrsize(lowpass));
	for (i = 0; i < size; i++)
		buf[i] = lowpass[i] + highpass[i];
	inverse_filter(buf, size);
}

/*
 *  Custom filters
 */

/* ... */

/*
 *  Debug stuff
 */

void dump_time_for_plot(const char *name, double *buf, unsigned size)
{
	int i;
	FILE *f = fopen(name, "w");
	if (!f) {
		perror("fopen");
		return;
	}
	fprintf(f, "# plot \"%s\" using 1:2 with lines title \"amp\"\n", name);
	fprintf(f, "# sample\tamplitude\n");
	for (i = 0; i < size; i++) {
		fprintf(f, "%d\t%f\n", i, buf[i]);
	}
	fclose(f);
}

void dump_freq_for_plot(const char *name, double *mag, double *ph,
			unsigned size)
{
	int i;
	FILE *f = fopen(name, "w");
	if (!f) {
		perror("fopen");
		return;
	}
	fprintf(f, "# plot \"%s\" using 1:2 with lines title \"mag\",\\\n"
		"#      \"%s\" using 1:3 with lines title \"phase\"\n",
		name, name);
	fprintf(f, "# freqiency\tmagnitude\tphase\tmag in dB\n");
	for (i = 0; i < size; i++) {
		double freq = (double)i * SAMPLE_RATE / (size);
		fprintf(f, "%f\t%f\t%f\t%f\n", freq, mag[i], ph[i],
			20. * log10(mag[i]));
	}
	fclose(f);
}

void dump_filter_for_plot(const char *name, double *filter, unsigned taps,
			  unsigned size)
{
	char filename[256];
	double buf[size];
	double mag[size], ph[size];
	double re[size], im[size];
	int i, j;
	sprintf(filename, "%st.%s", name, "dat");
	dump_time_for_plot(filename, filter, taps);
	for (i = 0; i < size; i++) {
		buf[i] = i < taps ? filter[i] : 0;
	}
	/* slowest dft */
	for (i = 0; i < size; i++) {
		re[i] = im[i] = 0;
		for (j = 0; j < size; j++) {
			re[i] += buf[j] * cos(2 * M_PI * i * j / size);
			im[i] -= buf[j] * sin(2 * M_PI * i * j / size);
		}
		re[i] /= size;
		im[i] /= size;
	}
	sprintf(filename, "%sf.%s", name, "dat");
	for (i = 0; i < size; i++) {
		mag[i] = sqrt(re[i] * re[i] + im[i] * im[i]);
		ph[i] = atan2(im[i], re[i]);
	}
	dump_freq_for_plot(filename, mag, ph, size);
}

#ifdef USE_FFTW3
#include <complex.h>
#include <fftw3.h>

static void show_freq_domain_fftw3(const char *name, const double *buf,
				   unsigned size)
{
	fftw_plan p;
	fftw_complex *in, *out;
	int i;

	in = fftw_malloc(sizeof(*in) * size);
	out = fftw_malloc(sizeof(*out) * size);

	p = fftw_plan_dft_1d(size, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

	for (i = 0; i < size; i++) {
		in[i] = buf[i];
	}

	fftw_execute(p);

	for (i = 0; i < size; i++) {
		double re = creal(out[i]) / size;
		double im = cimag(out[i]) / size;
		double mag = re * re + im * im;
		fprintf(stderr, "%d (%f): re = %f , im = %f ; mag^2 = %f\n",
			i, (double)i / size, re, im, mag);
		if (i > size / 2)
			break;
	}

	fftw_destroy_plan(p);
	fftw_free(in);
	fftw_free(out);
}
#endif	/* USE_FFTW3 */

static void show_freq_domain_slow_dft(const char *name, const double *buf,
				      unsigned size)
{
	double re, im, mag;
	int i, j;
	for (i = 0; i < size; i++) {
		re = im = 0;
		for (j = 0; j < size; j++) {
			re += cos(2 * M_PI * j * i / size) * buf[j];
			im -= sin(2 * M_PI * j * i / size) * buf[j];
		}
		re /= size;
		im /= size;
		mag = re * re + im * im;
		fprintf(stderr, "%d (%f): re = %f , im = %f ; mag^2 = %f\n",
			i, (double)i / size, re, im, mag);
		if (i > size / 2)
			break;
	}
}

void show_freq_domain(const char *name, const double *buf, unsigned size)
{
	fprintf(stderr, "\'%s\' frequency domain:\n", name);
#ifdef USE_FFTW3
	show_freq_domain_fftw3(name, buf, size);
#else
	show_freq_domain_slow_dft(name, buf, size);
#endif
}
