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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>
#include <phon/array.hpp>
#include <phon/error.hpp>
#include <phon/analysis/voice_quality.hpp>
#include <phon/analysis/signal_processing.hpp>
#include <REAPER/epoch_tracker/epoch_tracker.h>

namespace phonometrica::speech {

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// ── Period & amplitude validity predicates ────────────────────────────

inline bool period_in_range(double T, const PeriodFilter &f)
{
	return T >= f.period_floor && T <= f.period_ceiling;
}

inline bool ratio_ok(double a, double b, double max_factor)
{
	// Both a and b are assumed > 0.
	if (a <= 0.0 || b <= 0.0) return false;
	double lo = std::min(a, b);
	double hi = std::max(a, b);
	return hi <= max_factor * lo;
}

inline bool pair_valid(double T_i, double T_next, const PeriodFilter &f)
{
	return period_in_range(T_i, f) && period_in_range(T_next, f)
	    && ratio_ok(T_i, T_next, f.max_period_factor);
}

// A K-period window centred on index i is valid when every period in
// the window is in range and every adjacent pair satisfies the ratio
// constraint. K must be odd; for K=3 the window is (i-1, i, i+1).
bool window_valid(const std::vector<double> &T, intptr_t i, int K,
                  const PeriodFilter &f)
{
	int half = K / 2;
	if (i - half < 0 || i + half >= static_cast<intptr_t>(T.size())) return false;
	for (int k = -half; k <= half; ++k) {
		if (!period_in_range(T[i + k], f)) return false;
	}
	for (int k = -half; k < half; ++k) {
		if (!ratio_ok(T[i + k], T[i + k + 1], f.max_period_factor)) return false;
	}
	return true;
}

// Compute periods T_i = t_{i+1} - t_i from a pulse-time vector.
std::vector<double> compute_periods(std::span<const double> pulses)
{
	std::vector<double> T;
	if (pulses.size() < 2) return T;
	T.reserve(pulses.size() - 1);
	for (size_t i = 0; i + 1 < pulses.size(); ++i) {
		T.push_back(pulses[i + 1] - pulses[i]);
	}
	return T;
}


// ── Generic jitter aggregators ────────────────────────────────────────
//
// All jitter measures share the same denominator pattern: the mean of
// every period that participates in at least one accepted window. We
// track participation with a bool[] of the same length as T so each
// period contributes to the denominator at most once.

struct JitterAccumulator
{
	double num_sum = 0.0;
	intptr_t num_count = 0;
	std::vector<bool> participates;

	explicit JitterAccumulator(size_t n_periods) : participates(n_periods, false) {}

	void add_numerator(double dev) { num_sum += dev; ++num_count; }

	void mark_window(intptr_t i_center, int K)
	{
		int half = K / 2;
		for (int k = -half; k <= half; ++k) {
			participates[i_center + k] = true;
		}
	}

	double finalize_relative(const std::vector<double> &T) const
	{
		if (num_count == 0) return NaN;
		double mean_dev = num_sum / static_cast<double>(num_count);

		double period_sum = 0.0;
		intptr_t period_count = 0;
		for (size_t i = 0; i < T.size(); ++i) {
			if (participates[i]) {
				period_sum += T[i];
				++period_count;
			}
		}
		if (period_count == 0) return NaN;
		double mean_period = period_sum / static_cast<double>(period_count);
		if (mean_period <= 0.0) return NaN;
		return mean_dev / mean_period;
	}

	double finalize_absolute() const
	{
		if (num_count == 0) return NaN;
		return num_sum / static_cast<double>(num_count);
	}
};

} // anonymous namespace


// ── Glottal-pulse detection (REAPER) ──────────────────────────────────

