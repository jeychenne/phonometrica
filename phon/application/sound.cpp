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
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <cmath>
#include <set>
#if PHON_WINDOWS
#include <windows.h>
#endif
#include <sndfile.h>
//#include <iostream>
#include <phon/runtime.hpp>
#include <phon/application/bindings.hpp>

#include <phon/engine/types/table.hpp>
#include <phon/application/sound.hpp>
#include <phon/application/settings.hpp>
#include <phon/application/resampler.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/analysis/speech_utils.hpp>
#include <phon/analysis/statistics.hpp>
#include <phon/third_party/swipe/swipe.h>
#include <phon/utils/matrix.hpp>

#if PHON_MACOS
const int BUFFER_SIZE = 4096;
#else
const int BUFFER_SIZE = 2048;
#endif

namespace phonometrica {

Array<String> Sound::the_supported_sound_formats;
Array<String> Sound::the_common_sound_formats;
Signal<const String&, const String&, int> Sound::start_loading;
Signal<int> Sound::update_loading;


Sound::Sound(Directory *parent, String path) :
		Document(parent, std::move(path))
{
    if (has_path()) {
        auto h = handle();
        m_sample_rate = h.samplerate();
        m_nchannel = h.channels();
        m_nframes = h.frames();
        m_duration = double(m_nframes) / m_sample_rate;
    }
}

void Sound::set_sound_formats()
{
	SF_FORMAT_INFO format_info;
	std::set<String> temp;
	int count;

	sf_command(nullptr, SFC_GET_SIMPLE_FORMAT_COUNT, &count, sizeof(int));

	for (int k = 0; k < count; k++) {
		format_info.format = k;
		sf_command(nullptr, SFC_GET_SIMPLE_FORMAT, &format_info, sizeof(format_info));
		temp.emplace(format_info.extension);
	}

	for (auto &format : temp) {
		the_supported_sound_formats.emplace_back(format);
	}

	// it seems that "oga" is the default extension for OGG files in libsndfile
	if (the_supported_sound_formats.contains("oga") && ! the_supported_sound_formats.contains("ogg")) {
		the_supported_sound_formats.emplace_back("ogg");
	}

	the_common_sound_formats.emplace_back("wav");
	the_common_sound_formats.emplace_back("flac");
	the_common_sound_formats.emplace_back("aiff");
}

const Array<String> &Sound::supported_sound_formats()
{
	return the_supported_sound_formats;
}

const Array<String> &Sound::common_sound_formats()
{
	return the_common_sound_formats;
}

void Sound::load()
{
    auto h = handle();

    // A missing/wrong path or an undecodable file yields a libsndfile handle with no
    // channels and no sample rate. Fail with a clear error here instead of allocating an
    // empty buffer and (previously) spinning forever in the progress loop below, which
    // froze the whole UI because load() runs on the GUI thread.
    if (h.channels() < 1 || h.samplerate() < 1) {
        throw error("File '%' is missing or is not a valid audio file", path());
    }

    h.seek(0, SEEK_SET);

    m_data = Array<float>((intptr_t) m_nframes, (intptr_t) m_nchannel);
    // Clamp to at least 1: when m_nframes < 100, size_t(m_nframes / 100) is 0, which turns
    // each "while (accumulator >= slice)" below into an infinite loop (an unsigned value is
    // always >= 0 and subtracting 0 never makes progress).
    auto slice = std::max<size_t>(size_t(1), size_t(m_nframes / 100));
    auto msg = String::format("Reading file %s from disk...", this->label().data());
    start_loading(msg, "Loading data", 100);
    size_t accumulator = 0;
    int counter = 1;

    if (m_nchannel == 1)
    {
        auto ptr = m_data.begin();
        auto end = m_data.end();

        while (ptr < end)
        {
            auto count = h.readf(ptr, BUFFER_SIZE);  // readf(float*) overload
            if (count == 0) break; // stop on a short/truncated read instead of spinning forever
            ptr += count * m_nchannel;
            accumulator += count;
            while (accumulator >= slice)
            {
                update_loading(counter++);
                accumulator -= slice;
            }
        }
    }
    else
    {
        Array<float> buffer;
        buffer.resize(BUFFER_SIZE * m_nchannel);
        auto ptr = buffer.begin();
        intptr_t count = 1;
        intptr_t ioffset = 0;

        while (count != 0)
        {
            count = (intptr_t) h.readf(ptr, BUFFER_SIZE);
            auto p = ptr;
            for (intptr_t i = 0; i < count; ++i)
            {
                for (intptr_t j = 0; j < m_nchannel; j++) {
                    m_data(ioffset+i, j) = *p++;
                }
            }
            ioffset += count;
            accumulator += count;
            while (accumulator >= slice)
            {
                update_loading(counter++);
                accumulator -= slice;
            }
        }
    }
    h.seek(0, SEEK_SET);
}

void Sound::write()
{

}

bool Sound::supports_format(const String &format)
{
	// Strip leading dot.
	std::string_view fmt(format.data() + 1, format.size() - 1);

	for (auto &f : the_supported_sound_formats) {
		if (f == fmt) {
			return true;
		}
	}

	return false;
}

String Sound::extension_for(Format fmt)
{
	switch (fmt)
	{
		case Format::WAV:  return String(".wav");
		case Format::AIFF: return String(".aiff");
		case Format::FLAC: return String(".flac");
		case Format::OGG:  return String(".ogg");
#ifdef SF_FORMAT_MPEG
		case Format::MP3:  return String(".mp3");
#endif
	}
	return String(".wav");
}

Sound::Format Sound::parse_format(const String &name)
{
	// Normalize: lowercase, strip leading dot if present.
	String norm = name.to_lower();
	if (!norm.empty() && norm.first() == '.') {
		norm = norm.right(norm.size() - 1);
	}

	// Map to Format. We accept the common aliases libsndfile itself uses
	// (e.g. "aif" for AIFF, "oga" for OGG) so that names obtained from
	// supported_sound_formats() round-trip cleanly.
	Format fmt;
	String canonical;  // canonical extension for the runtime check below
	if (norm == "wav") {
		fmt = Format::WAV;
		canonical = "wav";
	}
	else if (norm == "aiff" || norm == "aif" || norm == "aifc") {
		fmt = Format::AIFF;
		canonical = "aiff";
	}
	else if (norm == "flac") {
		fmt = Format::FLAC;
		canonical = "flac";
	}
	else if (norm == "ogg" || norm == "oga") {
		fmt = Format::OGG;
		canonical = "ogg";
	}
#ifdef SF_FORMAT_MPEG
	else if (norm == "mp3") {
		fmt = Format::MP3;
		canonical = "mp3";
	}
#endif
	else {
		throw error("[Argument error] Unknown sound format: \"%\". Expected one of: "
		            "\"wav\", \"aiff\", \"flac\", \"ogg\", \"mp3\".", name);
	}

	// Runtime check: does the libsndfile this binary is linked against
	// actually support the format we just mapped to? The compile-time
	// SF_FORMAT_MPEG guard only confirms the headers know about MPEG;
	// the actual library may have been built without mpg123/LAME.
	bool runtime_ok = false;
	for (auto &f : the_supported_sound_formats) {
		if (f == canonical) { runtime_ok = true; break; }
	}
	if (!runtime_ok) {
		throw error("[I/O error] The bundled libsndfile does not support "
		            "writing \"%\" files on this system.", name);
	}

	return fmt;
}

Array<String> Sound::supported_sound_format_names()
{
	SF_FORMAT_INFO info;
	int count;
	Array<String> formats;

	sf_command(nullptr, SFC_GET_SIMPLE_FORMAT_COUNT, &count, sizeof(int));

	for (int k = 0; k < count; k++)
	{
		info.format = k;
		sf_command(nullptr, SFC_GET_SIMPLE_FORMAT, &info, sizeof(info));
		formats.append(info.name);
	}

	return formats;
}

String Sound::rtaudio_version()
{
	return String(RtAudio::getVersion());
}

String Sound::libsndfile_version()
{
	String sf_version;
	char buffer[128] ;

	sf_command(nullptr, SFC_GET_LIB_VERSION, buffer, sizeof(buffer)) ;
	sf_version = buffer;

	return sf_version.split("-")[1];
}

SndfileHandle Sound::handle() const
{
	if (m_handle) {
		return m_handle;
	}

#if PHON_WINDOWS
	auto wpath = m_path.to_wide();
	m_handle = SndfileHandle(wpath.data());
#else
	m_handle = SndfileHandle(m_path.data());
#endif

	return m_handle;
}

void Sound::convert(const String &path, int sample_rate, Sound::Format fmt)
{
	// Delegate to the free function in annotation_ops, which handles
	// multichannel sources, per-channel resampling, and format-aware
	// subtype selection. The earlier in-method implementation here was
	// mono-only and hardcoded PCM_32, so we drop it.
	convert_sound(*this, path, fmt, sample_rate);
}

int Sound::get_intensity_window_size() const
{
	return int(std::ceil(get_intensity_window_duration() * m_handle.samplerate()));
}

Array<double>
Sound::get_formants(int channel, const Array<double> &times, int nformant, double nyquist_frequency, double window_size,
                    int lpc_order)
{
	Array<double> result(nformant, 2, 0.0);

	for (auto t : times)
	{
		auto tmp = get_formants(channel, t, nformant, nyquist_frequency, window_size, lpc_order);
		for (intptr_t i = 0; i < nformant; i++)
		{
			for (intptr_t j = 0; j < 2; j++)
			{
				result(i, j) += tmp(i, j);
			}
		}
	}

	for (intptr_t i = 0; i < nformant; i++)
	{
		for (intptr_t j = 0; j < 2; j++)
		{
			result(i, j) /= times.size();
		}
	}

	return result;
}

Array<double>
Sound::get_formants(int channel, double time, int nformant, double nyquist_frequency, double window_size, int lpc_order)
{
	using namespace speech;

	open();
	Array<double> result(nformant, 2, 0.0);

	window_size *= 2; // for the Gaussian window
	double Fs = nyquist_frequency * 2;
	int nframe_orig = int(ceil(window_size * this->sample_rate()));
	auto first_sample = this->time_to_frame(time) - nframe_orig / 2;
	auto last_sample = first_sample + nframe_orig;

	if (first_sample < 1) {
		throw error("File '%': time point % is too close to the beginning of the file", path(), time);
	}
	if (last_sample > this->channel_size()) {
		throw error("File '%': time point % is too close to the end of the file", path(), time);
	}

	auto sample_rate = this->sample_rate();
	auto input = get_channel(channel, first_sample, last_sample);
	Array<double> tmp; // not needed if sampling rates are equal
	std::span<double> output;

	if (Fs == sample_rate)
	{
		// Apply pre-emphasis from 50 Hz.
		pre_emphasis(input, this->sample_rate(), 50);
		output = input;
	}
	else
	{
		tmp = resample(input, sample_rate, Fs);
		// Apply pre-emphasis from 50 Hz.
		pre_emphasis(tmp, Fs, 50);
		output = std::span<double>(tmp);
	}
	int nframe = output.size();
	auto win = create_window(nframe, nframe, WindowType::Gaussian);
	Array<double> buffer(nframe, 0.0);

	// Apply window.
	auto it = output.begin();
	for (int j = 0; j < nframe; j++)
	{
		buffer[j] = *it++ * win[j];
	}

	auto coeffs = get_lpc_coefficients(buffer, lpc_order);
	std::vector<double> freqs, bw;
	bool ok = speech::get_formants(coeffs, Fs, freqs, bw);

	if (!ok)
	{
		for (int i = 0; i < nformant; i++)
		{
			result(i, 0) = std::nan("");
			result(i, 1) = std::nan("");
		}

		return result;
	}

	int count = 0;
	const double lowest_freq = 50.0;
	const double highest_freq = Fs / 2 - lowest_freq;
	for (size_t k = 0; k < freqs.size(); k++)
	{
		auto freq = freqs[k];
		if (freq > 50 && freq < highest_freq)
		{
			result(count, 0) = freq;
			result(count, 1) = bw[k];
			count++;
		}
		if (count == nformant) break;
	}
	for (int k = count; k < nformant; k++)
	{
		result(k, 0) = std::nan("");
		result(k, 1) = std::nan("");
	}

	return result;
}

double Sound::get_pitch(int channel, speech::PitchTracker method, double time, double min_pitch, double max_pitch, double threshold,
                        double octave_jump_cost, double voicing_cost, double silence_threshold, double octave_cost,
                        bool use_gaussian)
{
	open();
	double half_window = 0.025; // We use a 50 ms window
	auto start_time = std::max<double>(time - half_window, 0.0);
	auto end_time = std::min<double>(time + half_window, duration());
	// Where `time` falls inside the analysis window, as a fraction. The window is centred on
	// `time` except when it is clipped against either end of the file, so this is 0.5 in the
	// ordinary case. Clamped because a caller may ask for a time outside the file, and guarded
	// against a zero-length window (a file whose duration rounds to nothing).
	double span = end_time - start_time;
	double prop = (span > 0) ? (time - start_time) / span : 0.0;
	prop = std::clamp(prop, 0.0, 1.0);

	auto first_sample = time_to_frame(start_time);
	auto last_sample = time_to_frame(end_time);
	auto input = get_channel(channel, first_sample, last_sample);
	//TODO: use time step from settings?
	auto f0 = speech::get_pitch(method, input, sample_rate(), min_pitch, max_pitch, 0.01, threshold,
	                             octave_jump_cost, voicing_cost, silence_threshold, octave_cost, use_gaussian);

	if (f0.size() > 1)
	{
		// The tracker's frames span the window, so `prop` maps onto the closed index range
		// [0, size-1] — not [0, size], which would run one frame past the end. Getting this wrong
		// is what the previous version did: it evaluated the contour at `prop*size - 1`, i.e.
		// (1 - prop) frames too early — half a frame, 5 ms, for a window that is not clipped — and
		// indexed out of bounds at both ends.
		double rindex = prop * double(f0.size() - 1);
		auto i = size_t(std::floor(rindex));

		// At (or past) the last frame there is no pair to interpolate between.
		if (i + 1 >= f0.size()) {
			return f0.back();
		}
		double frac = rindex - double(i);

		return f0[i] + (f0[i + 1] - f0[i]) * frac;
	}
	else if (f0.size() == 1)
	{
		return f0[0];
	}

	return 0;
}

double Sound::get_mean_pitch(int channel, speech::PitchTracker method, double t1, double t2, double min_pitch, double max_pitch, double threshold,
                             double octave_jump_cost, double voicing_cost, double silence_threshold, double octave_cost,
                             double time_step, bool use_gaussian)
{
	open();
	auto first_sample = time_to_frame(t1);
	auto last_sample = time_to_frame(t2);
	auto input = get_channel(channel, first_sample, last_sample);
	auto f0 = speech::get_pitch(method, input, sample_rate(), min_pitch, max_pitch, time_step, threshold,
	                             octave_jump_cost, voicing_cost, silence_threshold, octave_cost, use_gaussian);
	double total = 0;
	int n = 0;

	for (auto value : f0)
	{
		if (value > 0)
		{
			total += value;
			n++;
		}
	}

	return total / n;
}

double Sound::get_intensity(int channel, double time)
{
	open();
	int window_size = get_intensity_window_size();
	auto first_sample = time_to_frame(time) - (window_size / 2);
	auto last_sample = first_sample + window_size - 1;

	if (first_sample < 1) {
		throw error("File '%': time point % is too close to the beginning of the file", path(), time);
	}
	if (last_sample > channel_size()) {
		throw error("File '%': time point % is too close to the end of the file", path(), time);
	}

	auto frame = get_channel(channel, first_sample, last_sample);
	auto win = speech::create_window(window_size, window_size, speech::WindowType::Hamming);

	return speech::get_intensity(frame, win);
}

double Sound::get_mean_intensity(int channel, double t1, double t2, double time_step)
{
	bool unused = false;
	auto db = get_intensity(channel, t1, t2, time_step, unused);

	return stats::mean(db);
}

Array<double> Sound::get_intensity(int channel, double from, double to, double time_step, bool &start_at_zero)
{
	auto window_duration = get_intensity_window_duration();
	auto half_window = window_duration / 2;

	// Try to take a measurement point at the start point of the window.
	start_at_zero = (from - half_window >= 0);
	if (start_at_zero) {
		from -= half_window;
	}
	// If possible, take enough samples after the end of the time window so that we can make a measurement
	// at (or past) the last time point. (In the latter case, drawing will be clipped at the end of window.)
	auto t = from;
	while (t < to) {
		t += time_step;
	}
	if (t + window_duration <= this->duration()) {
		to = t + window_duration;
	}

	auto start_pos = time_to_frame(from);
	auto end_pos = time_to_frame(to);
	auto input = get_channel(channel, start_pos, end_pos);
	int window_size = get_intensity_window_size();

	return speech::get_intensity(input, sample_rate(), window_size, time_step);
}

speech::VoiceReport Sound::compute_voice_report(int channel, double t1, double t2,
                                                double f0_min, double f0_max)
{
	open();

	if (t1 < 0.0 || t2 <= t1 || t2 > duration()) {
		throw error("Sound::compute_voice_report: invalid time range [%, %] for sound of duration %",
		            t1, t2, duration());
	}
	if (!(f0_min > 0.0) || !(f0_max > f0_min)) {
		throw error("Sound::compute_voice_report: invalid F0 range [%, %] Hz", f0_min, f0_max);
	}

	// Pad the analysis input around [t1, t2] so the Praat-style pitch tracker
	// has enough context to make confident voicing decisions on short
	// selections. Without padding, a short vowel (< ~50 ms) sits entirely
	// inside the tracker's tapered window edges, the Viterbi voicing-cost
	// penalty dominates over the per-frame correlation evidence, and the
	// optimal path can come back "all unvoiced" — yielding 0 pulses and
	// NaN jitter/shimmer on intervals that clearly contain pulses when
	// analysed in context. Pad = one analysis window (3/f0_min) plus a few
	// frames of Viterbi neighbourhood, with a 50 ms floor for safety.
	const double time_step = 0.01; // matches HnrOptions::time_step
	const double pad = std::max(0.05, 3.0 / f0_min + 3.0 * time_step);

	const double t_lo = std::max(0.0,        t1 - pad);
	const double t_hi = std::min(duration(), t2 + pad);

	auto first_pad = time_to_frame(t_lo);
	auto last_pad  = time_to_frame(t_hi);

	// Defensive: the original short-input check stays but now applies to the
	// padded buffer, which is essentially always satisfied — keeps the error
	// message useful for truly pathological inputs.
	if (last_pad - first_pad < 2) {
		throw error("Sound::compute_voice_report: selection too short (% s — % s)", t1, t2);
	}

	auto chan = get_channel(channel, first_pad, last_pad);
	std::span<const double> span(chan.data(), static_cast<size_t>(chan.size()));

	// Offsets of the original interval inside the padded buffer.
	const double sr = static_cast<double>(sample_rate());
	const double interval_start = (t1 - t_lo);
	const double interval_end   = (t2 - t_lo);

	return speech::compute_voice_report(span, sr, f0_min, f0_max,
	                                    interval_start, interval_end);
}

void Sound::initialize(Runtime &rt)
{
	using namespace bindings;

	// ── Fields (old sound_get_field dispatcher) ─────────────────────────

	rt.add_field<Sound>("path", [](const Sound &sound) -> String {
		return sound.path();
	});
	rt.add_field<Sound>("duration", [](Sound &sound) -> double {
		return sound.duration();
	});
	rt.add_field<Sound>("nchannel", [](Sound &sound) -> intptr_t {
		return intptr_t(sound.nchannel());
	});
	rt.add_field<Sound>("sample_rate", [](Sound &sound) -> intptr_t {
		return intptr_t(sound.sample_rate());
	});

	// ── Local helpers ───────────────────────────────────────────────────

	// Elementwise map over a numeric array (the script-visible "Array" class).
	// The result keeps the input's shape (e.g. mapping over an nformant-by-2
	// matrix from get_formants must not flatten it).
	auto apply_fn = [](const NumArray &a, auto &&f) -> NumArray {
		NumArray src = a.contiguous();
		intptr_t dims[PHON_MAX_RANK];
		for (int k = 0; k < src.rank(); ++k) {
			dims[k] = src.dim(k);
		}
		NumArray out = NumArray::make(src.rank(), dims);
		double *d = out.detach();
		const double *s = src.data() + src.offset();
		for (intptr_t i = 0; i < src.size(); ++i) {
			d[i] = f(s[i]);
		}
		return out;
	};

	// Pack a VoiceReport into a Table whose keys match the GUI report
	// layout. Doubles that came back NaN are stored as NaN; scripting
	// callers can detect them with the standard `x != x` idiom since
	// NaN compares unequal to itself. num_pulses is an integer so it
	// round-trips through arithmetic and comparisons without surprises.
	auto pack_voice_report = [](const speech::VoiceReport &r) -> Table {
		Table table;
		auto set = [&table](const char *k, Variant v) {
			table.set(Variant::make(String(k)), std::move(v));
		};

		set("num_pulses",       Variant::make<int64_t>(r.num_pulses));
		set("mean_period",      Variant::make(r.mean_period));
		set("mean_f0",          Variant::make(r.mean_f0));

		set("jitter_local",     Variant::make(r.jitter_local));
		set("jitter_local_abs", Variant::make(r.jitter_local_abs));
		set("jitter_rap",       Variant::make(r.jitter_rap));
		set("jitter_ppq5",      Variant::make(r.jitter_ppq5));
		set("jitter_ddp",       Variant::make(r.jitter_ddp));

		set("shimmer_local",    Variant::make(r.shimmer_local));
		set("shimmer_local_db", Variant::make(r.shimmer_local_db));
		set("shimmer_apq3",     Variant::make(r.shimmer_apq3));
		set("shimmer_apq5",     Variant::make(r.shimmer_apq5));
		set("shimmer_apq11",    Variant::make(r.shimmer_apq11));

		set("hnr",              Variant::make(r.hnr));

		return table;
	};

	// ── Options Tables for acoustic queries ────────────────────────────
	//
	// Each *_opts overload below takes a trailing `options as Table`. The
	// three parse helpers start from per-category defaults in Settings and
	// let the caller override whatever keys they care about. Validation is
	// strict: any unknown key is a hard error rather than a silent default
	// — typos like "min_picth" would otherwise leave the user wondering
	// why their override had no effect.
	//
	// Numeric values convert through to<double> so callers can write
	// `lpc_order = 10` or `lpc_order = 10.0` interchangeably. Strings
	// (method) and booleans (use_gaussian) are strict on type.

	struct PitchOpts {
		String method;
		double min_pitch;
		double max_pitch;
		double threshold;
		double octave_jump_cost;
		double voicing_cost;
		double silence_threshold;
		double octave_cost;
		double time_step;       // only consumed by get_mean_pitch
		bool   use_gaussian;
	};

	auto load_pitch_defaults = []() -> PitchOpts {
		String cat("pitch_tracking");
		PitchOpts o;
		o.method            = Settings::get_string(cat, "method");
		o.min_pitch         = Settings::get_number(cat, "minimum_pitch");
		o.max_pitch         = Settings::get_number(cat, "maximum_pitch");
		o.threshold         = Settings::get_number(cat, "voicing_threshold");
		o.octave_jump_cost  = Settings::get_number(cat, "octave_jump_cost");
		o.voicing_cost      = Settings::get_number(cat, "voicing_cost");
		o.silence_threshold = Settings::get_number(cat, "silence_threshold");
		o.octave_cost       = Settings::get_number(cat, "octave_cost");
		o.time_step         = Settings::get_number(cat, "time_step");
		o.use_gaussian      = Settings::get_boolean(cat, "use_gaussian");
		return o;
	};

	auto option_key = [](Isolate &iso, const Variant &k, const char *what) -> String {
		try
		{
			return k.to<String>();
		}
		catch (std::exception &)
		{
			iso.raise(String::format("[Type error] %s options Table: keys must be strings", what), 0);
		}
	};

	auto parse_pitch_options = [load_pitch_defaults, option_key](Isolate &iso, const Table &tab, bool allow_time_step) -> PitchOpts {
		PitchOpts o = load_pitch_defaults();
		List keys = tab.keys();
		for (intptr_t n = 1; n <= keys.size(); n++)
		{
			Variant k = keys.get(n);
			const String key = option_key(iso, k, allow_time_step ? "get_mean_pitch" : "get_pitch");
			Variant v = tab.get(k);
			if      (key == "method")            { o.method            = v.to<String>(); }
			else if (key == "min_pitch")         { o.min_pitch         = v.to<double>(); }
			else if (key == "max_pitch")         { o.max_pitch         = v.to<double>(); }
			else if (key == "threshold")         { o.threshold         = v.to<double>(); }
			else if (key == "octave_jump_cost")  { o.octave_jump_cost  = v.to<double>(); }
			else if (key == "voicing_cost")      { o.voicing_cost      = v.to<double>(); }
			else if (key == "silence_threshold") { o.silence_threshold = v.to<double>(); }
			else if (key == "octave_cost")       { o.octave_cost       = v.to<double>(); }
			else if (key == "use_gaussian")      { o.use_gaussian      = v.to<bool>(); }
			else if (key == "time_step" && allow_time_step) { o.time_step = v.to<double>(); }
			else {
				if (allow_time_step) {
					iso.raise(String::format(
					    "[Value error] get_mean_pitch options: unknown key \"%s\". "
					    "Supported keys: \"method\", \"min_pitch\", \"max_pitch\", \"threshold\", "
					    "\"octave_jump_cost\", \"voicing_cost\", \"silence_threshold\", "
					    "\"octave_cost\", \"time_step\", \"use_gaussian\".", key.data()), 0);
				} else {
					iso.raise(String::format(
					    "[Value error] get_pitch options: unknown key \"%s\". "
					    "Supported keys: \"method\", \"min_pitch\", \"max_pitch\", \"threshold\", "
					    "\"octave_jump_cost\", \"voicing_cost\", \"silence_threshold\", "
					    "\"octave_cost\", \"use_gaussian\".", key.data()), 0);
				}
			}
		}
		return o;
	};

	struct FormantOpts {
		intptr_t nformant;
		double   nyquist;
		double   window_size;
		intptr_t lpc_order;
	};

	auto parse_formant_options = [option_key](Isolate &iso, const Table &tab) -> FormantOpts {
		String cat("formants");
		FormantOpts o;
		o.nformant    = (intptr_t) Settings::get_number(cat, "number_of_formants");
		o.nyquist     = Settings::get_number(cat, "max_frequency");
		o.window_size = Settings::get_number(cat, "window_size");
		o.lpc_order   = (intptr_t) Settings::get_number(cat, "lpc_order");
		List keys = tab.keys();
		for (intptr_t n = 1; n <= keys.size(); n++)
		{
			Variant k = keys.get(n);
			const String key = option_key(iso, k, "get_formants");
			Variant v = tab.get(k);
			if      (key == "nformant")    { o.nformant    = (intptr_t) v.to<double>(); }
			else if (key == "nyquist")     { o.nyquist     = v.to<double>(); }
			else if (key == "window_size") { o.window_size = v.to<double>(); }
			else if (key == "lpc_order")   { o.lpc_order   = (intptr_t) v.to<double>(); }
			else {
				iso.raise(String::format(
				    "[Value error] get_formants options: unknown key \"%s\". "
				    "Supported keys: \"nformant\", \"nyquist\", \"window_size\", \"lpc_order\".",
				    key.data()), 0);
			}
		}
		return o;
	};

	struct VoiceReportOpts {
		double f0_min;
		double f0_max;
	};

	auto parse_voice_report_options = [option_key](Isolate &iso, const Table &tab) -> VoiceReportOpts {
		// Defaults match Sound::compute_voice_report (Praat's voice-report
		// defaults). There is no Settings category for the voice report at
		// the moment, so these are inlined rather than read from Settings.
		VoiceReportOpts o { 75.0, 600.0 };
		List keys = tab.keys();
		for (intptr_t n = 1; n <= keys.size(); n++)
		{
			Variant k = keys.get(n);
			const String key = option_key(iso, k, "get_voice_report");
			Variant v = tab.get(k);
			if      (key == "f0_min") { o.f0_min = v.to<double>(); }
			else if (key == "f0_max") { o.f0_max = v.to<double>(); }
			else {
				iso.raise(String::format(
				    "[Value error] get_voice_report options: unknown key \"%s\". "
				    "Supported keys: \"f0_min\", \"f0_max\".", key.data()), 0);
			}
		}
		return o;
	};

	// ── Intensity ───────────────────────────────────────────────────────

	rt.add_function("get_intensity", [](Isolate &iso, Sound &sound, intptr_t channel, double time) -> double {
		return guarded(iso, [&] {
			sound.open();
			if (time < 0 || time > sound.duration()) {
				throw error("File '%': invalid time %", sound.path(), time);
			}
			return sound.get_intensity((int) channel, time);
		});
	});
	rt.add_function("get_mean_intensity",
	                [](Isolate &iso, Sound &sound, intptr_t channel, double t1, double t2) -> double {
		return guarded(iso, [&] {
			sound.open();
			if (t1 < 0 || t1 > t2 || t2 > sound.duration()) {
				throw error("File '%': invalid time window [%, %]", sound.path(), t1, t2);
			}
			return sound.get_mean_intensity((int) channel, t1, t2);
		});
	});

	// ── Voice report ────────────────────────────────────────────────────

	rt.add_function("get_voice_report",
	                [pack_voice_report](Isolate &iso, Sound &sound, intptr_t channel, double t1, double t2) -> Table {
		return guarded(iso, [&] {
			return pack_voice_report(sound.compute_voice_report((int) channel, t1, t2));
		});
	});
	rt.add_function("get_voice_report",
	                [pack_voice_report, parse_voice_report_options](Isolate &iso, Sound &sound, intptr_t channel,
	                                                                double t1, double t2, const Table &options) -> Table {
		auto o = parse_voice_report_options(iso, options);
		return guarded(iso, [&] {
			return pack_voice_report(sound.compute_voice_report((int) channel, t1, t2, o.f0_min, o.f0_max));
		});
	});

	// ── Pitch ───────────────────────────────────────────────────────────

	rt.add_function("get_pitch", [](Isolate &iso, Sound &sound, intptr_t channel, double time) -> double {
		return guarded(iso, [&] {
			String category("pitch_tracking");
			auto min_pitch = Settings::get_number(category, "minimum_pitch");
			auto max_pitch = Settings::get_number(category, "maximum_pitch");
			auto threshold = Settings::get_number(category, "voicing_threshold");
			auto method = Settings::get_string(category, "method");
			auto m = Sound::get_pitch_tracker(method);
			return sound.get_pitch((int) channel, m, time, min_pitch, max_pitch, threshold);
		});
	});
	rt.add_function("get_pitch",
	                [parse_pitch_options](Isolate &iso, Sound &sound, intptr_t channel, double time, const Table &options) -> double {
		auto o = parse_pitch_options(iso, options, /*allow_time_step*/ false);
		return guarded(iso, [&] {
			auto m = Sound::get_pitch_tracker(o.method);
			return sound.get_pitch((int) channel, m, time, o.min_pitch, o.max_pitch, o.threshold,
			                       o.octave_jump_cost, o.voicing_cost, o.silence_threshold,
			                       o.octave_cost, o.use_gaussian);
		});
	});
	rt.add_function("get_mean_pitch",
	                [](Isolate &iso, Sound &sound, intptr_t channel, double t1, double t2) -> double {
		return guarded(iso, [&] {
			String category("pitch_tracking");
			auto min_pitch = Settings::get_number(category, "minimum_pitch");
			auto max_pitch = Settings::get_number(category, "maximum_pitch");
			auto threshold = Settings::get_number(category, "voicing_threshold");
			auto method = Settings::get_string(category, "method");
			auto m = Sound::get_pitch_tracker(method);
			return sound.get_mean_pitch((int) channel, m, t1, t2, min_pitch, max_pitch, threshold);
		});
	});
	rt.add_function("get_mean_pitch",
	                [parse_pitch_options](Isolate &iso, Sound &sound, intptr_t channel, double t1, double t2,
	                                      const Table &options) -> double {
		auto o = parse_pitch_options(iso, options, /*allow_time_step*/ true);
		return guarded(iso, [&] {
			auto m = Sound::get_pitch_tracker(o.method);
			return sound.get_mean_pitch((int) channel, m, t1, t2, o.min_pitch, o.max_pitch, o.threshold,
			                            o.octave_jump_cost, o.voicing_cost, o.silence_threshold,
			                            o.octave_cost, o.time_step, o.use_gaussian);
		});
	});

	// ── Formants ────────────────────────────────────────────────────────

	rt.add_function("get_formants",
	                [](Isolate &iso, Sound &sound, intptr_t channel, double time) -> NumArray {
		return guarded(iso, [&] {
			String category("formants");
			intptr_t nformant = (intptr_t) Settings::get_number(category, "number_of_formants");
			double nyquist = Settings::get_number(category, "max_frequency");
			double win_size = Settings::get_number(category, "window_size");
			intptr_t lpc_order = (intptr_t) Settings::get_number(category, "lpc_order");
			sound.open();
			return to_numarray(sound.get_formants((int) channel, time, nformant, nyquist, win_size, lpc_order));
		});
	});
	rt.add_function("get_formants",
	                [parse_formant_options](Isolate &iso, Sound &sound, intptr_t channel, double time,
	                                        const Table &options) -> NumArray {
		auto o = parse_formant_options(iso, options);
		return guarded(iso, [&] {
			sound.open();
			return to_numarray(sound.get_formants((int) channel, time, o.nformant, o.nyquist, o.window_size, o.lpc_order));
		});
	});

	// ── Frequency-scale conversions ─────────────────────────────────────

	rt.add_function("hertz_to_bark", [](double f) -> double { return speech::hertz_to_bark(f); });
	rt.add_function("hertz_to_bark", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double f) { return speech::hertz_to_bark(f); });
	});
	rt.add_function("bark_to_hertz", [](double z) -> double { return speech::bark_to_hertz(z); });
	rt.add_function("bark_to_hertz", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double z) { return speech::bark_to_hertz(z); });
	});
	rt.add_function("hertz_to_erb", [](double f) -> double { return speech::hertz_to_erb(f); });
	rt.add_function("hertz_to_erb", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double f) { return speech::hertz_to_erb(f); });
	});
	rt.add_function("erb_to_hertz", [](double e) -> double { return speech::erb_to_hertz(e); });
	rt.add_function("erb_to_hertz", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double e) { return speech::erb_to_hertz(e); });
	});
	rt.add_function("hertz_to_mel", [](double f) -> double { return speech::hertz_to_mel(f); });
	rt.add_function("hertz_to_mel", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double f) { return speech::hertz_to_mel(f); });
	});
	rt.add_function("mel_to_hertz", [](double m) -> double { return speech::mel_to_hertz(m); });
	rt.add_function("mel_to_hertz", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double m) { return speech::mel_to_hertz(m); });
	});
	rt.add_function("hertz_to_semitones", [](double f) -> double { return speech::hertz_to_semitones(f); });
	rt.add_function("hertz_to_semitones", [](double f, double ref) -> double { return speech::hertz_to_semitones(f, ref); });
	rt.add_function("hertz_to_semitones", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double f) { return speech::hertz_to_semitones(f); });
	});
	rt.add_function("hertz_to_semitones", [apply_fn](const NumArray &a, double ref) -> NumArray {
		return apply_fn(a, [ref](double f) { return speech::hertz_to_semitones(f, ref); });
	});
	rt.add_function("semitones_to_hertz", [](double st) -> double { return speech::semitones_to_hertz(st); });
	rt.add_function("semitones_to_hertz", [](double st, double ref) -> double { return speech::semitones_to_hertz(st, ref); });
	rt.add_function("semitones_to_hertz", [apply_fn](const NumArray &a) -> NumArray {
		return apply_fn(a, [](double st) { return speech::semitones_to_hertz(st); });
	});
	rt.add_function("semitones_to_hertz", [apply_fn](const NumArray &a, double ref) -> NumArray {
		return apply_fn(a, [ref](double st) { return speech::semitones_to_hertz(st, ref); });
	});

	// ── Format conversion ───────────────────────────────────────────────
	//
	// convert(sound, output_path, format)            -> keep source sample rate
	// convert(sound, output_path, format, samplerate)
	//
	// `format` is a case-insensitive string: "wav", "aiff", "flac", "ogg",
	// or "mp3" (the last requires a libsndfile build with MPEG support).
	// Channel count is preserved; sample-rate conversion uses r8brain.

	rt.add_function("convert", [](Isolate &iso, Sound &sound, const String &out_path, const String &format_str) {
		guarded(iso, [&] {
			auto fmt = Sound::parse_format(format_str);
			convert_sound(sound, out_path, fmt, /*target_sample_rate=*/0);
			return 0;
		});
	});
	rt.add_function("convert",
	                [](Isolate &iso, Sound &sound, const String &out_path, const String &format_str, double sr_d) {
		guarded(iso, [&] {
			if (!(sr_d > 0) || sr_d != std::floor(sr_d)) {
				throw error("[Argument error] Sample rate must be a positive integer, got %", sr_d);
			}
			auto fmt = Sound::parse_format(format_str);
			convert_sound(sound, out_path, fmt, (int) sr_d);
			return 0;
		});
	});
}

