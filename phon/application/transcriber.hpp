/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2026 Julien Eychenne                                                                                  *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more        *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 17/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Speech-to-text transcription using whisper.cpp. Produces a Layer of time-aligned interval events from a    *
 * Sound. Only compiled when CMake option WITH_WHISPER is enabled (PHON_WITH_WHISPER=1).                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_TRANSCRIBER_HPP
#define PHONOMETRICA_TRANSCRIBER_HPP

#include <functional>
#include <phon/string.hpp>
#include <phon/application/sound.hpp>
#include <phon/application/annotation_data.hpp>

namespace phonometrica {

class Transcriber final
{
public:

	struct Options
	{
		// Path to a ggml whisper model file (e.g. ggml-base.bin).
		String model_path;

		// ISO 639-1 language code (e.g. "en", "fr"). Empty or "auto" triggers automatic detection.
		String language;

		// Number of threads for inference. 0 means use hardware concurrency (bounded).
		int n_threads = 0;

		// Label assigned to the output layer.
		String layer_label = "transcription";
	};

	// Progress feedback. Return false from the callback to request cancellation.
	using ProgressCallback = std::function<bool(int percent, const String &message)>;

	// Sink for whisper + ggml diagnostic messages. Receives one message per call (trailing
	// newlines stripped). Installed globally via set_log_sink(); a null sink silences all
	// whisper/ggml output, including its default stderr fallback.
	using LogSink = std::function<void(const String &message)>;

	// Install a process-global log sink. Thread-safe to call from any thread. The sink is
	// itself invoked on whichever thread whisper/ggml chooses — typically the inference
	// thread. Callers routing to GUI widgets must marshal to the GUI thread themselves.
	static void set_log_sink(LogSink sink);

	Transcriber();
	~Transcriber();

	Transcriber(const Transcriber &) = delete;
	Transcriber &operator=(const Transcriber &) = delete;

	// Transcribe `sound` and return a single interval Layer. One event per whisper segment.
	// Throws on model load failure or inference failure. The sound is loaded (via open())
	// if not already in memory — hence the non-const reference.
	Layer transcribe(Sound &sound, const Options &opts, ProgressCallback progress = {});

private:

	// Produce a 16 kHz mono float buffer from an arbitrary Sound (downmix + resample).
	Array<float> prepare_samples(Sound &sound);

	// Opaque pointer to the whisper context; void* to keep whisper.h out of our header.
	void *m_ctx = nullptr;

	// Path of the currently loaded model (so we can reuse if the same model is requested twice).
	String m_loaded_model;

	void load_model(const String &path);
	void free_model();
};

} // namespace phonometrica

#endif // PHONOMETRICA_TRANSCRIBER_HPP
