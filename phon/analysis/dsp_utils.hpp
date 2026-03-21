/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2025 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
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
