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
 * Created: 03/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <phon/analysis/formant_selection.hpp>
#include <phon/analysis/weenink.hpp>
#include <phon/analysis/speech_utils.hpp>
#include <phon/application/sound.hpp>

#define MIN_POINTS 8
#define MAX_SELECT_POINTS 16

namespace phonometrica::speech {

static bool diag_enabled()
{
	static const bool on = [] {
		const char *v = std::getenv("PHON_DIAG_FORMANT_SELECT");
		return v && v[0] == '1';
	}();
	return on;
}

// Median of the finite (non-NaN) values in a container. Returns NaN if there are none.
static double finite_median(std::vector<double> &v)
{
	v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return std::isnan(x); }), v.end());
	if (v.empty()) return std::nan("");
	auto mid = v.begin() + v.size() / 2;
	std::nth_element(v.begin(), mid, v.end());
	double m = *mid;
	if (v.size() % 2 == 0) {
		double lo = *std::max_element(v.begin(), mid);
		m = 0.5 * (lo + m);
	}
	return m;
}

static double finite_mean(const Matrix<double> &F, int k)
{
	double sum = 0.0;
	int n = 0;
	for (int i = 0; i < F.rows(); ++i) {
		if (!std::isnan(F(i, k))) { sum += F(i, k); ++n; }
	}
	return n > 0 ? sum / n : std::nan("");
}

// Normalized time of row i on [-1, 1], matching model_segment's internal linspace(-1, 1, nrow).
static double norm_time(int i, int nrow)
{
	return nrow > 1 ? (-1.0 + 2.0 * double(i) / double(nrow - 1)) : 0.0;
}