std::vector<double> compute_glottal_pulses(
        std::span<const double> samples,
        double sample_rate,
        double f0_min,
        double f0_max,
        bool   do_highpass)
{
	if (samples.empty() || sample_rate <= 0.0) {
		throw error("compute_glottal_pulses: empty input or invalid sample rate");
	}
	if (f0_min <= 0.0 || f0_max <= f0_min) {
		throw error("compute_glottal_pulses: invalid F0 range");
	}

	// Peak-normalise to ~0.95 * INT16_MAX before casting to int16_t so REAPER's
	// internal LPC analysis has enough dynamic range. Mirrors the trick used by
	// the existing signal_processing.cpp REAPER path, with peak-based scaling
	// rather than a fixed 32768 multiplier (more robust on quiet recordings).
	double peak = 0.0;
	for (double s : samples) peak = std::max(peak, std::abs(s));
	if (peak <= 0.0) {
		// All-zero input — REAPER cannot track anything; return empty.
		return {};
	}
	const double scale = 0.95 * 32767.0 / peak;

	std::vector<int16_t> wav(samples.size());
	for (size_t i = 0; i < samples.size(); ++i) {
		double v = samples[i] * scale;
		if (v >  32767.0) v =  32767.0;
		if (v < -32768.0) v = -32768.0;
		wav[i] = static_cast<int16_t>(std::lround(v));
	}

	sptk::reaper::EpochTracker et;
	if (!et.Init(wav.data(),
	             static_cast<int32_t>(wav.size()),
	             static_cast<float>(sample_rate),
	             static_cast<float>(f0_min),
	             static_cast<float>(f0_max),
	             do_highpass,
	             /*do_hilbert_transform=*/false))
	{
		throw error("compute_glottal_pulses: REAPER EpochTracker Init failed");
	}
	if (!et.ComputeFeatures()) {
		throw error("compute_glottal_pulses: REAPER ComputeFeatures failed");
	}
	if (!et.TrackEpochs()) {
		throw error("compute_glottal_pulses: REAPER TrackEpochs failed");
	}

	// 5 ms unvoiced filler interval matches reaper::kUnvoicedPulseInterval used
	// by the SPTK wrapper. We discard those below by filtering on voicing.
	std::vector<float>   times;
	std::vector<int16_t> voicing;
	et.GetFilledEpochs(0.005f, &times, &voicing);

	std::vector<double> gcis;
	gcis.reserve(times.size());
	for (size_t i = 0; i < times.size(); ++i) {
		if (voicing[i]) gcis.push_back(static_cast<double>(times[i]));
	}
	return gcis;
}


// ── Jitter family ─────────────────────────────────────────────────────

double jitter_local(std::span<const double> pulses, PeriodFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 2) return NaN;

	JitterAccumulator acc(T.size());
	for (intptr_t i = 0; i + 1 < static_cast<intptr_t>(T.size()); ++i) {
		if (pair_valid(T[i], T[i + 1], f)) {
			acc.add_numerator(std::abs(T[i + 1] - T[i]));
			acc.participates[i] = true;
			acc.participates[i + 1] = true;
		}
	}
	return acc.finalize_relative(T);
}

double jitter_local_abs(std::span<const double> pulses, PeriodFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 2) return NaN;

	JitterAccumulator acc(T.size());
	for (intptr_t i = 0; i + 1 < static_cast<intptr_t>(T.size()); ++i) {
		if (pair_valid(T[i], T[i + 1], f)) {
			acc.add_numerator(std::abs(T[i + 1] - T[i]));
		}
	}
	return acc.finalize_absolute();
}

double jitter_rap(std::span<const double> pulses, PeriodFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 3) return NaN;

	JitterAccumulator acc(T.size());
	for (intptr_t i = 1; i + 1 < static_cast<intptr_t>(T.size()); ++i) {
		if (window_valid(T, i, 3, f)) {
			double mean3 = (T[i - 1] + T[i] + T[i + 1]) / 3.0;
			acc.add_numerator(std::abs(T[i] - mean3));
			acc.mark_window(i, 3);
		}
	}
	return acc.finalize_relative(T);
}

