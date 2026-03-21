/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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