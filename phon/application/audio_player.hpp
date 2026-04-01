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
 * Created: 22/03/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Audio player. This class is responsible for all sound playback in phonometrica. Each sound/annotation      *
 * view has its own player.                                                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_AUDIO_PLAYER_HPP
#define PHONOMETRICA_AUDIO_PLAYER_HPP

#include <atomic>
#include <memory>
#include <vector>
#include <cmath>
#include <phon/application/resampler.hpp>
#include <phon/third_party/rtaudio/RtAudio.h>
#include <phon/application/sound.hpp>

namespace phonometrica {

class AudioPlayer final
{
public:
	explicit AudioPlayer(const Handle<Sound> &data);

	AudioPlayer(const AudioPlayer &) = delete;
	AudioPlayer(AudioPlayer &&) = delete;

	~AudioPlayer();

	void play(double from, double to);

	bool paused() const;
	bool running() const;
	bool finished_naturally() const;
	bool has_error() const { return m_error != nullptr; }

	// Thread-safe accessor for the current playback time (in seconds).
	// Meant to be polled from a QTimer on the main thread.
	double currentPlaybackTime() const;

	void raise_error();

	Signal<double> current_time;
	Signal<> done;

	void interrupt();
	void pause();
	void resume();
	void stop();

private:
	static int playback(void *output, void *input, unsigned int nframe,
						double stream_time, RtAudioStreamStatus status, void *data);

	void prepare();
	void initialize_resampling(uint32_t output_rate);

	static void play_silence(double *data, size_t size);
	static void error_callback(RtAudioErrorType type, const std::string &msg);

	std::atomic<bool> m_paused = false;
	std::atomic<bool> m_running = false;
	std::atomic<bool> m_finished_naturally = false; // New: detects natural end

	std::atomic<sf_count_t> m_current_source_position;
	std::atomic<sf_count_t> m_playback_start_frame;
	std::atomic<sf_count_t> m_playback_end_frame;

	std::atomic<double> m_last_emitted_time{-1.0};
	static constexpr double TIME_UPDATE_INTERVAL = 1.0/30.0; // 24 FPS

	std::exception_ptr m_error = nullptr;

	// RtAudio
	RtAudio::StreamParameters m_params;
	RtAudio::StreamOptions m_options;
	RtAudio m_stream;
	unsigned int m_output_sample_rate = 0;

	// Audio data
	Handle<Sound> m_sound_data;
	std::unique_ptr<Resampler> m_left_resampler;   // Left channel resampler
	std::unique_ptr<Resampler> m_right_resampler;  // Right channel resampler

	std::vector<double> m_left_channel_buffer;     // Left channel buffer for resampling
	std::vector<double> m_right_channel_buffer;    // Right channel buffer for resampling
	std::vector<double> m_resampler_output_buffer; // Resampler output buffer

	// Helper
	sf_count_t calculate_remaining_source_frames() const;
};

} // namespace phonometrica

#endif // PHONOMETRICA_AUDIO_PLAYER_HPP