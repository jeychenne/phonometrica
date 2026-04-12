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
 * Purpose: Spectral moments computation (centre of gravity, spread, skewness, kurtosis) from a power spectrum.        *
 *          These four moments characterise the shape of the spectral distribution and are widely used in phonetics     *
 *          for the analysis of fricatives and other sounds. The power spectrum is treated as a probability              *
 *          distribution over frequency, and the moments are the standard statistical moments of that distribution.     *
 *                                                                                                                     *
 *          This module is Qt-free and belongs to the application layer.                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPECTRAL_MOMENTS_HPP
#define PHONOMETRICA_SPECTRAL_MOMENTS_HPP

#include <cmath>
#include <vector>
#include <phon/array.hpp>
#include <phon/application/sound.hpp>
#include <phon/analysis/signal_processing.hpp>

namespace phonometrica {

/// The four spectral moments.
struct SpectralMoments
{
	double cog      = std::nan("");   ///< Centre of gravity (1st moment), in Hz
	double spread   = std::nan("");   ///< Standard deviation (2nd moment), in Hz
	double skewness = std::nan("");   ///< Skewness (3rd moment), dimensionless
	double kurtosis = std::nan("");   ///< Excess kurtosis (4th moment), dimensionless
};

/// Compute spectral moments from a linear power spectrum.
///
/// @param power           Linear power values (not dB), one per frequency bin.
///                         Indices are 0-based.
/// @param bin_count       Number of bins in @p power.
/// @param freq_resolution Frequency spacing between consecutive bins (Hz).
/// @param min_freq        Lower bound of the frequency range to consider (Hz).
/// @param max_freq        Upper bound of the frequency range to consider (Hz).
///
/// @return The four spectral moments. If the total power in the range is zero
///         (silence), all moments are NaN.
SpectralMoments compute_spectral_moments(const double *power, intptr_t bin_count,
                                         double freq_resolution,
                                         double min_freq, double max_freq);

/// Convenience overload for std::vector.
inline SpectralMoments compute_spectral_moments(const std::vector<double> &power,
                                                double freq_resolution,
                                                double min_freq, double max_freq)
{
	return compute_spectral_moments(power.data(), static_cast<intptr_t>(power.size()),
	                                freq_resolution, min_freq, max_freq);
}

/// Compute spectral moments at a single time point in a sound file.
///
/// This is the high-level function used by both manual measurement (sound/annotation view)
/// and acoustic queries. It extracts a windowed segment centred on @p time, optionally
/// applies pre-emphasis, computes the FFT, and returns the four moments.
///
/// @param sound           The sound file.
/// @param channel         Channel to analyse (0 = average, 1..N = specific).
/// @param time            Centre of the analysis window (seconds).
/// @param window_duration Duration of the analysis window (seconds).
/// @param window_type     Window function to apply before the FFT.
/// @param min_freq        Lower bound of the frequency range (Hz). 0 = DC.
/// @param max_freq        Upper bound of the frequency range (Hz). 0 = Nyquist.
/// @param preemph         Pre-emphasis threshold frequency (Hz). 0 = disabled.
SpectralMoments compute_spectral_moments_at(const Handle<Sound> &sound,
                                            int channel,
                                            double time,
                                            double window_duration,
                                            speech::WindowType window_type,
                                            double min_freq,
                                            double max_freq,
                                            double preemph);

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTRAL_MOMENTS_HPP
