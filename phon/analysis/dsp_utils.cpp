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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/analysis/dsp_utils.hpp>

namespace phonometrica {
namespace speech {

// Adapted from Praat's pre-emphasis routine in Sound_to_Formant.cpp
// Copyright (C) 1992-2008,2010-2012,2014-2020 Paul Boersma
// License: GPL 2 or later

void pre_emphasis(Array<double> &data, double Fs, double threshold)
{
	auto x = data.data();
	double alpha = exp(-2 * M_PI * threshold * (1.0 / Fs));
	auto len = data.size();

	for (auto i = len-1; i >= 1; i--) {
		x[i] -= alpha * x[i-1];
	}
}

void apply_gaussian_window(std::span<double> win, size_t N)
{
	double imid = 0.5 * (N + 1), edge = exp (-12.0);
	for (size_t i = 1; i <= N; i++) {
		win[i-1] = (exp (-48.0 * (i - imid) * (i - imid) / (N + 1) / (N + 1)) - edge) / (1.0 - edge);
	}
}

}} // namespace phonometrica::speech