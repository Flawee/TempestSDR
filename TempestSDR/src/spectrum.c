/*
#-------------------------------------------------------------------------------
# Copyright (c) 2014 Martin Marinov.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html
# 
# Contributors:
#     Martin Marinov - initial API and implementation
#-------------------------------------------------------------------------------
*/
#include "spectrum.h"
#include "internaldefinitions.h"
#include "fft.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* time averaging coefficient applied to every new spectrum */
#define SPECTRUM_SMOOTHING_ALPHA (0.5)

void spectral_init(spectral_t * sp) {
	sp->fft_size = SPECTRUM_FFT_SIZE;
	sp->ring_len_floats = 2 * sp->fft_size;
	sp->ring = (float *) malloc(sizeof(float) * sp->ring_len_floats);
	sp->fftwork = (float *) malloc(sizeof(float) * sp->ring_len_floats);
	sp->ring_pos = 0;
	sp->samplerate = 0;
	sp->samples_since_last = 0;
	extbuffer_init_double(&sp->out);
}

void spectral_free(spectral_t * sp) {
	free(sp->ring);
	free(sp->fftwork);
	sp->ring = NULL;
	sp->fftwork = NULL;
	extbuffer_free(&sp->out);
}

void spectral_run(spectral_t * sp, float * iq, int complex_samples, uint32_t samplerate, tsdr_lib_t * tsdr) {
	if (sp->ring == NULL || complex_samples <= 0 || samplerate == 0) return;

	/* if the sample rate changed, restart the accumulation */
	if (samplerate != sp->samplerate) {
		sp->samplerate = samplerate;
		sp->samples_since_last = 0;
	}

	/* append the incoming raw IQ samples into the ring buffer, dropping the oldest ones */
	const int floats = complex_samples * 2;
	int i;
	for (i = 0; i < floats; i++) {
		sp->ring[sp->ring_pos] = iq[i];
		sp->ring_pos++;
		if (sp->ring_pos >= sp->ring_len_floats) sp->ring_pos = 0;
	}
	sp->samples_since_last += complex_samples;

	/* push a new spectrum when a full FFT window is available and at least ~100ms worth of data arrived */
	if (sp->samples_since_last < (uint64_t) sp->fft_size) return;
	if (sp->samples_since_last < samplerate / 10) return;
	sp->samples_since_last = 0;

	/* copy the ring (oldest sample first) into a contiguous work buffer, preserving sample order */
	int p = sp->ring_pos;
	for (i = 0; i < sp->ring_len_floats; i++) {
		sp->fftwork[i] = sp->ring[p];
		p++;
		if (p >= sp->ring_len_floats) p = 0;
	}

	fft_perform(sp->fftwork, sp->fft_size, 0);

	/*
	* Fill the output with the magnitude spectrum, re-ordered so that index 0 corresponds to -samplerate/2
	* and the middle of the array corresponds to the tuned (centre) frequency.
	* A light time averaging keeps the trace stable.
	*/
	extbuffer_preparetohandle(&sp->out, sp->fft_size);
	double * od = sp->out.dbuffer;
	const int fft = sp->fft_size;
	for (i = 0; i < fft; i++) {
		const float I = sp->fftwork[i << 1];
		const float Q = sp->fftwork[(i << 1) + 1];
		const double magnitude = sqrt((double) I * I + (double) Q * Q);
		const int outidx = (i + fft / 2) % fft;
		od[outidx] = magnitude * SPECTRUM_SMOOTHING_ALPHA + od[outidx] * (1.0 - SPECTRUM_SMOOTHING_ALPHA);
	}

	/* data_offset carries the FFT size so the GUI can map the bins to real frequencies */
	announce_plotready(tsdr, PLOT_ID_SPECTRUM, &sp->out, sp->fft_size, sp->fft_size, samplerate);
}