/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 1997-2005  Kåre Sjölander <kare@speech.kth.se>                                                        *
 * Copyright (C) 2019-2026 Julien Eychenne <jeychenne@gmail.com>                                                       *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 31/03/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: signal processing routines.                                                                                *
 *                                                                                                                     *
 * Note: This file contains code derived from the Snack Sound Toolkit. See file BSD.txt. The latest version can be     *
 * found at http://www.speech.kth.se/snack/.                                                                           *
 * The code for the Gaussian window is based on the description in Praat's documentation.                              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SIGNAL_PROCESSING_HPP
#define PHONOMETRICA_SIGNAL_PROCESSING_HPP

#include <cmath>
#include <complex>
#include <vector>
#include <span>
#include <phon/array.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica { namespace speech {

enum class WindowType
{
    Bartlett,
    Blackman,
    Gaussian,
    Hamming,
    Hann,
    Kaiser,
    Rectangular
};

enum class PitchTracker
{
	Harvest,
	Reaper,
	Rapt,
	Swipe,
	Praat
};

class FFT
{
public:

	FFT(intptr_t nfft);

	~FFT();

	Array<std::complex<double>> &process(const Array<double> &data);

private:

	void *impl;

	intptr_t nfft;

	Array<double> input;
	Array<std::complex<double>> output;
};


Array<double> create_window(intptr_t N, intptr_t fftlen, WindowType type);

// Get intensity for a frame.
double get_intensity(std::span<double> frame, std::span<double> window);

Array<double> get_intensity(std::span<double> input, int samplerate, intptr_t window_size, double time_step, WindowType type = WindowType::Hamming);


// Calculate LPC coefficients from a speech frame.
std::vector<double> get_lpc_coefficients(const Array<double> &frame, int npole);

// Get formant frequencies and bandwidths from a set of LPC coefficients.
bool get_formants(const std::vector<double> &lpc_coeffs, double Fs, std::vector<double> &freqs, std::vector<double> &bw);

Array<std::complex<double>> specgram(const Array<double> &data, int nfft, intptr_t noverlap, intptr_t window_size, WindowType window_type = WindowType::Hann);

Array<double> medfilt1(const Array<double> &signal, int n);

std::vector<double> get_pitch(PitchTracker algorithm, const Array<double> &input, double sample_rate, double min_pitch, double max_pitch, double time_step, double voicing_threshold, double octave_jump_cost = 0.35, double voicing_cost = 0.45);

std::vector<double> get_pitch_praat(const Array<double> &input, double sample_rate, double min_pitch, double max_pitch, double time_step, double voicing_threshold, double octave_jump_cost = 0.35, double voicing_cost = 0.45);

// Apply pre-emphasis for formant analysis.
void pre_emphasis(Array<double> &data, double Fs, double threshold);

void apply_gaussian_window(std::span<double> win, size_t N);

}} // namespace phonometrica::speech

#endif // PHONOMETRICA_SIGNAL_PROCESSING_HPP
