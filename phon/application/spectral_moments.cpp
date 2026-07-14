/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 11/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <complex>
#include <algorithm>
#include <phon/application/spectral_moments.hpp>
#include <phon/error.hpp>
#include <phon/third_party/pocketfft-cpp/pocketfft_hdronly.h>

namespace phonometrica {

// ─────────────────────────────────────────────────────────────────────────────
//  Low-level: compute moments from a linear power spectrum
// ─────────────────────────────────────────────────────────────────────────────

SpectralMoments compute_spectral_moments(const double *power, intptr_t bin_count,
                                         double freq_resolution,
                                         double min_freq, double max_freq)
{
	SpectralMoments result;

	if (bin_count <= 0 || freq_resolution <= 0) return result;

	intptr_t lo = static_cast<intptr_t>(std::ceil(min_freq / freq_resolution));
	intptr_t hi = static_cast<intptr_t>(std::floor(max_freq / freq_resolution));

	lo = std::max<intptr_t>(lo, 0);
	hi = std::min<intptr_t>(hi, bin_count - 1);

	if (lo > hi) return result;

	// ── Step 1: total power (normalisation constant) ────────────────────
	double total = 0;
	for (intptr_t k = lo; k <= hi; k++) {
		total += power[k];
	}

	if (total <= 0) return result; // silence → NaN

	// ── Step 2: centre of gravity (1st moment) ─────────────────────────
	double mu1 = 0;
	for (intptr_t k = lo; k <= hi; k++) {
		double f = k * freq_resolution;
		mu1 += f * power[k];
	}
	mu1 /= total;

	// ── Step 3: spread (2nd moment) ────────────────────────────────────
	double mu2 = 0;
	for (intptr_t k = lo; k <= hi; k++) {
		double f = k * freq_resolution;
		double d = f - mu1;
		mu2 += d * d * power[k];
	}
	mu2 /= total;
	double sigma = std::sqrt(mu2);

	// ── Step 4: skewness (3rd moment) ──────────────────────────────────
	double mu3 = 0;
	if (sigma > 0)
	{
		for (intptr_t k = lo; k <= hi; k++) {
			double f = k * freq_resolution;
			double d = f - mu1;
			mu3 += d * d * d * power[k];
		}
		mu3 /= total;
		mu3 /= (sigma * sigma * sigma);
	}

	// ── Step 5: excess kurtosis (4th moment − 3) ───────────────────────
	double mu4 = 0;
	if (sigma > 0)
	{
		for (intptr_t k = lo; k <= hi; k++) {
			double f = k * freq_resolution;
			double d = f - mu1;
			double d2 = d * d;
			mu4 += d2 * d2 * power[k];
		}
		mu4 /= total;
		mu4 /= (mu2 * mu2);
		mu4 -= 3.0;
	}

	result.cog = mu1;
	result.spread = sigma;
	result.skewness = mu3;
	result.kurtosis = mu4;

	return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  High-level: compute moments at a time point in a sound file
// ─────────────────────────────────────────────────────────────────────────────

static intptr_t next_power_of_two(intptr_t n)
{
	intptr_t p = 1;
	while (p < n) p <<= 1;
	return p;
}

SpectralMoments compute_spectral_moments_at(const Handle<Sound> &sound,
                                            int channel,
                                            double time,
                                            double window_duration,
                                            speech::WindowType window_type,
                                            double min_freq,
                                            double max_freq,
                                            double preemph)
{
	sound->open();

	int sr = sound->sample_rate();
	double nyquist = sr / 2.0;

	if (max_freq <= 0 || max_freq > nyquist) max_freq = nyquist;
	if (min_freq < 0) min_freq = 0;

	// ── Determine sample range centred on time ─────────────────────────
	double half = window_duration / 2.0;
	double t1 = time - half;
	double t2 = time + half;

	// Clamp to sound boundaries.
	if (t1 < 0) { t2 -= t1; t1 = 0; }
	if (t2 > sound->duration()) { t1 -= (t2 - sound->duration()); t2 = sound->duration(); }
	if (t1 < 0) t1 = 0;

	intptr_t first_sample = sound->time_to_frame(t1);
	intptr_t last_sample  = sound->time_to_frame(t2);

	first_sample = std::max<intptr_t>(first_sample, 1);
	last_sample  = std::min<intptr_t>(last_sample, sound->nframes());

	if (last_sample <= first_sample) {
		return {}; // too short
	}

	auto segment = sound->get_channel(channel, first_sample, last_sample);
	intptr_t N = segment.size();

	// ── Pre-emphasis ───────────────────────────────────────────────────
	if (preemph > 0.0) {
		speech::pre_emphasis(segment, sr, preemph);
	}

	// ── Apply window function ──────────────────────────────────────────
	auto window = speech::create_window(N, N, window_type);
	for (intptr_t i = 0; i < N; i++) {
		segment[i] *= window[i];
	}

	// ── FFT ────────────────────────────────────────────────────────────
	intptr_t nfft = next_power_of_two(N);
	intptr_t n_bins = nfft / 2 + 1;
	double freq_resolution = static_cast<double>(sr) / nfft;

	std::vector<double> input(nfft, 0.0);
	for (intptr_t i = 0; i < N; i++) {
		input[i] = segment[i];
	}

	std::vector<std::complex<double>> output(n_bins, std::complex<double>(0, 0));

	pocketfft::shape_t shape{static_cast<size_t>(nfft)};
	pocketfft::stride_t stride_in{sizeof(double)};
	pocketfft::stride_t stride_out{sizeof(std::complex<double>)};
	pocketfft::r2c(shape, stride_in, stride_out, {0}, true,
	               input.data(), output.data(), 1.0);

	// ── Build linear power spectrum ────────────────────────────────────
	// We need linear power (not dB) for the moment computation.
	// Normalisation is not critical since the moments normalise by total power,
	// but we keep it consistent with Spectrum::compute().
	double norm = static_cast<double>(sr) * N;
	std::vector<double> linear_power(n_bins);

	for (intptr_t k = 0; k < n_bins; k++)
	{
		double re = output[k].real();
		double im = output[k].imag();
		double p = (re * re + im * im) / norm;

		// One-sided spectrum: double non-DC, non-Nyquist bins.
		if (k > 0 && k < n_bins - 1) {
			p *= 2.0;
		}

		linear_power[k] = p;
	}

	return compute_spectral_moments(linear_power, freq_resolution, min_freq, max_freq);
}

} // namespace phonometrica