CandidateScore
score_candidate_intrinsic(const Matrix<double> &F, const Matrix<double> &B, double ceiling, int lpc_order,
                          const IntrinsicWeights &w)
{
	CandidateScore s;
	const int nrow = (int) F.rows();
	const int nf   = (int) F.cols();
	if (nrow < MIN_POINTS || nf < 1) return s; // invalid

	// Fit each formant track with Legendre polynomials (reuses Weenink's machinery). This gives us f_hat via predict().
	auto model = model_segment(F, B, (unsigned int) w.n_legendre);
	if (!model.success) return s; // invalid: too few observations / degenerate fit

	// ── R : de-biased relative roughness ────────────────────────────────────────────────────────────────────────
	// RMS of the relative residual (f - f_hat)/f_hat about the fit, pooled over formants. Relative (not absolute Hz)
	// so the three formants are on a comparable scale and measurement error ~ proportional to frequency (Hillenbrand
	// et al. 1995). Phase-1 uses uniform weights; the prominence weight (Module P-lite) plugs in here later.
	{
		double num = 0.0, den = 0.0;
		for (int k = 0; k < nf; ++k) {
			for (int i = 0; i < nrow; ++i) {
				if (std::isnan(F(i, k))) continue;
				double fhat  = model.predict(norm_time(i, nrow), (unsigned int) k);
				double denom = (std::max)(fhat, w.freq_floor);
				double rel   = (F(i, k) - fhat) / denom;
				const double weight = 1.0; // P-lite prominence hook
				num += weight * rel * rel;
				den += weight;
			}
		}
		double r_rms = den > 0.0 ? std::sqrt(num / den) : std::nan("");
		if (std::isnan(r_rms)) return s; // invalid: nothing to fit
		s.R = r_rms / w.bad_R;
	}

	// ── U : within-segment instability ──────────────────────────────────────────────────────────────────────────
	// Median relative frame-to-frame jump over adjacent *defined* pairs, across formants. Complements R: a smooth-but-
	// wrong track has low R and low U; a slot-jump (F2 landing in the F3 slot on some frames) spikes U while leaving
	// the segment mean plausible.
	{
		std::vector<double> jumps;
		jumps.reserve((size_t) nrow * nf);
		for (int k = 0; k < nf; ++k) {
			double fbar = finite_mean(F, k);
			if (std::isnan(fbar) || fbar <= 0.0) continue;
			for (int i = 0; i + 1 < nrow; ++i) {
				if (std::isnan(F(i, k)) || std::isnan(F(i + 1, k))) continue;
				jumps.push_back(std::abs(F(i + 1, k) - F(i, k)) / fbar);
			}
		}
		double u = finite_median(jumps);
		s.U = std::isnan(u) ? 0.0 : u / w.bad_U;
	}

	// ── Cov : coverage ──────────────────────────────────────────────────────────────────────────────────────────
	// Fraction of undefined frames per formant, penalized above a floor. A formant found on only a few frames can look
	// smooth and stable on those frames while being meaningless; U (defined frames only) is blind to this.
	{
		double cov = 0.0;
		for (int k = 0; k < nf; ++k) {
			int undef = 0;
			for (int i = 0; i < nrow; ++i) if (std::isnan(F(i, k))) ++undef;
			double rho = double(undef) / double(nrow);
			cov += (std::max)(0.0, rho - w.coverage_floor);
		}
		s.Cov = cov / w.bad_Cov;
	}

	// ── D : pole-density prior ──────────────────────────────────────────────────────────────────────────────────
	// The analysis can find ~ lpc_order/2 formants below the ceiling; we expect about ceiling/spacing. Penalizing the
	// squared mismatch kills the mismatched grid corners (many poles in a narrow band, or few in a wide one) that drove
	// the LFDI failures. The optional ceiling anchor (design eta) is deferred (density_anchor defaults to 0).
	{
		double formants_below = 0.5 * double(lpc_order);
		double expected       = ceiling / w.nominal_spacing;
		double d = formants_below - expected;
		s.D = d * d;

		if (w.density_anchor > 0.0) {
			// Gentle anchor against diagonal drift to a too-high ceiling: expect the top modeled formant near
			// (nf - 0.5) * spacing; penalize (perceptual) departure of its median from that expectation.
			double f_top = finite_mean(F, nf - 1);
			if (!std::isnan(f_top)) {
				auto erb = [](double f) { return 21.4 * std::log10(0.00437 * (std::max)(f, 0.0) + 1.0); };
				double expected_top = (double(nf) - 0.5) * w.nominal_spacing;
				double a = erb(f_top) - erb(expected_top);
				s.D += w.density_anchor * a * a;
			}
		}
	}

	// ── Bd : bandwidth plausibility ─────────────────────────────────────────────────────────────────────────────
	// Squared log-ratio of each formant's median bandwidth to an expected-bandwidth curve, averaged over formants.
	// Penalizes implausibly narrow *and* wide bandwidths. This is the only home for bandwidth in the objective.
	{
		double bd = 0.0;
		int counted = 0;
		for (int k = 0; k < nf; ++k) {
			std::vector<double> bw;
			bw.reserve(nrow);
			for (int i = 0; i < nrow; ++i) bw.push_back(B(i, k));
			double med_bw = finite_median(bw);
			double med_f  = finite_mean(F, k);
			if (std::isnan(med_bw) || std::isnan(med_f) || med_bw <= 0.0) continue;
			double bhat  = (std::max)(w.bw_min, w.bw_slope * med_f + w.bw_intercept);
			double ratio = med_bw / bhat;
			double l = std::log(ratio);
			bd += l * l;
			++counted;
		}
		s.Bd = counted > 0 ? bd / counted : 0.0;
	}

	// ── O : ordering ────────────────────────────────────────────────────────────────────────────────────────────
	// Strict monotonicity of the median centres. No minimum-gap floor: high back vowels legitimately have a small
	// F1-F2 gap, and a gap floor would punish exactly the correctly resolved cases. Rarely fires (Praat sorts poles).
	{
		double prev = -1.0;
		bool ok = true;
		for (int k = 0; k < nf; ++k) {
			double f = finite_mean(F, k);
			if (std::isnan(f)) continue;
			if (f <= prev) { ok = false; break; }
			prev = f;
		}
		s.O = ok ? 0.0 : w.ordering_penalty;
	}

	// ââ Cln : physical cleanliness (peak resolvedness) ââââââââââââââââââââââââââââââââââââââââ
	// Consensus-independent quality: are adjacent formants resolved as separate spectral peaks, or merged? Two poles
	// merge into one peak when their spacing is small RELATIVE TO their bandwidths, so we score bandwidth-relative
	// spacing. This leaves legitimately close but narrow formants (e.g. F1-F2 of high back vowels) unpenalized while
	// flagging the wide-banded F2-F3 merges behind the iy/er cases. Computed from F/B alone (no LPC envelope needed:
	// pole radius r = exp(-pi*B/Fs), so bandwidth already encodes peakiness). Median over frames.
	{
		std::vector<double> per_frame;
		per_frame.reserve(nrow);
		for (int i = 0; i < nrow; ++i) {
			double pen = 0.0; int pairs = 0;
			for (int k = 0; k + 1 < nf; ++k) {
				if (std::isnan(F(i, k)) || std::isnan(F(i, k + 1))) continue;
				double gap   = F(i, k + 1) - F(i, k);
				double bwavg = 0.5 * (std::abs(B(i, k)) + std::abs(B(i, k + 1)));
				if (bwavg <= 0.0) continue;
				double r = gap / bwavg;                                     // resolvedness; < ~1 => merged
				double d = (std::max)(0.0, w.cln_threshold - r) / w.cln_threshold;
				pen += d * d; ++pairs;
			}
			if (pairs > 0) per_frame.push_back(pen);
		}
		double c = finite_median(per_frame);
		s.Cln = std::isnan(c) ? 0.0 : c / w.bad_Cln;
	}

	s.badness = w.lambda_R * s.R + w.lambda_U * s.U + w.lambda_Cov * s.Cov
	          + w.lambda_D * s.D + w.lambda_Bd * s.Bd + w.lambda_Cln * s.Cln + s.O;
	s.valid = std::isfinite(s.badness);
	return s;
}

