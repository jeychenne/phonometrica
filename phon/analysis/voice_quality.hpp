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
 * Created: 16/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: voice-quality kernel.                                                                                      *
 *                                                                                                                     *
 *   * Glottal-pulse detection via REAPER's EpochTracker (Talkin/Google).                                              *
 *   * Jitter family: local (relative), local absolute, RAP, PPQ5, DDP.                                                *
 *   * Shimmer family: local (relative), local dB, APQ3, APQ5, APQ11.                                                  *
 *   * HNR derived from the Viterbi-chosen normalised autocorrelation strength of                                      *
 *     the Praat-style pitch tracker (Boersma 1993).                                                                   *
 *                                                                                                                     *
 * Defaults mirror Praat's "Voice report" so that values can be sanity-checked against Praat.                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VOICE_QUALITY_HPP
#define PHONOMETRICA_VOICE_QUALITY_HPP

#include <cmath>
#include <span>
#include <vector>

namespace phonometrica::speech {

// ── Filter options for jitter and shimmer measures ────────────────────
//
// Periods T_i = t_{i+1} - t_i computed from a sequence of glottal-closure
// instants (GCIs) are filtered before being entered into any jitter or
// shimmer aggregate. Defaults match Praat's "Voice report" defaults.

struct PeriodFilter
{
	// Reject periods outside [period_floor, period_ceiling] seconds.
	// Defaults correspond to a 75–600 Hz periodicity range.
	double period_floor   = 1.0 / 600.0;
	double period_ceiling = 1.0 / 75.0;

	// Reject pair (T_i, T_{i+1}) when max/min > max_period_factor.
	double max_period_factor = 1.3;
};

struct AmplitudeFilter
{
	PeriodFilter period;
	// Reject pair (A_i, A_{i+1}) when max/min > max_amplitude_factor.
	double max_amplitude_factor = 1.6;
};


// ── Glottal-pulse detection (REAPER EpochTracker) ─────────────────────
//
// Returns the times (seconds, relative to start of `samples`) of glottal
// closure instants within voiced regions. Pulses inserted by REAPER's
// interpolation across unvoiced regions are discarded.
//
//   f0_min, f0_max  : bound the periodicity search range.
//   do_highpass     : apply an 80 Hz high-pass to remove DC/rumble
//                     (recommended; mirrors REAPER's normal usage).
//
// Throws on REAPER init / feature / tracking failure.
std::vector<double> compute_glottal_pulses(
        std::span<const double> samples,
        double sample_rate,
        double f0_min      = 75.0,
        double f0_max      = 600.0,
        bool   do_highpass = true);


// ── Jitter family ─────────────────────────────────────────────────────
//
// All measures take a sequence of GCI times produced by
// `compute_glottal_pulses`. They apply the PeriodFilter before
// aggregating.
//
// Definitions follow Praat's PointProcess voice-quality routines:
//
//   jitter_local      : mean |T_{i+1} - T_i| / mean T_i
//                       (relative, multiply by 100 for "percent")
//   jitter_local_abs  : mean |T_{i+1} - T_i| in seconds
//   jitter_rap        : mean |T_i - mean(T_{i-1}, T_i, T_{i+1})| / mean T_i
//                       (3-point relative average perturbation)
//   jitter_ppq5       : 5-point version of rap
//   jitter_ddp        : difference-of-differences-of-periods.
//                       Exactly 3 × jitter_rap (the classic identity).
//
// Returns NaN if fewer than the minimum number of valid periods or
// windows is available for the measure.

double jitter_local    (std::span<const double> pulses, PeriodFilter f = {});
double jitter_local_abs(std::span<const double> pulses, PeriodFilter f = {});
double jitter_rap      (std::span<const double> pulses, PeriodFilter f = {});
double jitter_ppq5     (std::span<const double> pulses, PeriodFilter f = {});
double jitter_ddp      (std::span<const double> pulses, PeriodFilter f = {});


// ── Shimmer family ────────────────────────────────────────────────────
//
// Shimmer measures take both GCI times and the source samples. Peak
// amplitudes A_i are computed as max |x(t)| in a symmetric window of
// width equal to the local mean period centred on each GCI, with the
// window clipped to the valid sample range.
//
//   shimmer_local    : mean |A_{i+1} - A_i| / mean A_i
//   shimmer_local_db : mean |20 · log10(A_{i+1} / A_i)| in dB
//   shimmer_apq3     : 3-point amplitude perturbation quotient
//   shimmer_apq5     : 5-point amplitude perturbation quotient
//   shimmer_apq11    : 11-point amplitude perturbation quotient
//
// Filtering rejects pairs whose periods are out of range, whose period
// ratio exceeds max_period_factor, or whose amplitude ratio exceeds
// max_amplitude_factor. Returns NaN if fewer than the minimum number of
// valid pairs or windows is available.

double shimmer_local   (std::span<const double> pulses,
                        std::span<const double> samples, double sample_rate,
                        AmplitudeFilter f = {});

double shimmer_local_db(std::span<const double> pulses,
                        std::span<const double> samples, double sample_rate,
                        AmplitudeFilter f = {});

double shimmer_apq3    (std::span<const double> pulses,
                        std::span<const double> samples, double sample_rate,
                        AmplitudeFilter f = {});

double shimmer_apq5    (std::span<const double> pulses,
                        std::span<const double> samples, double sample_rate,
                        AmplitudeFilter f = {});

double shimmer_apq11   (std::span<const double> pulses,
                        std::span<const double> samples, double sample_rate,
                        AmplitudeFilter f = {});


// ── HNR (Harmonics-to-Noise Ratio) ────────────────────────────────────
//
// HNR is derived from the per-frame normalised-autocorrelation peak r
// along the Viterbi path of the Praat-style pitch tracker (Boersma 1993):
//
//   HNR_dB(frame) = 10 · log10( r / (1 - r) )   for r in (0, 1)
//
// Frames whose chosen candidate is the unvoiced candidate are excluded
// (their HNR is NaN in the contour, and they do not contribute to the
// mean). The pitch-tracker parameters here mirror the defaults used
// elsewhere in Phonometrica.

struct HnrOptions
{
	double f0_min            = 75.0;
	double f0_max            = 600.0;
	double time_step         = 0.01;
	double voicing_threshold = 0.45;
	double silence_threshold = 0.03;
	double octave_cost       = 0.01;
	double octave_jump_cost  = 0.35;
	double voicing_cost      = 0.14;
	bool   use_gaussian      = false;
};

// Mean HNR across voiced frames, in dB. NaN if no voiced frames.
double hnr_mean_db(std::span<const double> samples, double sample_rate,
                   HnrOptions opts = {});

// Per-frame HNR contour. NaN entries mark unvoiced frames. Frame i
// corresponds to time i · opts.time_step + (window_centre offset
// inherited from the Praat tracker), but for plotting purposes the
// caller can treat frames as equispaced with step `opts.time_step`.
std::vector<double> hnr_contour(std::span<const double> samples, double sample_rate,
                                HnrOptions opts = {});

} // namespace phonometrica::speech

#endif // PHONOMETRICA_VOICE_QUALITY_HPP
