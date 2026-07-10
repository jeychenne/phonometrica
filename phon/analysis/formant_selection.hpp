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
 * Purpose: Phase-1 "intrinsic ladder" for automatic LPC parameter selection in formant queries. This is a drop-in    *
 * replacement for speech::find_lpc_parameters() (Weenink's smoothness-only W criterion). Instead of picking the       *
 * smoothest analysis, it minimizes a weighted sum of single-token penalties, each with an interior optimum or a       *
 * plausibility floor, so selection is not driven to the grid corners (the documented failure mode of W on natural     *
 * data; see Eychenne & Courdès-Murphy, LFDI 2024).                                                                    *
 *                                                                                                                     *
 * Every term looks only at the token being measured, so this pass is non-circular by construction: no cell centres,   *
 * no cross-token consensus, no EM. Those belong to Phase 2. The Legendre fit itself is reused from weenink.hpp        *
 * (model_segment / WeeninkModel::predict).                                                                            *
 *                                                                                                                     *
 * Terms (all scaled so a "meaningfully bad" value is ~1, hence default weights of 1):                                 *
 *   R    de-biased relative roughness   RMS of (f - f_hat)/f_hat about the Legendre fit                               *
 *   U    within-segment instability     median relative frame-to-frame jump (catches slot-jumping)                    *
 *   Cov  coverage                        penalizes formants left undefined on too many frames                          *
 *   D    pole-density prior              (formants_below_C - C/spacing)^2                                              *
 *   Bd   bandwidth plausibility          squared log-ratio of median bandwidth to an expected-bandwidth curve          *
 *   O    ordering                        hard constant when the fitted centres are not strictly increasing            *
 *                                                                                                                     *
 * Set the environment variable PHON_DIAG_FORMANT_SELECT=1 to dump per-candidate scores to stderr. Permanent, zero     *
 * cost when off.                                                                                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FORMANT_SELECTION_HPP
#define PHONOMETRICA_FORMANT_SELECTION_HPP

#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <phon/utils/matrix.hpp>

namespace phonometrica {
class Sound;
}

namespace phonometrica::speech {

// Tunable weights and term parameters for the intrinsic ladder. Defaults are chosen so each scaled term is ~1 at a
// "meaningfully bad" level; leave the lambdas at 1 unless an ablation on gold data shows a term is mis-weighted.
struct IntrinsicWeights final
{
	// Term weights (relative trust; unitless once each term is scaled internally).
	double lambda_R   = 1.0;   // roughness
	double lambda_U   = 1.0;   // instability
	double lambda_Cov = 1.0;   // coverage
	double lambda_D   = 1.0;   // pole density
	double lambda_Bd  = 1.0;   // bandwidth plausibility
	double lambda_Cln = 0.0;   // physical cleanliness (peak resolvedness); OFF by default until validated on gold

	// Legendre model order (number of polynomials), passed to model_segment.
	int n_legendre = 4;

	// Pole-density prior. The analysis offers ~ lpc_order/2 formants below the ceiling C; we expect about
	// C / nominal_spacing of them, where nominal_spacing ~ c / 2L is the average formant spacing of the speaker's
	// vocal tract (~1000 Hz for a ~17.5 cm adult-male tract). Phase 1 uses a single nominal value; Phase 2 will
	// refine it per speaker via L_s. The optional "ceiling anchor" of the design (eta) is deferred until it can be
	// calibrated on gold data, so eta defaults to 0 here.
	double nominal_spacing = 1000.0; // Hz
	double density_anchor  = 0.0;    // eta; 0 = anchor disabled in Phase 1

	// Coverage: fraction of undefined frames per formant that is tolerated before the penalty starts.
	double coverage_floor = 0.15;

	// Relative-residual denominator floor, so a pathological near-zero f_hat cannot blow up R.
	double freq_floor = 200.0; // Hz

	// Expected-bandwidth curve B_hat(f) = max(bw_min, bw_slope * f + bw_intercept), used by the Bd term only.
	double bw_slope     = 0.05;
	double bw_intercept = 40.0;
	double bw_min       = 30.0;

	// Hard penalty added when the fitted formant centres are not strictly increasing.
	double ordering_penalty = 1000.0;

	// Cln (peak resolvedness): two adjacent formants count as merged when their spacing is below cln_threshold times
	// their mean bandwidth. Bandwidth-relative, so legitimately close but narrow formants are not penalized.
	double cln_threshold = 1.0;

	// Internal "bad-level" scales (a term equal to its bad level contributes ~1 before weighting).
	double bad_R   = 0.05;  // 5 % RMS relative roughness
	double bad_U   = 0.05;  // 5 % median relative jump
	double bad_Cov = 0.35;  // ~half a formant's frames undefined, above the floor
	double bad_Cln = 1.0;   // one fully-merged adjacent pair
};

// Per-candidate breakdown. Components are already scaled (comparable across terms); `badness` is their weighted sum.
struct CandidateScore final
{
	double badness = (std::numeric_limits<double>::max)();
	double R   = 0.0;
	double U   = 0.0;
	double Cov = 0.0;
	double D   = 0.0;
	double Bd  = 0.0;
	double Cln = 0.0;
	double O   = 0.0;
	bool   valid = false;
};

// Score one already-sampled candidate. F and B are ndata x nformant matrices of per-frame formant frequencies and
// bandwidths (undefined values are NaN), exactly as built by the sampling loop in the search below. `ceiling` (Hz) and
// `lpc_order` identify the candidate and feed the density term.
CandidateScore
score_candidate_intrinsic(const Matrix<double> &F, const Matrix<double> &B, double ceiling, int lpc_order,
                          const IntrinsicWeights &w);

// One scored analysis candidate together with the formants it yields at the measurement point. This is the per-token
// cache consumed by the consensus / EM pass in formant queries (Phase 2b): the pass re-weights score.badness with a
// pull toward each token's (speaker x vowel) cell centre and re-picks the argmin, all without recomputing LPC.
struct AnalysisCandidate final
{
	double ceiling   = 0.0;
	int    lpc_order = 0;
	CandidateScore score;
	double formants[3] = { std::nan(""), std::nan(""), std::nan("") }; // F1..F3 at the measurement point
};

// Build and score every <ceiling, order> candidate over the grid, recording each candidate's F1..F3 at t_measure.
// The single-token winner is still argmin over score.badness (as in select_analysis_intrinsic); this just exposes the
// whole scored set so a corpus-level pass can re-select with cross-token information.
std::vector<AnalysisCandidate>
build_intrinsic_candidates(Sound *sound, int channel, int nformant, double win_size, double t1, double t2,
                           double t_measure, double max_freq1, double max_freq2, double step,
                           int lpc_order1, int lpc_order2, const IntrinsicWeights &w = IntrinsicWeights());

// Phase-1 replacement for find_lpc_parameters(). Sweeps the same <ceiling, lpc_order> grid, samples formant tracks at
// ~1 point / 5 ms over [t1, t2], and returns the <ceiling, lpc_order> pair minimizing the intrinsic badness. Returns
// {0, 0} if no candidate could be scored (caller treats a zero ceiling as failure and leaves measurements undefined),
// mirroring find_lpc_parameters()'s failure convention.
std::pair<double, double>
select_analysis_intrinsic(Sound *sound, int channel, int nformant, double win_size, double t1, double t2,
                          double max_freq1, double max_freq2, double step, int lpc_order1, int lpc_order2,
                          const IntrinsicWeights &w = IntrinsicWeights());

} // namespace phonometrica::speech

#endif // PHONOMETRICA_FORMANT_SELECTION_HPP