double jitter_ppq5(std::span<const double> pulses, PeriodFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 5) return NaN;

	JitterAccumulator acc(T.size());
	for (intptr_t i = 2; i + 2 < static_cast<intptr_t>(T.size()); ++i) {
		if (window_valid(T, i, 5, f)) {
			double mean5 = (T[i - 2] + T[i - 1] + T[i] + T[i + 1] + T[i + 2]) / 5.0;
			acc.add_numerator(std::abs(T[i] - mean5));
			acc.mark_window(i, 5);
		}
	}
	return acc.finalize_relative(T);
}

double jitter_ddp(std::span<const double> pulses, PeriodFilter f)
{
	// Closed-form identity: DDP = 3 · RAP. Computing both arms separately
	// would lose this exactness because of the window-validity overlap.
	double rap = jitter_rap(pulses, f);
	return std::isnan(rap) ? NaN : 3.0 * rap;
}


// ── Amplitude extraction for shimmer ──────────────────────────────────

namespace {

// Peak amplitude A_i around GCI t_i in a symmetric window of width equal to
// the local mean period (one-sided neighbours used at the boundaries).
// Returns 0 if the window collapses (degenerate pulse spacing).
double peak_amplitude_at_gci(std::span<const double> samples, double sample_rate,
                             const std::span<const double> &pulses, size_t i)
{
	double T_local;
	if (pulses.size() < 2) return 0.0;
	if (i == 0) {
		T_local = pulses[1] - pulses[0];
	} else if (i + 1 == pulses.size()) {
		T_local = pulses[i] - pulses[i - 1];
	} else {
		T_local = 0.5 * (pulses[i + 1] - pulses[i - 1]);
	}
	if (T_local <= 0.0) return 0.0;

	intptr_t centre = static_cast<intptr_t>(std::lround(pulses[i] * sample_rate));
	intptr_t half   = static_cast<intptr_t>(std::lround(0.5 * T_local * sample_rate));
	if (half < 1) half = 1;

	intptr_t lo = std::max<intptr_t>(0, centre - half);
	intptr_t hi = std::min<intptr_t>(static_cast<intptr_t>(samples.size()) - 1, centre + half);
	if (hi < lo) return 0.0;

	double a = 0.0;
	for (intptr_t k = lo; k <= hi; ++k) {
		double v = std::abs(samples[k]);
		if (v > a) a = v;
	}
	return a;
}

// Pre-compute the amplitude per GCI once; downstream shimmer measures
// reuse the same vector.
std::vector<double> compute_amplitudes(std::span<const double> samples, double sample_rate,
                                       std::span<const double> pulses)
{
	std::vector<double> A(pulses.size(), 0.0);
	for (size_t i = 0; i < pulses.size(); ++i) {
		A[i] = peak_amplitude_at_gci(samples, sample_rate, pulses, i);
	}
	return A;
}

inline bool amp_pair_valid(double T_i, double T_next, double A_i, double A_next,
                           const AmplitudeFilter &f)
{
	return pair_valid(T_i, T_next, f.period)
	    && ratio_ok(A_i, A_next, f.max_amplitude_factor);
}

bool amp_window_valid(const std::vector<double> &T, const std::vector<double> &A,
                      intptr_t i_amp, int K, const AmplitudeFilter &f)
{
	// Amplitude window of width K centred on amp index i_amp spans
	// A[i_amp - half .. i_amp + half], backed by K-1 periods
	// T[i_amp - half .. i_amp + half - 1].
	int half = K / 2;
	intptr_t a_lo = i_amp - half;
	intptr_t a_hi = i_amp + half;
	if (a_lo < 0 || a_hi >= static_cast<intptr_t>(A.size())) return false;

	intptr_t t_lo = a_lo;
	intptr_t t_hi = a_hi - 1;
	if (t_lo < 0 || t_hi >= static_cast<intptr_t>(T.size())) return false;

	// All periods in range.
	for (intptr_t k = t_lo; k <= t_hi; ++k) {
		if (!period_in_range(T[k], f.period)) return false;
	}
	// Adjacent period ratios.
	for (intptr_t k = t_lo; k < t_hi; ++k) {
		if (!ratio_ok(T[k], T[k + 1], f.period.max_period_factor)) return false;
	}
	// Adjacent amplitude ratios.
	for (intptr_t k = a_lo; k < a_hi; ++k) {
		if (!ratio_ok(A[k], A[k + 1], f.max_amplitude_factor)) return false;
	}
	return true;
}

struct ShimmerAccumulator
{
	double num_sum = 0.0;
	intptr_t num_count = 0;
	std::vector<bool> participates;