double Sound::max_value() const
{
    return static_cast<double>(*std::max_element(m_data.begin(), m_data.end()));
}

double Sound::min_value() const
{
    return static_cast<double>(*std::min_element(m_data.begin(), m_data.end()));
}

const Array<float> &Sound::data() const
{
	return m_data;
}

Array<float> &Sound::data()
{
	return m_data;
}

double Sound::frame_to_time(intptr_t index) const
{
	return double(--index) / sample_rate();
}

intptr_t Sound::time_to_frame(double time) const
{
	auto s = (intptr_t) round(time * sample_rate()) + 1;
	return std::min<intptr_t>(s, m_handle.frames());
}

std::span<const float> Sound::channel_view(int n) const
{
	assert(n <= nchannel());
	auto start = m_data.data() + (n-1) * channel_size();

	return { start, start + channel_size() };
}

std::span<const float> Sound::channel_view(int n, intptr_t first_sample, intptr_t last_sample) const
{
    assert(n >= 1 && n <= m_nchannel);
    assert(first_sample >= 1 && last_sample <= m_nframes);
    auto base = m_data.data() + (n-1) * m_nframes;
    return { base + first_sample - 1, base + last_sample };
}

Array<double> Sound::get_channel(int n, intptr_t first_sample, intptr_t last_sample) const
{
    assert(first_sample >= 1 && first_sample <= m_nframes);
    assert(last_sample >= first_sample && last_sample <= m_nframes);

    if (n == 0)
    {
        return average_channels(first_sample, last_sample);
    }

    auto view = channel_view(n, first_sample, last_sample);
    Array<double> result;
    result.reserve(view.size());

    for (auto sample : view) {
        result.append(static_cast<double>(sample));
    }

    return result;
}

