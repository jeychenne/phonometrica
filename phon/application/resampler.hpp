/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 11/10/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: audio resampler based on SPEEX.                                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_RESAMPLER_HPP
#define PHONOMETRICA_RESAMPLER_HPP

#include <cstdint>
#include <phon/array.hpp>


#include <phon/third_party/r8brain/CDSPResampler.h>

namespace phonometrica {

using Resampler = r8b::CDSPResampler24;

Array<double> resample(std::span<double> input, double input_rate, double output_rate);

} // namespace phonometrica



#endif // PHONOMETRICA_RESAMPLER_HPP
