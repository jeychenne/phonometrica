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
 * Purpose: Energy-based silence detector. Given a Sound, returns a list of speech regions (non-silent time spans)     *
 * in seconds by thresholding short-term mean-square power against the peak frame power. Intended for two user-facing  *
 * workflows: (1) producing an annotation layer with alternating silence/speech intervals for manual inspection and    *
 * label filling (the "Find silences" tool, mirroring Praat's "To TextGrid (silences)"); (2) as a building block for   *
 * future features that want speech regions as input (e.g. batch transcription of word lists).                         *
 *                                                                                                                     *
 * The detector is intentionally simple: it trusts the dynamic range of the recording and does not distinguish speech  *
 * from other energy-bearing sounds. It works well on clean studio or field recordings and on read speech, less well   *
 * on recordings with substantial continuous background noise near the threshold. For the latter case a Silero VAD     *
 * port via ONNX Runtime is planned as part of the forced-alignment work.                                              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SILENCE_DETECTOR_HPP
#define PHONOMETRICA_SILENCE_DETECTOR_HPP

#include <vector>
#include <phon/application/sound.hpp>

namespace phonometrica {

class SilenceDetector final
{
public:

	struct Options
	{
		// Level below the peak short-term power (in dB) at or below which a frame is
		// considered silent. Typical useful range: -40 to -15 dB. Praat's default for
		// "To TextGrid (silences)" is -25 dB; we use the same default.
		double silence_threshold_db = -25.0;

		// Minimum duration of a silent region (seconds). Silences shorter than this
		// are absorbed back into surrounding speech. Acts as hangover to avoid splitting
		// on plosive closures and short intra-word gaps.
		double min_silence_duration = 0.7;

		// Minimum duration of a speech region (seconds). Isolated speech runs shorter
		// than this are treated as noise (clicks, taps) and discarded.
		double min_speech_duration = 0.1;

		// Padding added on each side of every detected speech region (seconds). Extends
		// each region slightly to avoid clipping plosive bursts and offsets. After
		// padding, overlapping regions are merged.
		double speech_padding = 0.1;
	};

	// A contiguous speech region, in seconds from the start of the sound.
	struct Region
	{
		double start;
		double end;

		double duration() const { return end - start; }
	};

	// Analyse `sound` and return the detected speech regions, sorted by start time,
	// non-overlapping, clamped to [0, sound.duration()]. If the sound is silent or has
	// zero length, returns an empty vector.
	//
	// `sound` is opened (loaded into memory) if not already; this is why the parameter
	// is a non-const reference. Safe to call on any sample rate; internally the analysis
	// runs at the native rate of the sound, so region boundaries land on the audio's
	// natural time grid.
	static std::vector<Region> find_speech_regions(Sound &sound, const Options &opts);
};

} // namespace phonometrica

#endif // PHONOMETRICA_SILENCE_DETECTOR_HPP