Array<double> Sound::average_channels(intptr_t first_frame, intptr_t last_frame) const
{
    if (last_frame < 0) last_frame = m_nframes;
    assert(first_frame >= 1);
    auto len = last_frame - first_frame + 1;
    Array<double> result(len, 0.0);

    for (intptr_t i = 0; i < len; i++)
    {
        double value = 0.0;
        for (intptr_t j = 0; j < m_nchannel; j++) {
            // first_frame is a 1-based frame number; the data matrix is 0-based.
            value += m_data(first_frame - 1 + i, j);
        }
        result[i] = value / m_nchannel;
    }

    return result;
}

double Sound::get_sample(int channel, intptr_t index) const
{
    assert(index >= 1 && index <= m_nframes);

    if (channel == 0)
    {
        double value = 0.0;
        for (intptr_t j = 0; j < m_nchannel; ++j) {
            // index is a 1-based frame number; the data matrix is 0-based.
            value += m_data(index - 1, j);
        }
        return value / m_nchannel;
    }

    return static_cast<double>(m_data(index - 1, channel - 1));
}

speech::PitchTracker Sound::get_pitch_tracker(const String &name)
{
	if (name == "harvest") {
		return speech::PitchTracker::Harvest;
	}
	else if (name == "rapt") {
		return speech::PitchTracker::Rapt;
	}
	else if (name == "reaper") {
		return speech::PitchTracker::Reaper;
	}
	else if (name == "swipe") {
		return speech::PitchTracker::Swipe;
	}
	else if (name == "praat") {
		return speech::PitchTracker::Praat;
	}

	throw error("Invalid pitch tracking method : %", name);
}


} // namespace phonometrica
