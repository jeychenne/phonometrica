/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 04/10/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: helper routines for signal processing.                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPEECH_UTILS_HPP
#define PHONOMETRICA_SPEECH_UTILS_HPP

#include <cmath>
#include <cstdint>
#include <vector>
#include <phon/utils/matrix.hpp>

namespace phonometrica::speech {

std::vector<double> linspace(double from, double to, int num, bool include_boundaries = true);

std::vector<double> get_time_points(double from, double to, double step, bool include_boundaries = true);

static inline
double frame_to_time(intptr_t index, double Fs)
{
	return double(index) / Fs;
}

static inline
intptr_t time_to_frame(double time, double Fs)
{
	return (intptr_t) round(time * Fs);
}

double hertz_to_bark(double hz);

double bark_to_hertz(double z);

double hertz_to_erb(double hz);

double erb_to_hertz(double erb);

double hertz_to_mel(double hz);

double mel_to_hertz(double mel);

double hertz_to_semitones(double f0, double ref = 100);

double semitones_to_hertz(double st, double ref = 100);

} // namespace phonometrica::speech

#endif // PHONOMETRICA_SPEECH_UTILS_HPP
