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
 * Created: 21/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Automatic selection of the maximum formant frequency with Santiago Barreda's Fast Track method:            *
 *                                                                                                                     *
 *     Barreda, S. (2021). Fast Track: fast (nearly) automatic formant-tracking using Praat. Linguistics Vanguard,     *
 *     7(1). https://doi.org/10.1515/lingvan-2020-0051                                                                 *
 *                                                                                                                     *
 * This is a C++ reimplementation of the selection procedure of the Fast Track Praat plugin                            *
 * (https://github.com/santiagobarreda/FastTrack, MIT licence, Copyright (c) 2020 Santiago Barreda), following its     *
 * scripts trackAutoselectProcedure.praat, findError.praat and predictFormants.praat:                                  *
 *                                                                                                                     *
 *   1. The token is analysed with Burg's method (5.5 formants, i.e. LPC order 11) at `nsteps` maximum formant         *
 *      frequencies ("ceilings") evenly spaced between the lowest and highest ceiling.                                 *
 *   2. For each analysis, only the frames in which more than 3 formants were found are kept. Each formant track is    *
 *      fitted by ordinary least squares with an intercept and `ncoeff` cosine terms cos(pi * c * i / N), where i is   *
 *      the rank of the frame (1..N) among the kept frames.                                                            *
 *   3. The error of a formant is its mean absolute residual (Hz), rounded to 0.1 Hz; the error of an analysis is the  *
 *      sum over formants, rounded to 0.1 Hz. Heuristics add 10000 to the error of a formant when the medians of the   *
 *      tracks are implausible.                                                                                        *
 *   4. The analysis with the lowest error wins (the first one in case of a tie). Its smooth tracks (the fitted         *
 *      cosine series) are the measured formants.                                                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FAST_TRACK_HPP
#define PHONOMETRICA_FAST_TRACK_HPP

#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace phonometrica {

class Sound;

namespace speech {

// Settings of a Fast Track analysis. The defaults are those of the Fast Track plugin.
struct FastTrackSettings final
{
	double lowest_ceiling = 4700;   // Hz
	double highest_ceiling = 7550;  // Hz
	int nsteps = 20;                // number of ceilings between the two bounds (inclusive)
	int ncoeff = 5;                 // number of cosine terms in each formant's model (plus an intercept)
	int nformant = 3;               // number of formants used for selection: 3 or 4
	double time_step = 0.002;       // s
	double window_size = 0.025;     // s (nominal; the Gaussian window spans twice this length)
	int lpc_order = 11;             // 5.5 formants

	// Heuristics: each one that fires adds `penalty` to the error of a formant. Enabled/disabled as in the plugin.
	bool f1_frequency = false;  double max_f1 = 1200;    // median F1 above max_f1 (F1 penalised)
	bool b1_bandwidth = false;  double max_b1 = 500;     // median B1 above max_b1 (F1 penalised)
	bool b2_bandwidth = false;  double max_b2 = 600;     // median B2 above max_b2 (F2 penalised)
	bool b3_bandwidth = false;  double max_b3 = 900;     // median B3 above max_b3 (F3 penalised)
	bool f4_frequency = true;   double min_f4 = 2900;    // median F4 below min_f4 (F4 penalised; 4 formants only)
	bool rhotic = true;         // F2 - F1 < 500 and F3 < 2000 (F3 penalised)
	bool f3f4_proximity = true; // F2 - F1 < 1500 and F4 - F3 < 500 (F4 penalised; 4 formants only)
	double penalty = 10000;

	// Ceilings analysed, as in the plugin: round(lowest + (highest - lowest) / (nsteps - 1) * (i - 1)).
	std::vector<double> ceilings() const;
};

// Formants found in each frame of one analysis: (frequency, bandwidth) pairs in ascending frequency.
using FormantFrames = std::vector<std::vector<std::pair<double,double>>>;

// One candidate analysis (one ceiling) and its smooth model.
struct FastTrackCandidate final
{
	double ceiling = 0;
	bool valid = false;  // false if too few frames to fit the model
	double error = std::numeric_limits<double>::infinity();  // total error, rounded to 0.1 Hz
	std::array<double, 4> formant_error = { 0, 0, 0, 0 };   // per formant, rounded to 0.1 Hz, heuristics included

	// Model: coefficients[k] = { intercept, c1, ..., c_ncoeff } for formant k, fitted on the kept frames, whose times
	// are `frame_times` (the frames with more than 3 formants, in order).
	std::vector<std::vector<double>> coefficients;
	std::vector<double> frame_times;

	// Value of the smooth track of formant k (0-based) at time t. Times are mapped to the model's normalised time
	// through the kept frames (linear interpolation of the frame rank, clamped to the first and last frames).
	double predict(int k, double t) const;
};

struct FastTrackResult final
{
	bool ok = false;     // false if no candidate could be modelled
	int winner = -1;     // index in `candidates`
	std::vector<FastTrackCandidate> candidates;

	const FastTrackCandidate &best() const { return candidates[winner]; }
	double ceiling() const { return ok ? best().ceiling : std::nan(""); }
};

// Frame times used for a token spanning [t1, t2]: frames every `time_step`, centred in the interval, as Praat places
// them in a sound extracted with a buffer of half the physical window on each side (Fast Track's default extraction).
std::vector<double> fast_track_frame_times(double t1, double t2, double time_step);

// Score one analysis: keep the frames with more than 3 formants, fit the models, compute the error and heuristics.
FastTrackCandidate fast_track_score(const std::vector<double> &times, const FormantFrames &frames, double ceiling,
                                    const FastTrackSettings &settings);

// Run Fast Track on the interval [t1, t2] of a channel of a sound.
FastTrackResult fast_track(Sound &sound, int channel, double t1, double t2, const FastTrackSettings &settings);

} // namespace speech

} // namespace phonometrica

#endif // PHONOMETRICA_FAST_TRACK_HPP
