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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/application/transcriber.hpp>

#if PHON_WITH_WHISPER

#include <algorithm>
#include <mutex>
#include <thread>
#include <whisper.h>
#include <ggml.h>
#include <phon/error.hpp>
#include <phon/application/resampler.hpp>

namespace phonometrica {

// whisper.h already defines WHISPER_SAMPLE_RATE as a macro (16000). We use that directly
// rather than shadowing it with our own constant.

// ---------------------------------------------------------------------------------------
// Log sink plumbing.
//
// whisper and ggml each have their own global log_set() hook. We install a single C shim
// for both, which reads a std::function kept here. The function is protected by a mutex
// to allow set_log_sink() to be called from any thread without races against in-flight
// log writes. Each message arrives with its trailing '\n' intact — we forward it as-is
// so sinks that append line-by-line (e.g. OutputPanel::appendText, which uses insertText
// and does NOT add newlines) keep their line structure.

static std::mutex s_log_mutex;
static Transcriber::LogSink s_log_sink;  // null = drop silently

static void phon_whisper_log_cb(ggml_log_level /*level*/, const char *text, void * /*ud*/)
{
	if (!text || !*text) return;

	// Grab a copy of the sink under the lock so the caller can reset it concurrently.
	Transcriber::LogSink sink;
	{
		std::lock_guard<std::mutex> lk(s_log_mutex);
		sink = s_log_sink;
	}
	if (!sink) return;

	sink(String(text));
}

void Transcriber::set_log_sink(Transcriber::LogSink sink)
{
	{
		std::lock_guard<std::mutex> lk(s_log_mutex);
		s_log_sink = std::move(sink);
	}
	// Install (or re-install) the C callback. whisper and ggml each take their own.
	whisper_log_set(&phon_whisper_log_cb, nullptr);
	ggml_log_set   (&phon_whisper_log_cb, nullptr);
}

// ---------------------------------------------------------------------------------------

// Progress callback state passed to whisper via the user_data void*. We keep it here
// (and not in the class) so that only the C ABI shims see it.
struct ProgressState
{
	Transcriber::ProgressCallback *cb;
	bool cancelled;
};

static void on_new_segment(struct whisper_context *, struct whisper_state *,
                           int /*n_new*/, void *user_data)
{
	// Could be used for streaming updates. For now, progress_callback is enough.
	(void) user_data;
}

static void on_progress(struct whisper_context *, struct whisper_state *,
                        int progress, void *user_data)
{
	auto *state = static_cast<ProgressState *>(user_data);
	if (!state || !state->cb) return;
	if (!(*state->cb)(progress, "Transcribing..."))
		state->cancelled = true;
}

static bool on_encoder_abort(struct whisper_context *, struct whisper_state *, void *user_data)
{
	// whisper's contract: return FALSE to abort. So we return true to continue, and only
	// flip to false if the user has requested cancellation.
	auto *state = static_cast<ProgressState *>(user_data);
	if (!state) return true;                  // no state — always continue
	return !state->cancelled;                 // continue unless cancelled
}


Transcriber::Transcriber() = default;

Transcriber::~Transcriber()
{
	free_model();
}

void Transcriber::free_model()
{
	if (m_ctx)
	{
		whisper_free(static_cast<whisper_context *>(m_ctx));
		m_ctx = nullptr;
	}
	m_loaded_model.clear();
}

void Transcriber::load_model(const String &path)
{
	if (m_ctx && m_loaded_model == path)
		return;

	free_model();

	whisper_context_params cparams = whisper_context_default_params();
	// Keep GPU off by default for portability; can be exposed in Options later if needed.
	cparams.use_gpu = false;

	auto *ctx = whisper_init_from_file_with_params(path.data(), cparams);
	if (!ctx)
		throw error("Could not load whisper model from '%'", path);

	m_ctx = ctx;
	m_loaded_model = path;
}

Array<float> Transcriber::prepare_samples(Sound &sound)
{
	// Sound documents in Phonometrica are loaded lazily — picking a sound from the
	// project doesn't populate its PCM buffer. open() is a no-op if already loaded.
	sound.open();

	const int nchan = sound.nchannel();
	const int in_rate = sound.sample_rate();
	const intptr_t nframes = sound.nframes();

	// Downmix to mono (sum and average) as doubles for the resampler.
	Array<double> mono(nframes, 0.0);

	if (nchan == 1)
	{
		auto view = sound.channel_view(1);
		for (intptr_t i = 0; i < nframes; i++)
			mono[i + 1] = double(view[i]);
	}
	else
	{
		const double scale = 1.0 / double(nchan);
		for (int c = 1; c <= nchan; c++)
		{
			auto view = sound.channel_view(c);
			for (intptr_t i = 0; i < nframes; i++)
				mono[i + 1] += double(view[i]) * scale;
		}
	}

	// Resample if needed.
	Array<double> resampled;
	if (in_rate == WHISPER_SAMPLE_RATE)
	{
		resampled = std::move(mono);
	}
	else
	{
		std::span<double> in_span(mono.data(), (size_t) mono.size());
		resampled = resample(in_span, double(in_rate), double(WHISPER_SAMPLE_RATE));
	}

	// Convert to float for whisper; clip defensively.
	Array<float> out(resampled.size(), 0.0f);
	for (intptr_t i = 1; i <= resampled.size(); i++)
	{
		double v = resampled[i];
		if (v > 1.0) v = 1.0;
		else if (v < -1.0) v = -1.0;
		out[i] = float(v);
	}
	return out;
}

Layer Transcriber::transcribe(Sound &sound, const Options &opts, ProgressCallback progress)
{
	load_model(opts.model_path);

	auto samples = prepare_samples(sound);

	// Flatten to a raw float* that whisper can read. Array<T> is 1-based but data()
	// still points at element [1], so we pass data() and the physical count.
	const int n_samples = int(samples.size());
	const float *sample_ptr = samples.data();

	whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

	int threads = opts.n_threads;
	if (threads <= 0)
	{
		threads = int(std::thread::hardware_concurrency());
		if (threads <= 0) threads = 4;
		if (threads > 8) threads = 8;
	}
	wparams.n_threads = threads;

	wparams.print_realtime   = false;
	wparams.print_progress   = false;
	wparams.print_timestamps = false;
	wparams.print_special    = false;
	wparams.translate        = opts.translate;
	wparams.single_segment   = false;
	wparams.no_context       = true;
	wparams.suppress_blank   = true;

	if (!opts.language.empty() && opts.language != "auto")
	{
		// whisper holds a pointer to this string; `opts` outlives the call.
		wparams.language = opts.language.data();
	}
	else
	{
		wparams.language = "auto";
	}

	ProgressState pstate{ progress ? &progress : nullptr, false };
	wparams.progress_callback           = on_progress;
	wparams.progress_callback_user_data = &pstate;
	wparams.new_segment_callback        = on_new_segment;
	wparams.new_segment_callback_user_data = &pstate;
	wparams.encoder_begin_callback      = on_encoder_abort;
	wparams.encoder_begin_callback_user_data = &pstate;

	auto *ctx = static_cast<whisper_context *>(m_ctx);
	int rc = whisper_full(ctx, wparams, sample_ptr, n_samples);

	if (pstate.cancelled)
		throw error("Transcription cancelled by user");

	if (rc != 0)
		throw error("Whisper failed during inference (code %)", intptr_t(rc));

	// Build the output layer from segments.
	Layer layer(opts.layer_label, false);
	const int n_seg = whisper_full_n_segments(ctx);
	const double duration = sound.duration();

	for (int i = 0; i < n_seg; i++)
	{
		// whisper timestamps are in units of 10 ms.
		int64_t t0 = whisper_full_get_segment_t0(ctx, i);
		int64_t t1 = whisper_full_get_segment_t1(ctx, i);
		double start = double(t0) / 100.0;
		double end   = double(t1) / 100.0;

		// Guard against rounding past the actual sound duration.
		if (end > duration) end = duration;
		if (start < 0.0)    start = 0.0;
		if (end <= start)
		{
			// Degenerate segment; skip rather than corrupt the layer invariants.
			continue;
		}

		const char *txt = whisper_full_get_segment_text(ctx, i);
		String text(txt ? txt : "");
		// Whisper prepends a leading space to most segments; trim it.
		text.rtrim();
		text.ltrim();

		layer.add_interval(start, end, text);
	}

	return layer;
}

} // namespace phonometrica

#else // !PHON_WITH_WHISPER

#include <phon/error.hpp>

namespace phonometrica {

Transcriber::Transcriber() = default;
Transcriber::~Transcriber() = default;
void Transcriber::free_model() {}
void Transcriber::load_model(const String &) {}
void Transcriber::set_log_sink(Transcriber::LogSink) {}
Array<float> Transcriber::prepare_samples(Sound &) { return {}; }

Layer Transcriber::transcribe(Sound &, const Options &, ProgressCallback)
{
	throw error("This build of Phonometrica does not include whisper support. "
	            "Rebuild with -DWITH_WHISPER=ON to enable transcription.");
}

} // namespace phonometrica

#endif // PHON_WITH_WHISPER
