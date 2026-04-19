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
 * Created: 19/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/application/silence_detector.hpp>

#include <algorithm>
#include <cmath>

namespace phonometrica {

// Algorithm overview:
//   1. Downmix to mono at the native sample rate (average across channels).
//   2. Frame the signal with 25 ms windows at a 10 ms hop (standard speech framing
//      defaults). Compute per-frame mean-square power.
//   3. Use the peak frame power as the reference level. A frame is provisionally
//      silent iff its power is below peak * 10^(threshold_db / 10).
//   4. Smoothing pass A: any interior silent run shorter than `min_silence_duration`
//      is flipped back to speech. This prevents splitting on plosive closures and
//      brief word-internal gaps. Leading and trailing silence are left alone so we
//      don't accidentally grow a single speech region to cover the whole file when
//      there's a short silent head or tail.
//   5. Smoothing pass B: any speech run shorter than `min_speech_duration` is flipped
//      to silence. Filters out isolated clicks, taps, coughs.
//   6. Extract contiguous speech runs, pad each by `speech_padding` on both sides,
//      clamp to [0, total_samples], and merge any resulting overlaps.
//   7. Convert sample indices to seconds.

std::vector<SilenceDetector::Region>
SilenceDetector::find_speech_regions(Sound &sound, const Options &opts)
{
	std::vector<Region> regions;

	// Ensure the PCM data is in memory. This is a no-op if already loaded.
	sound.open();

	const int      sample_rate = sound.sample_rate();
	const int      nchan       = sound.nchannel();
	const intptr_t nframes     = sound.nframes();

	if (sample_rate <= 0 || nframes <= 0 || nchan <= 0)
		return regions;

	// Downmix to mono. We accumulate in double for numerical stability in case of
	// large channel counts, then keep mono samples as float (enough precision for
	// an energy calculation and cheaper than dragging doubles through two passes).
	std::vector<float> mono((size_t) nframes, 0.0f);

	if (nchan == 1)
	{
		auto view = sound.channel_view(1);
		for (intptr_t i = 0; i < nframes; i++)
			mono[(size_t) i] = view[i];
	}
	else
	{
		const double scale = 1.0 / double(nchan);
		for (int c = 1; c <= nchan; c++)
		{
			auto view = sound.channel_view(c);
			for (intptr_t i = 0; i < nframes; i++)
				mono[(size_t) i] += float(double(view[i]) * scale);
		}
	}

	const int frame_size = std::max(1, sample_rate * 25 / 1000);   // 25 ms
	const int hop_size   = std::max(1, sample_rate * 10 / 1000);   // 10 ms

	// File too short to frame: treat as a single speech region covering the whole file.
	if ((intptr_t) frame_size > nframes)
	{
		regions.push_back({ 0.0, sound.duration() });
		return regions;
	}

	const intptr_t n_frames = (nframes - frame_size) / hop_size + 1;

	// Per-frame mean-square power, and the peak across all frames.
	std::vector<float> energy((size_t) n_frames, 0.0f);
	double peak = 0.0;

	for (intptr_t i = 0; i < n_frames; i++)
	{
		const intptr_t off = i * hop_size;
		double sum = 0.0;
		for (int j = 0; j < frame_size; j++)
		{
			const double s = double(mono[(size_t) (off + j)]);
			sum += s * s;
		}
		const double e = sum / double(frame_size);
		energy[(size_t) i] = float(e);
		if (e > peak) peak = e;
	}

	// Completely silent (or numerically zero) sound: no speech regions.
	if (peak <= 0.0)
		return regions;

	// Threshold is in dB of power; ratio is 10^(db / 10).
	const double thr_ratio = std::pow(10.0, opts.silence_threshold_db / 10.0);
	const float  thr       = float(peak * thr_ratio);

	std::vector<unsigned char> is_speech((size_t) n_frames, 0u);
	for (intptr_t i = 0; i < n_frames; i++)
		is_speech[(size_t) i] = (energy[(size_t) i] >= thr) ? 1u : 0u;

	// Frames per second (for converting durations in seconds to frame counts).
	const double frames_per_s = double(sample_rate) / double(hop_size);
	const intptr_t min_sil_frames = std::max<intptr_t>(1,
		intptr_t(std::round(opts.min_silence_duration * frames_per_s)));
	const intptr_t min_sp_frames  = std::max<intptr_t>(1,
		intptr_t(std::round(opts.min_speech_duration  * frames_per_s)));

	// Pass A: absorb interior silences shorter than min_silence.
	{
		intptr_t i = 0;
		while (i < n_frames)
		{
			if (is_speech[(size_t) i]) { i++; continue; }
			intptr_t j = i;
			while (j < n_frames && !is_speech[(size_t) j]) j++;
			const bool interior = (i > 0) && (j < n_frames);
			if (interior && (j - i) < min_sil_frames)
			{
				for (intptr_t k = i; k < j; k++) is_speech[(size_t) k] = 1u;
			}
			i = j;
		}
	}

	// Pass B: drop speech runs shorter than min_speech.
	{
		intptr_t i = 0;
		while (i < n_frames)
		{
			if (!is_speech[(size_t) i]) { i++; continue; }
			intptr_t j = i;
			while (j < n_frames && is_speech[(size_t) j]) j++;
			if ((j - i) < min_sp_frames)
			{
				for (intptr_t k = i; k < j; k++) is_speech[(size_t) k] = 0u;
			}
			i = j;
		}
	}

	// Extract speech runs in sample coordinates, apply padding, merge overlaps.
	const intptr_t pad_samples = std::max<intptr_t>(0,
		intptr_t(std::round(opts.speech_padding * sample_rate)));

	struct SampleRange { intptr_t start; intptr_t end; };
	std::vector<SampleRange> sample_regions;

	{
		intptr_t i = 0;
		while (i < n_frames)
		{
			if (!is_speech[(size_t) i]) { i++; continue; }
			intptr_t j = i;
			while (j < n_frames && is_speech[(size_t) j]) j++;

			// Frames [i, j) cover samples [i*hop, (j-1)*hop + frame_size).
			intptr_t s_start = i * hop_size - pad_samples;
			intptr_t s_end   = (j - 1) * hop_size + frame_size + pad_samples;
			if (s_start < 0)       s_start = 0;
			if (s_end   > nframes) s_end   = nframes;

			if (!sample_regions.empty() && s_start <= sample_regions.back().end)
			{
				if (s_end > sample_regions.back().end)
					sample_regions.back().end = s_end;
			}
			else
			{
				sample_regions.push_back({ s_start, s_end });
			}
			i = j;
		}
	}

	// Convert to seconds. Clamp to the reported sound duration in case of any
	// rounding discrepancy between nframes/sample_rate and sound.duration().
	const double duration = sound.duration();
	regions.reserve(sample_regions.size());
	for (const auto &r : sample_regions)
	{
		double start = double(r.start) / double(sample_rate);
		double end   = double(r.end)   / double(sample_rate);
		if (start < 0.0)      start = 0.0;
		if (end   > duration) end   = duration;
		if (end   <= start)   continue;
		regions.push_back({ start, end });
	}

	return regions;
}

} // namespace phonometrica