	explicit ShimmerAccumulator(size_t n_amps) : participates(n_amps, false) {}

	void add_numerator(double dev) { num_sum += dev; ++num_count; }

	void mark_window(intptr_t i_center, int K)
	{
		int half = K / 2;
		for (int k = -half; k <= half; ++k) {
			participates[i_center + k] = true;
		}
	}

	double finalize_relative(const std::vector<double> &A) const
	{
		if (num_count == 0) return NaN;
		double mean_dev = num_sum / static_cast<double>(num_count);

		double amp_sum = 0.0;
		intptr_t amp_count = 0;
		for (size_t i = 0; i < A.size(); ++i) {
			if (participates[i]) {
				amp_sum += A[i];
				++amp_count;
			}
		}
		if (amp_count == 0) return NaN;
		double mean_amp = amp_sum / static_cast<double>(amp_count);
		if (mean_amp <= 0.0) return NaN;
		return mean_dev / mean_amp;
	}

	double finalize_absolute() const
	{
		if (num_count == 0) return NaN;
		return num_sum / static_cast<double>(num_count);
	}
};

} // anonymous namespace


// ── Shimmer family ────────────────────────────────────────────────────

double shimmer_local(std::span<const double> pulses,
                     std::span<const double> samples, double sample_rate,
                     AmplitudeFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 2) return NaN;
	auto A = compute_amplitudes(samples, sample_rate, pulses);

	// Pair (A[i], A[i+1]) is backed by periods (T[i], T[i+1]) for i in
	// [0, T.size() - 2]. Both periods and both amplitudes must be valid.
	ShimmerAccumulator acc(A.size());
	for (intptr_t i = 0; i + 1 < static_cast<intptr_t>(T.size()); ++i) {
		if (A[i] <= 0.0 || A[i + 1] <= 0.0) continue;
		if (!amp_pair_valid(T[i], T[i + 1], A[i], A[i + 1], f)) continue;
		acc.add_numerator(std::abs(A[i + 1] - A[i]));
		acc.participates[i] = true;
		acc.participates[i + 1] = true;
	}
	return acc.finalize_relative(A);
}

double shimmer_local_db(std::span<const double> pulses,
                        std::span<const double> samples, double sample_rate,
                        AmplitudeFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 1) return NaN;
	auto A = compute_amplitudes(samples, sample_rate, pulses);

	ShimmerAccumulator acc(A.size());
	for (intptr_t i = 0; i + 1 < static_cast<intptr_t>(T.size()); ++i) {
		if (A[i] <= 0.0 || A[i + 1] <= 0.0) continue;
		if (!amp_pair_valid(T[i], T[i + 1], A[i], A[i + 1], f)) continue;
		acc.add_numerator(std::abs(20.0 * std::log10(A[i + 1] / A[i])));
	}
	return acc.finalize_absolute();
}

double shimmer_apq3(std::span<const double> pulses,
                    std::span<const double> samples, double sample_rate,
                    AmplitudeFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 2) return NaN;
	auto A = compute_amplitudes(samples, sample_rate, pulses);

	ShimmerAccumulator acc(A.size());
	for (intptr_t i = 1; i + 1 < static_cast<intptr_t>(A.size()); ++i) {
		if (A[i - 1] <= 0.0 || A[i] <= 0.0 || A[i + 1] <= 0.0) continue;
		if (!amp_window_valid(T, A, i, 3, f)) continue;
		double mean3 = (A[i - 1] + A[i] + A[i + 1]) / 3.0;
		acc.add_numerator(std::abs(A[i] - mean3));
		acc.mark_window(i, 3);
	}
	return acc.finalize_relative(A);
}

