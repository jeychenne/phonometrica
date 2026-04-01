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
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Power spectrum (spectral slice) computed from a windowed segment of a sound file. Given a sound, a         *
 *          channel, a time window [t1, t2], and analysis parameters, this class extracts the segment, applies a        *
 *          window function, computes the FFT, and stores the resulting power spectral density in dB.                   *
 *                                                                                                                     *
 *          Spectrum is a Document subclass, so it can be saved to disk, added to a project, and manipulated            *
 *          uniformly with other file types (Sound, Annotation, Script, etc.).                                          *
 *                                                                                                                     *
 *          File format (.phon-spectrum): a simple text format with a header section followed by one dB value           *
 *          per line. See write() for details.                                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPECTRUM_HPP
#define PHONOMETRICA_SPECTRUM_HPP

#include <vector>
#include <phon/application/sound.hpp>
#include <phon/analysis/signal_processing.hpp>

#define PHON_EXT_SPECTRUM  ".phon-spectrum"

namespace phonometrica {

class Spectrum final : public Document
{
public:

	/// Construct a Spectrum by loading it from a saved file.
	explicit Spectrum(Directory *parent, String path = String());

	/// Construct a Spectrum by computing it from a sound segment.
	///
	/// @param parent           Parent directory in the project's VFS.
	/// @param sound            The sound file to analyse.
	/// @param channel          Channel index: 0 = average, 1..N = specific channel.
	/// @param t1               Start time of the analysis window (seconds).
	/// @param t2               End time of the analysis window (seconds).
	/// @param window_type      Window function applied before the FFT.
	/// @param zero_padding     Zero-padding factor (1 = no padding, 2 = double, etc.).
	///                         Higher values interpolate the spectrum for smoother display.
	/// @param preemph          Pre-emphasis threshold frequency (Hz). 0 disables pre-emphasis.
	/// @param max_frequency    Maximum frequency to retain (Hz). 0 = Nyquist.
	/// @param dynamic_range    Dynamic range in dB below the peak. Bins below this floor
	///                         are clamped. 0 = no clamping.
	Spectrum(Directory *parent,
	         const Handle<Sound> &sound,
	         int channel,
	         double t1,
	         double t2,
	         speech::WindowType window_type = speech::WindowType::Gaussian,
	         int zero_padding = 2,
	         double preemph = 50.0,
	         double max_frequency = 0.0,
	         double dynamic_range = 70.0);

	/// Number of frequency bins in the spectrum.
	intptr_t bin_count() const { return static_cast<intptr_t>(m_power_dB.size()); }

	/// Frequency resolution: spacing between consecutive bins (Hz).
	double frequency_resolution() const { return m_freq_resolution; }

	/// Maximum frequency in the spectrum (Hz). May be less than Nyquist if max_frequency was set.
	double max_frequency() const { return m_max_freq; }

	/// Nyquist frequency of the source sound (Hz).
	double nyquist_frequency() const { return m_nyquist; }

	/// Sample rate of the source sound (Hz).
	int sample_rate() const { return m_sample_rate; }

	/// Duration of the analysis window (seconds).
	double window_duration() const { return m_t2 - m_t1; }

	/// Start time of the analysis window (seconds).
	double start_time() const { return m_t1; }

	/// End time of the analysis window (seconds).
	double end_time() const { return m_t2; }

	/// The effective bandwidth of the analysis in Hz.
	double bandwidth() const { return m_bandwidth; }

	/// FFT size used (including zero-padding).
	intptr_t fft_size() const { return m_nfft; }

	/// Power spectral density in dB for each frequency bin.
	const std::vector<double> &power_dB() const { return m_power_dB; }

	/// Frequency in Hz for a given bin index (0-based).
	double bin_frequency(intptr_t bin) const { return bin * m_freq_resolution; }

	/// Peak power in dB across all bins.
	double peak_dB() const { return m_peak_dB; }

	/// Floor power in dB (peak minus dynamic range).
	double floor_dB() const { return m_floor_dB; }

	/// Path to the source sound file (may be empty if the sound was not saved).
	const String &sound_path() const { return m_sound_path; }

	/// Channel that was analysed.
	int channel() const { return m_channel; }

	/// Window type that was used for the analysis.
	speech::WindowType window_type() const { return m_window_type; }

	/// Register the Spectrum type with the scripting runtime.
	static void initialize(Runtime &rt);

private:

	void load() override;

	void write() override;

	void compute(const Handle<Sound> &sound,
	             int zero_padding, double preemph,
	             double max_frequency, double dynamic_range);

	/// Find the smallest power-of-two >= n.
	static intptr_t next_power_of_two(intptr_t n);

	/// Helpers for serialization.
	static String window_type_to_string(speech::WindowType wt);
	static speech::WindowType string_to_window_type(const String &s);

	// ── Source metadata ────────────────────────────
	String m_sound_path;
	int m_channel = 0;
	speech::WindowType m_window_type = speech::WindowType::Gaussian;

	// ── Analysis parameters ────────────────────────
	double m_t1 = 0;
	double m_t2 = 0;
	int m_sample_rate = 0;
	double m_nyquist = 0;
	double m_max_freq = 0;
	double m_freq_resolution = 0;
	double m_bandwidth = 0;
	intptr_t m_nfft = 0;
	double m_peak_dB = 0;
	double m_floor_dB = 0;

	/// Power spectral density in dB, one value per frequency bin (0 .. nfft/2).
	std::vector<double> m_power_dB;
};


namespace traits {
template<> struct maybe_cyclic<Spectrum> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTRUM_HPP
