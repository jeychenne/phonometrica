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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <complex>
#include <phon/file.hpp>
#include <phon/runtime.hpp>
#include <phon/application/bindings.hpp>
#include <phon/application/spectrum.hpp>
#include <phon/application/resampler.hpp>
#include <phon/application/spectral_moments.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/third_party/pocketfft-cpp/pocketfft_hdronly.h>

namespace phonometrica {

// ─────────────────────────────────────────────────────────────────────────────
//  Constructors
// ─────────────────────────────────────────────────────────────────────────────

Spectrum::Spectrum(Directory *parent, String path) :
	Document(parent, std::move(path))
{
}

Spectrum::Spectrum(Directory *parent, const Handle<Sound> &sound, int channel,
                   double t1, double t2, speech::WindowType window_type,
                   int zero_padding, double preemph, double max_frequency,
                   double dynamic_range, int lpc_order) :
	Document(parent, String()),
	m_sound_path(sound->path()),
	m_channel(channel),
	m_window_type(window_type),
	m_t1(t1), m_t2(t2),
	m_sample_rate(sound->sample_rate()),
	m_nyquist(sound->sample_rate() / 2.0),
	m_peak_dB(-std::numeric_limits<double>::infinity())
{
	if (t1 >= t2) {
		throw error("Spectrum: start time must be less than end time (t1 = %, t2 = %)", t1, t2);
	}
	if (t1 < 0) {
		throw error("Spectrum: start time must be non-negative (t1 = %)", t1);
	}
	if (t2 > sound->duration()) {
		throw error("Spectrum: end time exceeds sound duration (t2 = %, duration = %)", t2, sound->duration());
	}
	if (zero_padding < 1) {
		zero_padding = 1;
	}

	compute(sound, zero_padding, preemph, max_frequency, dynamic_range);

	// Compute LPC spectral envelope if requested.
	if (lpc_order > 0) {
		compute_lpc(sound, lpc_order, preemph);
	}

	m_loaded = true;
}


// ─────────────────────────────────────────────────────────────────────────────
//  FFT Computation
// ─────────────────────────────────────────────────────────────────────────────

void Spectrum::compute(const Handle<Sound> &sound, int zero_padding, double preemph,
                       double max_frequency, double dynamic_range)
{
	// ── Extract audio segment ──────────────────────
	intptr_t first_sample = sound->time_to_frame(m_t1);
	intptr_t last_sample = sound->time_to_frame(m_t2);

	// Clamp to valid range (1-based indexing).
	first_sample = std::max<intptr_t>(first_sample, 1);
	last_sample = std::min<intptr_t>(last_sample, sound->nframes());

	if (last_sample <= first_sample) {
		throw error("Spectrum: analysis window is too short");
	}

	auto segment = sound->get_channel(m_channel, first_sample, last_sample);
	intptr_t N = segment.size(); // number of samples in the segment

	// ── Pre-emphasis ───────────────────────────────
	if (preemph > 0.0) {
		speech::pre_emphasis(segment, m_sample_rate, preemph);
	}

	// ── Apply window function ──────────────────────
	auto window = speech::create_window(N, N, m_window_type);

	for (intptr_t i = 0; i < N; i++) {
		segment[i] *= window[i];
	}

	// ── Compute effective bandwidth ────────────────
	double T = static_cast<double>(N) / m_sample_rate;
	switch (m_window_type)
	{
		case speech::WindowType::Gaussian:
			m_bandwidth = std::sqrt(2.0 * 12.0 * std::log(2.0)) / (M_PI * T);
			break;
		case speech::WindowType::Hann:
			m_bandwidth = 2.0 / T;
			break;
		case speech::WindowType::Hamming:
			m_bandwidth = 1.81 / T;
			break;
		case speech::WindowType::Blackman:
			m_bandwidth = 2.52 / T;
			break;
		case speech::WindowType::Kaiser:
			m_bandwidth = 2.0 / T;
			break;
		default:
			m_bandwidth = 1.0 / T; // rectangular
			break;
	}

	// ── FFT ────────────────────────────────────────
	m_nfft = next_power_of_two(N) * zero_padding;
	intptr_t n_bins = m_nfft / 2 + 1;
	m_freq_resolution = static_cast<double>(m_sample_rate) / m_nfft;

	// Prepare zero-padded input buffer for pocketfft.
	std::vector<double> input(m_nfft, 0.0);
	for (intptr_t i = 0; i < N; i++) {
		input[i] = segment[i];
	}

	std::vector<std::complex<double>> output(n_bins, std::complex<double>(0, 0));

	pocketfft::shape_t shape{static_cast<size_t>(m_nfft)};
	pocketfft::stride_t stride_in{sizeof(double)};
	pocketfft::stride_t stride_out{sizeof(std::complex<double>)};
	pocketfft::r2c(shape, stride_in, stride_out, {0}, true,
	               input.data(), output.data(), 1.0);

	// ── Power spectrum (dB) ────────────────────────
	double norm = static_cast<double>(m_sample_rate) * N;
	constexpr double epsilon = 1e-300;

	if (max_frequency <= 0 || max_frequency > m_nyquist) {
		max_frequency = m_nyquist;
	}
	m_max_freq = max_frequency;

	intptr_t max_bin = static_cast<intptr_t>(std::ceil(max_frequency / m_freq_resolution));
	max_bin = std::min(max_bin, n_bins - 1);

	m_power_dB.resize(max_bin + 1);

	for (intptr_t k = 0; k <= max_bin; k++)
	{
		double re = output[k].real();
		double im = output[k].imag();
		double power = (re * re + im * im) / norm;

		// DC and Nyquist bins are not doubled; all others are (one-sided spectrum).
		if (k > 0 && k < n_bins - 1) {
			power *= 2.0;
		}

		double dB = 10.0 * std::log10(std::max(power, epsilon));
		m_power_dB[k] = dB;

		if (dB > m_peak_dB) {
			m_peak_dB = dB;
		}
	}

	// ── Dynamic range clamping ─────────────────────
	if (dynamic_range > 0 && std::isfinite(m_peak_dB))
	{
		m_floor_dB = m_peak_dB - dynamic_range;
		for (auto &dB : m_power_dB) {
			if (dB < m_floor_dB) {
				dB = m_floor_dB;
			}
		}
	}
	else
	{
		m_floor_dB = m_peak_dB;
		for (auto dB : m_power_dB) {
			if (dB < m_floor_dB) {
				m_floor_dB = dB;
			}
		}
	}
}


// ─────────────────────────────────────────────────────────────────────────────
//  LPC Spectrum Computation
//
//  The LPC spectral envelope is the frequency response of the all-pole model
//  estimated from the signal segment using Burg's method:
//
//      H(f) = 1 / |A(e^{j2πf/Fs})|²
//
//  where A(z) = 1 + a₁z⁻¹ + a₂z⁻² + … + aₚz⁻ᵖ.
//
//  To match the formant tracker's behaviour and produce a meaningful spectral
//  envelope, the signal is first resampled to Fs = 2 × max_frequency. This
//  concentrates all LPC poles in the displayed frequency range — without this
//  step the poles are spread across the full Nyquist bandwidth and only one or
//  two may fall within the displayed range.
//
//  The signal preparation mirrors Sound::get_formants():
//    1. Resample to Fs = 2 × max_frequency.
//    2. Apply pre-emphasis.
//    3. Apply a Gaussian window.
//    4. Compute LPC coefficients (Burg's method).
//
//  The envelope is evaluated at the same frequency bins as the FFT spectrum
//  and shifted vertically so its peak matches the FFT peak, giving a visually
//  coherent overlay.
// ─────────────────────────────────────────────────────────────────────────────

void Spectrum::compute_lpc(const Handle<Sound> &sound,
                           int lpc_order, double preemph)
{
	m_lpc_order = lpc_order;

	// Re-extract the segment from the sound.
	intptr_t first_sample = sound->time_to_frame(m_t1);
	intptr_t last_sample = sound->time_to_frame(m_t2);
	first_sample = std::max<intptr_t>(first_sample, 1);
	last_sample = std::min<intptr_t>(last_sample, sound->nframes());

	auto segment = sound->get_channel(m_channel, first_sample, last_sample);

	// ── Resample to concentrate poles in the displayed range ──
	// Target sample rate = 2 × max displayed frequency, matching the formant
	// tracker (see Sound::get_formants).
	double Fs = 2.0 * m_max_freq;

	Array<double> resampled_buf;
	Array<double> *signal_ptr;

	if (Fs < m_sample_rate - 1.0)
	{
		resampled_buf = resample(segment, m_sample_rate, Fs);
		if (preemph > 0.0) {
			speech::pre_emphasis(resampled_buf, Fs, preemph);
		}
		signal_ptr = &resampled_buf;
	}
	else
	{
		// max_frequency is already at Nyquist — no resampling needed.
		Fs = m_sample_rate;
		if (preemph > 0.0) {
			speech::pre_emphasis(segment, Fs, preemph);
		}
		signal_ptr = &segment;
	}

	Array<double> &signal = *signal_ptr;
	intptr_t nframe = signal.size();

	// ── Apply Gaussian window (matching formant tracker) ──
	auto win = speech::create_window(nframe, nframe, speech::WindowType::Gaussian);
	Array<double> buffer(nframe, 0.0);

	for (intptr_t j = 0; j < nframe; j++) {
		buffer[j] = signal[j] * win[j];
	}

	// ── Compute LPC coefficients (Burg's method) ──
	auto coeffs = speech::get_lpc_coefficients(buffer, lpc_order);

	if (coeffs.empty()) {
		return; // ill-conditioned signal — skip LPC
	}

	// ── Evaluate the all-pole frequency response ──
	intptr_t n_bins = static_cast<intptr_t>(m_power_dB.size());
	m_lpc_dB.resize(n_bins);

	constexpr double epsilon = 1e-300;
	double lpc_peak = -std::numeric_limits<double>::infinity();

	int p = static_cast<int>(coeffs.size());

	for (intptr_t k = 0; k < n_bins; k++)
	{
		double freq = k * m_freq_resolution;

		// Use the resampled rate Fs for the frequency-to-angle mapping.
		// Since Fs = 2 × max_freq, frequencies from 0 to max_freq map to
		// ω ∈ [0, π], which covers the full spectrum of the resampled signal.
		double omega = 2.0 * M_PI * freq / Fs;

		// Evaluate A(e^{jω}) = Σ_{i=0}^{p-1} a_i · e^{-jωi}
		double re = 0.0, im = 0.0;
		for (int i = 0; i < p; i++)
		{
			double angle = i * omega;
			re += coeffs[i] * std::cos(angle);
			im -= coeffs[i] * std::sin(angle);
		}

		// |A(e^{jω})|²
		double mag_sq = re * re + im * im;

		// H(f) = 1 / |A|² → dB = -10 log10(|A|²)
		double dB = -10.0 * std::log10(std::max(mag_sq, epsilon));
		m_lpc_dB[k] = dB;

		if (dB > lpc_peak) {
			lpc_peak = dB;
		}
	}

	// ── Shift the LPC envelope to match the FFT peak ──
	// This makes the overlay visually coherent: the two curves share the same
	// vertical reference.
	double shift = m_peak_dB - lpc_peak;
	for (auto &dB : m_lpc_dB) {
		dB += shift;
	}

	// ── Clamp to the same dynamic-range floor ─────
	for (auto &dB : m_lpc_dB) {
		if (dB < m_floor_dB) {
			dB = m_floor_dB;
		}
	}
}


intptr_t Spectrum::next_power_of_two(intptr_t n)
{
	if (n <= 0) return 1;

	intptr_t p = 1;
	while (p < n) {
		p <<= 1;
	}
	return p;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Serialization
// ─────────────────────────────────────────────────────────────────────────────
//
// File format (.phon-spectrum) — plain text, UTF-8:
//
//   [header]
//   sound_path = /path/to/sound.wav
//   channel = 1
//   t1 = 0.5000
//   t2 = 0.5300
//   sample_rate = 44100
//   nfft = 2048
//   max_frequency = 22050.0
//   bandwidth = 43.27
//   window_type = Gaussian
//   peak_dB = -12.34
//   floor_dB = -82.34
//   bin_count = 1025
//   lpc_order = 14
//
//   [data]
//   -82.34
//   -78.12
//   ...
//
//   [lpc]          ← optional section, present only when lpc_order > 0
//   -45.23
//   -42.10
//   ...
//
// ─────────────────────────────────────────────────────────────────────────────

void Spectrum::write()
{
	File file(m_path, File::Write, Encoding::Utf8);

	file.write_line("[header]");
	file.write_line(String::format("sound_path = %s", m_sound_path.data()));
	file.write_line(String::format("channel = %d", m_channel));
	file.write_line(String::format("t1 = %.10f", m_t1));
	file.write_line(String::format("t2 = %.10f", m_t2));
	file.write_line(String::format("sample_rate = %d", m_sample_rate));
	file.write_line(String::format("nfft = %td", m_nfft));
	file.write_line(String::format("max_frequency = %.6f", m_max_freq));
	file.write_line(String::format("freq_resolution = %.10f", m_freq_resolution));
	file.write_line(String::format("bandwidth = %.6f", m_bandwidth));
	file.write_line(String::format("nyquist = %.6f", m_nyquist));
	file.write_line(String::format("window_type = %s", window_type_to_string(m_window_type).data()));
	file.write_line(String::format("peak_dB = %.10f", m_peak_dB));
	file.write_line(String::format("floor_dB = %.10f", m_floor_dB));
	file.write_line(String::format("bin_count = %td", static_cast<intptr_t>(m_power_dB.size())));
	file.write_line(String::format("lpc_order = %d", m_lpc_order));
	file.write_line("");
	file.write_line("[data]");

	for (auto dB : m_power_dB) {
		file.write_line(String::format("%.10f", dB));
	}

	// Write LPC section only if LPC was computed.
	if (!m_lpc_dB.empty())
	{
		file.write_line("");
		file.write_line("[lpc]");

		for (auto dB : m_lpc_dB) {
			file.write_line(String::format("%.10f", dB));
		}
	}

	file.close();
}

void Spectrum::load()
{
	File file(m_path, File::Read, Encoding::Utf8);
	bool in_header = false;
	bool in_data = false;
	bool in_lpc = false;
	intptr_t expected_bins = 0;

	auto lines = file.read_lines();

	for (intptr_t i = 1; i <= lines.size(); i++)
	{
		auto line = lines.get(i).to<String>();
		line.trim();

		if (line.empty()) continue;

		if (line == "[header]") {
			in_header = true;
			in_data = false;
			in_lpc = false;
			continue;
		}
		if (line == "[data]") {
			in_header = false;
			in_data = true;
			in_lpc = false;
			m_power_dB.clear();
			if (expected_bins > 0) {
				m_power_dB.reserve(expected_bins);
			}
			continue;
		}
		if (line == "[lpc]") {
			in_header = false;
			in_data = false;
			in_lpc = true;
			m_lpc_dB.clear();
			if (expected_bins > 0) {
				m_lpc_dB.reserve(expected_bins);
			}
			continue;
		}

		if (in_header)
		{
			auto eq = line.find('=', line.begin());
			if (eq == line.end()) continue;

			auto key = String(line.mid(line.begin(), eq));
			auto val = String(line.mid(eq + 1, line.end()));
			key.trim();
			val.trim();

			if (key == "sound_path")           m_sound_path = val;
			else if (key == "channel")         m_channel = std::stoi(std::string(val.data(), val.size()));
			else if (key == "t1")              m_t1 = std::stod(std::string(val.data(), val.size()));
			else if (key == "t2")              m_t2 = std::stod(std::string(val.data(), val.size()));
			else if (key == "sample_rate")     m_sample_rate = std::stoi(std::string(val.data(), val.size()));
			else if (key == "nfft")            m_nfft = std::stoll(std::string(val.data(), val.size()));
			else if (key == "max_frequency")   m_max_freq = std::stod(std::string(val.data(), val.size()));
			else if (key == "freq_resolution") m_freq_resolution = std::stod(std::string(val.data(), val.size()));
			else if (key == "bandwidth")       m_bandwidth = std::stod(std::string(val.data(), val.size()));
			else if (key == "nyquist")         m_nyquist = std::stod(std::string(val.data(), val.size()));
			else if (key == "window_type")     m_window_type = string_to_window_type(val);
			else if (key == "peak_dB")         m_peak_dB = std::stod(std::string(val.data(), val.size()));
			else if (key == "floor_dB")        m_floor_dB = std::stod(std::string(val.data(), val.size()));
			else if (key == "bin_count")        expected_bins = std::stoll(std::string(val.data(), val.size()));
			else if (key == "lpc_order")       m_lpc_order = std::stoi(std::string(val.data(), val.size()));
		}
		else if (in_data)
		{
			double dB = std::stod(std::string(line.data(), line.size()));
			m_power_dB.push_back(dB);
		}
		else if (in_lpc)
		{
			double dB = std::stod(std::string(line.data(), line.size()));
			m_lpc_dB.push_back(dB);
		}
	}

	if (m_power_dB.empty()) {
		throw error("Spectrum file '%' contains no data", m_path);
	}
}


// ─────────────────────────────────────────────────────────────────────────────
//  Window type string conversion
// ─────────────────────────────────────────────────────────────────────────────

String Spectrum::window_type_to_string(speech::WindowType wt)
{
	switch (wt)
	{
		case speech::WindowType::Bartlett:    return "Bartlett";
		case speech::WindowType::Blackman:    return "Blackman";
		case speech::WindowType::Gaussian:    return "Gaussian";
		case speech::WindowType::Hamming:     return "Hamming";
		case speech::WindowType::Hann:        return "Hann";
		case speech::WindowType::Kaiser:      return "Kaiser";
		case speech::WindowType::Rectangular: return "Rectangular";
	}
	return "Gaussian";
}

speech::WindowType Spectrum::string_to_window_type(const String &s)
{
	if (s == "Bartlett")    return speech::WindowType::Bartlett;
	if (s == "Blackman")    return speech::WindowType::Blackman;
	if (s == "Gaussian")    return speech::WindowType::Gaussian;
	if (s == "Hamming")     return speech::WindowType::Hamming;
	if (s == "Hann")        return speech::WindowType::Hann;
	if (s == "Kaiser")      return speech::WindowType::Kaiser;
	if (s == "Rectangular") return speech::WindowType::Rectangular;

	return speech::WindowType::Gaussian; // safe default
}


// ─────────────────────────────────────────────────────────────────────────────
//  Scripting runtime registration
// ─────────────────────────────────────────────────────────────────────────────

void Spectrum::initialize(Runtime &rt)
{
	using namespace bindings;

	// ── Fields (old spectrum_get_field dispatcher) ──────────────

	rt.add_field<Spectrum>("path", [](const Spectrum &spec) -> String { return spec.path(); });
	rt.add_field<Spectrum>("bin_count", [](Spectrum &spec) -> intptr_t { return spec.bin_count(); });
	rt.add_field<Spectrum>("sample_rate", [](Spectrum &spec) -> intptr_t { return intptr_t(spec.sample_rate()); });
	rt.add_field<Spectrum>("bandwidth", [](Spectrum &spec) -> double { return spec.bandwidth(); });
	rt.add_field<Spectrum>("max_frequency", [](Spectrum &spec) -> double { return spec.max_frequency(); });
	rt.add_field<Spectrum>("start_time", [](Spectrum &spec) -> double { return spec.start_time(); });
	rt.add_field<Spectrum>("end_time", [](Spectrum &spec) -> double { return spec.end_time(); });
	rt.add_field<Spectrum>("peak_dB", [](Spectrum &spec) -> double { return spec.peak_dB(); });
	rt.add_field<Spectrum>("floor_dB", [](Spectrum &spec) -> double { return spec.floor_dB(); });
	rt.add_field<Spectrum>("lpc_order", [](Spectrum &spec) -> intptr_t { return intptr_t(spec.lpc_order()); });
	rt.add_field<Spectrum>("has_lpc", [](Spectrum &spec) -> bool { return spec.has_lpc(); });

	// ── Spectrum creation from Sound ────────────────────────────

	rt.add_function("get_spectrum",
	                [](Isolate &iso, Sound &sound, intptr_t channel, double t1, double t2) -> Handle<Spectrum> {
		return guarded(iso, [&] {
			sound.open();
			return Handle<Spectrum>::make(nullptr, Handle<Sound>(&sound), (int) channel, t1, t2);
		});
	});

	// ── Spectral moments ────────────────────────────────────────

	rt.add_function("get_spectral_moments",
	                [](Isolate &iso, Sound &sound, intptr_t channel, double time, double window_duration,
	                   double min_freq, double max_freq) -> Table {
		return guarded(iso, [&] {
			sound.open();
			auto m = compute_spectral_moments_at(
				Handle<Sound>(&sound), (int) channel, time, window_duration,
				speech::WindowType::Gaussian, min_freq, max_freq, 50.0);
			Table result;
			result.set(Variant::make(String("cog")), Variant::make(m.cog));
			result.set(Variant::make(String("spread")), Variant::make(m.spread));
			result.set(Variant::make(String("skewness")), Variant::make(m.skewness));
			result.set(Variant::make(String("kurtosis")), Variant::make(m.kurtosis));
			return result;
		});
	});
}

} // namespace phonometrica