double shimmer_apq5(std::span<const double> pulses,
                    std::span<const double> samples, double sample_rate,
                    AmplitudeFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 4) return NaN;
	auto A = compute_amplitudes(samples, sample_rate, pulses);

	ShimmerAccumulator acc(A.size());
	for (intptr_t i = 2; i + 2 < static_cast<intptr_t>(A.size()); ++i) {
		bool all_pos = true;
		for (int k = -2; k <= 2; ++k) { if (A[i + k] <= 0.0) { all_pos = false; break; } }
		if (!all_pos) continue;
		if (!amp_window_valid(T, A, i, 5, f)) continue;
		double sum5 = A[i - 2] + A[i - 1] + A[i] + A[i + 1] + A[i + 2];
		acc.add_numerator(std::abs(A[i] - sum5 / 5.0));
		acc.mark_window(i, 5);
	}
	return acc.finalize_relative(A);
}

double shimmer_apq11(std::span<const double> pulses,
                     std::span<const double> samples, double sample_rate,
                     AmplitudeFilter f)
{
	auto T = compute_periods(pulses);
	if (T.size() < 10) return NaN;
	auto A = compute_amplitudes(samples, sample_rate, pulses);

	ShimmerAccumulator acc(A.size());
	for (intptr_t i = 5; i + 5 < static_cast<intptr_t>(A.size()); ++i) {
		bool all_pos = true;
		for (int k = -5; k <= 5; ++k) { if (A[i + k] <= 0.0) { all_pos = false; break; } }
		if (!all_pos) continue;
		if (!amp_window_valid(T, A, i, 11, f)) continue;
		double sum11 = 0.0;
		for (int k = -5; k <= 5; ++k) sum11 += A[i + k];
		acc.add_numerator(std::abs(A[i] - sum11 / 11.0));
		acc.mark_window(i, 11);
	}
	return acc.finalize_relative(A);
}


// ── HNR ───────────────────────────────────────────────────────────────

namespace {

// Convert the per-frame chosen-path strength r to HNR in dB.
// r = 0 marks unvoiced frames (or numerical floor); we map to NaN.
// r clamps to [eps, 1 - eps] to keep the logarithm finite.
inline double r_to_hnr_db(double r)
{
	if (!(r > 0.0)) return NaN;          // unvoiced or non-finite
	constexpr double eps = 1e-12;
	if (r >= 1.0 - eps) r = 1.0 - eps;   // saturated periodicity
	if (r <  eps)       r = eps;
	return 10.0 * std::log10(r / (1.0 - r));
}

// Wrap samples in an Array<double> for the existing `get_pitch_praat` API,
// which takes Array by const ref. Array owns its storage so a copy is
// unavoidable here without a deeper refactor of the pitch tracker.
inline Array<double> samples_to_array(std::span<const double> samples)
{
	return Array<double>(samples.data(), static_cast<intptr_t>(samples.size()));
}

} // anonymous namespace


double hnr_mean_db(std::span<const double> samples, double sample_rate, HnrOptions opts)
{
	if (samples.empty() || sample_rate <= 0.0) return NaN;

	Array<double> input = samples_to_array(samples);
	std::vector<double> strengths;
	(void) get_pitch_praat(input, sample_rate,
	                       opts.f0_min, opts.f0_max, opts.time_step,
	                       opts.voicing_threshold,
	                       opts.octave_jump_cost, opts.voicing_cost,
	                       opts.silence_threshold, opts.octave_cost,
	                       opts.use_gaussian, &strengths);

	double sum = 0.0;
	intptr_t n = 0;
	for (double r : strengths) {
		double db = r_to_hnr_db(r);
		if (std::isnan(db)) continue;
		sum += db;
		++n;
	}
	if (n == 0) return NaN;
	return sum / static_cast<double>(n);
}

