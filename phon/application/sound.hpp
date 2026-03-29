/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Sound file.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SOUND_HPP
#define PHONOMETRICA_SOUND_HPP

#if PHON_WINDOWS
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#endif

#include <cmath>
#include <sndfile.hh>
#include <phon/application/vfs.hpp>
#include <phon/third_party/rtaudio/RtAudio.h>
#include <phon/utils/matrix.hpp>
#include <phon/array.hpp>
#include <phon/utils/slice.hpp>
#include <phon/utils/signal.hpp>
#include <phon/analysis/signal_processing.hpp>


namespace phonometrica {

class Runtime;
class Object;


class Sound final : public Document
{
public:

	enum class Format
	{
		WAV  = SF_FORMAT_WAV,
		AIFF = SF_FORMAT_AIFF,
		FLAC = SF_FORMAT_FLAC,
		OGG  = SF_FORMAT_OGG
	};

	Sound(Directory *parent, String path);

	static void set_sound_formats();

	static const Array<String> &supported_sound_formats();

	static const Array<String> &common_sound_formats();

	static Array<String> supported_sound_format_names();

	static String rtaudio_version();

	static String libsndfile_version();

	static bool supports_format(const String &format);

	static speech::PitchTracker get_pitch_tracker(const String &name);

	double max_value() const;

	double min_value() const;

    const Array<float> &data() const;

    Array<float> &data();

    SndfileHandle handle() const;

    void convert(const String &path, int sample_rate, Format fmt);

    double get_pitch(int channel, speech::PitchTracker method, double time, double min_pitch, double max_pitch, double threshold,
                     double octave_jump_cost = 0.35, double voicing_cost = 0.45, double silence_threshold = 0.03, double octave_cost = 0.01);

	double get_mean_pitch(int channel, speech::PitchTracker method, double t1, double t2, double min_pitch, double max_pitch, double threshold, double time_step = 0.01);

	double get_intensity(int channel, double time);

	double get_mean_intensity(int channel, double t1, double t2, double time_step = 0.01);

	Array<double> get_intensity(int channel, double from, double to, double time_step, bool &start_at_zero);

	Array<double> get_formants(int channel, double time, int nformant, double nyquist_frequency, double window_size, int lpc_order);

	Array<double> get_formants(int channel, const Array<double> &times, int nformant, double nyquist_frequency, double window_size, int lpc_order);

	static void initialize(Runtime &rt);

	Array<double> get_channel(int n, intptr_t first_sample, intptr_t last_sample) const;

    double duration() const { return m_duration; }

    int sample_rate() const { return m_sample_rate; }

    intptr_t nframes() const { return m_nframes; }

    int nchannel() const { return m_nchannel; }

    intptr_t channel_size() const { return m_nframes; }

    intptr_t size() const { return m_nframes * m_nchannel; }

    bool is_mono() const { return m_nchannel == 1; }

	double frame_to_time(intptr_t index) const;

	intptr_t time_to_frame(double time) const;

	double get_sample(int channel, intptr_t index) const;

	constexpr double get_intensity_window_duration() const
	{
		// Praat's settings: use 3.2 pitch periods
		constexpr double min_pitch = 100;
		constexpr double effective_duration = 3.2 / min_pitch;

		return effective_duration;
	}

	int get_intensity_window_size() const;


    std::span<const float> channel_view(int n) const;

    std::span<const float> channel_view(int n, intptr_t first_sample, intptr_t last_sample) const;

	static Signal<const String&, const String&, int> start_loading;

	static Signal<int> update_loading;

private:

	void load() override;

	void write() override;

	Array<double> average_channels(intptr_t first_frame = 0, intptr_t last_frame = -1) const;

	static Array<String> the_supported_sound_formats, the_common_sound_formats;

    Array<float> m_data;

    mutable SndfileHandle m_handle;

    // Cached metadata:
    int m_sample_rate = 0;
    int m_nchannel = 0;
    intptr_t m_nframes = 0;
    double m_duration = 0.0;
};

namespace traits {
template<> struct maybe_cyclic<Sound> : std::false_type { };
}
} // namespace phonometrica

#endif // PHONOMETRICA_SOUND_HPP
