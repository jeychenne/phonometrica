/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2026 Julien Eychenne                                                                                  *
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
 * Created: 09/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Live audio capture from an input device. Streams interleaved float samples through a pre-allocated         *
 * lock-free block pool to a background writer thread that calls libsndfile. Recording is unbounded in duration: the   *
 * cost model is one disk-write per BLOCK_FRAMES, never an in-memory buffer the size of the recording. The audio      *
 * callback never allocates and never touches the file. Pure C++; no Qt dependency.                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SOUND_RECORDER_HPP
#define PHONOMETRICA_SOUND_RECORDER_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sndfile.hh>
#include <phon/string.hpp>
#include <phon/application/sound.hpp>
#include <phon/third_party/rtaudio/RtAudio.h>

namespace phonometrica {

class SoundRecorder final
{
public:

	// Mirror of RtAudio::DeviceInfo restricted to what we need, so that
	// callers (notably GUI code) do not have to include RtAudio.h.
	struct InputDevice
	{
		unsigned int id = 0;
		std::string  name;
		unsigned int max_input_channels = 0;
		std::vector<unsigned int> sample_rates;
		unsigned int preferred_sample_rate = 0;
		bool         is_default = false;
	};

	// Enumerates the input-capable devices on the host's preferred audio API
	// (Core Audio on macOS, ALSA on Linux, DirectSound on Windows). The returned
	// list is empty if no device is available; the caller should disable the
	// "Record sound" UI in that case.
	static std::vector<InputDevice> enumerate_input_devices();

	// Construct, allocate the block pool, and open `output_path` for writing
	// in `fmt` at `sample_rate` and `channels`. Throws on any of: invalid
	// device, unopenable file, unsupported (rate, channels, format) triple.
	// The constructor does not start the stream; call start().
	SoundRecorder(const String &output_path,
	              Sound::Format fmt,
	              unsigned int device_id,
	              unsigned int sample_rate,
	              unsigned int channels);

	SoundRecorder(const SoundRecorder &) = delete;
	SoundRecorder(SoundRecorder &&) = delete;

	~SoundRecorder();

	// Open the RtAudio stream and the writer thread, and begin capture.
	// Idempotent on a recorder that is already running.
	void start();

	// Cease capture, drain pending blocks to disk, close the file. Idempotent.
	// After stop() returns, the output file is guaranteed flushed and closed.
	void stop();

	bool is_recording() const { return m_running.load(std::memory_order_relaxed); }

	// Total seconds of audio committed to the file plus what is in the pipeline.
	// Computed from the atomic frame counter — safe to call from any thread.
	double current_duration() const;

	// Maximum |sample| seen since the previous call, normalised to [0, 1]. Reset
	// to 0 on read so the GUI can drive a falling-back VU meter from a timer.
	double peak_level();

	// Number of frames the audio thread had to drop because the free-block pool
	// was empty (writer falling behind). Stays at 0 in normal operation; a
	// non-zero value at stop() should be surfaced to the user.
	std::size_t dropped_frames() const { return m_dropped_frames.load(std::memory_order_relaxed); }

	unsigned int sample_rate() const { return m_sample_rate; }
	unsigned int channels()    const { return m_channels; }
	const String &output_path() const { return m_output_path; }

private:

	// One unit of work in the pool. `data` is sized at construction and never
	// reallocated; `valid_frames` is what the callback actually filled before
	// handing the block to the writer.
	struct Block
	{
		std::vector<float> data;
		std::size_t valid_frames = 0;
	};

	// Bounded SPSC ring of Block*. Producer and consumer roles are fixed:
	// for the "free" queue, writer pushes / callback pops; for the "filled"
	// queue, callback pushes / writer pops. Capacity must be a power of two.
	class BlockQueue
	{
	public:
		void init(std::size_t capacity_pow2);
		bool try_push(Block *b);
		Block *try_pop();

	private:
		std::vector<Block *> m_slots;
		std::size_t          m_mask = 0;
		std::atomic<std::size_t> m_head{0};
		std::atomic<std::size_t> m_tail{0};
	};

	// Static trampoline forwarding to capture(). The void* cookie is `this`.
	static int capture(void *output, void *input, unsigned int nframes,
	                   double stream_time, RtAudioStreamStatus status, void *cookie);

	// Per-callback work: copy interleaved input into the current block; when
	// the block fills up, push it to the writer and pop a fresh one.
	int capture_impl(const float *input, unsigned int nframes);

	// Writer-thread main loop. Drains the filled queue to libsndfile and
	// returns blocks to the free pool. Sleeps briefly when nothing is ready.
	void writer_main();

	// Map a Sound::Format value to the libsndfile flags this recorder writes.
	// Default is PCM_16; FLAC also uses PCM_16 (FLAC has no float subformat),
	// OGG uses Vorbis. Throws if `fmt` is not one of WAV/AIFF/FLAC/OGG.
	static int sndfile_flags(Sound::Format fmt);

	static void rt_error_callback(RtAudioErrorType type, const std::string &msg);

	// Configuration captured at construction.
	String          m_output_path;
	Sound::Format   m_format;
	unsigned int    m_device_id    = 0;
	unsigned int    m_sample_rate  = 0;
	unsigned int    m_channels     = 0;
	std::size_t     m_block_frames = 0;
	std::size_t     m_pool_blocks  = 0;

	// Output: opened in ctor so failure is reported before any audio happens.
	SndfileHandle m_outfile;

	// Block pool: owns the storage; the queues hold non-owning pointers into it.
	std::vector<std::unique_ptr<Block>> m_pool;
	BlockQueue m_free_blocks;
	BlockQueue m_filled_blocks;

	// Audio side: current block being filled by the callback. Touched only
	// from the audio thread; not atomic.
	Block *m_current_block      = nullptr;

	// RtAudio plumbing.
	RtAudio                    m_stream;
	RtAudio::StreamParameters  m_params;
	RtAudio::StreamOptions     m_options;

	// Writer thread.
	std::thread             m_writer;
	std::mutex              m_writer_mutex;          // guards the cv + m_writer_should_stop
	std::condition_variable m_writer_cv;
	bool                    m_writer_should_stop = false;

	// Cross-thread state.
	std::atomic<bool>        m_running{false};
	std::atomic<std::size_t> m_frames_captured{0};   // monotonically increasing
	std::atomic<std::size_t> m_dropped_frames{0};
	std::atomic<float>       m_peak_since_last_read{0.0f};
};

} // namespace phonometrica

#endif // PHONOMETRICA_SOUND_RECORDER_HPP