std::vector<double> hnr_contour(std::span<const double> samples, double sample_rate, HnrOptions opts)
{
	std::vector<double> out;
	if (samples.empty() || sample_rate <= 0.0) return out;

	Array<double> input = samples_to_array(samples);
	std::vector<double> strengths;
	(void) get_pitch_praat(input, sample_rate,
	                       opts.f0_min, opts.f0_max, opts.time_step,
	                       opts.voicing_threshold,
	                       opts.octave_jump_cost, opts.voicing_cost,
	                       opts.silence_threshold, opts.octave_cost,
	                       opts.use_gaussian, &strengths);

	out.reserve(strengths.size());
	for (double r : strengths) out.push_back(r_to_hnr_db(r));
	return out;
}


// ── Aggregate voice report ────────────────────────────────────────────

namespace {

// Mean period from valid in-range periods. Excludes out-of-range
// periods but does not apply the pair-ratio constraint — this is the
// informational summary line ("Mean period / mean F0"), not a
// perturbation measure. Mirrors the helper previously inlined in the
// sound view; centralising it here lets the GUI and scripting paths
// share one definition.
double mean_in_range_period(std::span<const double> pulses, const PeriodFilter &f)
{
	if (pulses.size() < 2) return NaN;
	double sum = 0.0;
	intptr_t n = 0;
	for (size_t i = 0; i + 1 < pulses.size(); ++i)
	{
		double T = pulses[i + 1] - pulses[i];
		if (T >= f.period_floor && T <= f.period_ceiling)
		{
			sum += T;
			++n;
		}
	}
	if (n == 0) return NaN;
	return sum / static_cast<double>(n);
}

} // anonymous namespace


VoiceReport compute_voice_report(std::span<const double> samples,
                                 double sample_rate,
                                 double f0_min,
                                 double f0_max)
{
	VoiceReport r;

	// REAPER pulse detection. Failures (rare: ComputeFeatures/TrackEpochs
	// occasionally bail on extremely short or all-silent inputs) leave the
	// pulse list empty so jitter/shimmer report NaN, but they do not
	// short-circuit HNR — HNR has its own tracker and can still produce a
	// useful value when REAPER cannot lock.
	std::vector<double> pulses;
	try
	{
		pulses = compute_glottal_pulses(samples, sample_rate, f0_min, f0_max);
	}
	catch (...)
	{
		// Keep pulses empty; downstream measures will return NaN naturally.
	}

	r.num_pulses = static_cast<intptr_t>(pulses.size());

	// Period filter inherits f0_min/f0_max from the caller so the
	// in-range definition is consistent with pulse detection.
	PeriodFilter    pf;
	pf.period_floor   = 1.0 / f0_max;
	pf.period_ceiling = 1.0 / f0_min;
	AmplitudeFilter af;
	af.period = pf;

	std::span<const double> pulses_span(pulses.data(), pulses.size());

	r.mean_period = mean_in_range_period(pulses_span, pf);
	r.mean_f0     = std::isnan(r.mean_period) || r.mean_period <= 0.0
	                  ? NaN
	                  : 1.0 / r.mean_period;

	r.jitter_local     = jitter_local    (pulses_span, pf);
	r.jitter_local_abs = jitter_local_abs(pulses_span, pf);
	r.jitter_rap       = jitter_rap      (pulses_span, pf);
	r.jitter_ppq5      = jitter_ppq5     (pulses_span, pf);
	r.jitter_ddp       = jitter_ddp      (pulses_span, pf);

	r.shimmer_local    = shimmer_local   (pulses_span, samples, sample_rate, af);
	r.shimmer_local_db = shimmer_local_db(pulses_span, samples, sample_rate, af);
	r.shimmer_apq3     = shimmer_apq3    (pulses_span, samples, sample_rate, af);
	r.shimmer_apq5     = shimmer_apq5    (pulses_span, samples, sample_rate, af);
	r.shimmer_apq11    = shimmer_apq11   (pulses_span, samples, sample_rate, af);

	HnrOptions hopts;
	hopts.f0_min = f0_min;
	hopts.f0_max = f0_max;
	r.hnr = hnr_mean_db(samples, sample_rate, hopts);

	return r;
}

} // namespace phonometrica::speech
