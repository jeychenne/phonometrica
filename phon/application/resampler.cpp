/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 11/10/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cassert>
#include <cmath>
#include <phon/application/resampler.hpp>
#include <phon/error.hpp>

namespace phonometrica {

Array<double> resample(std::span<double> input, double input_rate, double output_rate)
{
	assert(input_rate > 0);
	assert(output_rate > 0);
	const int BUFFER_SIZE = 1024;
	double buffer[BUFFER_SIZE];
	memset(buffer, 0, sizeof(double) * BUFFER_SIZE);
	Resampler resampler(input_rate, output_rate, BUFFER_SIZE);
	intptr_t ol = double(input.size()) * output_rate / input_rate;
	Array<double> output(ol, 0.0);
	auto it = input.begin();
	double *out = nullptr;
	double *data = output.data();

	while (ol > 0)
	{
		intptr_t count = (it + BUFFER_SIZE > input.end()) ? intptr_t(input.end() - it) : BUFFER_SIZE;
		std::span<double> chunk;

		if (count == 0)
		{
			chunk = std::span<double>(buffer, BUFFER_SIZE);
		}
		else
		{
			chunk = std::span<double>(it, count);
		}
		intptr_t len = resampler.process(chunk.data(), (int)chunk.size(), out);
		if (len > ol) len = ol;
		std::copy(out, out+len, data);
		it += count;
		data += len;
		ol -= len;
	}

	return output;
}

} // namespace phonometrica
