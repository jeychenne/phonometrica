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
 *   * Glottal-pulse detection via Praat-style Sound_Pitch_to_PointProcess_cc.                                          *
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
#include <cstdint>
#include <limits>
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


// ── Glottal-pulse detection (Praat-style) ─────────────────────────────
//
// Returns the times (seconds, relative to start of `samples`) of glottal
// pulses within voiced regions. The implementation tracks the F0 contour
// with the Praat-style autocorrelation tracker (Boersma 1993), then walks
// each voiced run forward at intervals of the local period (1/F0), snapping
// each predicted pulse time to the nearest local |signal| extremum within
// ±period/4 — Praat's Sound_Pitch_to_PointProcess_cc algorithm.
//
//   f0_min, f0_max : bound the periodicity search range.
//
// Throws on invalid input or invalid F0 range.
std::vector<double> compute_glottal_pulses(
        std::span<const double> samples,
        double sample_rate,
        double f0_min      = 75.0,
        double f0_max      = 600.0);


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


// ── Aggregate voice report ────────────────────────────────────────────
//
// One-shot computation of the full voice-quality battery on a single
// channel of speech. The Praat-style pitch tracker is run once and its
// output (F0 contour + per-frame strengths) is shared across pulse
// derivation, jitter, shimmer, mean F0, voicing fraction, and HNR.
// Every measure inherits the same single voicing decision.
//
// All double fields are NaN when the underlying measure is undefined
// (typically not enough valid pulses or no voiced frames). num_pulses
// is always populated (it is the count of derived pulses from the
// Praat-style contour); the GUI and scripting bindings rely on a zero
// count to surface a single "no pulses found" message rather than ten
// NaNs.
//
// Field order is kept close to the layout of the GUI report so a
// scripting-side dump matches what the user sees in the output panel.

struct VoiceReport
{
	// Glottal pulses (Praat-derived from the F0 contour via
	// Sound_Pitch_to_PointProcess_cc). num_pulses counts pulses within
	// voiced runs; mean_period is the mean of consecutive in-range
	// periods (rejects periods that span an unvoiced gap, so partial
	// voicing still yields a meaningful value provided the contour
	// produces at least one pair of consecutive voiced frames within
	// the configured F0 range).
	intptr_t num_pulses     = 0;
	double   mean_period    = std::numeric_limits<double>::quiet_NaN();  // seconds

	// Fraction of voiced frames in the Praat-style pitch contour (= 1 - Praat's
	// "fraction of locally unvoiced frames"). In [0, 1]; NaN only when the pitch
	// tracker cannot produce any frames (e.g. the input is shorter than one
	// analysis window). 0 means the entire interval is voiceless; 1 means
	// fully voiced.
	double   voiced_frame_fraction = std::numeric_limits<double>::quiet_NaN();

	// Mean F0, computed as the arithmetic mean of voiced-frame F0 in the
	// pitch contour (Praat's "Mean pitch"). NaN only when there are no voiced
	// frames at all. Will differ slightly from 1/mean_period in general
	// (arithmetic vs harmonic mean over different sampling grids); both
	// values are reported so the user can choose which one to use.
	double   mean_f0        = std::numeric_limits<double>::quiet_NaN();  // Hz

	// Jitter family (all relative, except jitter_local_abs which is in seconds)
	double   jitter_local     = std::numeric_limits<double>::quiet_NaN();
	double   jitter_local_abs = std::numeric_limits<double>::quiet_NaN();
	double   jitter_rap       = std::numeric_limits<double>::quiet_NaN();
	double   jitter_ppq5      = std::numeric_limits<double>::quiet_NaN();
	double   jitter_ddp       = std::numeric_limits<double>::quiet_NaN();

	// Shimmer family (all relative, except shimmer_local_db which is in dB)
	double   shimmer_local    = std::numeric_limits<double>::quiet_NaN();
	double   shimmer_local_db = std::numeric_limits<double>::quiet_NaN();
	double   shimmer_apq3     = std::numeric_limits<double>::quiet_NaN();
	double   shimmer_apq5     = std::numeric_limits<double>::quiet_NaN();
	double   shimmer_apq11    = std::numeric_limits<double>::quiet_NaN();

	// Harmonics-to-noise ratio (mean over voiced frames, dB)
	double   hnr              = std::numeric_limits<double>::quiet_NaN();
};

// Compute the full voice report on `samples` (one channel). A single
// Praat-style pitch tracking pass drives everything: pulses are derived
// from the F0 contour, and jitter/shimmer/mean F0/voicing fraction/HNR
// all consume the same shared output. The single pass means there is no
// inconsistency between which intervals each measure considers voiced.
//
// `interval_start_s` and `interval_end_s` (in seconds, measured from the
// start of `samples`) restrict the reported measures to a sub-window of
// the input. The pitch tracker still runs on the full `samples` so it has
// adequate context for its voicing decisions — typically the caller has
// padded the buffer around a user-selected interval to escape the
// short-input edge-effect failure mode of autocorrelation tracking. Only
// pulses and frames whose time falls inside [interval_start_s,
// interval_end_s] are tallied; jitter/shimmer/mean-period are computed
// on the filtered pulse list, and voicing-fraction/mean-F0/HNR over the
// filtered frame range. The default (interval_end_s < 0) skips cropping
// entirely — backward-compatible behaviour for callers that already pass
// the exact analysis window they want reported (the waveform overlay, or
// script bindings).
VoiceReport compute_voice_report(std::span<const double> samples,
                                 double sample_rate,
                                 double f0_min = 75.0,
                                 double f0_max = 600.0,
                                 double interval_start_s = 0.0,
                                 double interval_end_s   = -1.0);

} // namespace phonometrica::speech

#endif // PHONOMETRICA_VOICE_QUALITY_HPP
