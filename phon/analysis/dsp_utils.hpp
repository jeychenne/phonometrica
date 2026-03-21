/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: utilities for digital speech processing. The code for the Gaussian window and pre-emphasis is derived from *
 * Praat, see http://www.praat.org.                                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DSP_UTILS_HPP
#define PHONOMETRICA_DSP_UTILS_HPP

#include <phon/array.hpp>

namespace phonometrica {
namespace speech {

// Apply pre-emphasis for formant analysis.
void pre_emphasis(Array<double> &data, double Fs, double threshold);

void apply_gaussian_window(std::span<double> win, size_t N);

}} // namespace phonometrica::speech

#endif // PHONOMETRICA_DSP_UTILS_HPP