std::vector<AnalysisCandidate>
build_intrinsic_candidates(Sound *sound, int channel, int nformant, double win_size, double t1, double t2,
                           double t_measure, double max_freq1, double max_freq2, double step,
                           int lpc_order1, int lpc_order2, const IntrinsicWeights &w)
{
	std::vector<AnalysisCandidate> out;

	int npoint = int(1000 * (t2 - t1) / 5);
	if (npoint < MIN_POINTS) npoint = MIN_POINTS;
	if (npoint > MAX_SELECT_POINTS) npoint = MAX_SELECT_POINTS;
	auto time_points = linspace(t1, t2, npoint, false);

	Matrix<double> F(npoint, nformant);
	Matrix<double> B(npoint, nformant);
	const int kmax = nformant < 3 ? nformant : 3;

	for (double nyquist = max_freq1; nyquist <= max_freq2; nyquist += step)
	{
		for (int order = lpc_order1; order <= lpc_order2; ++order)
		{
			F.setZero(npoint, nformant);
			B.setZero(npoint, nformant);
			intptr_t i = 0;
			bool edge = false;
			for (auto t : time_points)
			{
				Array<double> fr;
				try { fr = sound->get_formants(channel, t, nformant, nyquist, win_size, order); }
				catch (...) { edge = true; break; } // window ran off the file edge for this point
				for (intptr_t j = 0; j < nformant; ++j) { F(i, j) = fr(j, 0); B(i, j) = fr(j, 1); }
				++i;
			}
			if (edge) continue;

			auto sc = score_candidate_intrinsic(F, B, nyquist, order, w);
			if (!sc.valid) continue;

			AnalysisCandidate c;
			c.ceiling = nyquist;
			c.lpc_order = order;
			c.score = sc;
			try {
				auto fm = sound->get_formants(channel, t_measure, nformant, nyquist, win_size, order);
				for (int k = 0; k < kmax; ++k) c.formants[k] = fm(k, 0);
			} catch (...) { /* leave NaN; consensus simply skips this formant for this candidate */ }
			out.push_back(c);
		}
	}
	return out;
}

std::pair<double, double>
select_analysis_intrinsic(Sound *sound, int channel, int nformant, double win_size, double t1, double t2,
                          double max_freq1, double max_freq2, double step, int lpc_order1, int lpc_order2,
                          const IntrinsicWeights &w)
{
	// Sample about one measurement every 5 ms, at least MIN_POINTS, capped at MAX_SELECT_POINTS: a handful of points
	// is enough to fit a 4-parameter Legendre and judge smoothness, so long vowels don't pay for dozens of redundant
	// LPC fits per candidate. The final measurement is taken separately at the true midpoint, so this affects
	// selection speed only, not the reported formants.
	int npoint = int(1000 * (t2 - t1) / 5);
	if (npoint < MIN_POINTS) npoint = MIN_POINTS;
	if (npoint > MAX_SELECT_POINTS) npoint = MAX_SELECT_POINTS;
	auto time_points = linspace(t1, t2, npoint, false);

	double best_badness = (std::numeric_limits<double>::max)();
	std::pair<double, double> best_parameters; // {0, 0} => failure (matches find_lpc_parameters)

	Matrix<double> F(npoint, nformant);
	Matrix<double> B(npoint, nformant);

	for (double nyquist = max_freq1; nyquist <= max_freq2; nyquist += step)
	{
		for (int order = lpc_order1; order <= lpc_order2; ++order)
		{
			F.setZero(npoint, nformant);
			B.setZero(npoint, nformant);
			intptr_t i = 0;

			for (auto t : time_points)
			{
				auto formants = sound->get_formants(channel, t, nformant, nyquist, win_size, order);
				for (intptr_t j = 0; j < nformant; ++j)
				{
					F(i, j) = formants(j, 0);
					B(i, j) = formants(j, 1);
				}
				++i;
			}

			auto sc = score_candidate_intrinsic(F, B, nyquist, order, w);
			if (!sc.valid) continue;

			if (diag_enabled())
			{
				std::fprintf(stderr,
				             "[formant-select] ceiling=%.0f order=%d  B=%.4f  R=%.3f U=%.3f Cov=%.3f D=%.3f Bd=%.3f O=%.0f\n",
				             nyquist, order, sc.badness, sc.R, sc.U, sc.Cov, sc.D, sc.Bd, sc.O);
			}

			if (sc.badness < best_badness)
			{
				best_badness = sc.badness;
				best_parameters = { nyquist, (double) order };
			}
		}
	}

	if (diag_enabled() && best_parameters.first > 0.0)
	{
		std::fprintf(stderr, "[formant-select] -> chosen ceiling=%.0f order=%d (B=%.4f)\n",
		             best_parameters.first, (int) best_parameters.second, best_badness);
	}

	return best_parameters;
}

} // namespace phonometrica::speech
