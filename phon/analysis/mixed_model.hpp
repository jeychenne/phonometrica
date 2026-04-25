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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: mixed-effects model fitting via Laplace approximation.                                                     *
 *                                                                                                                     *
 * This is a unified engine: the same code path handles Gaussian, binomial, Poisson, and negative binomial families    *
 * with one or more random-effects terms (intercepts and/or slopes, crossed or nested). The only family-dependent      *
 * piece is the conditional log-likelihood and its derivatives, supplied by the Family struct.                         *
 * Estimation is by maximum likelihood (not REML).                                                                     *
 *                                                                                                                     *
 * Algorithm references (all published mathematical specifications, no GPL code):                                      *
 *   - Breslow & Clayton (1993). Approximate inference in generalized linear mixed models. JASA 88(421).               *
 *   - Kristensen et al. (2016). TMB: Automatic Differentiation and Laplace Approximation. JSS 70(5).                  *
 *   - Bates et al. (2015). Fitting Linear Mixed-Effects Models Using lme4. JSS 67(1).                                 *
 *   - Henderson (1984). Applications of Linear Models in Animal Breeding. University of Guelph Press.                 *
 *                                                                                                                     *
 * Note: The core architecture and integration logic were designed and authored by Julien Eychenne. Portions of the    *
 * statistical estimation logic in this file were developed with the assistance of Claude Opus 4.6 (Anthropic), based  *
 * on published statistical literature and reference R implementations.                                                *
 * All AI-assisted logic has been manually audited, refactored, and validated against a diverse suite of datasets and  *
 * reference R packages to ensure mathematical accuracy and implementation integrity.                                  *
 * While every effort has been made to ensure reliability, this software is provided without a guarantee of being      *
 * bug-free. In the event that discrepancies or errors are discovered, the author will do his best to address them.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MIXED_MODEL_HPP
#define PHONOMETRICA_MIXED_MODEL_HPP

#include <vector>
#include <functional>
#include <phon/analysis/model.hpp>
#include <phon/analysis/family.hpp>

namespace phonometrica::stats {

// Progress callback: receives (current_step, max_steps).
using FittingCallback = std::function<void(int, int)>;

// Information about a single random-effects term, built by the fitting layer.
//
// A random-effects term like (1 + vowel | speaker) where vowel has 3 levels
// produces one GroupingInfo with:
//   name      = "speaker"
//   nlevels   = number of unique speakers
//   nterms    = 3 (intercept + 2 treatment-coded dummies for vowel)
//   term_names = {"Intercept", "vowel[i]", "vowel[u]"}
//   Z_design  = n × 3 row-major matrix
//
// For (1 | speaker) (intercept only), nterms=1 and Z_design is all 1s.
//
// The u vector for this group has nlevels × nterms elements, organised as:
//   u[j * nterms + t]  = random coefficient for level j, term t.
//
struct GroupingInfo
{
	String name;                       // grouping variable name, e.g. "speaker"
	Array<String> levels;              // sorted unique levels
	std::vector<intptr_t> indices;     // n_obs: per-observation level index [0, nlevels)
	intptr_t nlevels = 0;

	// Random-effects design.
	// nterms = q = total number of random terms (intercept + expanded slope columns).
	// Z_design: n_obs × q row-major matrix.
	//   Z_design[i * q + t] = design value for observation i, term t.
	//   First column is 1.0 if the intercept is included.
	intptr_t nterms = 1;
	Array<String> term_names;
	std::vector<double> Z_design;
};


//─────────────────────────────────────────────────────────────────────
// Multi-start support for Student-t (and any family with non-convex
// inner Laplace problems). The Student-t inner û-problem is not
// log-concave for low ν, so a single PIRLS run from a deterministic
// start can land in a local — not global — minimum of the marginal
// NLL. mixed_model_multistart() runs the optimization from N
// different starting points and returns the deepest one.
//
// For Student-t, the perturbations vary the initial σ and ν, which
// are the most basin-determining parameters. Other families currently
// fall through to single-start (n_starts is silently clamped to 1).
//─────────────────────────────────────────────────────────────────────

struct FitOptions
{
	int n_starts = 0;            // 0 = use family default; 1 = no multi-start;
	                              // ≥ 2 = run that many starts and keep best.
	uint64_t seed = 42;          // Reserved for future β-jitter; current
	                              // perturbations are deterministic per-k.
	bool report_starts = false;  // If true, attach multi-start summary to
	                              // model.fit_warning even on clean unimodal
	                              // convergence. Diagnostic / debugging.

	// Diagnostic: after picking the best-of-N, run an additional polishing
	// pass from that point with tighter FD step (h_scale 10× smaller) and
	// stricter gradient tolerance. If the polish improves loglik
	// noticeably, the original convergence was FD-noise-limited and AD
	// gradients would help. Adds one more outer optimization to the cost.
	// The improvement is reported in fit_warning.
	bool polish = false;

	// Diagnostic / experimental: for Student-t, run Phase 2 (joint
	// β + θ + σ + ν optimization) instead of skipping it. Phase 2 is
	// skipped by default for Student-t because the σ-ν correlation
	// can make the joint Hessian ill-conditioned, but on some
	// datasets it converges and gives a tighter fit.
	bool phase2_student = false;
};

// Internal helper: per-start initialization overrides supplied by the
// multi-start wrapper to the inner single-start fitter. Each override
// is opt-in via a has_* flag; unset fields fall through to the
// function's normal deterministic initialization.
struct InitOverrides
{
	bool has_sigma_init = false;  double sigma_init = 0;   // Student scale start
	bool has_nu_init    = false;  double nu_init    = 0;   // Student df start

