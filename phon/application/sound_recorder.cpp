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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <phon/application/sound_recorder.hpp>
#include <phon/application/settings.hpp>
#include <phon/definitions.hpp>

#if PHON_WINDOWS
#define RECORDER_API RtAudio::WINDOWS_DS
#elif PHON_MACOS
#define RECORDER_API RtAudio::MACOSX_CORE
#else
#define RECORDER_API RtAudio::LINUX_ALSA
#endif

#define KEEP_RECORDING 0
#define STOP_RECORDING 1

namespace phonometrica {

// --------------------------------------------------------------------------
// Block queue (single-producer / single-consumer ring of Block*).
// --------------------------------------------------------------------------

void SoundRecorder::BlockQueue::init(std::size_t capacity_pow2)
{
	// The ring needs one slot to disambiguate empty from full, so the actual
	// usable capacity is capacity_pow2 - 1. Caller sizes accordingly.
	m_slots.assign(capacity_pow2, nullptr);
	m_mask = capacity_pow2 - 1;
	m_head.store(0, std::memory_order_relaxed);
	m_tail.store(0, std::memory_order_relaxed);
}

bool SoundRecorder::BlockQueue::try_push(Block *b)
{
	auto tail = m_tail.load(std::memory_order_relaxed);
	auto next = (tail + 1) & m_mask;
	if (next == m_head.load(std::memory_order_acquire)) {
		return false; // full
	}
	m_slots[tail] = b;
	m_tail.store(next, std::memory_order_release);
	return true;
}

SoundRecorder::Block *SoundRecorder::BlockQueue::try_pop()
{
	auto head = m_head.load(std::memory_order_relaxed);
	if (head == m_tail.load(std::memory_order_acquire)) {
		return nullptr; // empty
	}
	Block *b = m_slots[head];
	m_head.store((head + 1) & m_mask, std::memory_order_release);
	return b;
}

// --------------------------------------------------------------------------
// Static helpers.
// --------------------------------------------------------------------------

void SoundRecorder::rt_error_callback(RtAudioErrorType, const std::string &msg)
{
	PHON_LOG("SoundRecorder RtAudio error: %s\n", msg.data());
}

int SoundRecorder::sndfile_flags(Sound::Format fmt)
{
	switch (fmt)
	{
		case Sound::Format::WAV:
			return SF_FORMAT_WAV  | SF_FORMAT_PCM_16;
		case Sound::Format::AIFF:
			return SF_FORMAT_AIFF | SF_FORMAT_PCM_16;
		case Sound::Format::FLAC:
			// FLAC has no float subformat; PCM_16 is the universal default.
			return SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
		case Sound::Format::OGG:
			return SF_FORMAT_OGG  | SF_FORMAT_VORBIS;
	}
	throw std::runtime_error("Unsupported recording format");
}

std::vector<SoundRecorder::InputDevice> SoundRecorder::enumerate_input_devices()
{
	std::vector<InputDevice> out;
	RtAudio probe(RECORDER_API, &SoundRecorder::rt_error_callback);

	const auto default_in = probe.getDefaultInputDevice();
	for (auto id : probe.getDeviceIds())
	{
		auto info = probe.getDeviceInfo(id);
		if (info.inputChannels == 0) continue;

		InputDevice d;
		d.id   = info.ID;
		d.name = info.name;
		d.max_input_channels    = info.inputChannels;
		d.sample_rates          = info.sampleRates;
		d.preferred_sample_rate = info.preferredSampleRate;
		// On RtAudio, isDefaultInput is set on the device record returned for
		// the default. We also belt-and-braces against getDefaultInputDevice
		// in case the flag is not populated by every backend.
		d.is_default = info.isDefaultInput || (info.ID == default_in);
		out.push_back(std::move(d));
	}
	return out;
}

// --------------------------------------------------------------------------
// Construction / destruction.
// --------------------------------------------------------------------------

SoundRecorder::SoundRecorder(const String &output_path,
                             Sound::Format fmt,
                             unsigned int device_id,
                             unsigned int sample_rate,
                             unsigned int channels)
	: m_output_path(output_path),
	  m_format(fmt),
	  m_device_id(device_id),
	  m_sample_rate(sample_rate),
	  m_channels(channels),
	  m_stream(RECORDER_API, &SoundRecorder::rt_error_callback)
{
	if (channels == 0 || channels > 2) {
		throw std::runtime_error("Recorder supports 1 or 2 channels only");
	}
	if (sample_rate == 0) {
		throw std::runtime_error("Recorder requires a non-zero sample rate");
	}

	// Pull tunables from Settings, falling back to sensible defaults if the
	// table is missing (older configs that haven't seen reset_recording yet).
	try {
		m_block_frames = static_cast<std::size_t>(Settings::get_int("recording", "block_frames"));
		m_pool_blocks  = static_cast<std::size_t>(Settings::get_int("recording", "pool_blocks"));
	}
	catch (...) {
		m_block_frames = 4096;
		m_pool_blocks  = 64;
	}
	if (m_block_frames < 256)  m_block_frames = 256;
	if (m_pool_blocks  < 8)    m_pool_blocks  = 8;

	// Round pool size up to a power of two for the SPSC mask trick. We need
	// one extra slot per ring so the ring can hold m_pool_blocks Block*.
	std::size_t ring_cap = 1;
	while (ring_cap < (m_pool_blocks + 1)) ring_cap <<= 1;
	m_free_blocks.init(ring_cap);
	m_filled_blocks.init(ring_cap);

	// Allocate the pool once. Every Block's vector keeps its capacity for the
	// full lifetime of the recorder; .data() pointers are stable.
	m_pool.reserve(m_pool_blocks);
	for (std::size_t i = 0; i < m_pool_blocks; i++) {
		auto blk = std::make_unique<Block>();
		blk->data.assign(m_block_frames * m_channels, 0.0f);
		blk->valid_frames = 0;
		// All blocks start out free.
		m_free_blocks.try_push(blk.get());
		m_pool.push_back(std::move(blk));
	}

	// Open the output file before we start so any I/O failure surfaces
	// synchronously, not a minute into the recording.
	const int flags = sndfile_flags(m_format);
#if PHON_WINDOWS
	auto wpath = m_output_path.to_wide();
	m_outfile = SndfileHandle(wpath.data(), SFM_WRITE, flags,
	                          static_cast<int>(m_channels),
	                          static_cast<int>(m_sample_rate));
#else
	m_outfile = SndfileHandle(m_output_path.data(), SFM_WRITE, flags,
	                          static_cast<int>(m_channels),
	                          static_cast<int>(m_sample_rate));
#endif
	if (m_outfile.error() != SF_ERR_NO_ERROR) {
		throw std::runtime_error(std::string("Could not open output file: ") + m_outfile.strError());
	}

	// Configure the RtAudio input parameters; the stream itself is opened in start().
	m_params.deviceId     = m_device_id;
	m_params.nChannels    = m_channels;
	m_params.firstChannel = 0;
	m_options.flags       = 0;
}

SoundRecorder::~SoundRecorder()
{
	stop();
}

// --------------------------------------------------------------------------
// Lifecycle.
// --------------------------------------------------------------------------

void SoundRecorder::start()
{
	if (m_running.load(std::memory_order_relaxed)) return;

	// Take the first free block as the audio thread's initial workspace.
	m_current_block = m_free_blocks.try_pop();
	if (!m_current_block) {
		// Should be impossible right after construction (pool starts full),
		// but guard anyway so we don't dereference null in the callback.
		throw std::runtime_error("SoundRecorder: pool unexpectedly empty at start()");
	}
	m_current_block->valid_frames = 0;

	// Reset cross-thread counters.
	m_frames_captured.store(0, std::memory_order_relaxed);
	m_dropped_frames.store(0, std::memory_order_relaxed);
	m_peak_since_last_read.store(0.0f, std::memory_order_relaxed);

	{
		std::lock_guard<std::mutex> lk(m_writer_mutex);
		m_writer_should_stop = false;
	}

	// Start the writer first so it is ready to drain the very first block.
	m_writer = std::thread([this]() { writer_main(); });

	// Open the RtAudio stream. Note we pass FLOAT32: matches Sound's storage
	// representation and avoids a double->float conversion before disk write.
	unsigned int buffer_frames = 1024;
	auto err = m_stream.openStream(
		nullptr,            // no output
		&m_params,          // input parameters
		RTAUDIO_FLOAT32,
		m_sample_rate,
		&buffer_frames,
		&SoundRecorder::capture,
		this,
		&m_options);

	if (err != RTAUDIO_NO_ERROR) {
		// Tear down the writer we just spun up, then surface the error.
		{
			std::lock_guard<std::mutex> lk(m_writer_mutex);
			m_writer_should_stop = true;
		}
		m_writer_cv.notify_all();
		if (m_writer.joinable()) m_writer.join();
		throw std::runtime_error(std::string("Could not open audio input stream: ") + m_stream.getErrorText());
	}

	m_running.store(true, std::memory_order_release);

	err = m_stream.startStream();
	if (err != RTAUDIO_NO_ERROR) {
		m_running.store(false, std::memory_order_release);
		if (m_stream.isStreamOpen()) m_stream.closeStream();
		{
			std::lock_guard<std::mutex> lk(m_writer_mutex);
			m_writer_should_stop = true;
		}
		m_writer_cv.notify_all();
		if (m_writer.joinable()) m_writer.join();
		throw std::runtime_error(std::string("Could not start audio input stream: ") + m_stream.getErrorText());
	}
}

void SoundRecorder::stop()
{
	if (!m_running.exchange(false)) {
		// Either never started, or already stopped. Reassigning to a
		// default-constructed handle is a no-op when the handle is already
		// empty, and flushes/closes when it is not. Either way, idempotent.
		m_outfile = SndfileHandle();
		return;
	}

	// Tell the audio callback to stop pushing new blocks. The next callback
	// invocation will return STOP_RECORDING; we then close the stream.
	if (m_stream.isStreamRunning()) m_stream.stopStream();
	if (m_stream.isStreamOpen())    m_stream.closeStream();

	// Hand off the in-flight current block (if it has any data) so the writer
	// gets the tail. If the pool is full we can't push without dropping the
	// tail, but in practice the writer is keeping up by the time we get here.
	if (m_current_block && m_current_block->valid_frames > 0) {
		if (!m_filled_blocks.try_push(m_current_block)) {
			// Last-ditch: write directly. We are on the GUI thread now so
			// this is safe; libsndfile will simply append.
			m_outfile.writef(m_current_block->data.data(),
			                 static_cast<sf_count_t>(m_current_block->valid_frames));
		}
		m_current_block = nullptr;
	}

	// Tell the writer to drain everything still queued and exit.
	{
		std::lock_guard<std::mutex> lk(m_writer_mutex);
		m_writer_should_stop = true;
	}
	m_writer_cv.notify_all();
	if (m_writer.joinable()) m_writer.join();

	// Close the file. SndfileHandle's destructor flushes; reassigning to a
	// default-constructed handle releases the resource synchronously.
	m_outfile = SndfileHandle();
}

// --------------------------------------------------------------------------
// Audio callback (RT-safe).
// --------------------------------------------------------------------------

int SoundRecorder::capture(void *, void *input, unsigned int nframes,
                           double, RtAudioStreamStatus status, void *cookie)
{
	auto *self = reinterpret_cast<SoundRecorder *>(cookie);
	if (status) {
		// Overrun on the input side. Nothing actionable here; libsndfile will
		// receive a clean copy of whatever the OS gave us.
	}
	if (!self->m_running.load(std::memory_order_acquire)) {
		return STOP_RECORDING;
	}
	return self->capture_impl(reinterpret_cast<const float *>(input), nframes);
}

int SoundRecorder::capture_impl(const float *input, unsigned int nframes)
{
	const unsigned int ch = m_channels;
	std::size_t remaining = nframes;
	const float *src = input;

	float local_peak = 0.0f;

	while (remaining > 0)
	{
		if (!m_current_block) {
			// Try to grab a free block. If the pool is dry, the writer has
			// fallen far enough behind that we have to drop these samples.
			m_current_block = m_free_blocks.try_pop();
			if (!m_current_block) {
				m_dropped_frames.fetch_add(remaining, std::memory_order_relaxed);
				break;
			}
			m_current_block->valid_frames = 0;
		}

		const std::size_t cap   = m_block_frames - m_current_block->valid_frames;
		const std::size_t take  = std::min(cap, remaining);
		const std::size_t bytes = take * ch * sizeof(float);
		float *dst = m_current_block->data.data() + (m_current_block->valid_frames * ch);
		std::memcpy(dst, src, bytes);

		// Update peak over the just-copied span. Cheap and gives the GUI a
		// usable VU reading without ever touching the writer thread.
		const std::size_t samples = take * ch;
		for (std::size_t i = 0; i < samples; i++) {
			float a = std::fabs(dst[i]);
			if (a > local_peak) local_peak = a;
		}

		m_current_block->valid_frames += take;
		src       += take * ch;
		remaining -= take;

		if (m_current_block->valid_frames >= m_block_frames) {
			// Block full: hand off, get a new one. If we cannot push (filled
			// queue full because writer hasn't caught up *and* free queue was
			// empty so we couldn't have arrived here?  In a power-of-two ring
			// of equal capacity per direction this can only happen during a
			// transient; treat it as a drop.
			if (!m_filled_blocks.try_push(m_current_block)) {
				m_dropped_frames.fetch_add(m_current_block->valid_frames,
				                           std::memory_order_relaxed);
				m_current_block->valid_frames = 0;
				// keep using the same block; no allocation needed
			}
			else {
				m_current_block = nullptr;
			}
		}
	}

	// Update atomic counters (release for the frame counter so the GUI's
	// current_duration() observes a coherent value).
	m_frames_captured.fetch_add(nframes, std::memory_order_release);

	// Peak: keep the larger of the existing reading and what we just saw.
	float prev = m_peak_since_last_read.load(std::memory_order_relaxed);
	while (local_peak > prev &&
	       !m_peak_since_last_read.compare_exchange_weak(
	           prev, local_peak, std::memory_order_relaxed))
	{ /* retry */ }

	return KEEP_RECORDING;
}

// --------------------------------------------------------------------------
// Writer thread.
// --------------------------------------------------------------------------

void SoundRecorder::writer_main()
{
	using namespace std::chrono_literals;

	auto drain_one = [this]() -> bool {
		Block *b = m_filled_blocks.try_pop();
		if (!b) return false;
		// libsndfile writef takes frames and accepts the float* directly when
		// the file was opened with a float-compatible API.
		m_outfile.writef(b->data.data(), static_cast<sf_count_t>(b->valid_frames));
		b->valid_frames = 0;
		// Return the block to the free pool. If the pool ring is full
		// (shouldn't happen in steady state), we leak the slot — which is
		// recovered when the recorder is destroyed.
		m_free_blocks.try_push(b);
		return true;
	};

	for (;;)
	{
		// Drain whatever is queued.
		while (drain_one()) { /* keep going */ }

		// Sleep until either there might be more work, or we are told to stop.
		// We poll on a short timeout so a missed notification (extremely
		// unlikely in this code, but possible if the audio thread's notify
		// races with us locking) cannot deadlock the writer.
		std::unique_lock<std::mutex> lk(m_writer_mutex);
		if (m_writer_should_stop) {
			lk.unlock();
			// Final drain after the stop flag is set: the audio thread may
			// have pushed one more block between our last drain_one() and
			// taking the lock.
			while (drain_one()) { /* keep going */ }
			return;
		}
		m_writer_cv.wait_for(lk, 5ms);
	}
}

// --------------------------------------------------------------------------
// Status accessors.
// --------------------------------------------------------------------------

double SoundRecorder::current_duration() const
{
	auto frames = m_frames_captured.load(std::memory_order_acquire);
	return static_cast<double>(frames) / static_cast<double>(m_sample_rate);
}

double SoundRecorder::peak_level()
{
	float v = m_peak_since_last_read.exchange(0.0f, std::memory_order_relaxed);
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	return static_cast<double>(v);
}

} // namespace phonometrica
