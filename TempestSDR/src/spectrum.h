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
#ifndef SPECTRUM_H_
#define SPECTRUM_H_

#include <stdint.h>
#include "extbuffer.h"
#include "include/TSDRLibrary.h"

#define SPECTRUM_FFT_SIZE (2048)

typedef struct spectral {

		int fft_size;
		float * ring;             /* ring buffer of raw IQ samples (2 floats per complex sample) */
		int ring_len_floats;      /* fft_size * 2 */
		int ring_pos;             /* next write position (in floats) */

		float * fftwork;          /* contiguous work buffer for the FFT (ring_len_floats floats) */

		uint32_t samplerate;      /* current sample rate, kept between calls */
		uint64_t samples_since_last; /* complex samples accumulated since the last spectrum was announced */

		extbuffer_t out;          /* output spectrum (linear magnitudes, EXTBUFFER_TYPE_DOUBLE) */

} spectral_t;

void spectral_init(spectral_t * sp);
void spectral_free(spectral_t * sp);
void spectral_run(spectral_t * sp, float * iq, int complex_samples, uint32_t samplerate, tsdr_lib_t * tsdr);

#endif