	// Polish mode: if set, the inner Student-t L-BFGS uses a tighter FD
	// step and gradient tolerance, intended to be combined with a warm
	// start near the converged optimum. Used by FitOptions.polish.
	bool tight_tolerance = false;

	// Experimental: enable Phase 2 (joint β/θ/σ/ν optimization) for
	// Student-t models. Skipped by default due to σ-ν correlation
	// concerns; this flag overrides the skip. Used by
	// FitOptions.phase2_student.
	bool phase2_student = false;
};

//! Fit a mixed-effects model via Laplace approximation.
//!
//! Supports random intercepts and random slopes with a Cholesky-parameterised
//! covariance structure for each grouping factor.
//!
//! Model:   y_i | u ~ Family(mu_i)
//!          eta_i = x_i' beta + sum_g z_{g,i}' u_{g, k_g(i)}
//!          u_{g,j} ~ N(0, D_g)    for each grouping factor g, level j
//!
//! where D_g = L_g L_g' is the q_g × q_g covariance matrix for random effects
//! within group g, parameterised via its Cholesky factor.
//!
//! \param y         response vector (n observations)
//! \param X         fixed-effects design matrix (n × p, first column is intercept)
//! \param groups    vector of grouping factor information (one per random-effects term)
//! \param fam       GLM family (gaussian, binomial, poisson, negbin)
//! \return a fitted Model with random_effects populated (one RandomEffectGroup per grouping factor)
Model mixed_model(const Array<double> &y, const Array<double> &X,
                  const std::vector<GroupingInfo> &groups, const Family &fam,
                  FittingCallback progress = nullptr,
                  const PriorSpec *priors = nullptr,
                  const Array<String> *coef_names = nullptr,
                  int max_iter = 200,
                  const Array<double> &offset = Array<double>(),
                  const InitOverrides *init_overrides = nullptr);


// Multi-start orchestration entry point. For n_starts ≤ 1 (or for
// families without a multi-start default), this is equivalent to a
// single mixed_model() call.
Model mixed_model_multistart(const Array<double> &y, const Array<double> &X,
                              const std::vector<GroupingInfo> &groups, const Family &fam,
                              FittingCallback progress = nullptr,
                              const PriorSpec *priors = nullptr,
                              const Array<String> *coef_names = nullptr,
                              int max_iter = 200,
                              const Array<double> &offset = Array<double>(),
                              const FitOptions &opts = FitOptions{});


//─────────────────────────────────────────────────────────────────────
// Diagnostic evaluation harness: compute the four Laplace-NLL
// components at a user-supplied (β, θ, û) WITHOUT running outer
// optimization. Used for cross-engine validation against reference
// packages (lme4, glmmTMB) — feed in their converged parameters and
// read back which component (cond_nll, prior_nll, log_det_Huu,
// const_term) accounts for any logLik discrepancy.
//
// This iteration: mixed models only. GAMs and any model loaded from
// a saved file (where Z_design / indices were not preserved) are
// rejected with EvaluationResult.ok = false. Gaussian + refit_u is
// also rejected for now (the σ²-aware Henderson u-only solve is not
// yet wired in here; non-Gaussian PIRLS handles σ² internally and
// works fine).
//─────────────────────────────────────────────────────────────────────

// Override values: any field set to non-empty / has_*=true overrides
// the corresponding fitted value; otherwise the model's stored value
// is used. For overriding β alone, leave Sigma / dispersion / u all
// null.
struct EvaluationOverrides
{
	const Array<double> *beta = nullptr;             // length nfixed
	// One full q_g × q_g matrix per RE group, in fitted group order.
	// Σ is symmetric — layout-agnostic. nullptr or empty entry means
	// fall back to the fitted Σ_g (reconstructed from cov_chol).
	const std::vector<Array<double>> *Sigma = nullptr;
	bool has_sigma = false;     double sigma_val = 0;     // Gaussian/Student scale
	bool has_nu = false;        double nu_val = 0;        // Student df
	bool has_theta_nb = false;  double theta_nb_val = 0;  // NB overdispersion
	bool has_phi = false;       double phi_val = 0;       // Beta precision
	const Array<double> *u = nullptr;                // length J_total, group-major
	                                                  //   then j*q + t within group
	bool refit_u = false;                            // re-run PIRLS from u = 0
};

// Components of the Laplace approximation to the marginal NLL.
//   laplace_nll = cond_nll + prior_nll + log_det_Huu + const_term
struct EvaluationResult
{
	bool ok = false;             // false if model state is incomplete or invalid
	String error;                // populated when ok = false
	double laplace_nll = 0;
	double cond_nll = 0;         // -log p(y | β, û)
	double prior_nll = 0;        // -log p(û | θ)
	double log_det_Huu = 0;      // ½ log|H_uu|
	double const_term = 0;       // -½ J_total log(2π)
	Array<double> u_used;        // the û actually used (refit or supplied)
	bool u_refit = false;        // true if PIRLS was re-run from u = 0

	// Student-t only: which Hessian formula was used in the Laplace
	// correction. "exact" if the exact Student-t Hessian gave a PD
	// H_uu at this point; "fisher_info" if the engine fell back to
	// the IRLS / Fisher-information form. Empty for non-Student
	// families.
	String laplace_method;
};

// Compute Laplace NLL components at a user-supplied parameter point.
EvaluationResult evaluate_at(const Model &model, const EvaluationOverrides &ov);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_MIXED_MODEL_HPP
