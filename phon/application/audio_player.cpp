/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <phon/application/audio_player.hpp>
#include <phon/application/settings.hpp>

#if PHON_WINDOWS
#define SOUND_API RtAudio::WINDOWS_DS
#elif PHON_MACOS
#define SOUND_API RtAudio::MACOSX_CORE
#define MAC_SAMPLE_RATE 44100.0
#else
#define SOUND_API RtAudio::LINUX_ALSA
#endif

#define MONO_CHANNEL 0
#define KEEP_PLAYING 0
#define STOP_PLAYING 1
#define FRAME_COUNT 1024
#define MINIMUM_DURATION 1.0

namespace phonometrica {

void AudioPlayer::play_silence(double* data, size_t size) {
	memset(data, 0, size * sizeof(double));
}

void AudioPlayer::error_callback(RtAudioErrorType, const std::string &msg) {
	PHON_LOG("%s\n", msg.data());
}

AudioPlayer::AudioPlayer(const Handle<Sound> &snd)
		: m_stream(SOUND_API),
		  m_sound_data(snd),
		  m_paused(true),
		  m_running(false),
		  m_current_source_position(0),
		  m_playback_start_frame(0),
		  m_playback_end_frame(0),
		  m_finished_naturally(false)
{
	prepare();
	initialize_resampling(m_output_sample_rate);

	// Prepare buffers for resampling - Sound object already has data in memory
	double ratio = static_cast<double>(m_sound_data->sample_rate()) / m_output_sample_rate;
	size_t max_source_frames = static_cast<size_t>(std::ceil(FRAME_COUNT * ratio)) + 256; // margin for resampler

	m_left_channel_buffer.resize(max_source_frames);
	m_right_channel_buffer.resize(max_source_frames);
	m_resampler_output_buffer.resize(FRAME_COUNT * 2); // enough for stereo
}

AudioPlayer::~AudioPlayer() {
	stop();
}

sf_count_t AudioPlayer::calculate_remaining_source_frames() const {
	sf_count_t remaining = m_playback_end_frame.load(std::memory_order_relaxed) -
						   m_current_source_position.load(std::memory_order_relaxed);
	return std::max<sf_count_t>(0LL, remaining);
}

// Simplified RtAudio callback - all work is done here
int AudioPlayer::playback(void *out, void *, unsigned int nframe, double stream_time, RtAudioStreamStatus status, void *d) {
	auto player = reinterpret_cast<AudioPlayer*>(d);
	auto output = reinterpret_cast<double*>(out);
	const size_t total_output_samples = nframe * player->m_params.nChannels;

	// Initialize with silence
	player->play_silence(output, total_output_samples);

	if (status) {
		PHON_LOG("Stream underflow detected! Status: %d\n", (int)status);
	}

	// If paused or stopped, play silence
	if (player->paused() || !player->m_running.load(std::memory_order_relaxed)) {
		return KEEP_PLAYING;
	}

	// Check if there are frames left to read
	sf_count_t remaining_frames = player->calculate_remaining_source_frames();
	if (remaining_frames <= 0) {
		// Natural end reached
		player->m_finished_naturally.store(true, std::memory_order_relaxed);
		player->m_running.store(false, std::memory_order_relaxed);
		player->done(); // End signal
		return STOP_PLAYING;
	}

	// Calculate how many source frames to read
	double input_rate = player->m_sound_data->sample_rate();
	double output_rate = player->m_output_sample_rate;
	double ratio = input_rate / output_rate;

	sf_count_t frames_to_read = static_cast<sf_count_t>(std::ceil(nframe * ratio));
	frames_to_read = std::min(frames_to_read, remaining_frames);

	if (frames_to_read <= 0) {
		return KEEP_PLAYING; // Keep silence
	}

	// Read from Sound object's in-memory data
	sf_count_t actual_frames_read = 0;
	sf_count_t current_pos = player->m_current_source_position.load(std::memory_order_relaxed);
	sf_count_t end_pos = std::min(current_pos + frames_to_read,
								  player->m_playback_end_frame.load(std::memory_order_relaxed));
	actual_frames_read = end_pos - current_pos;

	if (actual_frames_read > 0) {
		// Get channel data directly from Sound's in-memory data using zero-copy views.
		// channel_view returns std::span<const float>; we widen to double into pre-allocated buffers.
		if (player->m_sound_data->is_mono()) {
			auto view = player->m_sound_data->channel_view(1, current_pos, end_pos - 1);
			for (size_t i = 0; i < view.size(); ++i) {
				double s = static_cast<double>(view[i]);
				player->m_left_channel_buffer[i] = s;
				player->m_right_channel_buffer[i] = s;
			}
		} else {
			auto left_view = player->m_sound_data->channel_view(1, current_pos, end_pos - 1);
			auto right_view = player->m_sound_data->channel_view(2, current_pos, end_pos - 1);
			for (size_t i = 0; i < left_view.size(); ++i)
				player->m_left_channel_buffer[i] = static_cast<double>(left_view[i]);
			for (size_t i = 0; i < right_view.size(); ++i)
				player->m_right_channel_buffer[i] = static_cast<double>(right_view[i]);
		}
	}

	// Update position
	player->m_current_source_position.fetch_add(actual_frames_read, std::memory_order_relaxed);

	// Fill with silence if incomplete read
	if (actual_frames_read < frames_to_read) {
		player->play_silence(player->m_left_channel_buffer.data() + actual_frames_read,
							 frames_to_read - actual_frames_read);
		player->play_silence(player->m_right_channel_buffer.data() + actual_frames_read,
							 frames_to_read - actual_frames_read);
	}

	// Resample both channels
	double *left_resampled_ptr = nullptr;
	double *right_resampled_ptr = nullptr;
	int left_resampled_samples = 0;
	int right_resampled_samples = 0;

	if (actual_frames_read > 0) {
		left_resampled_samples = player->m_left_resampler->process(
				player->m_left_channel_buffer.data(), (int)actual_frames_read, left_resampled_ptr);
		right_resampled_samples = player->m_right_resampler->process(
				player->m_right_channel_buffer.data(), (int)actual_frames_read, right_resampled_ptr);

		if (left_resampled_samples < 0 || !left_resampled_ptr ||
			right_resampled_samples < 0 || !right_resampled_ptr) {
			PHON_LOG("Resampler error\n");
			return KEEP_PLAYING;
		}
	}

	// Copy to output with proper stereo handling
	size_t samples_to_copy = std::min({(size_t)left_resampled_samples,
									   (size_t)right_resampled_samples,
									   (size_t)nframe});

	if (player->m_params.nChannels == 1) {
		// Mono output: mix left and right channels
		for (size_t i = 0; i < samples_to_copy; ++i) {
			output[i] = (left_resampled_ptr[i] + right_resampled_ptr[i]) * 0.5;
		}
	} else {
		// Stereo output: interleave left and right channels
		for (size_t i = 0; i < samples_to_copy; ++i) {
			output[i * 2] = left_resampled_ptr[i];      // Left channel
			output[i * 2 + 1] = right_resampled_ptr[i]; // Right channel
		}
	}

	// Emit current time signal (THROTTLED)
	double current_time_seconds = static_cast<double>(
										  player->m_current_source_position.load(std::memory_order_relaxed)) / input_rate;

	double last_time = player->m_last_emitted_time.load(std::memory_order_relaxed);

	// Only emit if enough time has passed (30 FPS = ~33ms intervals)
	if (current_time_seconds - last_time >= TIME_UPDATE_INTERVAL) {
		player->m_last_emitted_time.store(current_time_seconds, std::memory_order_relaxed);
		player->current_time(current_time_seconds);
	}

	return KEEP_PLAYING;
}

void AudioPlayer::prepare() {
	m_params.deviceId = m_stream.getDefaultOutputDevice();

#if PHON_MACOS
	m_params.nChannels = 2; // Toujours stéréo sur macOS
	m_output_sample_rate = MAC_SAMPLE_RATE;
#else
	// Other platforms: adapt according to needs
    m_params.nChannels = 2; // Force stereo for proper stereo playback
    m_output_sample_rate = (unsigned int) m_sound_data->sample_rate();
#endif
	m_params.firstChannel = 0;

#if PHON_LINUX
	m_options.flags = RTAUDIO_ALSA_USE_DEFAULT;
#elif PHON_MACOS
	m_options.flags = RTAUDIO_SCHEDULE_REALTIME;
#else
	m_options.flags = RTAUDIO_SCHEDULE_REALTIME;
#endif
}

void AudioPlayer::play(double from, double to) {
	// Validation of playback range
	double duration = m_sound_data->duration();
	if (from < 0.0) from = 0.0;
	if (to > duration) to = duration;

	if (from == to) {
		if (duration < MINIMUM_DURATION) {
			from = 0.0;
			to = duration;
		} else {
			from = std::max(0.0, from - MINIMUM_DURATION / 2);
			to = std::min(duration, to + MINIMUM_DURATION / 2);
		}
	}

	// Stop if already running
	if (m_stream.isStreamOpen()) {
		stop();
	}

	// Configure playback range
	m_playback_start_frame.store(m_sound_data->time_to_frame(from), std::memory_order_relaxed);
	m_playback_end_frame.store(m_sound_data->time_to_frame(to), std::memory_order_relaxed);
	m_current_source_position.store(m_playback_start_frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
	m_finished_naturally.store(false, std::memory_order_relaxed);

	// Open RtAudio stream
	unsigned int frame_count = FRAME_COUNT;

	RtAudioErrorType error = m_stream.openStream(&m_params,
												 nullptr,
												 RTAUDIO_FLOAT64,
												 m_output_sample_rate,
												 &frame_count,
												 &AudioPlayer::playback,
												 this,
												 &m_options);

	if (error != RTAUDIO_NO_ERROR) {
		PHON_LOG("RtAudio Error during stream setup: %d\n", (int)error);
		return;
	}

	m_stream.setErrorCallback(error_callback);

	error = m_stream.startStream();
	if (error != RTAUDIO_NO_ERROR) {
		PHON_LOG("RtAudio Error starting stream: %d\n", (int)error);
		m_stream.closeStream();
		return;
	}

	m_running.store(true, std::memory_order_relaxed);
	m_paused.store(false, std::memory_order_relaxed);
}

void AudioPlayer::initialize_resampling(uint32_t output_rate) {
	if (m_left_resampler == nullptr) {
		auto input_rate = (uint32_t) m_sound_data->sample_rate();
		// r8brain CDSPResampler24: input_rate, output_rate, max_input_frames
		m_left_resampler = std::make_unique<Resampler>(input_rate, output_rate, FRAME_COUNT * 2);
		m_right_resampler = std::make_unique<Resampler>(input_rate, output_rate, FRAME_COUNT * 2);
	}
}

bool AudioPlayer::paused() const {
	return m_paused.load(std::memory_order_relaxed);
}

bool AudioPlayer::finished_naturally() const {
	return m_finished_naturally.load(std::memory_order_relaxed);
}

double AudioPlayer::currentPlaybackTime() const {
	double input_rate = m_sound_data->sample_rate();
	auto pos = m_current_source_position.load(std::memory_order_relaxed);
	return static_cast<double>(pos) / input_rate;
}

void AudioPlayer::raise_error() {
	if (m_error) {
		auto e = std::move(m_error);
		m_error = nullptr;
		std::rethrow_exception(std::move(e));
	}
}

void AudioPlayer::pause() {
	m_paused.store(true, std::memory_order_relaxed);
	if (m_stream.isStreamRunning()) {
		RtAudioErrorType error = m_stream.stopStream();
		if (error != RTAUDIO_NO_ERROR) {
			PHON_LOG("RtAudio Error pausing stream: %d\n", (int)error);
		}
	}
}

void AudioPlayer::resume() {
	if (!m_running.load(std::memory_order_relaxed)) {
		// If sound finished naturally, don't resume
		return;
	}

	m_paused.store(false, std::memory_order_relaxed);
	if (m_stream.isStreamOpen() && !m_stream.isStreamRunning()) {
		RtAudioErrorType error = m_stream.startStream();
		if (error != RTAUDIO_NO_ERROR) {
			PHON_LOG("RtAudio Error while resuming stream: %d\n", (int)error);
		}
	}
}

void AudioPlayer::interrupt() {
	stop();
}

void AudioPlayer::stop() {
	m_running.store(false, std::memory_order_relaxed);

	if (m_stream.isStreamOpen()) {
		if (m_stream.isStreamRunning()) {
			RtAudioErrorType error = m_stream.stopStream();
			if (error != RTAUDIO_NO_ERROR) {
				PHON_LOG("RtAudio Error stopping stream: %d\n", (int)error);
			}
		}
		m_stream.closeStream();
	}

	// Reset positions
	m_current_source_position.store(0, std::memory_order_relaxed);
	m_playback_start_frame.store(0, std::memory_order_relaxed);
	m_playback_end_frame.store(0, std::memory_order_relaxed);
	m_paused.store(true, std::memory_order_relaxed);
	m_finished_naturally.store(false, std::memory_order_relaxed);
}

bool AudioPlayer::running() const {
	return m_running.load(std::memory_order_relaxed) && m_stream.isStreamRunning();
}

} // namespace phonometrica