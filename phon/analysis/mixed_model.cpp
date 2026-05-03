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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 * The outer optimization over variance parameters dispatches between Newton's method (for trivial                     *
 * random-intercept-only models with outer dimension ≤ 3) and L-BFGS (for anything else, including                     *
 * random-slope models and non-Gaussian Phase 2).  Newton uses a finite-difference Hessian with                        *
 * Levenberg-Marquardt eigenvalue damping when indefinite, and Armijo backtracking line search.                        *
 * For Gaussian LMMs, β is profiled out and the objective is evaluated by a single Henderson solve.                    *
 * For non-Gaussian GLMMs, Phase 1 concentrates β out via PIRLS; Phase 2 then re-optimizes (β, θ)                      *
 * jointly with u profiled out, matching the strategy used by lme4 / glmmTMB.  The random-effects                      *
 * covariance D_g is parameterized via its lower Cholesky factor (log-Cholesky diagonal,                               *
 * unconstrained off-diagonal).                                                                                        *
 *                                                                                                                     *
 * Mathematical references:                                                                                            *
 *   Breslow & Clayton (1993). Approximate inference in generalized linear mixed models.                               *
 *       JASA 88(421), 9–25.                                                                                           *
 *   Bates, Mächler, Bolker & Walker (2015). Fitting linear mixed-effects models using lme4.                           *
 *       JSS 67(1), 1–48.                                                                                              *
 *   Henderson (1984). Applications of Linear Models in Animal Breeding. University of Guelph Press.                   *
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

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <optional>
#include <random>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/special_functions/trigamma.hpp>
#include <phon/third_party/LBFGSpp/LBFGS.h>
#include <phon/analysis/mixed_model.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/analysis/waic.hpp>
#include <phon/analysis/psis.hpp>
#include <phon/utils/matrix.hpp>
#include <phon/third_party/Eigen/SparseCore>
#include <phon/third_party/Eigen/SparseCholesky>

namespace phonometrica::stats {

namespace {

// Helper: add offset vector to linear predictor η (no-op if off_ptr is null).
static inline void add_offset(Eigen::VectorXd &eta, const Eigen::VectorXd *off_ptr) {
	if (off_ptr) eta += *off_ptr;
}

// =====================================================================
// Multi-group random effects layout (unchanged)
// =====================================================================

struct GroupLayout
{
	intptr_t G;
	intptr_t J_total;       // total random-effect coefficients = Σ J[g] × q[g]
	std::vector<intptr_t> J;       // nlevels per group
	std::vector<intptr_t> q;       // nterms per group (1 = intercept only)
	std::vector<intptr_t> offset;  // u-vector offset: offset[g] = Σ_{g'<g} J[g'] × q[g']
	std::vector<const std::vector<intptr_t> *> group_indices;
	std::vector<const double *> Z_data;  // pointer to each group's Z_design data
	intptr_t n_obs = 0;

	static GroupLayout build(const std::vector<GroupingInfo> &groups)
	{
		GroupLayout lay;
		lay.G = (intptr_t)groups.size();
		lay.J_total = 0;
		lay.J.resize(lay.G);
		lay.q.resize(lay.G);
		lay.offset.resize(lay.G);
		lay.group_indices.resize(lay.G);
		lay.Z_data.resize(lay.G);
		lay.n_obs = groups.empty() ? 0 : (intptr_t)groups[0].indices.size();

		for (intptr_t g = 0; g < lay.G; g++)
		{
			lay.offset[g] = lay.J_total;
			lay.J[g] = groups[g].nlevels;
			lay.q[g] = groups[g].nterms;
			lay.J_total += lay.J[g] * lay.q[g];
			lay.group_indices[g] = &groups[g].indices;
			lay.Z_data[g] = groups[g].Z_design.data();
		}
		return lay;
	}

	// Z design value for observation i, group g, term t.
	double Z(intptr_t g, intptr_t i, intptr_t t) const
	{
		return Z_data[g][i * q[g] + t];
	}

	// u-vector index for group g, level j, term t.
	intptr_t u_idx(intptr_t g, intptr_t j, intptr_t t) const
	{
		return offset[g] + j * q[g] + t;
	}
};


// Result of an inner solve (β̂, û, μ̂) plus the Laplace-approximated NLL.
// Returned by solve_gaussian_henderson, solve_pirls, and solve_u_given_beta.
struct ProfiledResult
{
	Eigen::VectorXd beta;
	Eigen::VectorXd u;
	Eigen::VectorXd mu;
	double laplace_nll;
};


// =====================================================================
// Cholesky parameterization helpers
// =====================================================================
//
// The prior covariance D_g for group g (q × q SPD) is parameterized via
// its lower Cholesky factor: D_g = L_g L_g'.
//
// Packed parameter vector θ_g of length q(q+1)/2:
//   θ[r(r+1)/2 + c] = L_{r,c}  for r > c  (off-diagonal: unconstrained)
//   θ[r(r+1)/2 + r] = log L_{r,r}         (diagonal: log-scale for positivity)
//
// For q=1 this is θ = (log σ_u), consistent with the scalar parameterization.

// Number of Cholesky parameters for a q × q covariance.
static intptr_t n_chol_params(intptr_t q)
{
	return q * (q + 1) / 2;
}

// Total Cholesky parameters across all groups.
static intptr_t total_chol_params(const GroupLayout &lay)
{
	intptr_t total = 0;
	for (intptr_t g = 0; g < lay.G; g++) {
		total += n_chol_params(lay.q[g]);
	}
	return total;
}

// Offset into the packed outer θ vector for group g's Cholesky parameters.
static intptr_t chol_offset(const GroupLayout &lay, intptr_t g)
{
	intptr_t off = 0;
	for (intptr_t g2 = 0; g2 < g; g2++) {
		off += n_chol_params(lay.q[g2]);
	}
	return off;
}

// =====================================================================
// (τ, ω) parameterization of the random-effects covariance D
// =====================================================================
//
// The outer optimizer parameterizes D via separate scale (σ) and
// correlation (R) factors:
//
//     D = diag(σ) · R · diag(σ),   R = L_R · L_R'
//
// The unconstrained parameter slice for group g of size q has layout
//
//     theta_g = [ τ_0, τ_1, ..., τ_{q-1},                // log scales
//                 ω_0, ω_1, ..., ω_{q(q-1)/2 - 1} ]      // stickbreaking
//
// where τ_t = log σ_t and ω parameterizes L_R via Stan's stickbreaking
// transform: traverse the lower-triangle of L_R row by row in (i, j)
// order, set z_{i,j} = tanh(ω_k), then
//
//     L_R[i, j] = z_{i,j} · sqrt(1 − Σ_{k<j} L_R[i, k]²)        (j < i)
//     L_R[i, i] = sqrt(1 − Σ_{k<i} L_R[i, k]²)
//     L_R[0, 0] = 1
//
// For q == 1 the layout is just [τ_0] (no ω), and the (τ, ω) form
// reduces exactly to the previous log-Cholesky form (since L_R = [[1]]
// and L = σ_0).
//
// `unpack_cholesky` returns L = diag(σ) · L_R, the lower Cholesky factor
// of D.  All downstream consumers see the same shape of L as before, so
// the parameterization change is localised to this section and to
// `variance_prior_log_density` / theta initialisation sites.

// Unpack a (τ, ω) parameter slice into the lower-triangular Cholesky
// factor L of D = diag(σ) · L_R · L_R' · diag(σ).
// theta_g: packed vector of length q(q+1)/2.
// Returns q × q lower-triangular L with positive diagonal.
static Eigen::MatrixXd unpack_cholesky(const double *theta_g, intptr_t q)
{
	// Build σ from log scales.
	Eigen::VectorXd sigma(q);
	for (intptr_t t = 0; t < q; t++) {
		sigma[t] = std::exp(theta_g[t]);
	}

	// Build L_R via stickbreaking; L_R[0,0] = 1, all other entries
	// derived from the q(q-1)/2 ω parameters.
	Eigen::MatrixXd L_R = Eigen::MatrixXd::Zero(q, q);
	if (q > 0) L_R(0, 0) = 1.0;
	intptr_t omega_idx = q;
	for (intptr_t i = 1; i < q; i++) {
		double remaining = 1.0;
		for (intptr_t j = 0; j < i; j++) {
			double z = std::tanh(theta_g[omega_idx++]);
			L_R(i, j) = z * std::sqrt(std::max(remaining, 0.0));
			remaining -= L_R(i, j) * L_R(i, j);
			if (remaining < 0.0) remaining = 0.0; // numerical safety
		}
		L_R(i, i) = std::sqrt(std::max(remaining, 0.0));
	}

	// L = diag(σ) · L_R.
	Eigen::MatrixXd L = Eigen::MatrixXd::Zero(q, q);
	for (intptr_t i = 0; i < q; i++) {
		for (intptr_t j = 0; j <= i; j++) {
			L(i, j) = sigma[i] * L_R(i, j);
		}
	}
	return L;
}

// Pack a physical Cholesky factor L (= chol(D), positive diagonal) into
// the (τ, ω) parameter layout. Used by warm-start paths that take a
// previously-fitted covariance and seed the optimizer with it.
//
// Output: writes q(q+1)/2 entries into theta_g.
static void pack_chol_to_theta(const Eigen::MatrixXd &L,
                                double *theta_g,
                                intptr_t q)
{
	// σ_t = L_t,t · sqrt(1 + ...)  — actually σ_t = sqrt(D_tt) = sqrt(Σ_k L²_tk).
	// L_R[t, k] = L[t, k] / σ_t.
	for (intptr_t t = 0; t < q; t++) {
		double row_norm_sq = 0.0;
		for (intptr_t k = 0; k <= t; k++) row_norm_sq += L(t, k) * L(t, k);
		double sigma_t = std::sqrt(std::max(row_norm_sq, 1e-20));
		theta_g[t] = std::log(sigma_t);
	}

	intptr_t omega_idx = q;
	for (intptr_t i = 1; i < q; i++) {
		// Recompute σ_i for this row, then walk the off-diagonals.
		double row_norm_sq = 0.0;
		for (intptr_t k = 0; k <= i; k++) row_norm_sq += L(i, k) * L(i, k);
		double sigma_i = std::sqrt(std::max(row_norm_sq, 1e-20));

		double remaining = 1.0;  // = 1 − Σ_{k<j} L_R[i, k]²
		for (intptr_t j = 0; j < i; j++) {
			double L_R_ij = L(i, j) / sigma_i;
			double v_j = std::sqrt(std::max(remaining, 1e-20));
			double z = L_R_ij / v_j;
			z = std::clamp(z, -1.0 + 1e-12, 1.0 - 1e-12);
			theta_g[omega_idx++] = std::atanh(z);
			remaining -= L_R_ij * L_R_ij;
			if (remaining < 0.0) remaining = 0.0;
		}
	}
}

// Compute D = L L' from the Cholesky factor.
static Eigen::MatrixXd cholesky_to_cov(const Eigen::MatrixXd &L)
{
	return L * L.transpose();
}

// Compute D⁻¹ from the Cholesky factor: D⁻¹ = L⁻ᵀ L⁻¹.
static Eigen::MatrixXd cholesky_to_precision(const Eigen::MatrixXd &L)
{
	Eigen::MatrixXd Linv = L.triangularView<Eigen::Lower>().solve(
		Eigen::MatrixXd::Identity(L.rows(), L.cols()));
	return Linv.transpose() * Linv;
}

// log|D| = 2 Σ log L_kk where L is the Cholesky factor returned by
// `unpack_cholesky`. Under the (τ, ω) parameterization:
//
//     L_kk = σ_k · L_R[k, k] = exp(τ_k) · sqrt(1 − Σ_{j<k} L_R[k, j]²)
//
// so log L_kk = τ_k + (1/2) Σ_{j<k} log(1 − z_{k,j}²) and
// log|D| = 2 Σ τ_k + Σ_k Σ_{j<k} log(1 − z_{k,j}²).
static double log_det_D(const double *theta_g, intptr_t q)
{
	double ld = 0.0;
	for (intptr_t t = 0; t < q; t++) ld += theta_g[t];   // 2 Σ τ_t

	intptr_t omega_idx = q;
	for (intptr_t i = 1; i < q; i++) {
		for (intptr_t j = 0; j < i; j++) {
			double z = std::tanh(theta_g[omega_idx++]);
			ld += 0.5 * std::log(std::max(1.0 - z * z, 1e-20));
		}
	}
	return 2.0 * ld;
}


// =====================================================================
// Method-of-moments variance component estimation (ANOVA decomposition)
// =====================================================================
//
// Given a vector of residuals (or working residuals on the link scale) and
// a grouping factor, estimates the between-group variance σ²_u via the
// one-way ANOVA identity:
//
//     E[MSB] = σ²_within + n₀ σ²_between
//     E[MSW] = σ²_within
//
// where n₀ is the "effective group size" for unbalanced designs:
//     n₀ = (n − Σ n_j² / n) / (J − 1)
//
// Returns max((MSB − MSW) / n₀, 0).  The caller applies a floor.

static double anova_variance_component(const Eigen::VectorXd &resid,
                                        const std::vector<intptr_t> &indices,
                                        intptr_t nlevels, intptr_t n)
{
	std::vector<double> gsum(nlevels, 0.0);
	std::vector<intptr_t> gcnt(nlevels, 0);

	for (intptr_t i = 0; i < n; i++)
	{
		gsum[indices[i]] += resid[i];
		gcnt[indices[i]]++;
	}

	// SSB = Σ_j n_j * mean_j²  (grand mean of OLS residuals ≈ 0)
	double ssb = 0;
	for (intptr_t j = 0; j < nlevels; j++)
	{
		if (gcnt[j] > 0) {
			double mean_j = gsum[j] / gcnt[j];
			ssb += gcnt[j] * mean_j * mean_j;
		}
	}

	// SSW = Σ_i (r_i − mean_{j(i)})²
	double ssw = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double mean_j = (gcnt[indices[i]] > 0) ? gsum[indices[i]] / gcnt[indices[i]] : 0.0;
		double d = resid[i] - mean_j;
		ssw += d * d;
	}

	double msb = ssb / std::max(nlevels - 1, (intptr_t)1);
	double msw = ssw / std::max(n - nlevels, (intptr_t)1);

	// Effective group size for unbalanced designs
	double sum_nj2 = 0;
	for (intptr_t j = 0; j < nlevels; j++) {
		sum_nj2 += (double)gcnt[j] * (double)gcnt[j];
	}
	double n0 = (nlevels > 1)
	            ? ((double)n - sum_nj2 / (double)n) / (double)(nlevels - 1)
	            : 1.0;

	return std::max((msb - msw) / std::max(n0, 1.0), 0.0);
}


// =====================================================================
// Prior contribution helpers (Bayesian posterior mode optimization)
// =====================================================================
//
// When a PriorSpec is supplied, the Laplace engine minimizes the negative
// log-posterior instead of the negative log-likelihood:
//
//   NLP(θ) = laplace_nll(θ, β̂(θ))  -  log p(β̂(θ))
//            - Σ_g Σ_t [log p_var(σ_{g,t}) + log σ_{g,t}]
//            - [log p_res(σ) + log σ]   (Gaussian only)
//
// The prior on β is incorporated into the Henderson system (shifting β̂
// toward the prior mean). The Jacobian terms (+ log σ) arise from the
// change of variables log σ → σ in the outer parameterization.

// Add Normal prior precision to the Henderson system's X'WX block.
// This shifts β̂ toward the prior mean, giving the MAP estimate.
#if defined(PHON_INLA_BAYES_DIAG)
// One-shot flag: set to true at the fit call site before each Bayesian
// fit; cleared by the first subsequent call to add_fixed_prior_to_henderson
// (or fixed_prior_nll), which dumps the full prior/coef lookup state.
// thread_local so multiple concurrent fits wouldn't interleave dumps;
// the rest of this TU is effectively single-threaded, but the declaration
// costs nothing extra. "dump_" prefix avoids clashing with any user name.
namespace {
thread_local bool diag_dump_prior_lookup_once = false;
}

// Emit the prior/coef-lookup trace. Called from both add_fixed_prior_to_henderson
// and fixed_prior_nll; whichever fires first consumes the one-shot flag.
// `context` is a short label ("add_fixed_prior_to_henderson" etc.).
static void diag_dump_fixed_prior_state(
	const PriorSpec &priors,
	const Array<String> &coef_names,
	intptr_t p,
	const char *context)
{
	std::fprintf(stderr,
		"\n--- PHON_INLA_BAYES_DIAG: fixed-effect prior lookup (%s) ---\n",
		context);

	std::fprintf(stderr, "  priors.fixed_effects:  mean=%.4f  sd=%.4f",
	             priors.fixed_effects.mean, priors.fixed_effects.sd);
	if (!(priors.fixed_effects.sd > 0) || !std::isfinite(priors.fixed_effects.sd))
		std::fprintf(stderr, "   [INVALID SD]");
	std::fprintf(stderr, "\n  priors.fixed_auto = %s\n",
	             priors.fixed_auto ? "true" : "false");

	std::fprintf(stderr, "  priors.coefficient_priors (%zu entries):\n",
	             priors.coefficient_priors.size());
	for (const auto &[name, np] : priors.coefficient_priors)
	{
		std::fprintf(stderr, "    \"%.*s\"  mean=%.4f  sd=%.4f%s\n",
		             (int) name.size(), name.data(),
		             np.mean, np.sd,
		             (!(np.sd > 0) || !std::isfinite(np.sd)) ? "   [INVALID SD]" : "");
	}

	std::fprintf(stderr, "  coef_names (p=%ld, 1-indexed):\n", (long) p);
	for (intptr_t j = 0; j < p; j++)
	{
		const auto &coef = coef_names[j + 1];
		std::fprintf(stderr, "    [%ld] \"%.*s\"  (size=%ld)\n",
		             (long) j, (int) coef.size(), coef.data(),
		             (long) coef.size());
	}

	std::fprintf(stderr, "  per-coef lookups and precision contributions:\n");
	bool any_invalid = false;
	for (intptr_t j = 0; j < p; j++)
	{
		const auto &coef = coef_names[j + 1];
		auto it = priors.coefficient_priors.find(coef);
		bool hit = (it != priors.coefficient_priors.end());
		const NormalPrior &pr = hit ? it->second : priors.fixed_effects;
		double lambda = 1.0 / (pr.sd * pr.sd);
		bool lambda_bad = !std::isfinite(lambda);
		std::fprintf(stderr,
			"    j=%ld  coef=\"%.*s\"  map=%s  mean=%.4f  sd=%.4f  1/sd²=%.4e%s\n",
			(long) j,
			(int) coef.size(), coef.data(),
			hit ? "HIT " : "miss",
			pr.mean, pr.sd, lambda,
			lambda_bad ? "   [NON-FINITE]" : "");
		if (lambda_bad) any_invalid = true;
	}

	if (any_invalid)
		std::fprintf(stderr,
			"  >>> At least one precision contribution is non-finite.\n"
			"      This will poison the Henderson solve and propagate NaN\n"
			"      through laplace_nll, explaining the universal NaN seen\n"
			"      downstream at inla_grid_integrate_gaussian.\n");
	else
		std::fprintf(stderr,
			"  (all precision contributions finite — NaN origin is elsewhere)\n");

	std::fprintf(stderr, "--- end prior-lookup dump ---\n\n");
}
#endif  // PHON_INLA_BAYES_DIAG

static void add_fixed_prior_to_henderson(Eigen::MatrixXd &H, Eigen::VectorXd &rhs,
                                          const PriorSpec &priors,
                                          const Array<String> &coef_names,
                                          intptr_t p)
{
#if defined(PHON_INLA_BAYES_DIAG)
	if (diag_dump_prior_lookup_once)
	{
		diag_dump_prior_lookup_once = false;
		diag_dump_fixed_prior_state(priors, coef_names, p,
		                             "add_fixed_prior_to_henderson");
	}
#endif
	for (intptr_t j = 0; j < p; j++)
	{
		const auto &pr = priors.prior_for(coef_names[j + 1]);
		double lambda = 1.0 / (pr.sd * pr.sd);
		H(j, j) += lambda;
		rhs[j] += lambda * pr.mean;
	}
}

// Compute -log p(β) for the fixed-effect Normal priors.
static double fixed_prior_nll(const Eigen::VectorXd &beta,
                               const PriorSpec &priors,
                               const Array<String> &coef_names,
                               intptr_t p)
{
#if defined(PHON_INLA_BAYES_DIAG)
	if (diag_dump_prior_lookup_once)
	{
		diag_dump_prior_lookup_once = false;
		diag_dump_fixed_prior_state(priors, coef_names, p, "fixed_prior_nll");
	}
#endif
	double nll = 0;
	for (intptr_t j = 0; j < p; j++)
	{
		const auto &pr = priors.prior_for(coef_names[j + 1]);
		double z = (beta[j] - pr.mean) / pr.sd;
		nll += 0.5 * std::log(2.0 * M_PI) + std::log(pr.sd) + 0.5 * z * z;
	}
	return nll;
}

// Compute the variance-component prior contribution (log-density + Jacobian).
// Returns a value to be SUBTRACTED from the negative log-posterior.
// D_cov[g] is the q_g × q_g covariance matrix for group g.
//
// Under the (τ, ω) parameterization (see header at unpack_cholesky), this
// function computes the change-of-variables-corrected prior in φ = (τ, ω)
// coordinates:
//
//   log p(φ)  =  Σ_t log π(σ_t)                     // PC / Student-t / etc.
//             + Σ_t log σ_t                          // τ → σ Jacobian
//             + Σ_i (q-i-1+2(η-1)) · log L_R[i,i]    // LKJ-Cholesky density
//             + Σ_{i,j<i} [log(1−z²_{i,j})
//                          + (1/2) log(1 − Σ_{k<j} L_R[i,k]²)]   // ω → L_R Jac
//
// where σ_t = sqrt(D_tt), L_R = diag(σ)⁻¹ · chol(D), and z_{i,j} are the
// stickbreaking parameters recoverable from L_R. Since this function only
// receives D_cov (not theta), we recompute σ and L_R via Cholesky locally;
// q is small (typically ≤ 4) so the cost is negligible.
//
// For q == 1: the L_R / stickbreaking pieces are empty and this collapses
// to log π(σ_0) + log σ_0 — identical to the previous q=1 form.
//
// For q ≥ 2 with η == 1: the LKJ-Cholesky term is zero only at i=0
// (where L_R[0,0]=1) and at i=q-1 (where the exponent is zero); inner
// rows (1 ≤ i ≤ q-2) contribute (q-i-1)·log L_R[i,i].
//
// This formula matches the prior brms/Stan apply when configured with
// independent SD priors and lkj_corr_cholesky(η). Verified against
// finite-difference Jacobians for the stickbreaking transform up to q=5.
static double variance_prior_log_density(const std::vector<Eigen::MatrixXd> &D_cov,
                                          const PriorSpec &priors,
                                          const GroupLayout &lay)
{
	double lp = 0.0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];

		// ── Recover σ and L_R from D_cov[g] ──────────────────────────
		Eigen::VectorXd sigma(qg);
		for (intptr_t t = 0; t < qg; t++) {
			sigma[t] = std::sqrt(std::max(D_cov[g](t, t), 1e-20));
		}

		Eigen::MatrixXd L_R = Eigen::MatrixXd::Zero(qg, qg);
		bool L_R_ok = true;
		if (qg >= 1) L_R(0, 0) = 1.0;
		if (qg >= 2) {
			Eigen::LLT<Eigen::MatrixXd> llt(D_cov[g]);
			if (llt.info() == Eigen::Success) {
				Eigen::MatrixXd L = llt.matrixL();
				for (intptr_t i = 0; i < qg; i++) {
					for (intptr_t j = 0; j <= i; j++) {
						L_R(i, j) = L(i, j) / sigma[i];
					}
				}
			} else {
				// D_cov is singular — set L_R = identity as a safe fallback.
				// Means we treat the correlation structure as zero at this
				// boundary point, which costs us the LKJ contribution but
				// keeps the optimizer well-behaved.
				L_R.setIdentity(qg, qg);
				L_R_ok = false;
			}
		}

		// ── Marginal SD prior + τ → σ Jacobian (per dim) ────────────
		for (intptr_t t = 0; t < qg; t++) {
			double s = sigma[t];
			lp += priors.variance_components.log_density(s) + std::log(s);
		}

		if (qg < 2) continue;

		// ── LKJ-Cholesky density on L_R ──────────────────────────────
		// Stan's formula (1-indexed k=1..q): exponent = q − k + 2η − 2.
		// In 0-indexed i = k−1, that's q − i − 1 + 2(η − 1).
		// L_R[0,0] = 1 always so the i=0 contribution vanishes.
		// For η = 1 and i = q-1 the exponent is also 0.
		for (intptr_t i = 1; i < qg; i++) {
			double exp_i = double(qg - i - 1) + 2.0 * (priors.lkj_eta - 1.0);
			if (std::abs(exp_i) > 1e-15) {
				lp += exp_i * std::log(std::max(L_R(i, i), 1e-20));
			}
		}

		// ── Stickbreaking ω → L_R Jacobian ──────────────────────────
		if (!L_R_ok) continue;  // skip Jacobian at the singular boundary
		for (intptr_t i = 1; i < qg; i++) {
			double sum_sq = 0.0;          // Σ_{k<j} L_R[i, k]²
			for (intptr_t j = 0; j < i; j++) {
				double v_j_sq = std::max(1.0 - sum_sq, 1e-20);
				double v_j    = std::sqrt(v_j_sq);
				double z      = L_R(i, j) / v_j;
				z = std::clamp(z, -1.0 + 1e-12, 1.0 - 1e-12);
				lp += std::log(1.0 - z * z) + 0.5 * std::log(v_j_sq);
				sum_sq += L_R(i, j) * L_R(i, j);
			}
		}
	}
	return lp;
}

// Compute the residual SD prior contribution (Gaussian only).
// Returns a value to be SUBTRACTED from the negative log-posterior.
static double residual_prior_log_density(double sigma, const PriorSpec &priors)
{
	return priors.residual.log_density(sigma) + std::log(sigma);
}


// =====================================================================
// Full log-determinant of the random-effects Hessian
// =====================================================================
//
// H_uu = Z'WZ + block-diag(D_1⁻¹, ..., D_G⁻¹)
//
// For a single grouping factor (G=1), H is block-diagonal with nlevels
// blocks of size q×q. We exploit this for an O(n q² + J q³) algorithm.
//
// For multiple grouping factors (G>1, crossed effects), the full
// J_total × J_total matrix is built and factorized.

// Emit triplets for the random-effects block H_uu = Z'WZ + D⁻¹ into an
// existing triplet list, with a configurable row/column offset. Used both
// standalone (offset=0, by build_full_H_sparse) and as part of the joint
// (β,u) Henderson system (offset=p, by build_joint_henderson_sparse).
//
// All entries (full symmetric matrix) are emitted; SimplicialLDLT<…, Lower>
// only references the lower triangle, but setFromTriplets handles the
// duplicate-summation cleanly either way.
static void emit_random_block_triplets(
    std::vector<Eigen::Triplet<double>> &triplets,
    intptr_t offset,
    const Eigen::VectorXd &w,
    const std::vector<Eigen::MatrixXd> &D_inv,
    const GroupLayout &lay,
    intptr_t n)
{
	// Prior precision: D_g⁻¹ block at each level
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = offset + lay.offset[g] + j * qg;
			for (intptr_t t1 = 0; t1 < qg; t1++)
				for (intptr_t t2 = 0; t2 < qg; t2++)
					triplets.emplace_back(base + t1, base + t2, D_inv[g](t1, t2));
		}
	}

	// Data contributions (Z'WZ within and cross-group)
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < lay.G; g1++)
		{
			intptr_t j1 = (*lay.group_indices[g1])[i];
			intptr_t base1 = offset + lay.offset[g1] + j1 * lay.q[g1];

			// Within-group: w[i] * z_g1 ⊗ z_g1   (full symmetric block)
			for (intptr_t t1 = 0; t1 < lay.q[g1]; t1++)
			{
				double wz1 = w[i] * lay.Z(g1, i, t1);
				for (intptr_t t2 = 0; t2 < lay.q[g1]; t2++)
				{
					double val = wz1 * lay.Z(g1, i, t2);
					triplets.emplace_back(base1 + t1, base1 + t2, val);
				}
			}

			// Cross-group: w[i] * z_g1 ⊗ z_g2   (and the transpose)
			for (intptr_t g2 = g1 + 1; g2 < lay.G; g2++)
			{
				intptr_t j2 = (*lay.group_indices[g2])[i];
				intptr_t base2 = offset + lay.offset[g2] + j2 * lay.q[g2];

				for (intptr_t t1 = 0; t1 < lay.q[g1]; t1++)
				{
					double wz1 = w[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = 0; t2 < lay.q[g2]; t2++)
					{
						double val = wz1 * lay.Z(g2, i, t2);
						triplets.emplace_back(base1 + t1, base2 + t2, val);
						triplets.emplace_back(base2 + t2, base1 + t1, val);
					}
				}
			}
		}
	}
}


// Build the random-effects-only Henderson Hessian H = Z'WZ + D⁻¹ as a
// sparse matrix (J × J). Used by full_log_det_H and solve_u_given_beta.
//
// On the schwa benchmark (J=1256, n=7787, ~1% nnz) the dense build+LDLT was
// ≈250 ms per call; this sparse path is ~6 ms.
static Eigen::SparseMatrix<double> build_full_H_sparse(
    const Eigen::VectorXd &w,
    const std::vector<Eigen::MatrixXd> &D_inv,
    const GroupLayout &lay,
    intptr_t n)
{
	intptr_t J = lay.J_total;

	std::vector<Eigen::Triplet<double>> triplets;

	// Capacity estimate: prior block-diagonal entries + per-obs cross-terms
	intptr_t prior_cap = 0;
	intptr_t qsum = 0;
	for (intptr_t g = 0; g < lay.G; g++) {
		prior_cap += lay.J[g] * lay.q[g] * lay.q[g];
		qsum += lay.q[g];
	}
	triplets.reserve(prior_cap + (size_t)n * (size_t)(qsum * qsum));

	emit_random_block_triplets(triplets, 0, w, D_inv, lay, n);

	Eigen::SparseMatrix<double> H(J, J);
	H.setFromTriplets(triplets.begin(), triplets.end());  // sums duplicates
	H.makeCompressed();
	return H;
}


// Build the joint Henderson Hessian
//   H = [ X'WX        X'WZ           ]   (top-left p × p block dense)
//       [ Z'WX    Z'WZ + D⁻¹         ]   (bottom-right J × J block sparse)
// as a (p+J) × (p+J) sparse matrix.  Used by solve_pirls (Phase 1 main
// optimizer for non-Gaussian GLMMs) and solve_gaussian_henderson.
//
// For the schwa benchmark (p≈6, J=1256), the dense build was the dominant
// cost of every PIRLS iteration (~250 ms × tens of inner iterations × tens
// of outer evaluations).  Sparse: ≈10–30 ms.
static Eigen::SparseMatrix<double> build_joint_henderson_sparse(
    const Eigen::VectorXd &w,
    const Eigen::Map<Matrix<double>> &Xm,
    const std::vector<Eigen::MatrixXd> &D_inv,
    const GroupLayout &lay,
    intptr_t n, intptr_t p)
{
	intptr_t J = lay.J_total;
	intptr_t sdim = p + J;

	std::vector<Eigen::Triplet<double>> triplets;

	// Capacity estimate:
	//   p² (X'WX, full)
	//   + Σ J_g q_g² (D⁻¹)
	//   + n · ((Σ q_g)² (Z'WZ) + 2 p (Σ q_g) (X'WZ + Z'WX))
	intptr_t qsum = 0, prior_cap = 0;
	for (intptr_t g = 0; g < lay.G; g++) {
		qsum += lay.q[g];
		prior_cap += lay.J[g] * lay.q[g] * lay.q[g];
	}
	triplets.reserve((size_t)p * p
	                 + prior_cap
	                 + (size_t)n * ((size_t)qsum * qsum
	                                + 2 * (size_t)p * qsum));

	// ── X'WX (p × p, dense): accumulate then emit ──
	Eigen::MatrixXd XWX = Eigen::MatrixXd::Zero(p, p);
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t j1 = 0; j1 < p; j1++)
		{
			double wx = Xm(i, j1) * w[i];
			for (intptr_t j2 = j1; j2 < p; j2++) {
				XWX(j1, j2) += wx * Xm(i, j2);
			}
		}
	}
	for (intptr_t j1 = 0; j1 < p; j1++)
		for (intptr_t j2 = j1 + 1; j2 < p; j2++)
			XWX(j2, j1) = XWX(j1, j2);
	for (intptr_t j1 = 0; j1 < p; j1++)
		for (intptr_t j2 = 0; j2 < p; j2++)
			triplets.emplace_back(j1, j2, XWX(j1, j2));

	// ── X'WZ and Z'WX (off-diagonal cross blocks) ──
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t lvl = (*lay.group_indices[g])[i];
			intptr_t qg = lay.q[g];
			intptr_t base = p + lay.offset[g] + lvl * qg;
			for (intptr_t t = 0; t < qg; t++)
			{
				double wz_val = w[i] * lay.Z(g, i, t);
				for (intptr_t j = 0; j < p; j++)
				{
					double val = Xm(i, j) * wz_val;
					triplets.emplace_back(j, base + t, val);
					triplets.emplace_back(base + t, j, val);
				}
			}
		}
	}

	// ── Random-effects block (D⁻¹ + Z'WZ), shifted by p ──
	emit_random_block_triplets(triplets, p, w, D_inv, lay, n);

	Eigen::SparseMatrix<double> H(sdim, sdim);
	H.setFromTriplets(triplets.begin(), triplets.end());  // sums duplicates
	H.makeCompressed();
	return H;
}


static double full_log_det_H(const Eigen::VectorXd &w,
                               const std::vector<Eigen::MatrixXd> &D_inv,
                               const GroupLayout &lay,
                               intptr_t n)
{
	intptr_t J = lay.J_total;

	// No random effects: log det of empty matrix = 0.
	if (J == 0) return 0.0;

	// Single grouping factor: block-diagonal fast path.
	// Each level j has a q×q block: D_inv + Σ_{i: idx[i]=j} w[i] z_i z_i'.
	if (lay.G == 1)
	{
		intptr_t qg = lay.q[0];
		auto &idx = *lay.group_indices[0];

		// Pre-accumulate Z'_j W Z_j for each level, initialized with D_inv.
		std::vector<Eigen::MatrixXd> blocks(lay.J[0], D_inv[0]);

		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t j = idx[i];
			for (intptr_t t1 = 0; t1 < qg; t1++)
			{
				double wz1 = w[i] * lay.Z(0, i, t1);
				for (intptr_t t2 = t1; t2 < qg; t2++)
				{
					double val = wz1 * lay.Z(0, i, t2);
					blocks[j](t1, t2) += val;
					if (t1 != t2) blocks[j](t2, t1) += val;
				}
			}
		}

		double ld = 0;
		for (intptr_t j = 0; j < lay.J[0]; j++)
		{
			if (qg == 1) {
				ld += std::log(blocks[j](0, 0));
			} else {
				Eigen::LDLT<Eigen::MatrixXd> ldlt(blocks[j]);
				ld += ldlt.vectorD().array().log().sum();
			}
		}
		return ld;
	}

	// General case (G > 1): build the full J × J sparse matrix and factorize.
	//
	// The Henderson Hessian H = Z'WZ + D⁻¹ is sparse: each observation
	// touches only one level per grouping factor, so for G groups the
	// nonzero pattern has O(n · (Σ q_g)²) entries — typically ~1% nnz on
	// realistic crossed designs. Dense LDLT was O(J³) ≈ 2 GFLOP per call
	// at J=1256; SimplicialLDLT exploits the sparsity for ~50× speedup.
	//
	// SimplicialLDLT requires the matrix to admit a non-pivoted LDL'
	// factorization. For all standard families (Gaussian, binomial,
	// Poisson, NB, beta) the IRLS weights w_i are non-negative, making H
	// SPD. Student-t can have negative w on outliers; that case has a
	// separate code path in student_full_log_det_H_hybrid (line below).
	// We still defensively check info() and fall back to dense if the
	// sparse Cholesky fails.
	Eigen::SparseMatrix<double> H = build_full_H_sparse(w, D_inv, lay, n);

	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::Lower> ldlt;
	ldlt.compute(H);
	if (ldlt.info() == Eigen::Success) {
		return ldlt.vectorD().array().log().sum();
	}

	// Fallback: dense LDLT (with Bunch-Kaufman pivoting) for indefinite
	// or near-singular H. Should be unreachable on standard families.
	Eigen::MatrixXd Hd = Eigen::MatrixXd(H);
	Eigen::LDLT<Eigen::MatrixXd> ldlt_dense(Hd);
	return ldlt_dense.vectorD().array().log().sum();
}


// =====================================================================
// Hybrid Student-t Laplace correction
// =====================================================================
//
// For Student-t at the converged inner mode û, the **exact** Hessian
// of -log p(y|μ) wrt μ is
//
//   w_exact_i = (ν+1)(νσ² − r_i²) / (νσ² + r_i²)²
//
// which differs from the IRLS / Fisher-information weight
//
//   w_fisher_i = (ν+1) / (νσ² + r_i²)
//
// by the (νσ²−r²) factor in the numerator. For |r_i| > σ√ν the exact
// weight is *negative* — at outlying observations, the Student-t
// log-density curves toward zero (rather than pulling μ toward y), and
// the local posterior precision *decreases* with the data point.
//
// The exact form yields the tighter Laplace approximation that matches
// glmmTMB / TMB. The downside is that Z'·W_exact·Z + D⁻¹ can fail to
// be PD on data with severe outliers (|r| > σ√ν dominating D⁻¹).
//
// This helper tries the exact form first. If LDLT fails or yields a
// non-positive D-vector, it falls back to the Fisher form (always PD
// because all weights are non-negative). The chosen method is reported
// via `out_method`.

enum class LaplaceMethod { Exact, FisherInfo };

static double student_full_log_det_H_hybrid(
    const Eigen::VectorXd &y,
    const Eigen::VectorXd &mu,
    double sigma, double nu,
    const std::vector<Eigen::MatrixXd> &D_inv,
    const GroupLayout &lay,
    intptr_t n,
    LaplaceMethod &out_method)
{
	intptr_t J = lay.J_total;
	out_method = LaplaceMethod::Exact;
	if (J == 0) return 0.0;

	double nu_sigma2 = nu * sigma * sigma;

	// Build w_exact per observation.
	Eigen::VectorXd w_exact(n);
	for (intptr_t i = 0; i < n; i++) {
		double r = y[i] - mu[i];
		double r2 = r * r;
		double denom = nu_sigma2 + r2;
		w_exact[i] = (nu + 1.0) * (nu_sigma2 - r2) / (denom * denom);
	}

	// Try the exact form. Mirrors full_log_det_H structure but inlined
	// here so we can detect non-PD and fall back atomically.
	auto try_with_weights = [&](const Eigen::VectorXd &w) -> std::optional<double> {
		// Single-group fast path
		if (lay.G == 1) {
			intptr_t qg = lay.q[0];
			auto &idx = *lay.group_indices[0];
			std::vector<Eigen::MatrixXd> blocks(lay.J[0], D_inv[0]);

			for (intptr_t i = 0; i < n; i++) {
				intptr_t j = idx[i];
				for (intptr_t t1 = 0; t1 < qg; t1++) {
					double wz1 = w[i] * lay.Z(0, i, t1);
					for (intptr_t t2 = t1; t2 < qg; t2++) {
						double val = wz1 * lay.Z(0, i, t2);
						blocks[j](t1, t2) += val;
						if (t1 != t2) blocks[j](t2, t1) += val;
					}
				}
			}

			double ld = 0;
			for (intptr_t j = 0; j < lay.J[0]; j++) {
				if (qg == 1) {
					double d = blocks[j](0, 0);
					if (!(d > 0) || !std::isfinite(d)) return std::nullopt;
					ld += std::log(d);
				} else {
					Eigen::LDLT<Eigen::MatrixXd> ldlt(blocks[j]);
					if (ldlt.info() != Eigen::Success) return std::nullopt;
					Eigen::VectorXd D = ldlt.vectorD();
					double max_d = D.array().abs().maxCoeff();
					double tol = 1e-12 * std::max(max_d, 1.0);
					for (intptr_t k = 0; k < D.size(); k++) {
						if (!(D[k] > tol) || !std::isfinite(D[k])) return std::nullopt;
					}
					ld += D.array().log().sum();
				}
			}
			return ld;
		}

		// General path: assemble full J × J matrix
		Eigen::MatrixXd H = Eigen::MatrixXd::Zero(J, J);
		for (intptr_t g = 0; g < lay.G; g++) {
			intptr_t qg = lay.q[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				intptr_t base = lay.offset[g] + j * qg;
				for (intptr_t t1 = 0; t1 < qg; t1++) {
					for (intptr_t t2 = 0; t2 < qg; t2++) {
						H(base + t1, base + t2) = D_inv[g](t1, t2);
					}
				}
			}
		}
		for (intptr_t i = 0; i < n; i++) {
			for (intptr_t g1 = 0; g1 < lay.G; g1++) {
				intptr_t qg1 = lay.q[g1];
				intptr_t base1 = lay.offset[g1] + (*lay.group_indices[g1])[i] * qg1;
				for (intptr_t t1 = 0; t1 < qg1; t1++) {
					double wz1 = w[i] * lay.Z(g1, i, t1);
					for (intptr_t g2 = 0; g2 < lay.G; g2++) {
						intptr_t qg2 = lay.q[g2];
						intptr_t base2 = lay.offset[g2] + (*lay.group_indices[g2])[i] * qg2;
						for (intptr_t t2 = 0; t2 < qg2; t2++) {
							H(base1 + t1, base2 + t2) += wz1 * lay.Z(g2, i, t2);
						}
					}
				}
			}
		}

		Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
		if (ldlt.info() != Eigen::Success) return std::nullopt;
		Eigen::VectorXd D = ldlt.vectorD();
		double max_d = D.array().abs().maxCoeff();
		double tol = 1e-12 * std::max(max_d, 1.0);
		for (intptr_t k = 0; k < D.size(); k++) {
			if (!(D[k] > tol) || !std::isfinite(D[k])) return std::nullopt;
		}
		return D.array().log().sum();
	};

	auto exact_result = try_with_weights(w_exact);
	if (exact_result.has_value()) {
		out_method = LaplaceMethod::Exact;
		return *exact_result;
	}

	// Fallback: Fisher-info weights (guaranteed PD since w >= 0).
	Eigen::VectorXd w_fisher(n);
	for (intptr_t i = 0; i < n; i++) {
		double r = y[i] - mu[i];
		w_fisher[i] = (nu + 1.0) / (nu_sigma2 + r * r);
	}
	out_method = LaplaceMethod::FisherInfo;
	double fisher_result = full_log_det_H(w_fisher, D_inv, lay, n);
	return fisher_result;
}


// =====================================================================
// Gaussian Henderson solve with Cholesky-parameterized covariance
// =====================================================================
//
// One-shot Henderson system for Gaussian LMM with known σ² and
// generalized Z design matrix.  Supports random slopes via the
// Cholesky-parameterized D_g for each grouping factor.
//
// The Henderson system for Gaussian is linear, so no PIRLS iteration
// is needed — a single solve gives the exact (β̂, û) for given θ.

static ProfiledResult solve_gaussian_henderson(
	const std::vector<Eigen::MatrixXd> &D_inv,
	const std::vector<double> &log_det_Dg,
	double sigma2,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p,
	const PriorSpec *priors = nullptr,
	const Array<String> *coef_names = nullptr,
	const Eigen::VectorXd *off_ptr = nullptr)
{
	ProfiledResult res;
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;
	intptr_t sdim = p + J;

	double inv_sigma2 = 1.0 / sigma2;

	// Effective response: y - offset (identity link).
	Eigen::VectorXd y_work = ym;
	if (off_ptr) y_work -= *off_ptr;

	// Build Henderson system:
	// [(1/σ²)X'X     (1/σ²)X'Z           ] [β]   [(1/σ²)X'y_work ]
	// [(1/σ²)Z'X   (1/σ²)Z'Z + D⁻¹       ] [u ] = [(1/σ²)Z'y_work ]

	Eigen::MatrixXd H = Eigen::MatrixXd::Zero(sdim, sdim);
	Eigen::VectorXd rhs = Eigen::VectorXd::Zero(sdim);

	// (1/σ²)X'X and (1/σ²)X'y_work
	for (intptr_t i = 0; i < n; i++)
	{
		double wy = inv_sigma2 * y_work[i];
		for (intptr_t j1 = 0; j1 < p; j1++)
		{
			rhs[j1] += Xm(i, j1) * wy;
			double wx = Xm(i, j1) * inv_sigma2;
			for (intptr_t j2 = j1; j2 < p; j2++) {
				H(j1, j2) += wx * Xm(i, j2);
			}
		}
	}
	for (intptr_t j1 = 0; j1 < p; j1++) {
		for (intptr_t j2 = j1 + 1; j2 < p; j2++) {
			H(j2, j1) = H(j1, j2);
		}
	}

	// D⁻¹ blocks
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = p + lay.offset[g] + j * qg;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					H(base + t1, base + t2) += D_inv[g](t1, t2);
				}
			}
		}
	}

	// (1/σ²)X'Z, Z'X, Z'Z, Z'y
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < G; g1++)
		{
			intptr_t j1 = (*lay.group_indices[g1])[i];
			intptr_t q1 = lay.q[g1];
			intptr_t base1 = p + lay.offset[g1] + j1 * q1;

			for (intptr_t t = 0; t < q1; t++)
			{
				double z_val = lay.Z(g1, i, t);
				double wz = inv_sigma2 * z_val;

				for (intptr_t j = 0; j < p; j++)
				{
					double val = Xm(i, j) * wz;
					H(j, base1 + t) += val;
					H(base1 + t, j) += val;
				}

				rhs[base1 + t] += inv_sigma2 * y_work[i] * z_val;
			}

			// Z'Z within-group
			for (intptr_t t1 = 0; t1 < q1; t1++)
			{
				double wz1 = inv_sigma2 * lay.Z(g1, i, t1);
				for (intptr_t t2 = t1; t2 < q1; t2++)
				{
					double val = wz1 * lay.Z(g1, i, t2);
					H(base1 + t1, base1 + t2) += val;
					if (t1 != t2) H(base1 + t2, base1 + t1) += val;
				}
			}

			// Z'Z cross-group
			for (intptr_t g2 = g1 + 1; g2 < G; g2++)
			{
				intptr_t j2 = (*lay.group_indices[g2])[i];
				intptr_t q2 = lay.q[g2];
				intptr_t base2 = p + lay.offset[g2] + j2 * q2;

				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = inv_sigma2 * lay.Z(g1, i, t1);
					for (intptr_t t2 = 0; t2 < q2; t2++)
					{
						double val = wz1 * lay.Z(g2, i, t2);
						H(base1 + t1, base2 + t2) += val;
						H(base2 + t2, base1 + t1) += val;
					}
				}
			}
		}
	}

	// ── Fixed-effect prior (Bayesian mode) ──────────────────────────
	if (priors && coef_names)
		add_fixed_prior_to_henderson(H, rhs, *priors, *coef_names, p);

	// Solve
	Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
	Eigen::VectorXd sol = ldlt.solve(rhs);

	res.beta = sol.head(p);
	res.u = sol.tail(J);

	// Final η = Xβ + Zu + offset
	Eigen::VectorXd eta = Xm * res.beta;
	add_offset(eta, off_ptr);
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++) {
				eta[i] += lay.Z(g, i, t) * res.u[base + t];
			}
		}
	}
	res.mu = eta; // identity link

	// Laplace nll
	double rss = (ym - res.mu).squaredNorm();
	double cond_nll = rss / (2.0 * sigma2) + 0.5 * n * std::log(2.0 * M_PI * sigma2);

	double prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					quad += res.u[base + t1] * D_inv[g](t1, t2) * res.u[base + t2];
				}
			}
			prior_nll += quad / 2.0;
		}
		prior_nll += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI) + 0.5 * log_det_Dg[g]);
	}

	Eigen::VectorXd w_final = Eigen::VectorXd::Constant(n, inv_sigma2);
	double log_det_Huu = full_log_det_H(w_final, D_inv, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_Huu
	                  - 0.5 * J * std::log(2.0 * M_PI);

	// Add -log p(β̂) for Bayesian posterior mode.
	if (priors && coef_names)
		res.laplace_nll += fixed_prior_nll(res.beta, *priors, *coef_names, p);

	return res;
}


// =====================================================================
// Gaussian outer objective with Cholesky parameterization
// =====================================================================
//
// θ = (chol_1, ..., chol_G, log σ)
// dim = Σ q_g(q_g+1)/2 + 1

struct GaussianCholObjective
{
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	intptr_t n_chol;
	const PriorSpec *priors;
	const Array<String> *coef_names;
	const Eigen::VectorXd *off_ptr = nullptr;

	double eval(const Eigen::VectorXd &theta) const
	{
		std::vector<Eigen::MatrixXd> D_inv(lay.G);
		std::vector<double> log_det_Dg(lay.G);
		intptr_t chol_pos = 0;

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
			D_inv[g] = cholesky_to_precision(L);
			log_det_Dg[g] = log_det_D(theta.data() + chol_pos, qg);
			chol_pos += n_chol_params(qg);
		}
		double sigma2 = std::exp(2.0 * theta[n_chol]);

		// Henderson includes the fixed-effect prior (shifts β̂ to MAP).
		double nll = solve_gaussian_henderson(D_inv, log_det_Dg, sigma2,
		                                       Xm, ym, lay, n, p,
		                                       priors, coef_names, off_ptr).laplace_nll;

		// Variance-component and residual priors.
		if (priors)
		{
			// D_cov[g] = D_inv[g]⁻¹ (small matrix, at most q×q).
			std::vector<Eigen::MatrixXd> D_cov(lay.G);
			for (intptr_t g = 0; g < lay.G; g++)
				D_cov[g] = D_inv[g].inverse();

			nll -= variance_prior_log_density(D_cov, *priors, lay);
			nll -= residual_prior_log_density(std::sqrt(sigma2), *priors);
		}

		return nll;
	}
};


// =====================================================================
// PIRLS inner solve for non-Gaussian (β and u concentrated out)
// =====================================================================
//
// Penalized Iteratively Reweighted Least Squares (Breslow & Clayton 1993,
// Bates et al. 2015 §3).  For given variance parameters θ, finds the
// joint mode (β̂, û) by iterating:
//   1. Compute working response z and weights w from current (β, u)
//   2. Solve the weighted Henderson equations for (β, u)
//   3. Repeat until convergence
//
// The Henderson system is (p + J_total) × (p + J_total) and is solved
// directly by Eigen's LDLT.  For random intercepts in typical phonetics
// models this is at most a few hundred × a few hundred — negligible cost.

static ProfiledResult solve_pirls(const std::vector<Eigen::MatrixXd> &D_inv,
                                   const std::vector<double> &log_det_Dg,
                                   const Family &fam,
                                   const Eigen::Map<Matrix<double>> &Xm,
                                   const Eigen::Map<Vector<double>> &ym,
                                   const GroupLayout &lay,
                                   intptr_t n, intptr_t p,
                                   const Eigen::VectorXd &beta_init,
                                   const Eigen::VectorXd &u_init = Eigen::VectorXd(),
                                   const PriorSpec *priors = nullptr,
                                   const Array<String> *coef_names = nullptr,
                                   const Eigen::VectorXd *off_ptr = nullptr)
{
	ProfiledResult res;
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;
	intptr_t sdim = p + J;   // Henderson system dimension

	res.beta = beta_init;
	res.u = (u_init.size() == J) ? u_init : Eigen::VectorXd::Zero(J);

	// Symbolic factorization of the sparse Henderson matrix is reused
	// across all PIRLS iterations within this call: the sparsity pattern
	// of (Z'WZ + D⁻¹) and the X' blocks depends only on the design (X,Z)
	// and the random-effects layout — values change as μ moves but the
	// pattern is invariant. analyzePattern is ~80% of compute() cost, so
	// reusing it gives a further ≈1.9× speedup on top of dense → sparse.
	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::Lower> ldlt_p1;
	bool ldlt_p1_analyzed = false;

	for (int pirls_iter = 0; pirls_iter < 100; pirls_iter++)
	{
		// ── η = Xβ + Zu (without offset for working response) ──
		Eigen::VectorXd eta = Xm * res.beta;
		for (intptr_t g = 0; g < G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t qg = lay.q[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t base = lay.offset[g] + idx[i] * qg;
				for (intptr_t t = 0; t < qg; t++) {
					eta[i] += lay.Z(g, i, t) * res.u[base + t];
				}
			}
		}
		// μ uses full η (with offset); working response z uses η without offset.
		Eigen::VectorXd eta_full = eta;
		add_offset(eta_full, off_ptr);
		Eigen::VectorXd mu = fam.linkinv(eta_full);

		// ── Working weights and response ────────────────────────
		// z_i on the Xβ+Zu scale (offset excluded) so the Henderson solve
		// recovers β without absorbing the offset.
		Eigen::VectorXd w(n), z(n);
		if (fam.custom_weights)
		{
			w = fam.custom_weights(ym, mu);
			Eigen::VectorXd me = fam.mu_eta(mu);
			for (intptr_t i = 0; i < n; i++)
			{
				double d = std::max(me[i], 1e-10);
				z[i] = eta[i] + (ym[i] - mu[i]) / d;
			}
		}
		else
		{
			Eigen::VectorXd V = fam.variance(mu);
			Eigen::VectorXd me = fam.mu_eta(mu);
			for (intptr_t i = 0; i < n; i++)
			{
				double v = std::max(V[i], 1e-10);
				double d = std::max(me[i], 1e-10);
				w[i] = d * d / v;  // generalized IWLS weight
				z[i] = eta[i] + (ym[i] - mu[i]) / d;
			}
		}

		// ── Henderson system ────────────────────────────────────
		//  [X'WX       X'WZ           ] [β]   [X'Wz ]
		//  [Z'WX   Z'WZ + D⁻¹        ] [u ] = [Z'Wz ]
		//
		// Sparse build: see build_joint_henderson_sparse. For schwa
		// (sdim ≈ 1262) this is ~30× faster than the dense LDLT that
		// previously dominated each PIRLS iteration.

		Eigen::SparseMatrix<double> H = build_joint_henderson_sparse(w, Xm, D_inv, lay, n, p);
		Eigen::VectorXd rhs = Eigen::VectorXd::Zero(sdim);

		// Build rhs in a single pass (X'Wz on top, Z'Wz below).
		for (intptr_t i = 0; i < n; i++)
		{
			double wzi = w[i] * z[i];
			for (intptr_t j = 0; j < p; j++)
				rhs[j] += Xm(i, j) * wzi;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t lvl = (*lay.group_indices[g])[i];
				intptr_t qg = lay.q[g];
				intptr_t base = p + lay.offset[g] + lvl * qg;
				for (intptr_t t = 0; t < qg; t++)
					rhs[base + t] += wzi * lay.Z(g, i, t);
			}
		}

		// ── Fixed-effect prior (Bayesian mode) ──────────────────
		//
		// add_fixed_prior_to_henderson takes a dense reference; here
		// we apply the equivalent precision/mean adjustment directly.
		// All H(j,j) for j<p exist (X'WX is dense in the top-left
		// block), so coeffRef is O(log nnz) lookup with no structural
		// change to the compressed sparse matrix.
		if (priors && coef_names)
		{
			for (intptr_t j = 0; j < p; j++)
			{
				const auto &pr = priors->prior_for((*coef_names)[j + 1]);
				double lambda = 1.0 / (pr.sd * pr.sd);
				H.coeffRef(j, j) += lambda;
				rhs[j] += lambda * pr.mean;
			}
		}

		// ── Solve ───────────────────────────────────────────────
		if (!ldlt_p1_analyzed) {
			ldlt_p1.analyzePattern(H);
			ldlt_p1_analyzed = true;
		}
		ldlt_p1.factorize(H);
		Eigen::VectorXd sol;
		if (ldlt_p1.info() == Eigen::Success) {
			sol = ldlt_p1.solve(rhs);
		} else {
			// Fallback: dense LDLT (Bunch-Kaufman pivoting). Should be
			// unreachable on standard families since w ≥ 0 ⇒ H is SPD.
			Eigen::MatrixXd Hd = Eigen::MatrixXd(H);
			sol = Eigen::LDLT<Eigen::MatrixXd>(Hd).solve(rhs);
		}

		Eigen::VectorXd beta_new = sol.head(p);
		Eigen::VectorXd u_new = sol.tail(J);

		// ── Convergence ─────────────────────────────────────────
		double max_change = (beta_new - res.beta).cwiseAbs().maxCoeff();
		if (J > 0) {
			max_change = std::max(max_change, (u_new - res.u).cwiseAbs().maxCoeff());
		}

		res.beta = beta_new;
		res.u = u_new;

		if (max_change < 1e-8) break;
		if (pirls_iter == 99) break;
	}

	// ── Final η, μ ──────────────────────────────────────────────────
	Eigen::VectorXd eta = Xm * res.beta;
	add_offset(eta, off_ptr);
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++) {
				eta[i] += lay.Z(g, i, t) * res.u[base + t];
			}
		}
	}
	res.mu = fam.linkinv(eta);

	// ── Laplace nll ─────────────────────────────────────────────────
	double cond_nll = -fam.loglik(ym, res.mu);

	// Prior: Σ_g Σ_j [ u_{gj}' D_g⁻¹ u_{gj} / 2 + q_g/2 log(2π) + 1/2 log|D_g| ]
	double prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					quad += res.u[base + t1] * D_inv[g](t1, t2) * res.u[base + t2];
				}
			}
			prior_nll += quad / 2.0;
		}
		prior_nll += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI) + 0.5 * log_det_Dg[g]);
	}

	// Laplace correction: 1/2 log|H_uu| - J_total/2 log(2π)
	Eigen::VectorXd w_final(n);
	if (fam.custom_weights)
	{
		w_final = fam.custom_weights(ym, res.mu);
	}
	else
	{
		Eigen::VectorXd V_final = fam.variance(res.mu);
		Eigen::VectorXd me_final = fam.mu_eta(res.mu);
		for (intptr_t i = 0; i < n; i++) {
			double v = std::max(V_final[i], 1e-10);
			double d = std::max(me_final[i], 1e-10);
			w_final[i] = d * d / v;
		}
	}
	double log_det_Huu = full_log_det_H(w_final, D_inv, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_Huu
	                  - 0.5 * J * std::log(2.0 * M_PI);

	// Add -log p(β̂) for Bayesian posterior mode.
	if (priors && coef_names)
		res.laplace_nll += fixed_prior_nll(res.beta, *priors, *coef_names, p);

	return res;
}

// Backward-compatible overload: scalar variances (for random-intercept-only models).
static ProfiledResult solve_pirls(const Eigen::VectorXd &sigma2_u,
                                   const Family &fam,
                                   const Eigen::Map<Matrix<double>> &Xm,
                                   const Eigen::Map<Vector<double>> &ym,
                                   const GroupLayout &lay,
                                   intptr_t n, intptr_t p,
                                   const Eigen::VectorXd &beta_init)
{
	std::vector<Eigen::MatrixXd> D_inv(lay.G);
	std::vector<double> log_det_Dg(lay.G);
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		D_inv[g] = Eigen::MatrixXd::Identity(qg, qg) / sigma2_u[g];
		log_det_Dg[g] = qg * std::log(sigma2_u[g]);
	}
	return solve_pirls(D_inv, log_det_Dg, fam, Xm, ym, lay, n, p, beta_init);
}


// =====================================================================
// u-only IRLS solver (β fixed) — used by Phase 2 joint optimization
// =====================================================================
//
// Much cheaper than full PIRLS: no β solve, and for random intercepts
// the u-update is diagonal (no matrix factorization).

static ProfiledResult solve_u_given_beta(
    const std::vector<Eigen::MatrixXd> &D_inv,
    const std::vector<double> &log_det_Dg,
    const Family &fam,
    const Eigen::Map<Matrix<double>> &Xm,
    const Eigen::Map<Vector<double>> &ym,
    const GroupLayout &lay,
    intptr_t n, intptr_t p,
    const Eigen::VectorXd &beta,
    const Eigen::VectorXd &u_init = Eigen::VectorXd(),
    const Eigen::VectorXd *off_ptr = nullptr,
    bool is_final_report = false)
{
	ProfiledResult res;
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;

	res.beta = beta;  // fixed — not updated
	res.u = (u_init.size() == J) ? u_init : Eigen::VectorXd::Zero(J);

	Eigen::VectorXd Xbeta = Xm * beta;
	add_offset(Xbeta, off_ptr);

	// PIRLS iteration counters (visible to the laplace_diag print below).
	int n_iter_done = 0;
	int n_halvings_total = 0;
	int n_fallbacks = 0;

	// When there are no random effects (G=0, J=0), skip the u-update loop entirely.
	if (J > 0)
	{
	// Symbolic factorization reused across the u-only PIRLS iterations.
	// Same justification as solve_pirls: H_uu sparsity pattern is
	// invariant for a fixed design, only values change as μ moves.
	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::Lower> ldlt_u;
	bool ldlt_u_analyzed = false;

	// ── Armijo-style monotone descent on f(u) = cond_nll(u) + ½·u'D⁻¹u ──
	//
	// The Henderson Newton step is locally optimal for the QUADRATIC
	// surrogate of f at the current u (i.e. the IRLS working response).
	// Far from the mode the surrogate is bad and the full Newton step
	// can overshoot — most damagingly on binomial GLMMs with strong
	// random effects, where pure Newton from u=0 lands in the saturated
	// regime and converges to a non-mode stationary point.  Step-halving
	// guarantees monotone descent on the true (non-quadratic) objective:
	// only accept the proposed step if it actually lowers f, otherwise
	// halve until it does (or fall back to Newton after 10 halvings).
	//
	// f(u) needs cond_nll which needs μ which needs η = Xβ + Zu — a few
	// vector operations, no factorization. So step-halving costs at
	// most ~10 cheap evaluations per outer iteration, only on iterations
	// where the full Newton step is actually rejected.  Near the mode
	// (the warm-started Phase 2 case), the full step always satisfies
	// the descent test and step-halving is a no-op.
	auto eval_f = [&](const Eigen::VectorXd &u_eval, double &out_f) -> bool
	{
		Eigen::VectorXd eta_e = Xbeta;
		for (intptr_t g = 0; g < G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t qg = lay.q[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t base = lay.offset[g] + idx[i] * qg;
				for (intptr_t t = 0; t < qg; t++)
					eta_e[i] += lay.Z(g, i, t) * u_eval[base + t];
			}
		}
		Eigen::VectorXd mu_e = (fam.link_name == "identity")
		    ? fam.linkinv(eta_e)
		    : fam.linkinv(eta_e.cwiseMax(-30.0).cwiseMin(30.0));
		double cond = -fam.loglik(ym, mu_e);
		if (!std::isfinite(cond)) return false;

		double prior_q = 0;
		for (intptr_t g = 0; g < G; g++)
		{
			intptr_t qg = lay.q[g];
			for (intptr_t j = 0; j < lay.J[g]; j++)
			{
				intptr_t base = lay.offset[g] + j * qg;
				double quad = 0;
				for (intptr_t t1 = 0; t1 < qg; t1++)
					for (intptr_t t2 = 0; t2 < qg; t2++)
						quad += u_eval[base + t1] * D_inv[g](t1, t2) * u_eval[base + t2];
				prior_q += quad;
			}
		}
		out_f = cond + 0.5 * prior_q;
		return std::isfinite(out_f);
	};

	double f_old = 0;
	bool have_f_old = eval_f(res.u, f_old);

	for (int iter = 0; iter < 100; iter++)
	{
		n_iter_done = iter + 1;
		// ── η = Xβ + Zu ──
		Eigen::VectorXd eta = Xbeta;
		for (intptr_t g = 0; g < G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t qg = lay.q[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t base = lay.offset[g] + idx[i] * qg;
				for (intptr_t t = 0; t < qg; t++)
					eta[i] += lay.Z(g, i, t) * res.u[base + t];
			}
		}

		Eigen::VectorXd mu = (fam.link_name == "identity")
		    ? fam.linkinv(eta)
		    : fam.linkinv(eta.cwiseMax(-30.0).cwiseMin(30.0));

		// Working weights and residual r = z - Xβ
		Eigen::VectorXd w(n), r(n);
		bool bad = false;
		if (fam.custom_weights)
		{
			w = fam.custom_weights(ym, mu);
			Eigen::VectorXd me = fam.mu_eta(mu);
			for (intptr_t i = 0; i < n; i++)
			{
				double d = std::max(me[i], 1e-10);
				r[i] = eta[i] - Xbeta[i] + (ym[i] - mu[i]) / d;
				if (!std::isfinite(r[i]) || !std::isfinite(w[i])) bad = true;
			}
		}
		else
		{
			Eigen::VectorXd V = fam.variance(mu);
			Eigen::VectorXd me = fam.mu_eta(mu);
			for (intptr_t i = 0; i < n; i++)
			{
				double v = std::max(V[i], 1e-10);
				double d = std::max(me[i], 1e-10);
				w[i] = d * d / v;
				r[i] = eta[i] - Xbeta[i] + (ym[i] - mu[i]) / d;
				if (!std::isfinite(r[i]) || !std::isfinite(w[i])) bad = true;
			}
		}
		if (bad) break;

		Eigen::VectorXd u_new(J);

		if (G == 1)
		{
			// ── Single group: block-diagonal solve (exact) ──
			intptr_t qg = lay.q[0];
			intptr_t Jg = lay.J[0];
			auto &idx = *lay.group_indices[0];

			std::vector<Eigen::MatrixXd> blocks(Jg, D_inv[0]);
			std::vector<Eigen::VectorXd> rhs_v(Jg, Eigen::VectorXd::Zero(qg));

			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t j = idx[i];
				for (intptr_t t1 = 0; t1 < qg; t1++)
				{
					double wz1 = w[i] * lay.Z(0, i, t1);
					rhs_v[j][t1] += wz1 * r[i];
					for (intptr_t t2 = t1; t2 < qg; t2++)
					{
						double val = wz1 * lay.Z(0, i, t2);
						blocks[j](t1, t2) += val;
						if (t1 != t2) blocks[j](t2, t1) += val;
					}
				}
			}

			for (intptr_t j = 0; j < Jg; j++)
			{
				intptr_t base = lay.offset[0] + j * qg;
				if (qg == 1) {
					u_new[base] = rhs_v[j][0] / blocks[j](0, 0);
				} else {
					u_new.segment(base, qg) = blocks[j].ldlt().solve(rhs_v[j]);
				}
			}
		}
		else
		{
			// ── Crossed groups: full J × J sparse system ──
			// (Z'WZ + D⁻¹) u = Z'W r
			//
			// H is built sparsely via build_full_H_sparse (shared with
			// full_log_det_H for consistency). For the schwa benchmark
			// (J=1256, ~1% nnz) this is ~50× faster than dense LDLT and
			// reduces a single PIRLS iteration from ~250 ms to ~6 ms.
			Eigen::SparseMatrix<double> H = build_full_H_sparse(w, D_inv, lay, n);

			Eigen::VectorXd rhs = Eigen::VectorXd::Zero(J);
			for (intptr_t i = 0; i < n; i++)
			{
				for (intptr_t g1 = 0; g1 < G; g1++)
				{
					intptr_t j1 = (*lay.group_indices[g1])[i];
					intptr_t q1 = lay.q[g1];
					intptr_t base1 = lay.offset[g1] + j1 * q1;

					for (intptr_t t = 0; t < q1; t++)
						rhs[base1 + t] += w[i] * lay.Z(g1, i, t) * r[i];
				}
			}

			Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::Lower> &ldlt = ldlt_u;
			if (!ldlt_u_analyzed) {
				ldlt.analyzePattern(H);
				ldlt_u_analyzed = true;
			}
			ldlt.factorize(H);
			if (ldlt.info() == Eigen::Success) {
				u_new = ldlt.solve(rhs);
			} else {
				// Fallback: dense LDLT (Bunch-Kaufman pivoting) for
				// indefinite or near-singular H. Should be unreachable
				// on standard families.
				Eigen::MatrixXd Hd = Eigen::MatrixXd(H);
				u_new = Eigen::LDLT<Eigen::MatrixXd>(Hd).solve(rhs);
			}
		}

		// ── Step-halving on the true (non-quadratic) objective ──
		// Newton step δ = u_new − res.u; try α ∈ {1, ½, ¼, …, 2⁻¹⁰}.
		// Accept first α with f(res.u + α·δ) ≤ f(res.u) + tol_f.
		// If none works, fall back to the full Newton step (matches the
		// pre-step-halving behaviour, so this can never make things
		// worse than the previous code on hard problems).
		const double tol_f = 1e-12 * std::max(1.0, std::abs(f_old));

		Eigen::VectorXd u_accept = u_new;  // default: full Newton
		double f_accept = 0;
		bool accepted = false;
		int halvings_this_iter = 0;
		if (have_f_old)
		{
			double alpha = 1.0;
			for (int hi = 0; hi < 11; hi++)
			{
				Eigen::VectorXd u_try = (alpha == 1.0)
				    ? u_new
				    : (res.u + alpha * (u_new - res.u)).eval();
				double f_try = 0;
				if (eval_f(u_try, f_try) && f_try <= f_old + tol_f)
				{
					u_accept = std::move(u_try);
					f_accept = f_try;
					accepted = true;
					halvings_this_iter = hi;
					break;
				}
				alpha *= 0.5;
			}
		}
		n_halvings_total += halvings_this_iter;
		if (!accepted) n_fallbacks++;

		double max_change = (u_accept - res.u).cwiseAbs().maxCoeff();
		res.u = std::move(u_accept);
		if (accepted) {
			f_old = f_accept;
		} else {
			// Fall back: full Newton accepted unconditionally.  Refresh
			// f_old from the new u so future iterations have a reference.
			have_f_old = eval_f(res.u, f_old);
		}
		if (!std::isfinite(max_change)) break;
		if (max_change < 1e-10) break;
	}
	} // if (J > 0)

	// ── Final η, μ ──
	Eigen::VectorXd eta_f = Xbeta;
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++)
				eta_f[i] += lay.Z(g, i, t) * res.u[base + t];
		}
	}
	res.mu = (fam.link_name == "identity")
	    ? fam.linkinv(eta_f)
	    : fam.linkinv(eta_f.cwiseMax(-30.0).cwiseMin(30.0));

	// ── Laplace NLL ──
	double cond_nll = -fam.loglik(ym, res.mu);
	if (!std::isfinite(cond_nll)) {
		res.laplace_nll = 1e30;
		return res;
	}

	double prior_quad = 0;       // ½ Σ_j u_j' D⁻¹ u_j
	double prior_const = 0;      // Σ_g J_g [ q_g/2 log(2π) + ½ log|D_g| ]
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++)
				for (intptr_t t2 = 0; t2 < qg; t2++)
					quad += res.u[base + t1] * D_inv[g](t1, t2) * res.u[base + t2];
			prior_quad += quad / 2.0;
		}
		prior_const += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI) + 0.5 * log_det_Dg[g]);
	}
	double prior_nll = prior_quad + prior_const;

	Eigen::VectorXd w_f(n);
	if (fam.custom_weights)
	{
		w_f = fam.custom_weights(ym, res.mu);
	}
	else
	{
		Eigen::VectorXd V_f = fam.variance(res.mu);
		Eigen::VectorXd me_f = fam.mu_eta(res.mu);
		for (intptr_t i = 0; i < n; i++)
		{
			double v = std::max(V_f[i], 1e-10);
			double d = std::max(me_f[i], 1e-10);
			w_f[i] = d * d / v;
		}
	}
	double log_det_Huu = full_log_det_H(w_f, D_inv, lay, n);
	double half_log_det_Huu = 0.5 * log_det_Huu;
	double minus_half_J_log_2pi = -0.5 * J * std::log(2.0 * M_PI);

	res.laplace_nll = cond_nll + prior_nll + half_log_det_Huu + minus_half_J_log_2pi;

	// ── Diagnostic: per-component breakdown of the Laplace NLL ──
	//
	// Set the env var PHON_DIAG_LAPLACE=1 to print the breakdown to
	// stderr on every call. Useful for localizing offsets between Phon's
	// reported logLik and reference implementations (glmmTMB, lme4).
	//
	// The cold-start case (u_init.size() != J, used by the final
	// reporting call from the main fit driver) is the one that produces
	// the model.loglik value the user sees.
	{
		static const bool diag_on = []() {
			const char *e = std::getenv("PHON_DIAG_LAPLACE");
			return e && e[0] && e[0] != '0';
		}();
		if (diag_on)
		{
			std::fprintf(stderr,
				"[laplace_diag%s] cond_nll=%.6f  prior_quad=%.6f  prior_const=%.6f  "
				"half_log_det_Huu=%.6f  minus_half_J_log_2pi=%.6f  total=%.6f  "
				"(n=%ld J=%ld G=%ld p=%ld pirls_iters=%d halvings=%d fallbacks=%d)\n",
				is_final_report ? "/FINAL" : "",
				cond_nll, prior_quad, prior_const,
				half_log_det_Huu, minus_half_J_log_2pi, res.laplace_nll,
				(long)n, (long)J, (long)G, (long)p,
				n_iter_done, n_halvings_total, n_fallbacks);
		}
	}

	return res;
}


// =====================================================================
// PIRLS outer objective (non-Gaussian)
// =====================================================================
//
// Outer parameters:
//   θ = (chol_1, ..., chol_G, [log θ_nb | log φ | log σ, log ν])
// where chol_g is the packed lower Cholesky factor for group g
// (q_g(q_g+1)/2 elements, diagonal on log scale).
//
// Total dim = Σ q_g(q_g+1)/2 + n_dispersion_params().
//
// β and u are concentrated out via PIRLS at each θ evaluation.

struct PirlsObjective
{
	const Family &fam;
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	Eigen::VectorXd beta_init;
	intptr_t n_chol;  // total Cholesky params = Σ q_g(q_g+1)/2
	const PriorSpec *priors;
	const Array<String> *coef_names;
	const Eigen::VectorXd *off_ptr = nullptr;

	// Warm-start: cache the random effects from the last PIRLS solve
	// so consecutive evaluations at nearby θ start close to the mode.
	mutable Eigen::VectorXd last_u;

	double eval(const Eigen::VectorXd &theta) const
	{

		// Unpack Cholesky parameters → D_inv and log|D| for each group
		std::vector<Eigen::MatrixXd> D_inv(lay.G);
		std::vector<double> log_det_Dg(lay.G);
		intptr_t chol_pos = 0;

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			intptr_t np = n_chol_params(qg);
			Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
			D_inv[g] = cholesky_to_precision(L);
			log_det_Dg[g] = log_det_D(theta.data() + chol_pos, qg);
			chol_pos += np;
		}

		ProfiledResult res;

		// For NB, the element after the Cholesky params is log(θ_nb).
		if (fam.name == "negbin")
		{
			double theta_nb = std::exp(theta[n_chol]);
			auto fam_nb = Family::negbin(theta_nb);
			res = solve_pirls(D_inv, log_det_Dg, fam_nb, Xm, ym, lay, n, p, beta_init, last_u, priors, coef_names, off_ptr);
		}
		else if (fam.name == "beta")
		{
			double phi_beta = std::exp(theta[n_chol]);
			auto fam_beta = Family::beta(phi_beta);
			res = solve_pirls(D_inv, log_det_Dg, fam_beta, Xm, ym, lay, n, p, beta_init, last_u, priors, coef_names, off_ptr);
		}
		else if (fam.name == "student")
		{
			double sigma_t = std::exp(theta[n_chol]);
			double nu_t = std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0);
			auto fam_t = Family::student(sigma_t, nu_t);
			res = solve_pirls(D_inv, log_det_Dg, fam_t, Xm, ym, lay, n, p, beta_init, last_u, priors, coef_names, off_ptr);
		}
		else
		{
			res = solve_pirls(D_inv, log_det_Dg, fam, Xm, ym, lay, n, p, beta_init, last_u, priors, coef_names, off_ptr);
		}

		double nll = res.laplace_nll;

		// Variance-component priors.
		if (priors)
		{
			std::vector<Eigen::MatrixXd> D_cov(lay.G);
			for (intptr_t g = 0; g < lay.G; g++)
				D_cov[g] = D_inv[g].inverse();

			nll -= variance_prior_log_density(D_cov, *priors, lay);
		}

		last_u = std::move(res.u);
		return nll;
	}
};


// =====================================================================
// Phase 2 joint (β, θ) objective for non-Gaussian GLMMs
// =====================================================================
//
// After Phase 1 converges (PIRLS profiles β out, Newton over θ only),
// Phase 2 re-optimizes (β, θ) jointly with u profiled out via u-only
// IRLS. This matches the joint optimization done by lme4/glmmTMB.
// Phase 2 is only needed for non-Gaussian families (for Gaussian,
// the working weights are constant and ∂log|H|/∂β = 0).
//
// Outer parameter vector: phi = [β (p), θ_chol (n_chol), log(disp)...]

struct LaplaceJointObjective
{
	const Family &fam;
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p, n_chol;
	const PriorSpec *priors;
	const Array<String> *coef_names;
	const Eigen::VectorXd *off_ptr = nullptr;

	mutable Eigen::VectorXd last_u;

	double eval(const Eigen::VectorXd &phi) const
	{
		Eigen::VectorXd beta = phi.head(p);

		// Unpack Cholesky → D_inv, log_det_Dg
		std::vector<Eigen::MatrixXd> D_inv(lay.G);
		std::vector<double> log_det_Dg(lay.G);
		intptr_t chol_pos = 0;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			intptr_t np = n_chol_params(qg);
			Eigen::MatrixXd L = unpack_cholesky(phi.data() + p + chol_pos, qg);
			D_inv[g] = cholesky_to_precision(L);
			log_det_Dg[g] = log_det_D(phi.data() + p + chol_pos, qg);
			chol_pos += np;
		}

		Family fam_used = fam;
		if (fam.name == "negbin")
		{
			double theta_nb = std::exp(phi[p + n_chol]);
			fam_used = Family::negbin(theta_nb);
		}
		else if (fam.name == "beta")
		{
			double phi_beta = std::exp(phi[p + n_chol]);
			fam_used = Family::beta(phi_beta);
		}
		else if (fam.name == "student")
		{
			double sigma_t = std::exp(phi[p + n_chol]);
			double nu_t = std::clamp(std::exp(phi[p + n_chol + 1]), 2.0, 200.0);
			fam_used = Family::student(sigma_t, nu_t);
		}

		auto res = solve_u_given_beta(D_inv, log_det_Dg, fam_used,
		                               Xm, ym, lay, n, p, beta, last_u, off_ptr);
		double nll;
		if (std::isfinite(res.laplace_nll))
		{
			last_u = std::move(res.u);
			nll = res.laplace_nll;
		}
		else
		{
			return 1e30;  // reject this step
		}

		// Prior contributions (β is an explicit outer parameter here).
		if (priors)
		{
			if (coef_names)
				nll += fixed_prior_nll(beta, *priors, *coef_names, p);

			std::vector<Eigen::MatrixXd> D_cov(lay.G);
			for (intptr_t g = 0; g < lay.G; g++)
				D_cov[g] = D_inv[g].inverse();

			nll -= variance_prior_log_density(D_cov, *priors, lay);
		}

		return nll;
	}
};


// =====================================================================
// Newton optimizer
// =====================================================================
//
// For small outer dimensions (typically 2–10 parameters in phonetics
// models) full Newton (exact Hessian via finite differences of the
// objective, Armijo backtracking) converges in 5–15 iterations to
// machine precision.  This is what nlminb (the default optimizer in
// glmmTMB/lme4) does — L-BFGS is designed for thousands of parameters
// and is unreliable for these low-dimensional surfaces.
//
// The Objective type must provide: double eval(const Eigen::VectorXd &) const

struct NewtonResult
{
	Eigen::VectorXd theta;
	double fx = 0.0;
	int niter = 0;
	bool converged = false;
	const char *optimizer = "";   // "newton" or "lbfgs"; set by the optimizer.
};

template<typename Objective>
static NewtonResult newton_optimize(const Objective &obj,
                                     Eigen::VectorXd theta,
                                     int max_iter = 200,
                                     double grad_tol = 1e-8,
                                     FittingCallback progress = nullptr,
                                     double h_scale_hint = 0)
{
	intptr_t dim = theta.size();
	NewtonResult res;
	res.converged = false;
	res.optimizer = "newton";
	res.fx = obj.eval(theta);
	int stall_count = 0;

	// Finite-difference step scale.  When h_scale_hint > 0, use it directly
	// (the caller knows the noise floor of the objective).  Otherwise, use
	// dimension-dependent defaults for objectives with negligible noise
	// (e.g. Gaussian Henderson, which is solved exactly).
	double h_scale = (h_scale_hint > 0) ? h_scale_hint
	               : (dim <= 3) ? 1e-4 : 1e-3;

	for (int iter = 0; iter < max_iter; iter++)
	{
		// ── Gradient and Hessian from function values ─────────────
		//
		// For dim=3 this needs 1 + 2*3 + 4*3 = 19 function evals,
		// each of which is a fast profiled inner solve.

		double f0 = res.fx;

		// Per-dimension step sizes, forward/backward function values
		std::vector<double> h(dim), fp(dim), fm(dim);
		for (intptr_t j = 0; j < dim; j++)
		{
			h[j] = h_scale * std::max(std::abs(theta[j]), 1.0);
			Eigen::VectorXd tp = theta, tm = theta;
			tp[j] += h[j];
			tm[j] -= h[j];
			fp[j] = obj.eval(tp);
			fm[j] = obj.eval(tm);
		}

		Eigen::VectorXd grad(dim);
		Eigen::MatrixXd H(dim, dim);

		for (intptr_t j = 0; j < dim; j++)
		{
			grad[j] = (fp[j] - fm[j]) / (2.0 * h[j]);
			H(j, j) = (fp[j] - 2.0 * f0 + fm[j]) / (h[j] * h[j]);
		}

		// Off-diagonal: H_{jk} = (f++ − f+− − f−+ + f−−) / (4 h_j h_k)
		for (intptr_t j = 0; j < dim; j++)
		{
			for (intptr_t k = j + 1; k < dim; k++)
			{
				Eigen::VectorXd tpp = theta, tpm = theta, tmp = theta, tmm = theta;
				tpp[j] += h[j]; tpp[k] += h[k];
				tpm[j] += h[j]; tpm[k] -= h[k];
				tmp[j] -= h[j]; tmp[k] += h[k];
				tmm[j] -= h[j]; tmm[k] -= h[k];

				double val = (obj.eval(tpp) - obj.eval(tpm)
				              - obj.eval(tmp) + obj.eval(tmm))
				             / (4.0 * h[j] * h[k]);
				H(j, k) = val;
				H(k, j) = val;
			}
		}

		// ── Convergence check ────────────────────────────────────
		// Parameters below boundary_min are at the edge of the parameter
		// space (log σ → −∞, i.e. σ → 0); the gradient there doesn't
		// converge to 0 because the optimum is at −∞ on the log scale.
		// Convergence is checked only on the free (interior) parameters.
		{
			double grad_norm_free = 0;
			for (intptr_t j = 0; j < dim; j++)
			{
				bool at_boundary = (theta[j] < -10.0 && grad[j] > 0);
				if (!at_boundary) {
					grad_norm_free += grad[j] * grad[j];
				}
			}
			grad_norm_free = std::sqrt(grad_norm_free);

			if (grad_norm_free < grad_tol)
			{
				res.converged = true;
				res.theta = theta;
				res.niter = iter;
				return res;
			}
		}

		// ── Newton direction: −H⁻¹g ─────────────────────────────
		//
		// Fast path: if H is positive-definite, solve via LDLT (O(d³/3)).
		//
		// Slow path: if H is indefinite — which happens when the FD Hessian
		// picks up negative curvature from noise near a ridge, or when the
		// true objective surface is genuinely non-convex away from the
		// optimum — use Levenberg-Marquardt damping via eigenvalue
		// clamping.  Decompose H = V Λ V', floor each eigenvalue at a
		// positive value, and reconstruct the step as
		//     s = -V diag(1/Λ_clamped) V' g.
		// This preserves the correct step direction in all curvature-PD
		// directions and regularizes only those that are noisy or negative.
		// Steepest descent (the previous fallback) ignores all curvature
		// information and zig-zags along narrow valleys; LM damping keeps
		// whatever curvature is reliable.
		//
		// The floor 1e-8 × max|Λ| is conservative: it imposes LM damping
		// only when an eigenvalue falls 8 orders of magnitude below the
		// largest (genuinely degenerate or noise-driven), leaving well-
		// scaled directions untouched.
		Eigen::VectorXd step;
		{
			Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
			if (ldlt.info() == Eigen::Success && ldlt.isPositive()) {
				step = -ldlt.solve(grad);
			} else {
				Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(H);
				if (eig.info() == Eigen::Success)
				{
					Eigen::VectorXd evals = eig.eigenvalues();
					Eigen::MatrixXd V = eig.eigenvectors();
					double max_abs = evals.cwiseAbs().maxCoeff();
					double floor_val = std::max(1e-8 * max_abs, 1e-12);
					for (intptr_t j = 0; j < evals.size(); j++) {
						if (evals[j] < floor_val) evals[j] = floor_val;
					}
					Eigen::VectorXd g_rot = V.transpose() * grad;
					Eigen::VectorXd s_rot = -g_rot.array() / evals.array();
					step = V * s_rot;
				}
				else
				{
					// Eigendecomposition failed (extreme pathology); fall
					// back to steepest descent as a last resort.
					step = -grad * (1.0 / grad.norm());
				}
			}
		}

		// ── Backtracking line search (Armijo) ────────────────────
		double alpha = 1.0;
		double slope = grad.dot(step);

		// If slope ≥ 0 the step is not a descent direction
		if (slope >= 0) {
			step = -grad * (1.0 / grad.norm());
			slope = grad.dot(step);
		}

		for (int ls = 0; ls < 30; ls++)
		{
			Eigen::VectorXd theta_new = theta + alpha * step;
			double fx_new = obj.eval(theta_new);
			if (fx_new <= f0 + 1e-4 * alpha * slope)
			{
				theta = theta_new;
				res.fx = fx_new;
				break;
			}
			alpha *= 0.5;
			if (ls == 29)
			{
				// Line search stalled — accept current best
				theta = theta + alpha * step;
				res.fx = obj.eval(theta);
			}
		}

		// ── Function-value stall detection ───────────────────────
		// If the nll hasn't changed meaningfully in 5 consecutive
		// iterations, declare convergence.  The 1e-8 relative tolerance
		// matches the gradient tolerance and ensures the optimizer
		// continues refining as long as meaningful progress is possible.
		if (std::abs(res.fx - f0) < 1e-8 * (1.0 + std::abs(f0)))
		{
			stall_count++;
			if (stall_count >= 5)
			{
				res.converged = true;
				res.theta = theta;
				res.niter = iter;
				return res;
			}
		}
		else
		{
			stall_count = 0;
		}

		// Report progress.
		if (progress && iter % 5 == 0)
			progress(iter, max_iter);
	}

	if (progress) progress(max_iter, max_iter);
	res.theta = theta;
	res.niter = max_iter;
	return res;
}


// =====================================================================
// L-BFGS optimizer (for large-dim outer problems)
// =====================================================================
//
// For outer dimensions above ~15, Newton's finite-difference Hessian
// becomes the dominant cost: each iteration requires 1 + 2d + 4·d(d−1)/2
// function evaluations (so 1 + 2·20 + 380 = 421 for d = 20), each of
// which is a full inner solve.  L-BFGS trades the exact local Hessian
// for a low-rank secant approximation built from gradient history, which
// makes each iteration O(d) rather than O(d²).  For smooth, well-scaled
// objectives — which the Laplace marginal NLL is away from the boundary
// — L-BFGS converges in a comparable number of iterations to Newton.
//
// The gradient is computed by central finite differences; for an
// objective that is already O(d) evaluations per gradient, this is the
// same regime as L-BFGS with analytical gradients.
//
// Reference: Nocedal & Wright (2006), Numerical Optimization, §7.2.

template<typename Objective>
class LBfgsFdWrapper
{
public:
	LBfgsFdWrapper(const Objective &obj, double h_scale,
	               FittingCallback progress, int max_iter)
		: obj_(obj), h_scale_(h_scale),
		  progress_(progress), max_iter_(max_iter) {}

	double operator()(const Eigen::VectorXd &theta, Eigen::VectorXd &grad)
	{
		double f0 = obj_.eval(theta);
		intptr_t dim = theta.size();
		for (intptr_t j = 0; j < dim; j++)
		{
			double h = h_scale_ * std::max(std::abs(theta[j]), 1.0);
			Eigen::VectorXd tp = theta, tm = theta;
			tp[j] += h;
			tm[j] -= h;
			grad[j] = (obj_.eval(tp) - obj_.eval(tm)) / (2.0 * h);
		}
		// Progress reporting: LBFGSpp has no per-iteration callback, so we
		// fire progress from inside the functor.  Each call corresponds to
		// one gradient evaluation (typically 1 per L-BFGS iteration plus a
		// few per line search).  We report call_count/3 as an approximate
		// iteration count, capped at max_iter — this keeps the status bar
		// moving without overshooting when the optimizer stalls.
		call_count_++;
		if (progress_)
		{
			int approx_iter = std::min((int)(call_count_ / 3), max_iter_);
			progress_(approx_iter, max_iter_);
		}
		return f0;
	}

	int call_count() const { return call_count_; }

private:
	const Objective &obj_;
	double h_scale_;
	FittingCallback progress_;
	int max_iter_;
	int call_count_ = 0;
};

template<typename Objective>
static NewtonResult lbfgs_optimize(const Objective &obj,
                                    Eigen::VectorXd theta,
                                    int max_iter,
                                    double grad_tol,
                                    FittingCallback progress,
                                    double h_scale_hint)
{
	NewtonResult res;
	res.converged = false;
	res.optimizer = "lbfgs";

	double h_scale = (h_scale_hint > 0) ? h_scale_hint
	               : (theta.size() <= 3 ? 1e-4 : 1e-3);

	LBFGSpp::LBFGSParam<double> param;

	// Primary stopping criterion: relative gradient norm.  LBFGSpp stops
	// when ‖∇‖ < epsilon × max(1, ‖θ‖).  Newton uses absolute ‖∇‖ < grad_tol.
	// For typical θ norms of 3–10 on log-scale, setting epsilon = grad_tol
	// gives effective absolute tolerance 3–10 × grad_tol — slightly looser
	// than Newton, but well below the AIC-sensitivity threshold (noise in
	// log-likelihood from either optimizer is O(ε²) ≈ 1e-16, vs meaningful
	// ΔAIC > 2).
	param.epsilon = grad_tol;

	// Secondary stopping criterion: relative function-value change over
	// `past` iterations.  At very tight gradient tolerances the line search
	// can fail because sufficient-decrease is unachievable given the
	// floating-point resolution of the objective (~1e-14 relative).  The
	// function-value criterion handles this case: if the objective hasn't
	// changed meaningfully over 3 consecutive iterations, we're at the
	// optimum regardless of what the gradient norm says.
	param.past = 3;
	param.delta = 1e-10;

	param.max_iterations = max_iter;
	param.m = 10;  // memory size (history length)

	LBFGSpp::LBFGSSolver<double> solver(param);
	LBfgsFdWrapper<Objective> wrapper(obj, h_scale, progress, max_iter);

	double fx = 0.0;
	int niter = 0;
	bool threw = false;
	try {
		niter = solver.minimize(wrapper, theta, fx);
		if (niter < 1) niter = 1;
		res.converged = (niter < max_iter);
	}
	catch (const std::exception &) {
		// Common exception source: line search failure near convergence,
		// where function-value differences are below floating-point
		// resolution.  The iterate at this point is typically optimal —
		// LBFGSpp just can't formally certify it because the Armijo
		// condition can't be distinguished from noise.  We classify
		// empirically in the block below: probe for descent directions
		// at coordinate perturbations of size h_scale.  If no perturbation
		// decreases f beyond the noise floor, declare convergence.
		threw = true;
	}

	if (threw)
	{
		// Classify the exception as genuine non-convergence or spurious
		// line-search failure near the optimum.  Two independent tests
		// are computed from the same 2·dim evaluations used for FD
		// gradient:
		//
		//   (a) Descent probe: at each coordinate j, evaluate f at
		//       θ ± h·e_j.  At a local minimum, neither probe decreases
		//       f by more than the objective's evaluation noise floor,
		//       because pure curvature contributes +½h²·H_jj (positive).
		//       A genuinely non-converged θ with gradient g_j has linear
		//       response ±h·g_j, so at least one of the 2·dim probes
		//       produces a measurable decrease.  This test is robust:
		//       it compares function values on the same evaluation
		//       scale, so the signal is h·|g_j| against h²·|H_jj| —
		//       a factor 1/h separation, ≈ 100× at h_scale = 1e-2.
		//
		//   (b) Legacy FD gradient norm ‖∇f‖ < 1000·grad_tol.  Kept as
		//       a fallback when descent probing is inconclusive, but no
		//       longer the primary criterion: FD gradient noise scales
		//       as σ_f / h ≈ 1e-10·|f| / h, which with h_scale = 1e-2
		//       and |f| ~ 1e4 exceeds 1e-5 even at the exact optimum.
		//       This was the source of spurious non-convergence flags
		//       in Poisson/NB GLMMs where point estimates matched the
		//       reference implementation to 4 decimal places.
		//
		// Convergence is declared if EITHER test passes.  False-positive
		// rate is limited by the descent_tol threshold (1e-8 relative);
		// false-negative rate (missing genuine convergence) is nearly
		// zero because line-search exceptions almost always fire at or
		// within a few FD steps of the optimum.
		intptr_t dim = theta.size();
		double f0 = obj.eval(theta);
		Eigen::VectorXd grad(dim);
		double fref = std::max(std::abs(f0), 1.0);
		double descent_tol = 1e-8 * fref;
		bool found_descent = false;
		for (intptr_t j = 0; j < dim; j++)
		{
			double h = h_scale * std::max(std::abs(theta[j]), 1.0);
			Eigen::VectorXd tp = theta, tm = theta;
			tp[j] += h;
			tm[j] -= h;
			double fp = obj.eval(tp);
			double fm = obj.eval(tm);
			grad[j] = (fp - fm) / (2.0 * h);
			if (fp < f0 - descent_tol || fm < f0 - descent_tol) {
				found_descent = true;
			}
		}
		double gnorm = grad.norm();
		fx = f0;
		res.converged = !found_descent || (gnorm < 1000.0 * grad_tol);
		if (niter < 1) niter = std::max(1, wrapper.call_count() / 3);
	}

	// Final progress callback (100% bar — wrapper already reported per call).
	if (progress) progress(max_iter, max_iter);

	// ── NaN guard (correctness fix) ──────────────────────────────────
	// LBFGSpp throws when an iterate goes non-finite. The descent-probe
	// recovery above then evaluates at the NaN θ, producing NaN gradient
	// and NaN f0. Since IEEE 754 comparisons against NaN always return
	// false, neither the descent check (fp < f0 − tol) nor the gradient-
	// norm check (gnorm < 1000·tol) can fire, so the recovery silently
	// declares convergence=true and returns garbage θ. That garbage then
	// propagates to saved_theta and first surfaces at INLA grid
	// integration, where the user-facing error misleadingly blames the
	// random-effects structure.  Fail loud here instead.
	if (!theta.allFinite() || !std::isfinite(fx))
	{
		throw error(
			"L-BFGS optimizer produced non-finite state after % iteration(s) "
			"(θ all-finite: %; f(θ) finite: %). This typically indicates a "
			"numerically unstable combination of priors or data — most commonly "
			"very wide fixed-effect priors interacting with weakly-identified "
			"random-effect variance components. If the same model fits under "
			"the frequentist engine, the issue is in the prior specification. "
			"Compile with -DPHON_INLA_BAYES_DIAG=1 and rerun to see the full "
			"pre- and post-optimizer state.",
			(int) niter,
			theta.allFinite() ? "yes" : "no",
			std::isfinite(fx) ? "yes" : "no");
	}

	res.theta = theta;
	res.fx = fx;
	res.niter = niter;
	return res;
}


// Dispatch between Newton (small dim, trivially simple surfaces) and
// L-BFGS (everything else).  Although Newton's FD Hessian gives it
// quadratic convergence on well-conditioned surfaces, mixed-model outer
// objectives commonly have curved ridges — e.g. a between-cluster fixed
// effect partially aliased with the corresponding random intercept, or
// a weakly-identified random-slope variance.  On such ridges the FD
// Hessian is noisy in its smallest eigendirection, produces near-unit-
// length but mis-scaled steps, and convergence stalls.  L-BFGS with
// secant updates handles this regime gracefully: the accumulated
// curvature history smooths out FD noise and the backtracking line
// search keeps steps in-range.  The threshold of 2 keeps Newton for
// one or two independent random-intercept variances, where the log-
// posterior surface is well-scaled and Newton's quadratic convergence
// is an advantage.  We switch to L-BFGS as soon as dim ≥ 3 — this
// covers every random-slope model (a 2×2 random-slope covariance has
// q(q+1)/2 = 3 Cholesky parameters) and is also safe on the (rare)
// 3-intercept-only case.  The previous threshold of 3 routed exactly
// the smallest random-slope case to Newton, causing 200-iteration
// stalls on 2×2 random-slope covariances.
template<typename Objective>
static NewtonResult robust_optimize(const Objective &obj,
                                     Eigen::VectorXd theta,
                                     int max_iter = 200,
                                     double grad_tol = 1e-8,
                                     FittingCallback progress = nullptr,
                                     double h_scale_hint = 0)
{
	constexpr intptr_t LBFGS_THRESHOLD = 2;
	if (theta.size() > LBFGS_THRESHOLD) {
		return lbfgs_optimize(obj, theta, max_iter, grad_tol, progress, h_scale_hint);
	}
	return newton_optimize(obj, theta, max_iter, grad_tol, progress, h_scale_hint);
}


// =====================================================================
// INLA grid integration over hyperparameters
// =====================================================================
//
// After the outer Newton finds the posterior mode θ*, we:
//   1. Compute the Hessian H of the neg-log-posterior at θ*
//   2. Eigendecompose H = V Λ V' to define the integration coordinate system
//   3. Construct a Central Composite Design (CCD) in standardized z-space
//   4. Map each grid point to θ-space: θ_k = θ* + V Λ^{-1/2} z_k
//   5. At each θ_k, solve the inner problem → β̂(θ_k), Σ_β(θ_k), log p(θ_k|y)
//   6. Compute integration weights: w_k ∝ p(θ_k|y) / q_Gauss(θ_k)
//   7. Mixture posterior: E[β] = Σ w_k β̂_k
//                         Var[β] = Σ w_k [Σ_k + (β̂_k − E[β])(β̂_k − E[β])']
//
// This is the core INLA algorithm (Rue, Martino & Chopin 2009, §2.1–2.3).
// For d hyperparameters, the CCD has 1 + 2d + 2^d points.

struct GridPointResult
{
	Eigen::VectorXd beta;        // conditional mode β̂(θ)
	Eigen::MatrixXd vcov_beta;   // conditional Var(β|θ), p × p
	double neg_log_posterior;     // neg-log-posterior at θ
	Eigen::VectorXd d3;          // SLA third-derivative correction per coefficient (size p)
	                              // d3_j = Σ_i X³_{ij} ℓ'''(η̂_i)
	// Populated only for non-Gaussian PIRLS grid points; empty otherwise.
	// Used by compute_grid_waic to sample u from its Laplace-approximate
	// conditional posterior u|β,θ,y, matching the Gaussian closed-form path.
	//   working_weights[i] = μ'(η̂_i)² / V(μ̂_i)   (IRLS / Fisher weight at
	//     convergence; what PIRLS iterates with — always non-negative)
	//   working_response[i] = η̂_i + (y_i − μ̂_i) / μ'(η̂_i)   (without offset,
	//     on the Xβ+Zu scale matching solve_pirls convention)
	Eigen::VectorXd working_weights;
	Eigen::VectorXd working_response;

	// Student-t only: the **observed-Hessian** (a.k.a. exact Laplace) weights
	// at the grid point's mode,
	//
	//   w_L_i = (ν+1)(νσ² − r_i²) / (νσ² + r_i²)²
	//
	// vs the Fisher / IRLS form (ν+1)/(νσ²+r²) stored in working_weights.
	// Used by compute_grid_waic to build the M_k that defines the
	// posterior covariance of u | β, θ, y in the Laplace approximation.
	// This matches the convention of student_full_log_det_H_hybrid (used by
	// the optimizer's Laplace correction): observed Hessian where PD,
	// Fisher fallback elsewhere — keeping both code paths consistent.
	//
	// Fisher weights overestimate u-precision when |r| < σ√ν (which is the
	// typical residual regime), shrinking u^(s) draws toward the mode and
	// inflating WAIC's lppd. Using observed-Hessian weights here closes
	// most of the documented WAIC gap vs brms HMC for Student-t fits.
	//
	// `hessian_response` is the matching pseudo-response z_L such that
	//   M_L⁻¹ Z' W_L (z_L − X β̂_k)  =  û_k
	// at the grid point's mode, with M_L = Z' W_L Z + D_k⁻¹. The closed
	// form for Student-t (identity link) is
	//   z_L_i = (X β̂_k + Z û_k)_i + r̂_i · (νσ² + r_i²) / (νσ² − r_i²)
	// on the no-offset scale, matching working_response. Using this with
	// the matching W_L makes Z' W_L (z_L − μ̂_k) = D⁻¹ û_k via the score
	// equation, recovering û_k as required.
	//
	// **Outliers and PD-ness.** For |r_i| > σ√ν, w_L_i is negative —
	// individual observations contribute negatively to M_L. This is fine:
	// M_L = Z' W_L Z + D_k⁻¹ can still be PD if the prior precision D_k⁻¹
	// dominates the negative pile. eval_pirls_grid_point therefore
	// populates these fields *unconditionally* for Student-t, and
	// compute_grid_waic does the runtime PD check via Cholesky on the
	// resulting M_k (falling back to working_weights when LDLT fails).
	// The closed-form z_L is numerically robust thanks to lim_{r²→νσ²}
	// w_L · z_L being finite (= w_F · r̂ + w_L · μ̂); a defensive
	// epsilon in the divisor prevents exact-zero IEEE pathology.
	//
	// Both vectors are empty for non-Student families. compute_grid_waic
	// must use working_weights/working_response when either is empty.
	Eigen::VectorXd hessian_weights;
	Eigen::VectorXd hessian_response;
};


// Check whether a grid point produced valid results.
// Returns false if β, diag(Σ), or the neg-log-posterior contain NaN or Inf,
// which can happen when the inner solve (Henderson or PIRLS) fails to converge
// at an extreme hyperparameter configuration — e.g. a near-singular random-slope
// covariance matrix pushed further by the CCD grid.
static bool grid_point_valid(const GridPointResult &gpr, intptr_t p)
{
	if (!std::isfinite(gpr.neg_log_posterior))
		return false;
	if (gpr.beta.size() != p)
		return false;
	for (intptr_t j = 0; j < p; j++)
	{
		if (!std::isfinite(gpr.beta[j]))
			return false;
		if (gpr.vcov_beta.rows() > j && !std::isfinite(gpr.vcov_beta(j, j)))
			return false;
		if (gpr.vcov_beta.rows() > j && gpr.vcov_beta(j, j) < 0)
			return false;
	}
	return true;
}


// Sanitise grid evaluation results: set invalid points to -∞ log-posterior
// (zero weight) and zero out their beta/vcov_beta to prevent NaN propagation
// (since 0 × NaN = NaN in IEEE 754).  Returns the number of valid points.
// Throws a user-friendly error if no valid points remain.
static intptr_t sanitise_grid_points(
	std::vector<GridPointResult> &results,
	std::vector<double> &log_posterior,
	intptr_t n_grid, intptr_t p)
{
	intptr_t n_valid = 0;
	for (intptr_t k = 0; k < n_grid; k++)
	{
		if (grid_point_valid(results[k], p))
		{
			n_valid++;
		}
		else
		{
			log_posterior[k] = -std::numeric_limits<double>::infinity();
			// Zero out results so that w[k] * beta[k] = 0 * 0 = 0, not 0 * NaN.
			results[k].beta = Eigen::VectorXd::Zero(p);
			results[k].vcov_beta = Eigen::MatrixXd::Zero(p, p);
			results[k].d3 = Eigen::VectorXd::Zero(p);
			results[k].neg_log_posterior = std::numeric_limits<double>::infinity();
		}
	}
	if (n_valid == 0)
	{
		throw error(
			"Bayesian grid integration failed: all %ld evaluation points produced "
			"invalid results (NaN or non-finite values). This typically happens when "
			"the model is too complex for the data — for example, a random-slope "
			"specification whose variance cannot be estimated reliably. Consider "
			"simplifying the random-effects structure (e.g. removing random slopes "
			"or using a random-intercept-only model).", (long)n_grid);
	}
	return n_valid;
}


// Finite-difference Hessian of an objective at a given point.
template<typename Objective>
static Eigen::MatrixXd compute_fd_hessian(const Objective &obj,
                                            const Eigen::VectorXd &theta,
                                            double h_scale = 1e-4)
{
	intptr_t dim = theta.size();
	double f0 = obj.eval(theta);

	std::vector<double> h(dim), fp(dim), fm(dim);
	for (intptr_t j = 0; j < dim; j++)
	{
		h[j] = h_scale * std::max(std::abs(theta[j]), 1.0);
		Eigen::VectorXd tp = theta, tm = theta;
		tp[j] += h[j];
		tm[j] -= h[j];
		fp[j] = obj.eval(tp);
		fm[j] = obj.eval(tm);
	}

	Eigen::MatrixXd H(dim, dim);
	for (intptr_t j = 0; j < dim; j++) {
		H(j, j) = (fp[j] - 2.0 * f0 + fm[j]) / (h[j] * h[j]);
	}

	for (intptr_t j = 0; j < dim; j++)
	{
		for (intptr_t k = j + 1; k < dim; k++)
		{
			Eigen::VectorXd tpp = theta, tpm = theta, tmp = theta, tmm = theta;
			tpp[j] += h[j]; tpp[k] += h[k];
			tpm[j] += h[j]; tpm[k] -= h[k];
			tmp[j] -= h[j]; tmp[k] += h[k];
			tmm[j] -= h[j]; tmm[k] -= h[k];

			double val = (obj.eval(tpp) - obj.eval(tpm)
			              - obj.eval(tmp) + obj.eval(tmm))
			             / (4.0 * h[j] * h[k]);
			H(j, k) = val;
			H(k, j) = val;
		}
	}
	return H;
}


// Construct CCD grid points in standardized z-space.
// Returns a matrix where each row is a grid point (d-dimensional).
// CCD(d) = center (1) + axial (2d) + corners (2^d).
// delta = axial distance in standardized units.
static std::vector<Eigen::VectorXd> build_ccd_points(intptr_t d, double delta = 3.0)
{
	std::vector<Eigen::VectorXd> points;

	// Center point
	points.push_back(Eigen::VectorXd::Zero(d));

	// Axial points: ±δ along each axis
	for (intptr_t j = 0; j < d; j++)
	{
		Eigen::VectorXd z_plus = Eigen::VectorXd::Zero(d);
		Eigen::VectorXd z_minus = Eigen::VectorXd::Zero(d);
		z_plus[j] = delta;
		z_minus[j] = -delta;
		points.push_back(z_plus);
		points.push_back(z_minus);
	}

	// Corner points: ±(δ/√d) in all dimensions (2^d combinations).
	// Skip if d == 1: corner_val = δ/√1 = δ coincides exactly with the axial
	// points already placed at ±δ. Including them would duplicate grid
	// locations and double their weight in the mixture posterior, biasing
	// β moments and inflating the log-marginal likelihood. This case arises
	// for non-Gaussian GLMMs with a single random intercept and no extra
	// dispersion parameter (binomial/Poisson with (1|group) only).
	// Skip if d > 6 to avoid exponential blowup (64 corners).
	if (d >= 2 && d <= 6)
	{
		double corner_val = delta / std::sqrt((double)d);
		intptr_t n_corners = 1 << d;  // 2^d
		for (intptr_t mask = 0; mask < n_corners; mask++)
		{
			Eigen::VectorXd z(d);
			for (intptr_t j = 0; j < d; j++) {
				z[j] = (mask & (1 << j)) ? corner_val : -corner_val;
			}
			points.push_back(z);
		}
	}

	return points;
}


// Compute moment-matching CCD quadrature base weights for Gaussian integration
// in z-space (w.r.t. the standard multivariate normal N(0, I)). These weights
// pair with the grid points returned by build_ccd_points.
//
// Design: weights are chosen to integrate Gaussian moments exactly up to 2nd
// order (∫ 1·φ = 1 and ∫ z_i²·φ = 1). With 3 unknown weights (center, axial,
// corner) and 2 constraints for d ≥ 2, the under-determined system is closed
// by imposing equal total mass on the axial vs corner classes — a common
// convention in the CCD literature that produces positive weights for any δ.
//
// Closed form:
//   d = 1 (no corners — see build_ccd_points):
//     w_axial  = 1 / (2 δ²)     per axial point
//     w_center = 1 − 2 w_axial
//   d ≥ 2:
//     w_axial  = 1 / (4 δ²)            per axial point
//     w_corner = d / (2^{d+1} δ²)      per corner point
//     w_center = 1 − 2d · w_axial − 2^d · w_corner
//
// Points are classified by the number of non-zero components in z_k:
//   0 non-zero  → center (exactly one such point, by construction)
//   1 non-zero  → axial
//   ≥ 2 non-zero → corner
//
// When combined with the density-ratio correction P(θ_k|y) / φ(z_k), the
// effective grid weights produce a proper integration rule against the true
// posterior that reduces to exact CCD quadrature of Gaussian moments in the
// concentrated-posterior limit. This is the mechanism that prevents the
// mixture variance from collapsing to Σ(θ*) when the hyperparameter posterior
// is approximately Gaussian.
//
// Reference: Rue, Martino & Chopin (2009), JRSS-B 71(2), §6.5; design-of-
// experiments literature on central composite designs.
static std::vector<double> ccd_base_weights(intptr_t d, double delta,
                                             const std::vector<Eigen::VectorXd> &z_points)
{
	intptr_t n_pts = (intptr_t) z_points.size();
	std::vector<double> w(n_pts, 0.0);
	if (n_pts == 0 || d <= 0) return w;

	double delta2 = delta * delta;
	if (delta2 <= 0) return w;

	double w_axial, w_corner;
	if (d == 1)
	{
		w_axial = 1.0 / (2.0 * delta2);
		w_corner = 0.0;  // no corners for d=1 (see build_ccd_points)
	}
	else
	{
		w_axial = 1.0 / (4.0 * delta2);
		w_corner = (double) d / (std::pow(2.0, (double)(d + 1)) * delta2);
	}

	intptr_t center_idx = -1;
	for (intptr_t k = 0; k < n_pts; k++)
	{
		int nonzero = 0;
		for (intptr_t j = 0; j < d; j++) {
			if (std::abs(z_points[k][j]) > 1e-10) nonzero++;
		}
		if (nonzero == 0)       { w[k] = 0.0; center_idx = k; }
		else if (nonzero == 1)  { w[k] = w_axial; }
		else                    { w[k] = w_corner; }
	}

	// Set the center weight from the normalisation constraint.
	if (center_idx >= 0)
	{
		double non_center_sum = 0;
		for (intptr_t k = 0; k < n_pts; k++) {
			if (k != center_idx) non_center_sum += w[k];
		}
		w[center_idx] = 1.0 - non_center_sum;
	}

	// Defensive renormalisation (guards against floating-point drift and
	// against designs where the center might be missing).
	double sum_w = 0;
	for (double wk : w) sum_w += wk;
	if (sum_w > 0) {
		for (intptr_t k = 0; k < n_pts; k++) w[k] /= sum_w;
	}
	return w;
}
// returns β̂(θ), Σ_β(θ), and the neg-log-posterior.
static GridPointResult eval_gaussian_grid_point(
	const Eigen::VectorXd &theta,
	const GaussianCholObjective &obj,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p, intptr_t n_chol,
	const PriorSpec *priors,
	const Array<String> *coef_names,
	const Eigen::VectorXd *off_ptr = nullptr)
{
	GridPointResult gpr;

	// ── Unpack θ → D_inv, sigma2 ─────────────────────────────────
	std::vector<Eigen::MatrixXd> D_inv(lay.G);
	std::vector<double> log_det_Dg(lay.G);
	intptr_t chol_pos = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
		D_inv[g] = cholesky_to_precision(L);
		log_det_Dg[g] = log_det_D(theta.data() + chol_pos, qg);
		chol_pos += n_chol_params(qg);
	}
	double sigma2 = std::exp(2.0 * theta[n_chol]);

	// ── Solve Henderson → β̂, û ───────────────────────────────────
	auto inner = solve_gaussian_henderson(D_inv, log_det_Dg, sigma2,
	                                       Xm, ym, lay, n, p,
	                                       priors, coef_names, off_ptr);
	gpr.beta = inner.beta;

	// ── Neg-log-posterior (reuse objective which includes all prior terms) ──
	gpr.neg_log_posterior = obj.eval(theta);

	// ── Conditional Var(β|θ) from Henderson inverse ──────────────
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;
	intptr_t sdim = p + J;
	double inv_sigma2 = 1.0 / sigma2;

	Eigen::MatrixXd C = Eigen::MatrixXd::Zero(sdim, sdim);

	// X'WX (Gaussian: W = (1/σ²)I)
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t j1 = 0; j1 < p; j1++)
		{
			double wx = Xm(i, j1) * inv_sigma2;
			for (intptr_t j2 = j1; j2 < p; j2++)
				C(j1, j2) += wx * Xm(i, j2);
		}
	}
	for (intptr_t j1 = 0; j1 < p; j1++)
		for (intptr_t j2 = j1 + 1; j2 < p; j2++)
			C(j2, j1) = C(j1, j2);

	// Prior precision on fixed effects
	if (priors && coef_names)
	{
		for (intptr_t j = 0; j < p; j++)
		{
			const auto &pr = priors->prior_for((*coef_names)[j + 1]);
			C(j, j) += 1.0 / (pr.sd * pr.sd);
		}
	}

	// D⁻¹ blocks
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = p + lay.offset[g] + j * qg;
			for (intptr_t t1 = 0; t1 < qg; t1++)
				for (intptr_t t2 = 0; t2 < qg; t2++)
					C(base + t1, base + t2) += D_inv[g](t1, t2);
		}
	}

	// X'WZ, Z'WX, Z'WZ
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < G; g1++)
		{
			intptr_t j1 = (*lay.group_indices[g1])[i];
			intptr_t q1 = lay.q[g1];
			intptr_t base1 = p + lay.offset[g1] + j1 * q1;

			for (intptr_t t = 0; t < q1; t++)
			{
				double wz = inv_sigma2 * lay.Z(g1, i, t);
				for (intptr_t j = 0; j < p; j++) {
					double val = Xm(i, j) * wz;
					C(j, base1 + t) += val;
					C(base1 + t, j) += val;
				}
			}

			for (intptr_t t1 = 0; t1 < q1; t1++)
			{
				double wz1 = inv_sigma2 * lay.Z(g1, i, t1);
				for (intptr_t t2 = t1; t2 < q1; t2++) {
					double val = wz1 * lay.Z(g1, i, t2);
					C(base1 + t1, base1 + t2) += val;
					if (t1 != t2) C(base1 + t2, base1 + t1) += val;
				}
			}

			for (intptr_t g2 = g1 + 1; g2 < G; g2++)
			{
				intptr_t j2 = (*lay.group_indices[g2])[i];
				intptr_t q2 = lay.q[g2];
				intptr_t base2 = p + lay.offset[g2] + j2 * q2;

				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = inv_sigma2 * lay.Z(g1, i, t1);
					for (intptr_t t2 = 0; t2 < q2; t2++) {
						double val = wz1 * lay.Z(g2, i, t2);
						C(base1 + t1, base2 + t2) += val;
						C(base2 + t2, base1 + t1) += val;
					}
				}
			}
		}
	}

	Eigen::LDLT<Eigen::MatrixXd> ldlt(C);
	Eigen::MatrixXd Cinv = ldlt.solve(Eigen::MatrixXd::Identity(sdim, sdim));
	gpr.vcov_beta = Cinv.topLeftCorner(p, p);

	// Gaussian ℓ'''(η) = 0 ⇒ no simplified Laplace correction.
	gpr.d3 = Eigen::VectorXd::Zero(p);

	return gpr;
}


// Evaluate a non-Gaussian GLMM at a single grid point θ:
// returns β̂(θ), Σ_β(θ), and the neg-log-posterior.
//
// θ layout: (chol_1, ..., chol_G, [log disp...])
// where n_disp = 0 (binomial/Poisson), 1 (NB/beta), or 2 (Student t).
//
// The neg-log-posterior is computed from the PIRLS result + prior terms
// to avoid a redundant PIRLS solve (which PirlsObjective::eval would do).
//
// u_init: if size == lay.J_total, used as warm start for the inner PIRLS
// solver. Cold-starting from u=0 on binomial GLMMs with strong random
// effects can OVERSHOOT to a non-mode point (see comment at the
// declaration of phase2_warm_u in the public mixed_model entry point);
// passing the joint optimizer's converged û here keeps PIRLS at the
// true conditional mode.
static GridPointResult eval_pirls_grid_point(
	const Eigen::VectorXd &theta,
	const Family &fam,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p, intptr_t n_chol,
	const Eigen::VectorXd &beta_init,
	const PriorSpec *priors,
	const Array<String> *coef_names,
	const Eigen::VectorXd *off_ptr = nullptr,
	const Eigen::VectorXd &u_init = Eigen::VectorXd())
{
	GridPointResult gpr;
	intptr_t G = lay.G;

	// ── Unpack θ → D_inv ─────────────────────────────────────────
	std::vector<Eigen::MatrixXd> D_inv(G);
	std::vector<double> log_det_Dg(G);
	intptr_t chol_pos = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
		D_inv[g] = cholesky_to_precision(L);
		log_det_Dg[g] = log_det_D(theta.data() + chol_pos, qg);
		chol_pos += n_chol_params(qg);
	}

	// ── Create appropriate family with dispersion from θ ─────────
	ProfiledResult res;
	if (fam.name == "negbin")
	{
		double theta_nb = std::exp(theta[n_chol]);
		auto fam_nb = Family::negbin(theta_nb);
		res = solve_pirls(D_inv, log_det_Dg, fam_nb, Xm, ym, lay, n, p,
		                   beta_init, u_init, priors, coef_names, off_ptr);
	}
	else if (fam.name == "beta")
	{
		double phi_beta = std::exp(theta[n_chol]);
		auto fam_beta = Family::beta(phi_beta);
		res = solve_pirls(D_inv, log_det_Dg, fam_beta, Xm, ym, lay, n, p,
		                   beta_init, u_init, priors, coef_names, off_ptr);
	}
	else if (fam.name == "student")
	{
		double sigma_t = std::exp(theta[n_chol]);
		double nu_t = std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0);
		auto fam_t = Family::student(sigma_t, nu_t);
		res = solve_pirls(D_inv, log_det_Dg, fam_t, Xm, ym, lay, n, p,
		                   beta_init, u_init, priors, coef_names, off_ptr);
	}
	else
	{
		// Binomial, Poisson: no extra dispersion parameters.
		res = solve_pirls(D_inv, log_det_Dg, fam, Xm, ym, lay, n, p,
		                   beta_init, u_init, priors, coef_names, off_ptr);
	}

	gpr.beta = res.beta;

	// ── Neg-log-posterior ────────────────────────────────────────
	// res.laplace_nll already includes -log p(β̂) from solve_pirls.
	// Add variance-component priors (same as PirlsObjective::eval).
	double nll = res.laplace_nll;
	if (priors)
	{
		std::vector<Eigen::MatrixXd> D_cov(G);
		for (intptr_t g = 0; g < G; g++)
			D_cov[g] = D_inv[g].inverse();
		nll -= variance_prior_log_density(D_cov, *priors, lay);
	}

	// ── Student-t: replace IRLS log_det_Huu with observed-Hessian ──
	//
	// solve_pirls computes the Laplace correction ½ log|H_uu| using
	// `fam.custom_weights`, which for Student-t returns the IRLS form
	//
	//   w_PIRLS = (ν+1) / (νσ² + r²)
	//
	// Despite its name, this is neither the Fisher information nor the
	// observed Hessian — it's the score-equation linearization weight
	// PIRLS iterates with, chosen so that w·(z − μ) reproduces the score
	// for an identity link. For canonical-link exponential-family models
	// (Gaussian, Poisson with log link, NB with log link, binomial with
	// logit link), w_PIRLS coincides with the observed Hessian at the
	// score-equation root, so the Laplace approximation is correct as
	// computed by solve_pirls. For Student-t — which is not an exponential
	// family — w_PIRLS differs from the observed Hessian
	//
	//   w_obs = (ν+1)(νσ² − r²) / (νσ² + r²)²
	//
	// at every observation, even at the mode. The Laplace approximation
	// to ∫ p(y, u | β, θ) du is the **observed Hessian** form by the
	// standard derivation (second-order Taylor expansion of -log p around
	// û). Using w_PIRLS in its place biases log p(y | θ) by an amount
	// that varies with ν: the bias is most negative at small ν (more
	// outlier mass with |r| > σ√ν makes w_obs and w_PIRLS diverge most),
	// so the marginal posterior of ν is systematically biased UPWARD.
	// Empirically: ~8% upward at moderate complexity, ~20% on crossed
	// random-effects designs (validated against brms HMC).
	//
	// We therefore replace the IRLS Laplace term with the observed-Hessian
	// (hybrid: observed where PD, Fisher fallback otherwise) form at every
	// CCD grid point. This mirrors the post-fit correction at the bottom
	// of mixed_model() that was previously applied only to model.loglik —
	// extending it to the integrand makes the marginal posterior of θ
	// internally consistent with the reported point estimate.
	//
	// Cost: two extra log-det evaluations per grid point. For typical
	// phonetic data (J ≲ 100, n_grid ≲ 100), this is microseconds.
	if (fam.name == "student" && lay.J_total > 0)
	{
		double sigma_t = std::exp(theta[n_chol]);
		double nu_t    = std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0);
		double nu_sigma2 = nu_t * sigma_t * sigma_t;

		// Recompute IRLS log_det at the converged μ — this is what's
		// implicitly included in res.laplace_nll.
		Eigen::VectorXd w_irls(n);
		for (intptr_t i = 0; i < n; i++) {
			double r = ym[i] - res.mu[i];
			w_irls[i] = (nu_t + 1.0) / (nu_sigma2 + r * r);
		}
		double log_det_irls = full_log_det_H(w_irls, D_inv, lay, n);

		LaplaceMethod method;
		double log_det_hybrid = student_full_log_det_H_hybrid(
		    ym, res.mu, sigma_t, nu_t, D_inv, lay, n, method);

		// Replace the Laplace term: -½ log_det_irls + ½ log_det_hybrid
		nll += 0.5 * (log_det_hybrid - log_det_irls);
	}

	gpr.neg_log_posterior = nll;

	// ── Conditional Var(β|θ) from Henderson inverse ──────────────
	// Working weights from the converged μ̂.
	Eigen::VectorXd w_gp(n);
	Family fam_gp = fam;
	if (fam.name == "negbin")
		fam_gp = Family::negbin(std::exp(theta[n_chol]));
	else if (fam.name == "beta")
		fam_gp = Family::beta(std::exp(theta[n_chol]));
	else if (fam.name == "student")
		fam_gp = Family::student(std::exp(theta[n_chol]),
		                          std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0));

	if (fam.name == "beta")
	{
		// Beta: expected Fisher info weight for Var(β|θ).
		// I_ββ = φ² · X' diag(w_i) X,
		//   w_i = [μ_i(1-μ_i)]² · [ψ'(μ_i φ) + ψ'((1-μ_i) φ)]
		// The IRLS quasi-weight μ(1-μ)(1+φ) used during estimation
		// under-estimates I_ββ when μ is far from 0.5 (Ferrari &
		// Cribari-Neto 2004, eq. 5). Using the true Fisher info weight
		// here aligns Var(β|θ) with glmmTMB's vcov block.
		double phi_b = std::exp(theta[n_chol]);
		double phi_sq = phi_b * phi_b;
		for (intptr_t i = 0; i < n; i++)
		{
			double mi = std::clamp(res.mu[i], 1e-10, 1.0 - 1e-10);
			double mu1m = mi * (1.0 - mi);
			double tri = boost::math::trigamma(mi * phi_b)
			             + boost::math::trigamma((1.0 - mi) * phi_b);
			w_gp[i] = phi_sq * mu1m * mu1m * tri;
		}
	}
	else if (fam_gp.custom_weights)
	{
		w_gp = fam_gp.custom_weights(ym, res.mu);
	}
	else
	{
		Eigen::VectorXd V_gp = fam_gp.variance(res.mu);
		Eigen::VectorXd me_gp = fam_gp.mu_eta(res.mu);
		for (intptr_t i = 0; i < n; i++)
		{
			double v = std::max(V_gp[i], 1e-10);
			double d = std::max(me_gp[i], 1e-10);
			w_gp[i] = d * d / v;
		}
	}

	intptr_t J = lay.J_total;
	intptr_t sdim = p + J;
	Eigen::MatrixXd C = Eigen::MatrixXd::Zero(sdim, sdim);

	// X'WX
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t j1 = 0; j1 < p; j1++)
		{
			double wx = Xm(i, j1) * w_gp[i];
			for (intptr_t j2 = j1; j2 < p; j2++)
				C(j1, j2) += wx * Xm(i, j2);
		}
	}
	for (intptr_t j1 = 0; j1 < p; j1++)
		for (intptr_t j2 = j1 + 1; j2 < p; j2++)
			C(j2, j1) = C(j1, j2);

	// Prior precision on fixed effects
	if (priors && coef_names)
	{
		for (intptr_t j = 0; j < p; j++)
		{
			const auto &pr = priors->prior_for((*coef_names)[j + 1]);
			C(j, j) += 1.0 / (pr.sd * pr.sd);
		}
	}

	// D⁻¹ blocks
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = p + lay.offset[g] + j * qg;
			for (intptr_t t1 = 0; t1 < qg; t1++)
				for (intptr_t t2 = 0; t2 < qg; t2++)
					C(base + t1, base + t2) += D_inv[g](t1, t2);
		}
	}

	// X'WZ, Z'WX, Z'WZ
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < G; g1++)
		{
			intptr_t j1 = (*lay.group_indices[g1])[i];
			intptr_t q1 = lay.q[g1];
			intptr_t base1 = p + lay.offset[g1] + j1 * q1;

			for (intptr_t t = 0; t < q1; t++)
			{
				double wz = w_gp[i] * lay.Z(g1, i, t);
				for (intptr_t j = 0; j < p; j++) {
					double val = Xm(i, j) * wz;
					C(j, base1 + t) += val;
					C(base1 + t, j) += val;
				}
			}

			for (intptr_t t1 = 0; t1 < q1; t1++)
			{
				double wz1 = w_gp[i] * lay.Z(g1, i, t1);
				for (intptr_t t2 = t1; t2 < q1; t2++) {
					double val = wz1 * lay.Z(g1, i, t2);
					C(base1 + t1, base1 + t2) += val;
					if (t1 != t2) C(base1 + t2, base1 + t1) += val;
				}
			}

			for (intptr_t g2 = g1 + 1; g2 < G; g2++)
			{
				intptr_t j2 = (*lay.group_indices[g2])[i];
				intptr_t q2 = lay.q[g2];
				intptr_t base2 = p + lay.offset[g2] + j2 * q2;

				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = w_gp[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = 0; t2 < q2; t2++) {
						double val = wz1 * lay.Z(g2, i, t2);
						C(base1 + t1, base2 + t2) += val;
						C(base2 + t2, base1 + t1) += val;
					}
				}
			}
		}
	}

	Eigen::LDLT<Eigen::MatrixXd> ldlt(C);
	Eigen::MatrixXd Cinv = ldlt.solve(Eigen::MatrixXd::Identity(sdim, sdim));
	gpr.vcov_beta = Cinv.topLeftCorner(p, p);

	// ── Save working weights / response for conditional-u WAIC ──────
	//
	// The PIRLS-converged weights and a freshly computed working
	// response z_gp = η̂_no_offset + (y − μ̂) / μ'(η̂) define the local
	// Gaussian Laplace approximation to the conditional u | β, θ, y.
	// compute_grid_waic uses these to build M_k = Z'W_k Z + D_k⁻¹ and
	// sample u ~ N(û(β^(s)), M_k⁻¹) per posterior draw, capturing the
	// β ↔ u ridge cancellation that fixed-BLUP WAIC misses.
	//
	// Weight choice: we need the *IRLS* weight (what solve_pirls
	// iterated with), because the Laplace approximation to u | β, θ
	// is centered on PIRLS's converged mode.  For binomial/Poisson/
	// negbin/Student, the w_gp above is already the IRLS weight
	// (d²/v from the default branch at lines ~2489-2503 or family-
	// specific via custom_weights).  For the **beta** family, w_gp
	// uses Fisher information weights to match glmmTMB's vcov block
	// (see lines ~2469-2487) — those are NOT the IRLS weights, so we
	// recompute them here specifically for the WAIC u-sampling path.
	// The β vcov stored in gpr.vcov_beta remains unchanged.
	//
	// η̂ is reconstructed from (β̂, û) to match PIRLS's offset-excluded
	// convention (see solve_pirls for why offset is excluded here).

	if (fam.name == "beta")
	{
		// Beta IRLS weight: w_i = μ_i(1−μ_i)(1+φ) for logit link.
		// This matches solve_pirls's d²/v calculation for beta
		// (d = μ(1−μ), v = μ(1−μ)/(1+φ)).
		double phi_b = std::exp(theta[n_chol]);
		double one_plus_phi = 1.0 + phi_b;
		gpr.working_weights = Eigen::VectorXd(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double mi = std::clamp(res.mu[i], 1e-10, 1.0 - 1e-10);
			gpr.working_weights[i] = mi * (1.0 - mi) * one_plus_phi;
		}
	}
	else
	{
		gpr.working_weights = w_gp;
	}

	// ── Working response for IRLS / Fisher Laplace u-sampling ──────
	//
	// `eta_hat` here is X β̂ + Z û WITHOUT offset, matching solve_pirls's
	// internal convention. compute_grid_waic adds the offset back when
	// reconstructing η at sampling time, so the working response stored
	// here must also be on the no-offset scale.
	//
	// Same `eta_hat` is reused below for the Student-t Hessian-form
	// pseudo-response `z_L` so we can avoid recomputing the X β̂ + Z û
	// linear combination.
	Eigen::VectorXd eta_hat = Xm * res.beta;
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++)
				eta_hat[i] += lay.Z(g, i, t) * res.u[base + t];
		}
	}

	{
		Eigen::VectorXd me_z = fam_gp.mu_eta(res.mu);
		gpr.working_response = Eigen::VectorXd(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double d = std::max(me_z[i], 1e-10);
			gpr.working_response[i] = eta_hat[i] + (ym[i] - res.mu[i]) / d;
		}
	}

	// ── Student-t: observed-Hessian Laplace data for WAIC u-sampling ──
	//
	// `working_weights` above are the Fisher-information / IRLS form
	//   w_F_i = (ν+1) / (νσ² + r_i²)
	// always non-negative — that's what PIRLS iterates with. But the
	// **observed Hessian** of -log p(y|μ) at the converged mode is
	//   w_L_i = (ν+1)(νσ² − r_i²) / (νσ² + r_i²)²
	// which is the form glmmTMB / TMB use for the Laplace correction
	// at convergence (see also student_full_log_det_H_hybrid).
	//
	// For |r_i| < σ√ν we have w_L_i > 0; for |r_i| > σ√ν, w_L_i is
	// negative. Crucially, `M = Z' diag(w_L) Z + D_k⁻¹` can still be
	// PD with a few negative w_L_i as long as D_k⁻¹ dominates the
	// negative contribution. We therefore populate these fields
	// *unconditionally* for Student-t and let compute_grid_waic do
	// the runtime PD check via Cholesky. The fraction of observations
	// with |r| > σ√ν is non-trivial at finite ν (≈ 10% at ν=4), so
	// the obs-level non-negativity check would defeat the purpose of
	// this fix on typical Student-t fits.
	//
	// The two forms agree at r=0 and as ν → ∞ (Gaussian limit).
	// For typical residuals (small r), w_L < w_F, so the Fisher form
	// **overestimates** the posterior precision of u | β, θ, y, leaving
	// the WAIC u-sampling concentration too tight around û_k. The
	// observable consequence is too-low p_waic and too-low WAIC, which
	// is exactly the validation gap vs brms HMC documented for the F2
	// random-effects fit.
	//
	// **Hessian pseudo-response.** We need z_L such that
	//     M_L⁻¹ Z' W_L (z_L − X β̂_k) = û_k
	// at the grid point's mode. The closed form
	//     z_L_i = (Xβ̂_k + Zû_k)_i + r̂_i · (νσ² + r²) / (νσ² − r²)
	// gives Z' W_L (z_L − μ̂_k) = Z' W_F r̂ = D⁻¹ û_k (the score
	// equation, satisfied at convergence), recovering û_k as required.
	// On the no-offset scale matching working_response.
	//
	// Numerical robustness: the formula divides by (νσ² − r²) which
	// can be near zero for observations exactly at |r_i| = σ√ν. The
	// product w_L · z_L stays bounded in the limit (it cancels to
	// w_F · r̂_i + w_L · (Xβ̂+Zû)_i), but exact division by zero would
	// produce Inf in IEEE 754. We floor |denom| at a tiny epsilon to
	// keep z_L finite; this preserves the product to within rounding
	// because w_L → 0 in lockstep with denom.

	if (fam.name == "student")
	{
		double sigma_t = std::exp(theta[n_chol]);
		double nu_t    = std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0);
		double nu_sigma2 = nu_t * sigma_t * sigma_t;

		// Defensive epsilon to prevent exact division by zero in the
		// closed-form z_L when r² == νσ². See block comment above for
		// why this preserves the limit value of w_L · z_L.
		const double denom_floor = std::max(nu_sigma2, 1.0) * 1e-300;

		gpr.hessian_weights  = Eigen::VectorXd(n);
		gpr.hessian_response = Eigen::VectorXd(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double r    = ym[i] - res.mu[i];
			double r2   = r * r;
			double sum  = nu_sigma2 + r2;
			double diff = nu_sigma2 - r2;
			if (std::abs(diff) < denom_floor)
				diff = (diff >= 0 ? 1.0 : -1.0) * denom_floor;
			gpr.hessian_weights[i]  = (nu_t + 1.0) * diff / (sum * sum);
			gpr.hessian_response[i] = eta_hat[i] + r * sum / diff;
		}
	}

	// ── Simplified Laplace correction: third derivative d₃ ──────
	//
	// d3_j = Σ_i X³_{ij} × ℓ'''(η̂_i)
	//
	// where ℓ'''(η) is the third derivative of the per-observation
	// log-likelihood w.r.t. η.  For non-Gaussian families this is
	// generally nonzero and captures the skewness of the posterior.
	//
	// Reference: Rue, Martino & Chopin (2009), Section 3.2.2.

	gpr.d3 = Eigen::VectorXd::Zero(p);
	if (fam_gp.loglik_d3)
	{
		// Compute η̂ = g(μ̂) via the link function.
		Eigen::VectorXd eta_hat = fam_gp.link(res.mu);
		Eigen::VectorXd ell3 = fam_gp.loglik_d3(ym, res.mu, eta_hat);

		for (intptr_t j = 0; j < p; j++)
		{
			double sum = 0;
			for (intptr_t i = 0; i < n; i++) {
				double xij = Xm(i, j);
				sum += xij * xij * xij * ell3[i];
			}
			gpr.d3[j] = sum;
		}
	}

	return gpr;
}


// =====================================================================
// WAIC helpers for grid-integrated models
// =====================================================================

// Number of WAIC posterior draws.
static constexpr int WAIC_S = 1000;
static constexpr unsigned int WAIC_SEED = 12345;

// Compute the random-effects offset zu_i = Σ_g Σ_t Z_g(i,t) * u_hat[g, level_g(i), t]
// from the Model's conditional modes (BLUPs at the mode θ*).
// Returns a vector of length n.
static std::vector<double> compute_re_offset(const Model &model, intptr_t n)
{
	std::vector<double> zu(n, 0.0);

	for (intptr_t gi = 1; gi <= model.random_effects.size(); gi++)
	{
		auto &re = model.random_effects[gi];
		intptr_t q = re.nterms;

		if (re.Z_design.empty() || re.conditional_modes.empty() || re.indices.empty())
			continue;

		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t j = re.indices[i];   // level index for observation i
			for (intptr_t t = 0; t < q; t++)
			{
				double z_val = re.Z_design[i * q + t];
				double u_val = re.conditional_modes[j * q + t + 1]; // 1-based Array
				zu[i] += z_val * u_val;
			}
		}
	}

	return zu;
}


// Populate GridSummary and compute WAIC for a grid-integrated model.
//
// This function:
//   1. Stores the per-grid-point results in model.grid_summary (for PPC later)
//   2. Draws S posterior samples of (β, θ) from the mixture
//   3. Samples the random effects u either:
//        • from their Gaussian closed-form conditional posterior
//          N(E[u|β,θ,y], Cov[u|β,θ,y]) per draw (Gaussian family
//          with lay_cond_u != nullptr) — captures the β↔u ridge
//          cancellation and matches brms's default waic; or
//        • from the Laplace-approximate conditional posterior (non-
//          Gaussian family with lay_cond_u != nullptr AND every grid
//          point has converged working weights/response populated).
//          Uses the PIRLS-linearized quasi-Gaussian at each grid point
//          mode: u | β, θ ≈ N(M_k⁻¹ Z' W_k (z_k − X β), M_k⁻¹) where
//          M_k = Z' W_k Z + D_k⁻¹, W_k is the diagonal of converged
//          IRLS weights, and z_k is the converged working response.
//          The likelihood is then evaluated on the actual family
//          density (not the quasi-Gaussian), so the approximation is
//          only in the u-sampling distribution.  Same Laplace quality
//          as the overall fit; or
//        • held fixed at the mode BLUPs (fallback when lay_cond_u is
//          null, or non-Gaussian without working weights).
//   4. Evaluates pointwise log-likelihood using dispersion params from θ^(s)
//   5. Calls compute_waic_from_loglik to populate model.waic/p_waic/lppd/se_waic
//
// Parameters:
//   results, w: the per-grid-point β̂/Σ and normalised weights
//   theta_star, T, z_points: for reconstructing θ_k = θ* + T z_k
//   n_chol: number of Cholesky parameters (needed to find dispersion in θ)
//   linkinv_scalar: scalar inverse link function (identity for Gaussian, exp for log, etc.)
//   disp_from_theta: extracts dispersion params from a θ vector (family-dependent)
//   lay_cond_u: if non-null, enables conditional u-sampling (see (3) above).
//       Must be the GroupLayout used during grid integration.
//
// Historical note: an earlier version drew u from its *prior* N(0, D_k) per
// posterior sample, on the reasoning that this would match DHARMa's
// unconditional simulation. That was a conceptual error — DHARMa uses the
// prior because its goal is to simulate y for new groups, but WAIC needs u
// from its *posterior* because y_obs came from the observed groups. Drawing
// from the prior produces log-likelihoods disconnected from the group labels
// and catastrophically inflates p_waic.
//
static void compute_grid_waic(
	Model &model,
	const std::vector<GridPointResult> &results,
	const std::vector<double> &w,
	const std::vector<Eigen::VectorXd> &z_points,
	const Eigen::VectorXd &theta_star,
	const Eigen::MatrixXd &T,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	intptr_t n, intptr_t p,
	intptr_t n_chol,
	std::function<double(double)> linkinv_scalar,
	std::function<void(const Eigen::VectorXd &theta, double *disp)> disp_from_theta,
	const Eigen::VectorXd *off_ptr = nullptr,
	const GroupLayout *lay_cond_u = nullptr)
{
	intptr_t n_grid = (intptr_t)results.size();
	intptr_t d = theta_star.size();

	// ── 1. Populate GridSummary ──────────────────────────────────────

	GridSummary gs;
	gs.n_points = (int)n_grid;
	gs.n_beta   = (int)p;
	gs.n_theta  = (int)d;
	gs.weights.resize(n_grid);
	gs.beta.resize(n_grid * p);
	gs.vcov_diag.resize(n_grid * p);
	gs.theta.resize(n_grid * d);

	for (intptr_t k = 0; k < n_grid; k++)
	{
		gs.weights[k] = w[k];

		for (intptr_t j = 0; j < p; j++)
		{
			gs.beta[k * p + j] = results[k].beta[j];
			gs.vcov_diag[k * p + j] = results[k].vcov_beta(j, j);
		}

		Eigen::VectorXd theta_k = theta_star + T * z_points[k];
		for (intptr_t j = 0; j < d; j++)
			gs.theta[k * d + j] = theta_k[j];
	}

	model.grid_summary = std::move(gs);

	// ── 2. RE offset setup: mode BLUPs (fallback) and conditional-u ─
	//
	// zu_blup is always computed as a robust fallback used when:
	//   • conditional_u is off (non-Gaussian families or no layout passed), or
	//   • the per-grid-point u-system factorization fails for grid point k.
	//
	// When conditional_u is on (Gaussian + lay_cond_u != nullptr), we also
	// precompute per grid point:
	//   M_k = Z'Z / σ²_k + D_k⁻¹    (J × J, SPD)
	//   L_k L_kᵀ = M_k              (Cholesky)
	//   aux_y[k] = M_k⁻¹ · Z'y / σ²_k   (length J)
	//   aux_X[k] = M_k⁻¹ · Z'X / σ²_k   (J × p)
	// Per draw:
	//   u_mean = aux_y[k] − aux_X[k] · β^(s)
	//   u^(s)  = u_mean + (L_kᵀ)⁻¹ · ε,   ε ~ N(0, I_J)
	// The cross-block (Z'X) and Z'Z products are design-only and the same
	// across grid points, so they are computed once.

	std::vector<double> zu_blup = compute_re_offset(model, n);

	// Enable conditional u-sampling when the layout was passed AND either
	// (a) the family is Gaussian (closed-form conditional), or (b) every
	// grid point has working weights and response populated (PIRLS-Laplace
	// conditional).  The fallback is the mode-BLUP zu_blup above.
	bool conditional_u = (lay_cond_u != nullptr);
	bool is_gaussian_path = (model.family == "gaussian");
	if (conditional_u && !is_gaussian_path)
	{
		for (intptr_t k = 0; k < n_grid; k++)
		{
			if (results[k].working_weights.size() != n
			 || results[k].working_response.size() != n)
			{
				conditional_u = false;
				break;
			}
		}
	}

	std::vector<Eigen::LLT<Eigen::MatrixXd>> u_llt;
	std::vector<Eigen::VectorXd> aux_y;
	std::vector<Eigen::MatrixXd> aux_X;
	std::vector<bool> u_llt_ok;
	intptr_t J_cond = 0;

	if (conditional_u && is_gaussian_path)
	{
		J_cond = lay_cond_u->J_total;
		u_llt.resize(n_grid);
		aux_y.resize(n_grid);
		aux_X.resize(n_grid);
		u_llt_ok.assign(n_grid, false);

		// Z' (y − offset)    (length J)
		Eigen::VectorXd Zty = Eigen::VectorXd::Zero(J_cond);
		for (intptr_t i = 0; i < n; i++) {
			double y_eff = ym[i];
			if (off_ptr) y_eff -= (*off_ptr)[i];
			for (intptr_t g = 0; g < lay_cond_u->G; g++) {
				intptr_t jlev = (*lay_cond_u->group_indices[g])[i];
				intptr_t qg = lay_cond_u->q[g];
				intptr_t base = lay_cond_u->offset[g] + jlev * qg;
				for (intptr_t t = 0; t < qg; t++)
					Zty[base + t] += lay_cond_u->Z(g, i, t) * y_eff;
			}
		}

		// Z' X              (J × p)
		Eigen::MatrixXd ZtX = Eigen::MatrixXd::Zero(J_cond, p);
		for (intptr_t i = 0; i < n; i++) {
			for (intptr_t g = 0; g < lay_cond_u->G; g++) {
				intptr_t jlev = (*lay_cond_u->group_indices[g])[i];
				intptr_t qg = lay_cond_u->q[g];
				intptr_t base = lay_cond_u->offset[g] + jlev * qg;
				for (intptr_t t = 0; t < qg; t++) {
					double zv = lay_cond_u->Z(g, i, t);
					for (intptr_t jj = 0; jj < p; jj++)
						ZtX(base + t, jj) += zv * Xm(i, jj);
				}
			}
		}

		// Z' Z              (J × J)
		Eigen::MatrixXd ZtZ = Eigen::MatrixXd::Zero(J_cond, J_cond);
		for (intptr_t i = 0; i < n; i++) {
			for (intptr_t g1 = 0; g1 < lay_cond_u->G; g1++) {
				intptr_t j1lev = (*lay_cond_u->group_indices[g1])[i];
				intptr_t q1 = lay_cond_u->q[g1];
				intptr_t base1 = lay_cond_u->offset[g1] + j1lev * q1;
				for (intptr_t t1 = 0; t1 < q1; t1++) {
					double z1 = lay_cond_u->Z(g1, i, t1);
					// Same-group block
					for (intptr_t t2 = 0; t2 < q1; t2++)
						ZtZ(base1 + t1, base1 + t2) += z1 * lay_cond_u->Z(g1, i, t2);
					// Cross-group blocks
					for (intptr_t g2 = g1 + 1; g2 < lay_cond_u->G; g2++) {
						intptr_t j2lev = (*lay_cond_u->group_indices[g2])[i];
						intptr_t q2 = lay_cond_u->q[g2];
						intptr_t base2 = lay_cond_u->offset[g2] + j2lev * q2;
						for (intptr_t t2 = 0; t2 < q2; t2++) {
							double z2 = lay_cond_u->Z(g2, i, t2);
							ZtZ(base1 + t1, base2 + t2) += z1 * z2;
							ZtZ(base2 + t2, base1 + t1) += z1 * z2;
						}
					}
				}
			}
		}

		// Per grid point: factorize M_k = Z'Z / σ²_k + D_k⁻¹
		for (intptr_t k = 0; k < n_grid; k++) {
			Eigen::VectorXd theta_k = theta_star + T * z_points[k];
			double sigma2_k = std::exp(2.0 * theta_k[n_chol]);
			double inv_s2 = 1.0 / sigma2_k;

			Eigen::MatrixXd M = ZtZ * inv_s2;

			// Add block-diagonal D_k⁻¹ from the Cholesky slice of θ_k
			intptr_t cp = 0;
			for (intptr_t g = 0; g < lay_cond_u->G; g++) {
				intptr_t qg = lay_cond_u->q[g];
				Eigen::MatrixXd L = unpack_cholesky(theta_k.data() + cp, qg);
				Eigen::MatrixXd D_inv_g = cholesky_to_precision(L);
				for (intptr_t jlev = 0; jlev < lay_cond_u->J[g]; jlev++) {
					intptr_t base = lay_cond_u->offset[g] + jlev * qg;
					for (intptr_t t1 = 0; t1 < qg; t1++)
						for (intptr_t t2 = 0; t2 < qg; t2++)
							M(base + t1, base + t2) += D_inv_g(t1, t2);
				}
				cp += n_chol_params(qg);
			}

			u_llt[k].compute(M);
			u_llt_ok[k] = (u_llt[k].info() == Eigen::Success);
			if (u_llt_ok[k]) {
				aux_y[k] = u_llt[k].solve(Zty * inv_s2);
				aux_X[k] = u_llt[k].solve(ZtX * inv_s2);
			}
		}
	}
	else if (conditional_u /* && non-Gaussian */)
	{
		// PIRLS-Laplace conditional: for each grid point k the weights and
		// working response vary with k (unlike the Gaussian case where only
		// 1/σ²_k scales common Z'Z/Z'y/Z'X matrices). We rebuild the weighted
		// products per grid point. Cost is O(n(p+q)²) per grid point, trivial
		// compared to the PIRLS solve that already produced the mode.
		//
		// Sampling identity:
		//   u | β, θ, y  ≈  N(û_k(β), M_k⁻¹)
		// with û_k(β) = M_k⁻¹ · Z' W_k · (z_k − X β)
		//    = aux_y[k] − aux_X[k] · β
		// where
		//   aux_y[k] = M_k⁻¹ · Z' W_k z_k
		//   aux_X[k] = M_k⁻¹ · Z' W_k X
		//   M_k = Z' W_k Z + D_k⁻¹
		//
		// **Weight choice (Student-t).** For Student-t, the IRLS / Fisher
		// weights w_F = (ν+1)/(νσ²+r²) are always non-negative and what
		// PIRLS iterates with, but they overestimate posterior precision
		// vs the **observed Hessian** form
		//   w_L = (ν+1)(νσ²−r²) / (νσ²+r²)²
		// at the mode. Using w_F here shrinks u^(s) too tightly toward û_k
		// and inflates lppd (concretely: a ~32-unit WAIC gap against brms
		// HMC on the F2 random-effects fit).
		//
		// We try the observed-Hessian form first (using hessian_weights /
		// hessian_response stored at each grid point), falling back to
		// the Fisher form per grid point when the LDLT factorization of
		// the resulting M_L = Z'W_L Z + D_k⁻¹ fails. Individual
		// observations with |r| > σ√ν contribute w_L < 0; M_L can still
		// be PD if D_k⁻¹ dominates the negative pile, otherwise Cholesky
		// detects indefiniteness and we fall through. Non-Student
		// families have hessian_* empty by construction and always use
		// the Fisher form, preserving existing behaviour.

		J_cond = lay_cond_u->J_total;
		u_llt.resize(n_grid);
		aux_y.resize(n_grid);
		aux_X.resize(n_grid);
		u_llt_ok.assign(n_grid, false);

		// Diagnostic: count how many grid points used Hessian vs Fisher
		// when family is Student-t, exposed via PHON_DIAG_STUDENT_WAIC=1.
		// hessian_used + fisher_used + (failed both) == n_grid; the third
		// bucket is rare (both factorizations failing means the grid
		// point was already invalid).
		int hessian_used = 0;
		int fisher_used = 0;

		// Inner builder: assemble Z'Wz, Z'WX, M = Z'WZ + D_k⁻¹ at grid
		// point k from the supplied weights/pseudo-response and try the
		// LLT factorization. On success, populates u_llt[k] / aux_y[k] /
		// aux_X[k] and returns true.
		auto try_build_grid_point = [&](intptr_t k,
		                                 const Eigen::VectorXd &w_k,
		                                 const Eigen::VectorXd &z_k) -> bool
		{
			Eigen::VectorXd ZtWz = Eigen::VectorXd::Zero(J_cond);
			Eigen::MatrixXd ZtWX = Eigen::MatrixXd::Zero(J_cond, p);
			Eigen::MatrixXd M    = Eigen::MatrixXd::Zero(J_cond, J_cond);

			for (intptr_t i = 0; i < n; i++)
			{
				double wi = w_k[i];
				double wzi = wi * z_k[i];

				for (intptr_t g1 = 0; g1 < lay_cond_u->G; g1++)
				{
					intptr_t j1lev = (*lay_cond_u->group_indices[g1])[i];
					intptr_t q1 = lay_cond_u->q[g1];
					intptr_t base1 = lay_cond_u->offset[g1] + j1lev * q1;

					for (intptr_t t1 = 0; t1 < q1; t1++)
					{
						double z1 = lay_cond_u->Z(g1, i, t1);
						// Z'Wz
						ZtWz[base1 + t1] += z1 * wzi;
						// Z'WX
						double wz1 = wi * z1;
						for (intptr_t jj = 0; jj < p; jj++)
							ZtWX(base1 + t1, jj) += wz1 * Xm(i, jj);
						// Z'WZ within group
						for (intptr_t t2 = 0; t2 < q1; t2++)
							M(base1 + t1, base1 + t2) += wz1 * lay_cond_u->Z(g1, i, t2);
						// Z'WZ cross-group
						for (intptr_t g2 = g1 + 1; g2 < lay_cond_u->G; g2++)
						{
							intptr_t j2lev = (*lay_cond_u->group_indices[g2])[i];
							intptr_t q2 = lay_cond_u->q[g2];
							intptr_t base2 = lay_cond_u->offset[g2] + j2lev * q2;
							for (intptr_t t2 = 0; t2 < q2; t2++)
							{
								double val = wz1 * lay_cond_u->Z(g2, i, t2);
								M(base1 + t1, base2 + t2) += val;
								M(base2 + t2, base1 + t1) += val;
							}
						}
					}
				}
			}

			// Add block-diagonal D_k⁻¹
			Eigen::VectorXd theta_k = theta_star + T * z_points[k];
			intptr_t cp = 0;
			for (intptr_t g = 0; g < lay_cond_u->G; g++)
			{
				intptr_t qg = lay_cond_u->q[g];
				Eigen::MatrixXd L = unpack_cholesky(theta_k.data() + cp, qg);
				Eigen::MatrixXd D_inv_g = cholesky_to_precision(L);
				for (intptr_t jlev = 0; jlev < lay_cond_u->J[g]; jlev++)
				{
					intptr_t base = lay_cond_u->offset[g] + jlev * qg;
					for (intptr_t t1 = 0; t1 < qg; t1++)
						for (intptr_t t2 = 0; t2 < qg; t2++)
							M(base + t1, base + t2) += D_inv_g(t1, t2);
				}
				cp += n_chol_params(qg);
			}

			u_llt[k].compute(M);
			if (u_llt[k].info() != Eigen::Success)
				return false;

			aux_y[k] = u_llt[k].solve(ZtWz);
			aux_X[k] = u_llt[k].solve(ZtWX);
			return true;
		};

		const bool is_student = (model.family == "student");

		for (intptr_t k = 0; k < n_grid; k++)
		{
			bool ok = false;

			// Try observed-Hessian Laplace first for Student-t.
			if (is_student
			    && results[k].hessian_weights.size()  == n
			    && results[k].hessian_response.size() == n)
			{
				ok = try_build_grid_point(k,
				                          results[k].hessian_weights,
				                          results[k].hessian_response);
				if (ok) hessian_used++;
			}

			// Fall back to Fisher / IRLS form (also the default path for
			// non-Student non-Gaussian families).
			if (!ok)
			{
				ok = try_build_grid_point(k,
				                          results[k].working_weights,
				                          results[k].working_response);
				if (is_student && ok) fisher_used++;
			}

			u_llt_ok[k] = ok;
		}

		if (is_student)
		{
			static const bool diag_student_waic = []() {
				const char *e = std::getenv("PHON_DIAG_STUDENT_WAIC");
				return e && e[0] && e[0] != '0';
			}();
			if (diag_student_waic)
			{
				std::fprintf(stderr,
					"[student WAIC] u-Laplace per grid point: hessian=%d  fisher_fallback=%d  (n_grid=%ld)\n",
					hessian_used, fisher_used, (long)n_grid);
			}
		}
	}

	// ── 3. Precompute Cholesky factors of Σ_β(θ_k) for correlated β draws ──
	//
	// For invalid grid points (vcov_beta zeroed by sanitise_grid_points) the
	// LLT will fail; we track that and fall back to β̂_k (no perturbation)
	// when sampling.  In practice, invalid points have w[k] = 0 and should
	// never be drawn, but the guard makes this robustness explicit.

	std::vector<Eigen::LLT<Eigen::MatrixXd>> chol_vcov(n_grid);
	std::vector<bool> chol_ok(n_grid, false);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		chol_vcov[k].compute(results[k].vcov_beta);
		chol_ok[k] = (chol_vcov[k].info() == Eigen::Success);
	}

	// ── 4. Build cumulative weight distribution for grid sampling ────

	std::vector<double> cdf(n_grid);
	cdf[0] = w[0];
	for (intptr_t k = 1; k < n_grid; k++)
		cdf[k] = cdf[k - 1] + w[k];

	// ── 5. Draw S posterior samples and compute pointwise log-lik ────

	std::vector<double> loglik_matrix(n * WAIC_S);
	std::mt19937 rng(WAIC_SEED);
	std::uniform_real_distribution<double> unif(0.0, 1.0);
	std::normal_distribution<double> std_normal(0.0, 1.0);

	// Scratch buffer for conditional-u per-draw offset.
	std::vector<double> zu_draw(conditional_u ? (size_t)n : 0);

	for (int s = 0; s < WAIC_S; s++)
	{
		// Sample grid point k with probability w_k.
		double uu = unif(rng);
		intptr_t k = (intptr_t)(std::lower_bound(cdf.begin(), cdf.end(), uu) - cdf.begin());
		k = std::min(k, n_grid - 1);

		// Draw β^(s) ~ N(β̂_k, Σ_β(θ_k))
		Eigen::VectorXd z_beta(p);
		for (intptr_t j = 0; j < p; j++)
			z_beta[j] = std_normal(rng);

		Eigen::VectorXd beta_s = results[k].beta;
		if (chol_ok[k])
			beta_s += chol_vcov[k].matrixL() * z_beta;

		// Dispersion parameters from θ_k.
		Eigen::VectorXd theta_k = theta_star + T * z_points[k];
		double disp[2] = {0.0, 0.0};
		disp_from_theta(theta_k, disp);

		// Pick the RE-offset vector for this draw.
		const double *zu_ptr;
		if (conditional_u && u_llt_ok[k])
		{
			// u^(s) mean: aux_y[k] − aux_X[k] · β^(s)
			Eigen::VectorXd u_sample = aux_y[k] - aux_X[k] * beta_s;

			// Add N(0, M_k⁻¹) noise via (L_kᵀ)⁻¹ ε.
			// LLT decomposes M_k = L_k L_kᵀ so M_k⁻¹ = L_k⁻ᵀ L_k⁻¹; drawing
			// v = L_k⁻ᵀ ε with ε ~ N(0,I) gives Cov(v) = L_k⁻ᵀ L_k⁻¹ = M_k⁻¹.
			// Eigen's matrixU() returns L_kᵀ (upper), so solveInPlace applies
			// the inverse of the upper-triangular L_kᵀ, i.e. computes L_k⁻ᵀ ε.
			Eigen::VectorXd eps(J_cond);
			for (intptr_t j = 0; j < J_cond; j++)
				eps[j] = std_normal(rng);
			u_llt[k].matrixU().solveInPlace(eps);
			u_sample += eps;

			// Expand to per-observation zu_draw[i] = z_i' u^(s)
			std::fill(zu_draw.begin(), zu_draw.end(), 0.0);
			for (intptr_t i = 0; i < n; i++) {
				for (intptr_t g = 0; g < lay_cond_u->G; g++) {
					intptr_t jlev = (*lay_cond_u->group_indices[g])[i];
					intptr_t qg = lay_cond_u->q[g];
					intptr_t base = lay_cond_u->offset[g] + jlev * qg;
					for (intptr_t t = 0; t < qg; t++)
						zu_draw[i] += lay_cond_u->Z(g, i, t) * u_sample[base + t];
				}
			}
			zu_ptr = zu_draw.data();
		}
		else
		{
			zu_ptr = zu_blup.data();
		}

		// Compute pointwise log-likelihoods.
		for (intptr_t i = 0; i < n; i++)
		{
			// η_i = x_i' β^(s) + zu_i + offset_i
			double eta_i = zu_ptr[i];
			for (intptr_t j = 0; j < p; j++)
				eta_i += Xm(i, j) * beta_s[j];
			if (off_ptr) eta_i += (*off_ptr)[i];

			double mu_i = linkinv_scalar(eta_i);
			loglik_matrix[i * WAIC_S + s] = pointwise_loglik(ym[i], mu_i,
			                                                   model.family, disp);
		}
	}

	// ── 6. Compute WAIC ─────────────────────────────────────────────

	compute_waic_from_loglik(model, loglik_matrix, n, WAIC_S);

	// ── 7. Compute PSIS-LOO ─────────────────────────────────────────

	compute_loo_from_loglik(model, loglik_matrix, n, WAIC_S);
}


#if defined(PHON_INLA_BAYES_DIAG)
// ─────────────────────────────────────────────────────────────────
// Compile-gated diagnostic helper for inla_grid_integrate_gaussian.
//
// Decomposes the neg-log-posterior at a given θ into its seven
// contributions (see the layout below), so each subterm can be
// checked for NaN/Inf independently when the grid integration
// fails universally at every evaluation point.
//
// The breakdown is computed by replicating the arithmetic of
// solve_gaussian_henderson + GaussianCholObjective::eval rather
// than by instrumenting them — deliberately independent, so a bug
// in the inner solve does not silence the diagnostic itself.
//
// Identity (up to floating-point noise):
//   obj.eval(θ) = A + B + C + D + E + F + G
// where
//   A = cond_nll        = rss/(2σ²) + ½ n·log(2πσ²)
//   B = u_prior_nll     = ½ Σ_j u_j' D⁻¹ u_j
//                       + Σ_g J_g·[½ q_g·log(2π) + ½ log|D_g|]
//   C = ½ log|H_uu|                                  (Laplace correction)
//   D = −½ J_total · log(2π)                         (Laplace constant)
//   E = fixed_prior_nll(β̂)                          (per-coef N priors)
//   F = −variance_prior_log_density                  (σ_g,t priors + LKJ)
//   G = −residual_prior_log_density                  (σ_res prior)
// ─────────────────────────────────────────────────────────────────
struct DiagNllBreakdown
{
	// θ-space decomposition
	Eigen::VectorXd theta_raw;
	std::vector<Eigen::MatrixXd> D_cov;  // per-group RE covariance
	std::vector<Eigen::MatrixXd> D_cor;  // per-group RE correlation
	std::vector<double> log_det_Dg;
	double sigma2 = 0.0;
	bool sigma2_overflow = false;

	// Inner solve
	Eigen::VectorXd beta_hat;
	Eigen::VectorXd u_hat;
	double rss = 0.0;
	bool beta_finite = true;
	bool u_finite = true;

	// Subterms (signed so they sum to the total)
	double A_cond_nll = 0.0;
	double B_u_prior_nll = 0.0;
	double C_half_log_det_Huu = 0.0;
	double D_minus_half_J_log_2pi = 0.0;
	double E_fixed_prior_nll = 0.0;
	double F_minus_variance_prior_lp = 0.0;
	double G_minus_residual_prior_lp = 0.0;

	// Totals
	double total_breakdown = 0.0;   // A + B + C + D + E + F + G
	double total_obj_eval = 0.0;    // obj.eval(θ)

	// Validity mask: bit i set ⇒ subterm i (A=0, …, G=6) not finite
	uint32_t nan_mask = 0;
};

static DiagNllBreakdown diag_eval_gaussian_breakdown(
	const Eigen::VectorXd &theta,
	const GaussianCholObjective &obj,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p, intptr_t n_chol,
	const PriorSpec *priors,
	const Array<String> *coef_names,
	const Eigen::VectorXd *off_ptr)
{
	DiagNllBreakdown br;
	br.theta_raw = theta;

	// ── Unpack θ → D_inv, D_cov, D_cor, σ² ─────────────────────
	std::vector<Eigen::MatrixXd> D_inv(lay.G);
	br.D_cov.resize(lay.G);
	br.D_cor.resize(lay.G);
	br.log_det_Dg.resize(lay.G);
	intptr_t cp = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		Eigen::MatrixXd L = unpack_cholesky(theta.data() + cp, qg);
		D_inv[g]       = cholesky_to_precision(L);
		br.D_cov[g]    = cholesky_to_cov(L);
		br.log_det_Dg[g] = log_det_D(theta.data() + cp, qg);

		br.D_cor[g] = Eigen::MatrixXd::Identity(qg, qg);
		for (intptr_t i = 0; i < qg; i++)
			for (intptr_t j = 0; j < qg; j++)
			{
				double dii = br.D_cov[g](i, i);
				double djj = br.D_cov[g](j, j);
				if (dii > 0 && djj > 0 && std::isfinite(dii) && std::isfinite(djj))
					br.D_cor[g](i, j) = br.D_cov[g](i, j) / std::sqrt(dii * djj);
			}
		cp += n_chol_params(qg);
	}
	br.sigma2 = std::exp(2.0 * theta[n_chol]);
	br.sigma2_overflow = !std::isfinite(br.sigma2);

	// ── Inner Henderson solve ──────────────────────────────────
	ProfiledResult inner = solve_gaussian_henderson(
		D_inv, br.log_det_Dg, br.sigma2,
		Xm, ym, lay, n, p, priors, coef_names, off_ptr);
	br.beta_hat    = inner.beta;
	br.u_hat       = inner.u;
	br.beta_finite = br.beta_hat.allFinite();
	br.u_finite    = br.u_hat.allFinite();

	// ── A: conditional likelihood neg-log ──────────────────────
	br.rss = (ym - inner.mu).squaredNorm();
	br.A_cond_nll = br.rss / (2.0 * br.sigma2)
	              + 0.5 * (double) n * std::log(2.0 * M_PI * br.sigma2);

	// ── B: u-prior neg-log (quadratic form + normalisers) ──────
	double B = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++)
				for (intptr_t t2 = 0; t2 < qg; t2++)
					quad += br.u_hat[base + t1] * D_inv[g](t1, t2) * br.u_hat[base + t2];
			B += 0.5 * quad;
		}
		B += (double) lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI)
		                        + 0.5 * br.log_det_Dg[g]);
	}
	br.B_u_prior_nll = B;

	// ── C: +½ log|H_uu|   (Laplace correction) ─────────────────
	Eigen::VectorXd w_final = Eigen::VectorXd::Constant(n, 1.0 / br.sigma2);
	br.C_half_log_det_Huu = 0.5 * full_log_det_H(w_final, D_inv, lay, n);

	// ── D: −½ J log(2π)   (Laplace constant) ───────────────────
	br.D_minus_half_J_log_2pi = -0.5 * (double) lay.J_total * std::log(2.0 * M_PI);

	// ── E: fixed-effect prior neg-log at β̂ ─────────────────────
	if (priors && coef_names)
		br.E_fixed_prior_nll = fixed_prior_nll(br.beta_hat, *priors, *coef_names, p);

	// ── F: −variance prior log-density (σ_g,t + LKJ) ───────────
	if (priors)
		br.F_minus_variance_prior_lp = -variance_prior_log_density(br.D_cov, *priors, lay);

	// ── G: −residual prior log-density (σ_res) ─────────────────
	if (priors)
		br.G_minus_residual_prior_lp = -residual_prior_log_density(std::sqrt(br.sigma2), *priors);

	// ── Sum + cross-check against obj.eval ─────────────────────
	br.total_breakdown = br.A_cond_nll + br.B_u_prior_nll
	                   + br.C_half_log_det_Huu + br.D_minus_half_J_log_2pi
	                   + br.E_fixed_prior_nll
	                   + br.F_minus_variance_prior_lp
	                   + br.G_minus_residual_prior_lp;
	br.total_obj_eval = obj.eval(theta);

	const double terms[7] = {
		br.A_cond_nll, br.B_u_prior_nll,
		br.C_half_log_det_Huu, br.D_minus_half_J_log_2pi,
		br.E_fixed_prior_nll,
		br.F_minus_variance_prior_lp, br.G_minus_residual_prior_lp
	};
	for (int i = 0; i < 7; i++)
		if (!std::isfinite(terms[i])) br.nan_mask |= (1u << i);

	return br;
}
#endif  // PHON_INLA_BAYES_DIAG


// Main INLA grid integration for Gaussian LMMs.
// Populates the Model's posterior fields with mixture-based estimates.
static void inla_grid_integrate_gaussian(
	Model &model,
	const GaussianCholObjective &obj,
	const Eigen::VectorXd &theta_star,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p, intptr_t n_chol,
	const PriorSpec *priors,
	const Array<String> *coef_names,
	const Eigen::VectorXd *off_ptr = nullptr)
{
	intptr_t d = theta_star.size();

	// ── Defense-in-depth: guard against NaN θ* ───────────────────────
	// The outer mode-finder (robust_optimize → lbfgs_optimize) should
	// have thrown already if it produced non-finite state. Fail clearly
	// here rather than propagating NaN through the Hessian, eigen-
	// decomposition, and every grid point, which produces a cascade of
	// "all 43 grid points invalid" without actionable information.
	if (!theta_star.allFinite()) {
		throw error(
			"inla_grid_integrate_gaussian: θ* contains non-finite values "
			"(size=%). Mode-finding failed silently upstream — this is a "
			"bug in the Bayesian optimizer path, not in the model spec. "
			"Compile with -DPHON_INLA_BAYES_DIAG=1 to trace the mode-finder.",
			(long) d);
	}

	// ── 1. Hessian at the mode ───────────────────────────────────
	Eigen::MatrixXd H = compute_fd_hessian(obj, theta_star);

	// ── 2. Eigendecomposition H = V Λ V' ─────────────────────────
	Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(H);
	Eigen::VectorXd eigenvalues = eig.eigenvalues();
	Eigen::MatrixXd V = eig.eigenvectors();

	// Clamp eigenvalues to positive (Hessian should be PD at the mode,
	// but FD noise or near-boundary effects may produce small negatives).
	for (intptr_t j = 0; j < d; j++) {
		eigenvalues[j] = std::max(eigenvalues[j], 1e-6);
	}

	// Transform matrix: θ_k = θ* + V Λ^{-1/2} z_k
	Eigen::VectorXd inv_sqrt_lambda = eigenvalues.array().rsqrt().matrix();
	Eigen::MatrixXd T = V * inv_sqrt_lambda.asDiagonal();

	// ── 3. CCD grid points in z-space ────────────────────────────
	auto z_points = build_ccd_points(d, 3.0);
	intptr_t n_grid = (intptr_t)z_points.size();

	// ── 4. Evaluate at each grid point ───────────────────────────
	std::vector<GridPointResult> results(n_grid);
	std::vector<double> log_posterior(n_grid);

	for (intptr_t k = 0; k < n_grid; k++)
	{
		Eigen::VectorXd theta_k = theta_star + T * z_points[k];
		results[k] = eval_gaussian_grid_point(theta_k, obj, Xm, ym, lay,
		                                       n, p, n_chol, priors, coef_names,
		                                       off_ptr);

		log_posterior[k] = -results[k].neg_log_posterior;
	}

#if defined(PHON_INLA_BAYES_DIAG)
	// ── PHON_INLA_BAYES_DIAG dump ──────────────────────────────────────
	//
	// Target: prior-combination bug where `inla_grid_integrate_gaussian`
	// returns invalid (NaN/non-finite) results at every grid point even
	// though (a) each prior individually works and (b) the frequentist
	// engine converges on the same model.
	//
	// Reproducer: VOT ~ gender + (gender | Item) with the full Vasishth-
	// style stack (N(0, 50) slopes + N(0, 200) intercept + HalfNormal(100)
	// variance + HalfNormal(100) residual + LKJ(2)) on Mandarin VOT.
	// Bisection narrows the interaction to the two `set_fixed` calls
	// being jointly sufficient to trigger failure; singletons pass.
	//
	// Working hypothesis: the combined prior stack alters the profiled
	// β̂(θ) curvature enough that `compute_fd_hessian` at h=1e-4·|θ*|
	// produces one or more near-zero (or negative) eigenvalues, which
	// get clamped to 1e-6. Then Λ^{-½} ≈ 1000 in the clamped direction,
	// axial CCD steps at δ=3 move θ by ~3000 units along that axis, and
	// the non-chol decodings (σ_res = exp(θ[n_chol]), D_g = L Lᵀ with
	// log-scaled diagonal) over/underflow — producing NaN at every grid
	// point, not just a few.
	//
	// The block is placed BEFORE `sanitise_grid_points` so invalid entries
	// have not yet been overwritten with zero β / +∞ NLL. The per-point
	// breakdown re-uses the existing `diag_eval_gaussian_breakdown` helper
	// (A…G in that struct map to likelihood / u-prior / ½·log|H_uu| /
	// Laplace-const / fixed-prior / −variance-prior / −residual-prior),
	// deliberately computed independently of the inner solve so that a
	// bug in Henderson does not silence the diagnostic itself.
	//
	// Sections:
	//   [A] θ* decoded (σ per group, correlations, σ_res) + NLL at θ*
	//   [B] FD Hessian H_θ (raw) + FD step sizes
	//   [C] Eigendecomposition — pre- and post-clamp eigenvalues,
	//       eigenvectors, condition number, count of clamped λ
	//   [D] Transform column norms ‖T.col(j)‖ = Λ^{-½}[j] + predicted
	//       axial θ displacement at δ=3
	//   [E] Per-grid-point breakdown (first DIAG_MAX_POINTS points plus
	//       any additional invalid points, capped)
	//
	// Compile with -DPHON_INLA_BAYES_DIAG=1 and capture stderr:
	//   phonometrica ... 2> bayes_diag.log
	{
		std::fprintf(stderr,
			"\n=== PHON_INLA_BAYES_DIAG: inla_grid_integrate_gaussian ===\n");
		std::fprintf(stderr,
			"d=%ld  n_grid=%ld  n_chol=%ld  p=%ld  n=%ld  G=%ld  J_total=%ld\n",
			(long)d, (long)n_grid, (long)n_chol, (long)p,
			(long)n, (long)lay.G, (long)lay.J_total);

		// ── [A] Mode θ* ───────────────────────────────────────────────
		std::fprintf(stderr, "\n[A] Mode θ* (raw, chol-parameterised)\n");
		std::fprintf(stderr, "    θ*[%ld] =", (long)d);
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, " %+.6e", theta_star[j]);
		std::fprintf(stderr, "\n");

		{
			DiagNllBreakdown bm = diag_eval_gaussian_breakdown(
				theta_star, obj, Xm, ym, lay,
				n, p, n_chol, priors, coef_names, off_ptr);

			// Decoded σ / correlations per group (from D_cov / D_cor)
			for (intptr_t g = 0; g < lay.G; g++)
			{
				intptr_t qg = lay.q[g];
				std::fprintf(stderr, "    group %ld  q=%ld  σ=[", (long)g, (long)qg);
				for (intptr_t t = 0; t < qg; t++)
				{
					double v = bm.D_cov[g](t, t);
					double sd = (v >= 0 && std::isfinite(v))
					          ? std::sqrt(v) : std::nan("");
					std::fprintf(stderr, "%s%.4f", t ? ", " : "", sd);
				}
				std::fprintf(stderr, "]");
				if (qg >= 2)
				{
					std::fprintf(stderr, "  det(D)=%.3e  corr:",
					             bm.D_cov[g].determinant());
					for (intptr_t t1 = 0; t1 < qg; t1++)
						for (intptr_t t2 = t1 + 1; t2 < qg; t2++)
							std::fprintf(stderr, " r[%ld,%ld]=%+.4f",
							             (long)t1, (long)t2,
							             bm.D_cor[g](t1, t2));
				}
				std::fprintf(stderr, "\n");
			}
			std::fprintf(stderr, "    σ_res = exp(θ*[%ld]) = %.4f  σ²=%.4e%s\n",
			             (long)n_chol, std::exp(theta_star[n_chol]),
			             bm.sigma2, bm.sigma2_overflow ? "  [OVERFLOW]" : "");
			std::fprintf(stderr, "    β̂ finite: %s   û finite: %s\n",
			             bm.beta_finite ? "yes" : "NO",
			             bm.u_finite    ? "yes" : "NO");

			// NLL decomposition at the mode (sanity: should all be finite)
			auto tag = [](double v) {
				return std::isfinite(v) ? "" : "  [NON-FINITE]";
			};
			std::fprintf(stderr, "    NLL(θ*) components:\n");
			std::fprintf(stderr, "      A  cond_nll            = %+.6e%s\n",
			             bm.A_cond_nll, tag(bm.A_cond_nll));
			std::fprintf(stderr, "      B  u_prior_nll         = %+.6e%s\n",
			             bm.B_u_prior_nll, tag(bm.B_u_prior_nll));
			std::fprintf(stderr, "      C  ½·log|H_uu|         = %+.6e%s\n",
			             bm.C_half_log_det_Huu, tag(bm.C_half_log_det_Huu));
			std::fprintf(stderr, "      D  -½·J·log(2π)        = %+.6e\n",
			             bm.D_minus_half_J_log_2pi);
			std::fprintf(stderr, "      E  fixed_prior_nll(β̂)  = %+.6e%s\n",
			             bm.E_fixed_prior_nll, tag(bm.E_fixed_prior_nll));
			std::fprintf(stderr, "      F  -variance_prior_lp  = %+.6e%s\n",
			             bm.F_minus_variance_prior_lp,
			             tag(bm.F_minus_variance_prior_lp));
			std::fprintf(stderr, "      G  -residual_prior_lp  = %+.6e%s\n",
			             bm.G_minus_residual_prior_lp,
			             tag(bm.G_minus_residual_prior_lp));
			std::fprintf(stderr, "      Σ(A..G) breakdown      = %+.6e%s\n",
			             bm.total_breakdown, tag(bm.total_breakdown));
			std::fprintf(stderr, "      obj.eval(θ*) cross-chk = %+.6e  "
			             "(|Δ| = %.3e)\n",
			             bm.total_obj_eval,
			             std::abs(bm.total_breakdown - bm.total_obj_eval));
			if (bm.nan_mask != 0)
				std::fprintf(stderr,
				             "    >>> subterms non-finite at θ*: nan_mask=0x%x\n",
				             bm.nan_mask);
		}

		// ── [B] FD Hessian ────────────────────────────────────────────
		std::fprintf(stderr,
			"\n[B] FD Hessian H_θ  (d x d, central differences,"
			" h = 1e-4·max(|θ*[j]|, 1))\n    step h_j =");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, " %.3e",
			             1e-4 * std::max(std::abs(theta_star[j]), 1.0));
		std::fprintf(stderr, "\n");
		for (intptr_t i = 0; i < d; i++)
		{
			std::fprintf(stderr, "   ");
			for (intptr_t j = 0; j < d; j++)
				std::fprintf(stderr, " %+11.4e", H(i, j));
			std::fprintf(stderr, "\n");
		}
		std::fprintf(stderr, "    det(H) = %+.4e   trace(H) = %+.4e\n",
		             H.determinant(), H.trace());

		// ── [C] Eigendecomposition ───────────────────────────────────
		std::fprintf(stderr,
			"\n[C] Eigendecomposition H = V Λ Vᵀ  (ascending)\n");
		std::fprintf(stderr, "    λ (pre-clamp):       ");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, " %+11.4e", eig.eigenvalues()[j]);
		std::fprintf(stderr, "\n    λ (post-clamp ≥1e-6):");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, " %+11.4e", eigenvalues[j]);
		std::fprintf(stderr, "\n");
		{
			double lam_min = eigenvalues.minCoeff();
			double lam_max = eigenvalues.maxCoeff();
			double kappa = (lam_min > 0)
				? lam_max / lam_min
				: std::numeric_limits<double>::infinity();
			intptr_t n_clamped = 0;
			for (intptr_t j = 0; j < d; j++)
				if (eig.eigenvalues()[j] < 1e-6) n_clamped++;
			std::fprintf(stderr,
				"    condition(H, post-clamp) = %.4e"
				"   clamped eigenvalues: %ld/%ld\n",
				kappa, (long)n_clamped, (long)d);
			if (n_clamped > 0)
				std::fprintf(stderr,
					"    >>> WARNING: near-flat curvature in %ld direction(s).\n"
					"        θ displacement along clamped axes scales as δ/√λ\n"
					"        = 3/√1e-6 ≈ 3e3 — this is the expected failure\n"
					"        signature for the prior-combination bug. The mode\n"
					"        is fine; the grid pushes θ off a near-flat ridge\n"
					"        that the FD Hessian fails to resolve at h=1e-4·|θ|.\n",
					(long)n_clamped);
		}
		std::fprintf(stderr, "    V (columns = eigenvectors):\n");
		for (intptr_t i = 0; i < d; i++)
		{
			std::fprintf(stderr, "   ");
			for (intptr_t j = 0; j < d; j++)
				std::fprintf(stderr, " %+9.5f", V(i, j));
			std::fprintf(stderr, "\n");
		}

		// ── [D] Transform column norms — predicted θ displacement ─────
		std::fprintf(stderr,
			"\n[D] Transform T = V·Λ^{-½}  →  θ_k = θ* + T·z_k\n");
		std::fprintf(stderr, "    ‖T.col(j)‖ = Λ^{-½}[j]:");
		double max_step = 0;
		for (intptr_t j = 0; j < d; j++)
		{
			double tn = T.col(j).norm();
			std::fprintf(stderr, " %.3e", tn);
			if (tn > max_step) max_step = tn;
		}
		std::fprintf(stderr,
			"\n    max axial |θ_k − θ*| at δ=3 (predicted): %.3e\n",
			3.0 * max_step);
		if (3.0 * max_step > 20.0)
			std::fprintf(stderr,
				"    >>> WARNING: predicted axial θ step > 20. Non-chol\n"
				"        decodings (σ_res, D_g) will over/underflow by many\n"
				"        orders of magnitude; Henderson output will be NaN.\n");

		// ── [E] Per-grid-point breakdown ─────────────────────────────
		constexpr intptr_t DIAG_MAX_POINTS = 12;
		std::fprintf(stderr,
			"\n[E] Per-grid-point NLL breakdown\n"
			"    (first %ld points; up to %ld additional invalid points)\n",
			(long)DIAG_MAX_POINTS, (long)DIAG_MAX_POINTS);

		auto dump_grid_point = [&](intptr_t k, const char *label) {
			Eigen::VectorXd theta_k = theta_star + T * z_points[k];
			bool valid = grid_point_valid(results[k], p);
			std::fprintf(stderr,
				"\n  --- k=%ld  (%s)  |z|=%.3f  valid=%s ---\n",
				(long)k, label, z_points[k].norm(), valid ? "yes" : "NO");

			std::fprintf(stderr, "    z_k =");
			for (intptr_t j = 0; j < d; j++)
				std::fprintf(stderr, " %+.3f", z_points[k][j]);
			std::fprintf(stderr, "\n    θ_k =");
			for (intptr_t j = 0; j < d; j++)
				std::fprintf(stderr, " %+.4e", theta_k[j]);
			std::fprintf(stderr, "\n");

			DiagNllBreakdown bk = diag_eval_gaussian_breakdown(
				theta_k, obj, Xm, ym, lay,
				n, p, n_chol, priors, coef_names, off_ptr);

			// Decoded σ / correlations per group
			for (intptr_t g = 0; g < lay.G; g++)
			{
				intptr_t qg = lay.q[g];
				std::fprintf(stderr, "    group %ld σ =", (long)g);
				for (intptr_t t = 0; t < qg; t++)
				{
					double v = bk.D_cov[g](t, t);
					double sd = (v >= 0 && std::isfinite(v))
					          ? std::sqrt(v) : std::nan("");
					std::fprintf(stderr, " %.3e", sd);
				}
				std::fprintf(stderr, "   det(D_%ld)=%.3e",
				             (long)g, bk.D_cov[g].determinant());
				if (qg >= 2)
					std::fprintf(stderr, "   r[0,1]=%+.4f", bk.D_cor[g](0, 1));
				std::fprintf(stderr, "\n");
			}
			std::fprintf(stderr, "    σ_res=%.3e  σ²=%.3e%s\n",
			             std::exp(theta_k[n_chol]), bk.sigma2,
			             bk.sigma2_overflow ? "  [OVERFLOW]" : "");

			// Validity flags on the stored grid-point result (before sanitisation)
			if (!valid)
			{
				std::fprintf(stderr, "    failure: nll=%s  β=[",
					std::isfinite(results[k].neg_log_posterior)
						? "finite" : "NON-FINITE");
				for (intptr_t j = 0; j < p; j++)
					std::fprintf(stderr, "%s%s", j ? " " : "",
						std::isfinite(results[k].beta[j]) ? "ok" : "NaN");
				std::fprintf(stderr, "]  diag(vcov)=[");
				for (intptr_t j = 0; j < p; j++)
				{
					if (results[k].vcov_beta.rows() <= j)
					{
						std::fprintf(stderr, "%s-", j ? " " : "");
					}
					else
					{
						double v = results[k].vcov_beta(j, j);
						std::fprintf(stderr, "%s%s", j ? " " : "",
							!std::isfinite(v) ? "NaN" : (v < 0 ? "NEG" : "ok"));
					}
				}
				std::fprintf(stderr, "]\n");
			}

			// Seven-term NLL breakdown (A…G, from the existing helper)
			auto tag = [](double v) {
				return std::isfinite(v) ? "" : "  [NON-FINITE]";
			};
			std::fprintf(stderr, "    rss=%+.4e\n", bk.rss);
			std::fprintf(stderr, "    A  cond_nll           = %+.4e%s\n",
			             bk.A_cond_nll, tag(bk.A_cond_nll));
			std::fprintf(stderr, "    B  u_prior_nll        = %+.4e%s\n",
			             bk.B_u_prior_nll, tag(bk.B_u_prior_nll));
			std::fprintf(stderr, "    C  ½·log|H_uu|        = %+.4e%s\n",
			             bk.C_half_log_det_Huu, tag(bk.C_half_log_det_Huu));
			std::fprintf(stderr, "    D  -½·J·log(2π)       = %+.4e\n",
			             bk.D_minus_half_J_log_2pi);
			std::fprintf(stderr, "    E  fixed_prior_nll    = %+.4e%s\n",
			             bk.E_fixed_prior_nll, tag(bk.E_fixed_prior_nll));
			std::fprintf(stderr, "    F  -variance_prior    = %+.4e%s\n",
			             bk.F_minus_variance_prior_lp,
			             tag(bk.F_minus_variance_prior_lp));
			std::fprintf(stderr, "    G  -residual_prior    = %+.4e%s\n",
			             bk.G_minus_residual_prior_lp,
			             tag(bk.G_minus_residual_prior_lp));
			for (intptr_t g = 0; g < lay.G; g++)
				std::fprintf(stderr, "       log|D_%ld|         = %+.4e%s\n",
				             (long)g, bk.log_det_Dg[g], tag(bk.log_det_Dg[g]));
			std::fprintf(stderr, "    ─────────────────────\n");
			std::fprintf(stderr,
			             "    Σ(A..G) breakdown     = %+.4e%s\n",
			             bk.total_breakdown, tag(bk.total_breakdown));
			std::fprintf(stderr,
			             "    obj.eval(θ_k) check   = %+.4e  (|Δ|=%.3e)\n",
			             bk.total_obj_eval,
			             std::abs(bk.total_breakdown - bk.total_obj_eval));
			if (bk.nan_mask != 0)
				std::fprintf(stderr,
				             "    >>> non-finite subterms: nan_mask=0x%x\n",
				             bk.nan_mask);
		};

		intptr_t n_first = std::min((intptr_t)DIAG_MAX_POINTS, n_grid);
		for (intptr_t k = 0; k < n_first; k++)
			dump_grid_point(k, k == 0 ? "center" : "grid");

		intptr_t extra = 0;
		for (intptr_t k = n_first; k < n_grid && extra < DIAG_MAX_POINTS; k++)
		{
			if (!grid_point_valid(results[k], p))
			{
				dump_grid_point(k, "invalid");
				extra++;
			}
		}

		intptr_t n_invalid = 0;
		for (intptr_t k = 0; k < n_grid; k++)
			if (!grid_point_valid(results[k], p)) n_invalid++;
		std::fprintf(stderr,
			"\n  summary: %ld/%ld grid points invalid"
			"  (first-%ld dumped, %ld extra invalid shown)\n",
			(long)n_invalid, (long)n_grid, (long)n_first, (long)extra);

		std::fprintf(stderr, "=== end PHON_INLA_BAYES_DIAG ===\n\n");
	}
#endif  // PHON_INLA_BAYES_DIAG

	// ── 4b. Discard invalid grid points ──────────────────────────
	sanitise_grid_points(results, log_posterior, n_grid, p);

	// ── 5. Integration weights ───────────────────────────────────
	//
	// Weights combine CCD moment-matching base weights (against the Gaussian
	// reference N(0, I) in z-space) with a posterior/reference density-ratio
	// correction:
	//
	//   w_k ∝ w_k^{CCD} × P(θ_k | y) / φ(z_k)
	//       = w_k^{CCD} × exp(-nll_k + ½ ||z_k||²)
	//
	// Rationale: the CCD base weights integrate Gaussian polynomial moments up
	// to 2nd order exactly against φ(z). The density ratio corrects for
	// deviation of the true posterior from this Gaussian reference. In the
	// concentrated-posterior limit (true posterior ≈ φ after the linear
	// transform by T), the density ratio is constant across points and the
	// rule reduces to exact CCD quadrature — the mixture variance
	// Σ_k w_k [Σ_k + (β_k − β̄)(β_k − β̄)'] then correctly integrates both
	// E_θ[Σ(θ)] and Var_θ[β(θ)], rather than collapsing to Σ(θ*).
	//
	// Historical note: an earlier version used raw-posterior weights
	// w_k ∝ exp(-nll_k), which gave ~98% of the mass to the center point in
	// typical designs and effectively reduced the grid integration to
	// Laplace-at-the-mode, discarding hyperparameter uncertainty in β.

	auto w_ccd = ccd_base_weights(d, 3.0, z_points);
	std::vector<double> log_w(n_grid);
	double max_log_w = -std::numeric_limits<double>::infinity();
	for (intptr_t k = 0; k < n_grid; k++)
	{
		if (w_ccd[k] <= 0.0 || !std::isfinite(log_posterior[k]))
		{
			log_w[k] = -std::numeric_limits<double>::infinity();
			continue;
		}
		log_w[k] = std::log(w_ccd[k]) + log_posterior[k]
		         + 0.5 * z_points[k].squaredNorm();
		if (log_w[k] > max_log_w) max_log_w = log_w[k];
	}

	std::vector<double> w(n_grid, 0.0);
	if (std::isfinite(max_log_w))
	{
		double sum_w = 0;
		for (intptr_t k = 0; k < n_grid; k++)
		{
			w[k] = std::isfinite(log_w[k]) ? std::exp(log_w[k] - max_log_w) : 0.0;
			sum_w += w[k];
		}
		double inv_sum = (sum_w > 0) ? 1.0 / sum_w : 0.0;
		for (intptr_t k = 0; k < n_grid; k++) w[k] *= inv_sum;
	}
	else
	{
		// Every point invalid. sanitise_grid_points should have already
		// thrown if no valid points existed, so this is defensive.
		for (intptr_t k = 0; k < n_grid; k++) w[k] = 1.0 / (double) n_grid;
	}

	// ── 5b. Per-grid-point conditional means ───────────────────
	//
	// For Gaussian families ℓ'''(η) ≡ 0, so the SLA third-derivative
	// correction is mathematically zero here.  See the PIRLS path for
	// the full rationale (and for why SLA is also disabled there).
	// The variable name sla_beta is kept to minimise downstream churn.

	std::vector<Eigen::VectorXd> sla_beta(n_grid);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		sla_beta[k] = results[k].beta;
	}

	// ── 6. Mixture posterior for β ───────────────────────────────
	//
	// E[β] = Σ_k w_k β̃_k
	// Var[β] = Σ_k w_k [Σ_k + (β̃_k − E[β])(β̃_k − E[β])']

	Eigen::VectorXd mix_mean = Eigen::VectorXd::Zero(p);
	for (intptr_t k = 0; k < n_grid; k++) {
		mix_mean += w[k] * sla_beta[k];
	}

	Eigen::MatrixXd mix_var = Eigen::MatrixXd::Zero(p, p);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		Eigen::VectorXd diff = sla_beta[k] - mix_mean;
		mix_var += w[k] * (results[k].vcov_beta + diff * diff.transpose());
	}

	// ── Diagnostic dump (set env var PHON_DIAG_GRID=1 to enable) ────
	//
	// Built-in, runtime-toggled diagnostic for INLA grid integration.
	// Same idiom as PHON_DIAG_LAPLACE: compiled in always, zero cost
	// when off, no recompile required to enable.
	//
	// Useful for diagnosing:
	//   (1) p_WAIC inflation: per-grid-point correlation matrices of
	//       Σ_β(θ_k) and the mixture correlation matrix reveal whether
	//       the β↔β ridge correlations MCMC captures are present.
	//   (2) hyperparameter posterior collapse: if all the weight ends
	//       up at one grid point, hyper Post.SD will be 0 and any
	//       coefficient with a wildly displaced β̂(θ_k) at that point
	//       will dominate the mixture mean.
	//   (3) σ truncation: if the grid does not give weight to the
	//       posterior tail, the marginal σ posterior under-shrinks.
	//
	// Pipe the run through `2> grid_diag.log` to capture.
	{
		static const bool diag_grid_on = []() {
			const char *e = std::getenv("PHON_DIAG_GRID");
			return e && e[0] && e[0] != '0';
		}();
		if (diag_grid_on)
		{
		std::fprintf(stderr,
			"\n=== PHON_DIAG_GRID: inla_grid_integrate_gaussian ===\n");
		std::fprintf(stderr, "d=%ld  n_grid=%ld  n_chol=%ld  p=%ld  G=%ld\n",
			(long)d, (long)n_grid, (long)n_chol, (long)p, (long)lay.G);

		// Outer Hessian eigenvalues (clamped to ≥1e-6 already)
		std::fprintf(stderr, "H eigenvalues (clamped): ");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, "%.4g ", eigenvalues[j]);
		std::fprintf(stderr, "\n");

		// T column norms (= 1/√λ each), the per-axis step in θ-space
		// produced by a unit step in z-space.
		std::fprintf(stderr, "T column norms (axial θ step per unit z): ");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, "%.4g ", T.col(j).norm());
		std::fprintf(stderr, "\n");

		// θ* (the mode, in log-σ / Cholesky coords)
		std::fprintf(stderr, "theta_star = [");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, "%s%.4f", j ? ", " : "", theta_star[j]);
		std::fprintf(stderr, "]\n");

		// ── Per-grid-point table ────────────────────────────────────
		std::fprintf(stderr, "\nper-grid-point summary:\n");
		std::fprintf(stderr, "%-3s %-8s %-9s %-11s %-8s",
		             "k", "|z|", "w_ccd", "log_post", "w");
		for (intptr_t g = 0; g < lay.G; g++)
			for (intptr_t t = 0; t < lay.q[g]; t++)
				std::fprintf(stderr, " sd[g%ld,t%ld]", (long)g, (long)t);
		std::fprintf(stderr, " %-9s", "sd_res");
		for (intptr_t j = 0; j < p; j++)
			std::fprintf(stderr, " %-9s", "beta");
		std::fprintf(stderr, "\n");

		for (intptr_t k = 0; k < n_grid; k++)
		{
			Eigen::VectorXd theta_k = theta_star + T * z_points[k];
			std::fprintf(stderr, "%-3ld %-8.3f %-9.4g %-11.4f %-8.4f",
			             (long)k, z_points[k].norm(), w_ccd[k],
			             std::isfinite(log_posterior[k]) ? log_posterior[k] : NAN,
			             w[k]);

			// Recover σ values from θ_k for each RE diag term
			intptr_t cp = 0;
			for (intptr_t g = 0; g < lay.G; g++) {
				intptr_t qg = lay.q[g];
				Eigen::MatrixXd L = unpack_cholesky(theta_k.data() + cp, qg);
				Eigen::MatrixXd D = cholesky_to_cov(L);
				for (intptr_t t = 0; t < qg; t++)
					std::fprintf(stderr, " %-10.3f",
					             std::sqrt(std::max(D(t, t), 0.0)));
				cp += n_chol_params(qg);
			}
			std::fprintf(stderr, " %-9.3f", std::exp(theta_k[n_chol]));

			for (intptr_t j = 0; j < p; j++)
				std::fprintf(stderr, " %-9.3f", results[k].beta[j]);
			std::fprintf(stderr, "\n");
		}

		// ── Per-coefficient mixture mean breakdown ─────────────────
		// For each fixed effect j, list w_k · β_k[j] across k so that
		// Σ_k w_k · β_k[j] = mix_mean[j] is visible term-by-term.
		// Critical for diagnosing cases where mix_mean != β̂(θ*) but
		// no single grid point looks dominant.
		std::fprintf(stderr,
			"\nper-coefficient mixture mean breakdown (Σ_k w_k · β_k[j]):\n");
		for (intptr_t j = 0; j < p; j++)
		{
			const char *cname = "?";
			std::string cname_buf;
			if (coef_names && (intptr_t)(j + 1) <= coef_names->size()) {
				cname_buf = std::string((*coef_names)[j + 1].data(),
				                          (*coef_names)[j + 1].size());
				cname = cname_buf.c_str();
			}
			std::fprintf(stderr, "  β[%ld]=%s:  mode=%+.4f  mean=%+.4f\n",
				(long)j, cname, results[0].beta[j], mix_mean[j]);
			for (intptr_t k = 0; k < n_grid; k++) {
				if (std::abs(w[k]) < 1e-12) continue;
				std::fprintf(stderr,
					"    k=%-3ld w=%.4f  β_k=%+.4f  contrib=%+.4f\n",
					(long)k, w[k], results[k].beta[j],
					w[k] * results[k].beta[j]);
			}
		}

		// ── Per-grid-point correlation matrices of Σ_β(θ_k) ─────────
		// Only show grid points with non-negligible weight.
		std::fprintf(stderr,
			"\nper-grid-point correlation matrix of Σ_β(θ_k)  (coef_names: ");
		if (coef_names) {
			for (intptr_t j = 1; j <= p; j++)
				std::fprintf(stderr, "%s%s", j > 1 ? ", " : "",
				             std::string((*coef_names)[j].data(),
				                         (*coef_names)[j].size()).c_str());
		} else {
			std::fprintf(stderr, "unavailable");
		}
		std::fprintf(stderr, "):\n");

		for (intptr_t k = 0; k < n_grid; k++)
		{
			if (w[k] < 1e-6) continue;
			std::fprintf(stderr, "  k=%ld  (w=%.4f):\n", (long)k, w[k]);
			for (intptr_t i = 0; i < p; i++)
			{
				std::fprintf(stderr, "   ");
				for (intptr_t j = 0; j < p; j++)
				{
					double dii = std::max(results[k].vcov_beta(i, i), 1e-30);
					double djj = std::max(results[k].vcov_beta(j, j), 1e-30);
					double corr = results[k].vcov_beta(i, j)
					            / std::sqrt(dii * djj);
					std::fprintf(stderr, " %7.3f", corr);
				}
				std::fprintf(stderr, "\n");
			}
		}

		// ── Mixture posterior summary ───────────────────────────────
		std::fprintf(stderr, "\nmixture posterior for β:\n  mean = [");
		for (intptr_t j = 0; j < p; j++)
			std::fprintf(stderr, "%s%.4f", j ? ", " : "", mix_mean[j]);
		std::fprintf(stderr, "]\n  sd   = [");
		for (intptr_t j = 0; j < p; j++)
			std::fprintf(stderr, "%s%.4f", j ? ", " : "",
			             std::sqrt(std::max(mix_var(j, j), 0.0)));
		std::fprintf(stderr, "]\n  correlation matrix:\n");
		for (intptr_t i = 0; i < p; i++)
		{
			std::fprintf(stderr, "   ");
			for (intptr_t j = 0; j < p; j++)
			{
				double dii = std::max(mix_var(i, i), 1e-30);
				double djj = std::max(mix_var(j, j), 1e-30);
				double corr = mix_var(i, j) / std::sqrt(dii * djj);
				std::fprintf(stderr, " %7.3f", corr);
			}
			std::fprintf(stderr, "\n");
		}
		std::fprintf(stderr, "=== end PHON_DIAG_GRID ===\n\n");
		}
	}

	// ── 7. Mixture CDF, quantiles, and posterior summaries ──────

	boost::math::normal_distribution<double> normal;
	double z_975 = boost::math::quantile(normal, 0.975);

	// Mixture CDF for coefficient j at value x:
	//   F_j(x) = Σ_k w_k Φ((x − β̃_k[j]) / σ_k[j])
	auto mix_cdf = [&](intptr_t j, double x) -> double
	{
		double cdf = 0;
		for (intptr_t k = 0; k < n_grid; k++)
		{
			double mu_k = sla_beta[k][j];
			double sd_k = std::sqrt(std::max(results[k].vcov_beta(j, j), 1e-20));
			cdf += w[k] * boost::math::cdf(normal, (x - mu_k) / sd_k);
		}
		return cdf;
	};

	// Quantile of the mixture CDF for coefficient j via bisection.
	auto mix_quantile = [&](intptr_t j, double q) -> double
	{
		// Initial bracket: mean ± 10 × sd
		double sd_j = std::sqrt(std::max(mix_var(j, j), 1e-20));
		double lo = mix_mean[j] - 10.0 * sd_j;
		double hi = mix_mean[j] + 10.0 * sd_j;

		for (int iter = 0; iter < 60; iter++)
		{
			double mid = 0.5 * (lo + hi);
			if (mix_cdf(j, mid) < q)
				lo = mid;
			else
				hi = mid;
		}
		return 0.5 * (lo + hi);
	};

	// Save the mode: β̂(θ*) — already in model.beta before we overwrite it.
	model.posterior_mode = Array<double>(p, 0.0);
	for (intptr_t j = 0; j < p; j++) {
		model.posterior_mode[j + 1] = model.beta[j + 1];
	}

	model.posterior_mean = Array<double>(p, 0.0);
	model.posterior_median = Array<double>(p, 0.0);
	model.posterior_sd = Array<double>(p, 0.0);
	model.ci_lower = Array<double>(p, 0.0);
	model.ci_upper = Array<double>(p, 0.0);
	model.pd = Array<double>(p, 0.0);

	for (intptr_t j = 0; j < p; j++)
	{
		double mean = mix_mean[j];
		double var = mix_var(j, j);
		double sd = (var > 0) ? std::sqrt(var) : 0.0;

		model.posterior_mean[j + 1] = mean;
		model.posterior_sd[j + 1] = sd;

		// Quantile-based CI and median from the mixture CDF.
		model.ci_lower[j + 1] = mix_quantile(j, 0.025);
		model.ci_upper[j + 1] = mix_quantile(j, 0.975);
		model.posterior_median[j + 1] = mix_quantile(j, 0.5);

		// pd from the mixture CDF: P(sign(β) = sign(E[β]))
		double p_positive = 1.0 - mix_cdf(j, 0.0);
		model.pd[j + 1] = (mean >= 0) ? p_positive : (1.0 - p_positive);
	}

	// Update beta/se/stat for compatibility with display code.
	for (intptr_t j = 0; j < p; j++)
	{
		model.beta[j + 1] = mix_mean[j];
		model.se[j + 1] = model.posterior_sd[j + 1];
		model.stat[j + 1] = (model.se[j + 1] > 0) ? model.beta[j + 1] / model.se[j + 1] : 0.0;
		model.p[j + 1] = std::numeric_limits<double>::quiet_NaN();
	}

	// Update vcov to the mixture posterior covariance.
	for (intptr_t i = 0; i < p; i++) {
		for (intptr_t j = 0; j < p; j++) {
			model.vcov(i + 1, j + 1) = mix_var(i, j);
		}
	}

	// ── 8. Hyperparameter posteriors ─────────────────────────────
	//
	// From the grid weights, compute marginal posterior mean and SD
	// for each variance component SD and the residual SD.

	intptr_t n_hyper = 0;
	for (intptr_t g = 0; g < lay.G; g++)
		n_hyper += lay.q[g];
	n_hyper += 1;  // residual SD (Gaussian)

	model.hyper_names = Array<String>(n_hyper, String());
	model.hyper_posterior_mean = Array<double>(n_hyper, 0.0);
	model.hyper_posterior_sd = Array<double>(n_hyper, 0.0);
	model.hyper_ci_lower = Array<double>(n_hyper, 0.0);
	model.hyper_ci_upper = Array<double>(n_hyper, 0.0);

	{
		// For each grid point, extract the SDs from θ_k.
		// Build hyper names from the model's random_effects (already populated).
		intptr_t idx = 1;
		intptr_t chol_pos_base = 0;

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			auto &re = model.random_effects[g + 1]; // 1-based Array
			for (intptr_t t = 0; t < qg; t++)
			{
				// Name: "sd(term|group)"
				std::string name = "sd(" + std::string(re.term_names[t + 1].data(), re.term_names[t + 1].size())
				                 + "|" + std::string(re.group_name.data(), re.group_name.size()) + ")";
				model.hyper_names[idx] = String(name);

				// Marginal posterior of σ_{g,t}
				double mean_sd = 0, mean_sd2 = 0;
				for (intptr_t k = 0; k < n_grid; k++)
				{
					Eigen::VectorXd theta_k = theta_star + T * z_points[k];
					Eigen::MatrixXd L = unpack_cholesky(theta_k.data() + chol_pos_base, qg);
					Eigen::MatrixXd D = cholesky_to_cov(L);
					double sd_val = std::sqrt(std::max(D(t, t), 0.0));
					mean_sd += w[k] * sd_val;
					mean_sd2 += w[k] * sd_val * sd_val;
				}
				double var_sd = mean_sd2 - mean_sd * mean_sd;
				double sd_sd = (var_sd > 0) ? std::sqrt(var_sd) : 0.0;

				model.hyper_posterior_mean[idx] = mean_sd;
				model.hyper_posterior_sd[idx] = sd_sd;
				model.hyper_ci_lower[idx] = std::max(0.0, mean_sd - z_975 * sd_sd);
				model.hyper_ci_upper[idx] = mean_sd + z_975 * sd_sd;
				idx++;
			}
			chol_pos_base += n_chol_params(qg);
		}

		// Residual SD
		model.hyper_names[idx] = "sd(residual)";
		{
			double mean_sd = 0, mean_sd2 = 0;
			for (intptr_t k = 0; k < n_grid; k++)
			{
				Eigen::VectorXd theta_k = theta_star + T * z_points[k];
				double sd_val = std::exp(theta_k[n_chol]);
				mean_sd += w[k] * sd_val;
				mean_sd2 += w[k] * sd_val * sd_val;
			}
			double var_sd = mean_sd2 - mean_sd * mean_sd;
			double sd_sd = (var_sd > 0) ? std::sqrt(var_sd) : 0.0;

			model.hyper_posterior_mean[idx] = mean_sd;
			model.hyper_posterior_sd[idx] = sd_sd;
			model.hyper_ci_lower[idx] = std::max(0.0, mean_sd - z_975 * sd_sd);
			model.hyper_ci_upper[idx] = mean_sd + z_975 * sd_sd;
		}
	}

	// ── 9. Laplace-approximated log marginal likelihood ─────────
	//
	// log p(y) ≈ -f(θ*) + (d/2) log(2π) - 0.5 Σ_j log(λ_j)
	//
	// where f(θ*) is the neg-log-posterior at the mode and λ_j are
	// the eigenvalues of the Hessian of f at θ*.
	{
		// build_ccd_points places the center (z = 0, which maps to θ*) at
		// index 0; sanitise_grid_points preserves the order. If the grid
		// construction ever changes, this index must move with it.
		constexpr intptr_t CENTER_IDX = 0;

		static const double log_2pi = std::log(2.0 * M_PI);
		double sum_log_eig = 0;
		for (intptr_t j = 0; j < d; j++)
			sum_log_eig += std::log(eigenvalues[j]);

		model.log_marginal = -results[CENTER_IDX].neg_log_posterior
		                   + 0.5 * d * log_2pi
		                   - 0.5 * sum_log_eig;
	}

	// ── 10. WAIC (computed at fit time while X, y, grid results in scope) ──

	compute_grid_waic(model, results, w, z_points, theta_star, T,
	                   Xm, ym, n, p, n_chol,
	                   // Gaussian: identity link
	                   [](double eta) { return eta; },
	                   // Gaussian: σ = exp(θ[n_chol])
	                   [n_chol](const Eigen::VectorXd &theta, double *disp) {
	                       disp[0] = std::exp(theta[n_chol]);
	                   },
	                   off_ptr,
	                   // Gaussian enables conditional u-sampling to capture the
	                   // β↔u ridge cancellation (matches brms's default waic).
	                   &lay);
}
// Populates the Model's posterior fields with mixture-based estimates.
//
// Outer θ layout: (chol_1, ..., chol_G, [log disp...])
// where n_disp = 0 (binomial/Poisson), 1 (NB/beta), or 2 (Student t).
//
// u_warm_init: if size == lay.J_total, used as a warm start for both the
// FD-Hessian objective evaluations (via PirlsObjective::last_u) and the
// inner PIRLS solve at each grid point. The PIRLS Newton iterations have
// no step-halving or trust-region, so cold-starting from u=0 on binomial
// GLMMs with strong random effects can OVERSHOOT to a non-mode point;
// passing the joint optimizer's converged û keeps us on the correct mode
// and gives a sensible outer Hessian.
static void inla_grid_integrate_pirls(
	Model &model,
	const PirlsObjective &obj,
	const Eigen::VectorXd &theta_star,
	const Family &fam,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p, intptr_t n_chol,
	const Eigen::VectorXd &beta_init,
	const PriorSpec *priors,
	const Array<String> *coef_names,
	const Eigen::VectorXd *off_ptr = nullptr,
	const Eigen::VectorXd &u_warm_init = Eigen::VectorXd())
{
	intptr_t d = theta_star.size();  // n_chol + n_disp

	// ── Defense-in-depth: guard against NaN θ* ───────────────────────
	// See the Gaussian path for rationale. Non-finite θ* here means the
	// outer PIRLS/joint optimizer silently produced garbage — fail loud.
	if (!theta_star.allFinite()) {
		throw error(
			"inla_grid_integrate_pirls: θ* contains non-finite values "
			"(size=%). Mode-finding failed silently upstream — this is a "
			"bug in the Bayesian optimizer path, not in the model spec.",
			(long) d);
	}

	// ── Seed the PirlsObjective warm-start ───────────────────────────
	// PirlsObjective::eval uses last_u as the inner u_init for solve_pirls
	// and overwrites last_u with the converged u for the next eval. Without
	// a seed it starts from an empty vector → solve_pirls cold-starts u=0,
	// which on this regime (binomial + strong RE) overshoots to a non-mode
	// point and produces astronomical FD Hessians (observed: λ ≈ 1e17,
	// causing T = 1/√λ ≈ 1e-9 so the grid collapses to the mode and σ
	// hyperparameter posteriors collapse to a delta).
	//
	// last_u is mutable on a const PirlsObjective &, by design — see the
	// declaration in struct PirlsObjective.
	if (u_warm_init.size() == lay.J_total) {
		obj.last_u = u_warm_init;
	}

	// ── 1. Hessian at the mode ───────────────────────────────────
	Eigen::MatrixXd H = compute_fd_hessian(obj, theta_star);

	// ── 2. Eigendecomposition H = V Λ V' ─────────────────────────
	Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(H);
	Eigen::VectorXd eigenvalues = eig.eigenvalues();
	Eigen::MatrixXd V = eig.eigenvectors();

	for (intptr_t j = 0; j < d; j++) {
		eigenvalues[j] = std::max(eigenvalues[j], 1e-6);
	}

	Eigen::VectorXd inv_sqrt_lambda = eigenvalues.array().rsqrt().matrix();
	Eigen::MatrixXd T = V * inv_sqrt_lambda.asDiagonal();

	// ── 3. CCD grid points in z-space ────────────────────────────
	auto z_points = build_ccd_points(d, 3.0);
	intptr_t n_grid = (intptr_t)z_points.size();

	// ── 4. Evaluate at each grid point ───────────────────────────
	std::vector<GridPointResult> results(n_grid);
	std::vector<double> log_posterior(n_grid);

	for (intptr_t k = 0; k < n_grid; k++)
	{
		Eigen::VectorXd theta_k = theta_star + T * z_points[k];
		results[k] = eval_pirls_grid_point(theta_k, fam, Xm, ym, lay,
		                                    n, p, n_chol, beta_init,
		                                    priors, coef_names, off_ptr,
		                                    u_warm_init);
		log_posterior[k] = -results[k].neg_log_posterior;
	}

	// ── 4b. Discard invalid grid points ──────────────────────────
	sanitise_grid_points(results, log_posterior, n_grid, p);

	// ── 5. Integration weights ───────────────────────────────────
	// CCD base weights × density-ratio correction. See the parallel section
	// in inla_grid_integrate_gaussian for the full rationale.
	auto w_ccd = ccd_base_weights(d, 3.0, z_points);
	std::vector<double> log_w(n_grid);
	double max_log_w = -std::numeric_limits<double>::infinity();
	for (intptr_t k = 0; k < n_grid; k++)
	{
		if (w_ccd[k] <= 0.0 || !std::isfinite(log_posterior[k]))
		{
			log_w[k] = -std::numeric_limits<double>::infinity();
			continue;
		}
		log_w[k] = std::log(w_ccd[k]) + log_posterior[k]
		         + 0.5 * z_points[k].squaredNorm();
		if (log_w[k] > max_log_w) max_log_w = log_w[k];
	}

	std::vector<double> w(n_grid, 0.0);
	if (std::isfinite(max_log_w))
	{
		double sum_w = 0;
		for (intptr_t k = 0; k < n_grid; k++)
		{
			w[k] = std::isfinite(log_w[k]) ? std::exp(log_w[k] - max_log_w) : 0.0;
			sum_w += w[k];
		}
		double inv_sum = (sum_w > 0) ? 1.0 / sum_w : 0.0;
		for (intptr_t k = 0; k < n_grid; k++) w[k] *= inv_sum;
	}
	else
	{
		for (intptr_t k = 0; k < n_grid; k++) w[k] = 1.0 / (double) n_grid;
	}

	// ── 5b. Per-grid-point conditional means ───────────────────
	//
	// The current implementation uses the Gaussian Laplace approximation
	// at each grid point without any additional skewness correction:
	//
	//   μ̃_j(θ_k) = β̂_j(θ_k) = mode of π_G(β_j | θ_k, y)
	//
	// A simplified Laplace (SLA) correction along the lines of
	// Rue, Martino & Chopin (2009) §3.2.2 was previously applied as
	//
	//   μ̃_j(θ_k) = β̂_j(θ_k) + ½ d₃_j(θ_k) σ⁴_j(θ_k)
	//
	// where d₃_j = Σ_i X³_{ij} ℓ'''(η̂_i).  This formula is only first-
	// order correct for the *marginal* posterior of β_j when the mixed
	// third derivatives ∂³ℓ/∂β_j∂β_k∂β_l are negligible — i.e., when β_j
	// is effectively independent of the other fixed effects.  For the
	// **intercept** column (all 1's, correlated with every other coefficient)
	// this assumption is routinely violated, and we observed
	// 0.1–0.3 logit over-corrections on binomial/Poisson GLMM intercepts
	// relative to brms (NUTS MCMC) and INLA references, with no benefit
	// to slope estimates (whose SLA shifts are numerically negligible —
	// ≤ 0.003 on the logit scale for typical designs).
	//
	// A properly marginalised SLA correction would require the full
	// third-derivative tensor of ℓ plus a correction derived from
	// integrating the other β's out Laplace-style.  That is deferred.
	// For now we use the plain Laplace-at-the-mode β̂_j(θ_k), which
	// matches INLA's default for non-Gaussian models and gives posterior
	// means within MC noise of brms.
	//
	// Note: for Gaussian families d₃ = 0 always (ℓ'''(η) ≡ 0 for the
	// normal log-density w.r.t. η), so this branch was a no-op there.
	// The variable name sla_beta is kept to minimise downstream churn;
	// it now simply aliases β̂_k.

	std::vector<Eigen::VectorXd> sla_beta(n_grid);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		sla_beta[k] = results[k].beta;
	}

	// ── 6. Mixture posterior for β ───────────────────────────────
	Eigen::VectorXd mix_mean = Eigen::VectorXd::Zero(p);
	for (intptr_t k = 0; k < n_grid; k++) {
		mix_mean += w[k] * sla_beta[k];
	}

	Eigen::MatrixXd mix_var = Eigen::MatrixXd::Zero(p, p);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		Eigen::VectorXd diff = sla_beta[k] - mix_mean;
		mix_var += w[k] * (results[k].vcov_beta + diff * diff.transpose());
	}

	// ── Diagnostic dump (set env var PHON_DIAG_GRID=1 to enable) ────
	//
	// Built-in, runtime-toggled diagnostic for INLA grid integration in
	// the non-Gaussian (PIRLS) path. Same idiom as PHON_DIAG_LAPLACE:
	// compiled in always, zero cost when off, no recompile required.
	//
	// Useful for diagnosing:
	//   (1) hyperparameter posterior collapse (Post.SD = 0): if a single
	//       grid point gets all the weight, the σ marginal collapses to
	//       a delta. Verify by inspecting the w column.
	//   (2) mix_mean displaced from β̂(θ*) without any grid point
	//       looking dominant: the per-coefficient breakdown shows
	//       w_k · β_k[j] term-by-term so the displacement source is
	//       visible directly.
	//   (3) PIRLS divergence at axial points: extreme |β| at large |z|
	//       paired with finite log_post indicates the inner solver
	//       converged to a numerically unreasonable mode rather than
	//       the conditional posterior mode at θ_k.
	//
	// Pipe the run through `2> grid_diag.log` to capture.
	{
		static const bool diag_grid_on = []() {
			const char *e = std::getenv("PHON_DIAG_GRID");
			return e && e[0] && e[0] != '0';
		}();
		if (diag_grid_on)
		{
		intptr_t n_disp = d - n_chol;
		std::fprintf(stderr,
			"\n=== PHON_DIAG_GRID: inla_grid_integrate_pirls ===\n");
		std::fprintf(stderr,
			"family=%s  d=%ld  n_grid=%ld  n_chol=%ld  n_disp=%ld  p=%ld  G=%ld\n",
			std::string(fam.name.data(), fam.name.size()).c_str(),
			(long)d, (long)n_grid, (long)n_chol, (long)n_disp,
			(long)p, (long)lay.G);

		// Outer Hessian eigenvalues (clamped to ≥1e-6 already)
		std::fprintf(stderr, "H eigenvalues (clamped): ");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, "%.4g ", eigenvalues[j]);
		std::fprintf(stderr, "\n");

		// T column norms (= 1/√λ each), the per-axis step in θ-space
		// produced by a unit step in z-space.
		std::fprintf(stderr, "T column norms (axial θ step per unit z): ");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, "%.4g ", T.col(j).norm());
		std::fprintf(stderr, "\n");

		// θ* (the mode, in log-σ Cholesky [+ log dispersion] coords)
		std::fprintf(stderr, "theta_star = [");
		for (intptr_t j = 0; j < d; j++)
			std::fprintf(stderr, "%s%.4f", j ? ", " : "", theta_star[j]);
		std::fprintf(stderr, "]\n");

		// ── Per-grid-point table ────────────────────────────────────
		std::fprintf(stderr, "\nper-grid-point summary:\n");
		std::fprintf(stderr, "%-3s %-8s %-9s %-11s %-8s",
		             "k", "|z|", "w_ccd", "log_post", "w");
		for (intptr_t g = 0; g < lay.G; g++)
			for (intptr_t t = 0; t < lay.q[g]; t++)
				std::fprintf(stderr, " sd[g%ld,t%ld]", (long)g, (long)t);
		// Dispersion columns vary by family
		for (intptr_t j = 0; j < n_disp; j++)
			std::fprintf(stderr, " %-9s", "disp");
		for (intptr_t j = 0; j < p; j++)
			std::fprintf(stderr, " %-9s", "beta");
		std::fprintf(stderr, "\n");

		for (intptr_t k = 0; k < n_grid; k++)
		{
			Eigen::VectorXd theta_k = theta_star + T * z_points[k];
			std::fprintf(stderr, "%-3ld %-8.3f %-9.4g %-11.4f %-8.4f",
			             (long)k, z_points[k].norm(), w_ccd[k],
			             std::isfinite(log_posterior[k]) ? log_posterior[k] : NAN,
			             w[k]);

			// Recover σ values from θ_k for each RE diag term
			intptr_t cp = 0;
			for (intptr_t g = 0; g < lay.G; g++) {
				intptr_t qg = lay.q[g];
				Eigen::MatrixXd L = unpack_cholesky(theta_k.data() + cp, qg);
				Eigen::MatrixXd D = cholesky_to_cov(L);
				for (intptr_t t = 0; t < qg; t++)
					std::fprintf(stderr, " %-10.3f",
					             std::sqrt(std::max(D(t, t), 0.0)));
				cp += n_chol_params(qg);
			}

			// Dispersion parameters: NB θ_nb=exp(θ[n_chol]); Beta φ=exp(θ[n_chol]);
			// Student σ=exp(θ[n_chol]), ν=clamp(exp(θ[n_chol+1]), 2, 200).
			for (intptr_t j = 0; j < n_disp; j++) {
				double dv = std::exp(theta_k[n_chol + j]);
				if (fam.name == "student" && j == 1)
					dv = std::clamp(dv, 2.0, 200.0);
				std::fprintf(stderr, " %-9.3f", dv);
			}

			for (intptr_t j = 0; j < p; j++)
				std::fprintf(stderr, " %-9.3f", results[k].beta[j]);
			std::fprintf(stderr, "\n");
		}

		// ── Per-coefficient mixture mean breakdown ─────────────────
		// For each fixed effect j, list w_k · β_k[j] across k so that
		// Σ_k w_k · β_k[j] = mix_mean[j] is visible term-by-term.
		// Critical for diagnosing cases where mix_mean != β̂(θ*) but
		// no single grid point looks dominant.
		std::fprintf(stderr,
			"\nper-coefficient mixture mean breakdown (Σ_k w_k · β_k[j]):\n");
		for (intptr_t j = 0; j < p; j++)
		{
			const char *cname = "?";
			std::string cname_buf;
			if (coef_names && (intptr_t)(j + 1) <= coef_names->size()) {
				cname_buf = std::string((*coef_names)[j + 1].data(),
				                          (*coef_names)[j + 1].size());
				cname = cname_buf.c_str();
			}
			std::fprintf(stderr, "  β[%ld]=%s:  mode=%+.4f  mean=%+.4f\n",
				(long)j, cname, results[0].beta[j], mix_mean[j]);
			for (intptr_t k = 0; k < n_grid; k++) {
				if (std::abs(w[k]) < 1e-12) continue;
				std::fprintf(stderr,
					"    k=%-3ld w=%.4f  β_k=%+.4f  contrib=%+.4f\n",
					(long)k, w[k], results[k].beta[j],
					w[k] * results[k].beta[j]);
			}
		}

		// ── Per-grid-point correlation matrices of Σ_β(θ_k) ─────────
		// Only show grid points with non-negligible weight.
		std::fprintf(stderr,
			"\nper-grid-point correlation matrix of Σ_β(θ_k)  (coef_names: ");
		if (coef_names) {
			for (intptr_t j = 1; j <= p; j++)
				std::fprintf(stderr, "%s%s", j > 1 ? ", " : "",
				             std::string((*coef_names)[j].data(),
				                         (*coef_names)[j].size()).c_str());
		} else {
			std::fprintf(stderr, "unavailable");
		}
		std::fprintf(stderr, "):\n");

		for (intptr_t k = 0; k < n_grid; k++)
		{
			if (w[k] < 1e-6) continue;
			std::fprintf(stderr, "  k=%ld  (w=%.4f):\n", (long)k, w[k]);
			for (intptr_t i = 0; i < p; i++)
			{
				std::fprintf(stderr, "   ");
				for (intptr_t j = 0; j < p; j++)
				{
					double dii = std::max(results[k].vcov_beta(i, i), 1e-30);
					double djj = std::max(results[k].vcov_beta(j, j), 1e-30);
					double corr = results[k].vcov_beta(i, j)
					            / std::sqrt(dii * djj);
					std::fprintf(stderr, " %7.3f", corr);
				}
				std::fprintf(stderr, "\n");
			}
		}

		// ── Mixture posterior summary ───────────────────────────────
		std::fprintf(stderr, "\nmixture posterior for β:\n  mean = [");
		for (intptr_t j = 0; j < p; j++)
			std::fprintf(stderr, "%s%.4f", j ? ", " : "", mix_mean[j]);
		std::fprintf(stderr, "]\n  sd   = [");
		for (intptr_t j = 0; j < p; j++)
			std::fprintf(stderr, "%s%.4f", j ? ", " : "",
			             std::sqrt(std::max(mix_var(j, j), 0.0)));
		std::fprintf(stderr, "]\n  correlation matrix:\n");
		for (intptr_t i = 0; i < p; i++)
		{
			std::fprintf(stderr, "   ");
			for (intptr_t j = 0; j < p; j++)
			{
				double dii = std::max(mix_var(i, i), 1e-30);
				double djj = std::max(mix_var(j, j), 1e-30);
				double corr = mix_var(i, j) / std::sqrt(dii * djj);
				std::fprintf(stderr, " %7.3f", corr);
			}
			std::fprintf(stderr, "\n");
		}
		std::fprintf(stderr, "=== end PHON_DIAG_GRID ===\n\n");
		}
	}

	// ── 7. Mixture CDF, quantiles, and posterior summaries ──────

	boost::math::normal_distribution<double> normal;
	double z_975 = boost::math::quantile(normal, 0.975);

	auto mix_cdf = [&](intptr_t j, double x) -> double
	{
		double cdf = 0;
		for (intptr_t k = 0; k < n_grid; k++)
		{
			double mu_k = sla_beta[k][j];
			double sd_k = std::sqrt(std::max(results[k].vcov_beta(j, j), 1e-20));
			cdf += w[k] * boost::math::cdf(normal, (x - mu_k) / sd_k);
		}
		return cdf;
	};

	auto mix_quantile = [&](intptr_t j, double q) -> double
	{
		double sd_j = std::sqrt(std::max(mix_var(j, j), 1e-20));
		double lo = mix_mean[j] - 10.0 * sd_j;
		double hi = mix_mean[j] + 10.0 * sd_j;
		for (int iter = 0; iter < 60; iter++)
		{
			double mid = 0.5 * (lo + hi);
			if (mix_cdf(j, mid) < q)
				lo = mid;
			else
				hi = mid;
		}
		return 0.5 * (lo + hi);
	};

	// Save the mode: β̂(θ*) — already in model.beta before we overwrite it.
	model.posterior_mode = Array<double>(p, 0.0);
	for (intptr_t j = 0; j < p; j++) {
		model.posterior_mode[j + 1] = model.beta[j + 1];
	}

	model.posterior_mean = Array<double>(p, 0.0);
	model.posterior_median = Array<double>(p, 0.0);
	model.posterior_sd = Array<double>(p, 0.0);
	model.ci_lower = Array<double>(p, 0.0);
	model.ci_upper = Array<double>(p, 0.0);
	model.pd = Array<double>(p, 0.0);

	for (intptr_t j = 0; j < p; j++)
	{
		double mean = mix_mean[j];
		double var = mix_var(j, j);
		double sd = (var > 0) ? std::sqrt(var) : 0.0;

		model.posterior_mean[j + 1] = mean;
		model.posterior_sd[j + 1] = sd;

		model.ci_lower[j + 1] = mix_quantile(j, 0.025);
		model.ci_upper[j + 1] = mix_quantile(j, 0.975);
		model.posterior_median[j + 1] = mix_quantile(j, 0.5);

		double p_positive = 1.0 - mix_cdf(j, 0.0);
		model.pd[j + 1] = (mean >= 0) ? p_positive : (1.0 - p_positive);
	}

	// Update beta/se/stat for compatibility with display code.
	for (intptr_t j = 0; j < p; j++)
	{
		model.beta[j + 1] = mix_mean[j];
		model.se[j + 1] = model.posterior_sd[j + 1];
		model.stat[j + 1] = (model.se[j + 1] > 0) ? model.beta[j + 1] / model.se[j + 1] : 0.0;
		model.p[j + 1] = std::numeric_limits<double>::quiet_NaN();
	}

	// Update vcov to the mixture posterior covariance.
	for (intptr_t i = 0; i < p; i++) {
		for (intptr_t j = 0; j < p; j++) {
			model.vcov(i + 1, j + 1) = mix_var(i, j);
		}
	}

	// ── 8. Hyperparameter posteriors ─────────────────────────────
	//
	// Random-effect SDs (from Cholesky at each grid point) plus any
	// family-specific dispersion parameters (θ_nb, φ_beta, σ_t, ν_t).

	intptr_t n_re_hyper = 0;
	for (intptr_t g = 0; g < lay.G; g++)
		n_re_hyper += lay.q[g];

	intptr_t n_disp = fam.n_dispersion_params();

	// Count dispersion hyperparameters by name
	// NB: 1 (θ_nb), Beta: 1 (φ), Student: 2 (σ, ν)
	intptr_t n_hyper = n_re_hyper + n_disp;

	model.hyper_names = Array<String>(n_hyper, String());
	model.hyper_posterior_mean = Array<double>(n_hyper, 0.0);
	model.hyper_posterior_sd = Array<double>(n_hyper, 0.0);
	model.hyper_ci_lower = Array<double>(n_hyper, 0.0);
	model.hyper_ci_upper = Array<double>(n_hyper, 0.0);

	{
		intptr_t idx = 1;
		intptr_t chol_pos_base = 0;

		// ── Random-effect SDs ──────────────────────────────────────
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			auto &re = model.random_effects[g + 1]; // 1-based Array
			for (intptr_t t = 0; t < qg; t++)
			{
				std::string name = "sd(" + std::string(re.term_names[t + 1].data(), re.term_names[t + 1].size())
				                 + "|" + std::string(re.group_name.data(), re.group_name.size()) + ")";
				model.hyper_names[idx] = String(name);

				double mean_sd = 0, mean_sd2 = 0;
				for (intptr_t k = 0; k < n_grid; k++)
				{
					Eigen::VectorXd theta_k = theta_star + T * z_points[k];
					Eigen::MatrixXd L = unpack_cholesky(theta_k.data() + chol_pos_base, qg);
					Eigen::MatrixXd D = cholesky_to_cov(L);
					double sd_val = std::sqrt(std::max(D(t, t), 0.0));
					mean_sd += w[k] * sd_val;
					mean_sd2 += w[k] * sd_val * sd_val;
				}
				double var_sd = mean_sd2 - mean_sd * mean_sd;
				double sd_sd = (var_sd > 0) ? std::sqrt(var_sd) : 0.0;

				model.hyper_posterior_mean[idx] = mean_sd;
				model.hyper_posterior_sd[idx] = sd_sd;
				model.hyper_ci_lower[idx] = std::max(0.0, mean_sd - z_975 * sd_sd);
				model.hyper_ci_upper[idx] = mean_sd + z_975 * sd_sd;
				idx++;
			}
			chol_pos_base += n_chol_params(qg);
		}

		// ── Family-specific dispersion hyperparameters ─────────────
		if (fam.name == "negbin")
		{
			model.hyper_names[idx] = "theta(NB)";
			double mean_v = 0, mean_v2 = 0;
			for (intptr_t k = 0; k < n_grid; k++)
			{
				Eigen::VectorXd theta_k = theta_star + T * z_points[k];
				double val = std::exp(theta_k[n_chol]);
				mean_v += w[k] * val;
				mean_v2 += w[k] * val * val;
			}
			double var_v = mean_v2 - mean_v * mean_v;
			double sd_v = (var_v > 0) ? std::sqrt(var_v) : 0.0;
			model.hyper_posterior_mean[idx] = mean_v;
			model.hyper_posterior_sd[idx] = sd_v;
			model.hyper_ci_lower[idx] = std::max(0.0, mean_v - z_975 * sd_v);
			model.hyper_ci_upper[idx] = mean_v + z_975 * sd_v;
		}
		else if (fam.name == "beta")
		{
			model.hyper_names[idx] = "phi(beta)";
			double mean_v = 0, mean_v2 = 0;
			for (intptr_t k = 0; k < n_grid; k++)
			{
				Eigen::VectorXd theta_k = theta_star + T * z_points[k];
				double val = std::exp(theta_k[n_chol]);
				mean_v += w[k] * val;
				mean_v2 += w[k] * val * val;
			}
			double var_v = mean_v2 - mean_v * mean_v;
			double sd_v = (var_v > 0) ? std::sqrt(var_v) : 0.0;
			model.hyper_posterior_mean[idx] = mean_v;
			model.hyper_posterior_sd[idx] = sd_v;
			model.hyper_ci_lower[idx] = std::max(0.0, mean_v - z_975 * sd_v);
			model.hyper_ci_upper[idx] = mean_v + z_975 * sd_v;
		}
		else if (fam.name == "student")
		{
			// σ (scale)
			model.hyper_names[idx] = "sigma(student)";
			{
				double mean_v = 0, mean_v2 = 0;
				for (intptr_t k = 0; k < n_grid; k++)
				{
					Eigen::VectorXd theta_k = theta_star + T * z_points[k];
					double val = std::exp(theta_k[n_chol]);
					mean_v += w[k] * val;
					mean_v2 += w[k] * val * val;
				}
				double var_v = mean_v2 - mean_v * mean_v;
				double sd_v = (var_v > 0) ? std::sqrt(var_v) : 0.0;
				model.hyper_posterior_mean[idx] = mean_v;
				model.hyper_posterior_sd[idx] = sd_v;
				model.hyper_ci_lower[idx] = std::max(0.0, mean_v - z_975 * sd_v);
				model.hyper_ci_upper[idx] = mean_v + z_975 * sd_v;
			}
			idx++;

			// ν (degrees of freedom)
			model.hyper_names[idx] = "nu(student)";
			{
				double mean_v = 0, mean_v2 = 0;
				for (intptr_t k = 0; k < n_grid; k++)
				{
					Eigen::VectorXd theta_k = theta_star + T * z_points[k];
					double val = std::clamp(std::exp(theta_k[n_chol + 1]), 2.0, 200.0);
					mean_v += w[k] * val;
					mean_v2 += w[k] * val * val;
				}
				double var_v = mean_v2 - mean_v * mean_v;
				double sd_v = (var_v > 0) ? std::sqrt(var_v) : 0.0;
				model.hyper_posterior_mean[idx] = mean_v;
				model.hyper_posterior_sd[idx] = sd_v;
				model.hyper_ci_lower[idx] = std::max(0.0, mean_v - z_975 * sd_v);
				model.hyper_ci_upper[idx] = mean_v + z_975 * sd_v;
			}
		}
	}

	// ── 9. Laplace-approximated log marginal likelihood ─────────
	{
		// build_ccd_points places the center (z = 0, which maps to θ*) at
		// index 0; sanitise_grid_points preserves the order.
		constexpr intptr_t CENTER_IDX = 0;

		static const double log_2pi = std::log(2.0 * M_PI);
		double sum_log_eig = 0;
		for (intptr_t j = 0; j < d; j++)
			sum_log_eig += std::log(eigenvalues[j]);

		model.log_marginal = -results[CENTER_IDX].neg_log_posterior
		                   + 0.5 * d * log_2pi
		                   - 0.5 * sum_log_eig;
	}

	// ── 10. WAIC ────────────────────────────────────────────────────

	// Inverse link function (scalar).
	std::function<double(double)> linkinv_fn;
	if (model.family == "binomial" || model.family == "beta") {
		linkinv_fn = [](double eta) { return 1.0 / (1.0 + std::exp(-eta)); };
	} else if (model.family == "student") {
		linkinv_fn = [](double eta) { return eta; };
	} else {
		// Poisson, NB: log link → exp
		linkinv_fn = [](double eta) { return std::exp(std::clamp(eta, -30.0, 30.0)); };
	}

	// Extract dispersion from θ_k.
	std::function<void(const Eigen::VectorXd &, double *)> disp_fn;
	if (model.family == "negbin") {
		disp_fn = [n_chol](const Eigen::VectorXd &theta, double *disp) {
			disp[0] = std::exp(theta[n_chol]);  // θ_nb
		};
	} else if (model.family == "beta") {
		disp_fn = [n_chol](const Eigen::VectorXd &theta, double *disp) {
			disp[0] = std::exp(theta[n_chol]);  // φ
		};
	} else if (model.family == "student") {
		disp_fn = [n_chol](const Eigen::VectorXd &theta, double *disp) {
			disp[0] = std::exp(theta[n_chol]);                             // σ
			disp[1] = std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0); // ν
		};
	} else {
		// Binomial, Poisson: no dispersion parameter
		disp_fn = [](const Eigen::VectorXd &, double *) {};
	}

	compute_grid_waic(model, results, w, z_points, theta_star, T,
	                   Xm, ym, n, p, n_chol,
	                   linkinv_fn, disp_fn, off_ptr,
	                   // Enable conditional u-sampling via the PIRLS-Laplace
	                   // approximation (uses working weights / response stored
	                   // at each grid point).
	                   &lay);
}


// =====================================================================
// No-random-effects Bayesian Laplace approximation for non-Gaussian families
// =====================================================================
//
// For Student-t / NB / Beta with no random effects (G == 0), the existing
// INLA grid integration paths in mixed_model() are gated on G > 0. Without
// a substitute, the model returned by mixed_model() has β at the joint MAP
// but everything downstream is partial:
//
//   • model.vcov is the Henderson conditional vcov(β | σ̂, ν̂), not the
//     marginal posterior covariance of β over dispersion.
//   • model.hyper_posterior_* is empty — no SD or CI for σ, ν, θ_NB, φ_β.
//   • model.log_marginal is NaN.
//   • The downstream bayesian_summaries() in fitting.cpp draws S samples of
//     β but reads m.sigma / m.nu / m.theta / m.phi fixed across draws,
//     undercounting p_waic by the number of dispersion params (~6 vs ~3
//     in the validation case for Student-t fits).
//
// This helper closes all four gaps by running a full Laplace approximation
// on the joint parameter vector φ = [β, log dispersion(s)] using the same
// LaplaceJointObjective the optimizer converged on. We compute the joint
// FD Hessian H_full at φ̂, invert to get the joint posterior covariance Σ,
// and read off:
//
//   • β posterior moments: top-left p×p block of Σ, marginal over dispersion.
//   • Dispersion posterior moments: trailing block, transformed back from
//     log scale via the lognormal closed form (E[X], Var[X], symmetric CI
//     on log scale).
//   • log_marginal: standard Laplace formula
//                   log p(y) ≈ -joint_NLP(φ̂) + (D/2) log 2π - ½ log det H_full
//   • WAIC and LOO: draw S=1000 joint samples from N(φ̂, Σ), compute
//                   pointwise log-likelihood with per-draw dispersion via
//                   the explicit-disp overload of pointwise_loglik in
//                   waic.hpp. This is the actual WAIC bug fix.
//
// Setting model.posterior_mean here makes fitting.cpp:1657's
// `if (model.posterior_mean.empty()) bayesian_summaries(...)` skip the
// buggy fallback. No change to fitting.cpp is needed.
//
// **Prior caveat.** LaplaceJointObjective applies the β prior and the
// variance-component prior (the latter empty for G==0), but does NOT apply
// the residual / dispersion prior on log σ, log ν, log θ_NB, log φ_β. The
// optimizer converges to (β̂, log disp̂) under flat improper priors on the
// log-dispersion parameters; this helper's Hessian is consistent with that
// objective. The mixed-RE path (inla_grid_integrate_pirls) does the same
// thing — it doesn't apply the dispersion prior in the optimization either,
// only in CCD reweighting, so the no-RE behaviour is internally consistent
// with how Phon currently treats dispersion priors throughout. Tightening
// the dispersion-prior treatment is a separate concern.
static void no_re_bayesian_laplace(
    Model &model,
    const Family &fam,
    const Eigen::Map<Matrix<double>> &Xm,
    const Eigen::Map<Vector<double>> &ym,
    const GroupLayout &lay,           // G == 0 expected
    intptr_t n, intptr_t p,
    intptr_t saved_n_chol,            // 0 for G == 0
    const Eigen::VectorXd &saved_theta, // [log dispersion(s)]
    const Eigen::VectorXd &beta_hat,
    const PriorSpec *priors,
    const Array<String> *coef_names,
    const Eigen::VectorXd *off_ptr)
{
	intptr_t n_disp = saved_theta.size();
	if (n_disp <= 0) return;            // No dispersion params — nothing to do.

	// ── 1. Assemble φ̂ = [β̂, log dispersion(s)] ────────────────────
	intptr_t D = p + n_disp;
	Eigen::VectorXd phi_hat(D);
	for (intptr_t j = 0; j < p; j++) phi_hat[j] = beta_hat[j];
	for (intptr_t k = 0; k < n_disp; k++) phi_hat[p + k] = saved_theta[k];

	// ── 2. Joint FD Hessian on the negative log-posterior ─────────
	// Same h_scale (1e-3) used by the random-slopes block at line ~7019.
	// last_u stays empty: solve_u_given_beta short-circuits when J == 0,
	// so the eval cost is just the unweighted neg-log-likelihood.
	LaplaceJointObjective joint_obj{fam, Xm, ym, lay, n, p,
	                                 saved_n_chol, priors, coef_names, off_ptr};

	Eigen::MatrixXd H_full = compute_fd_hessian(joint_obj, phi_hat, 1e-3);
	Eigen::LDLT<Eigen::MatrixXd> ldlt_full(H_full);
	if (ldlt_full.info() != Eigen::Success || !ldlt_full.isPositive())
	{
		// Hessian not PD — leave model.posterior_mean empty so the
		// downstream bayesian_summaries fallback runs. WAIC will then
		// have the documented dispersion-fixed bug for this fit, but
		// at least β posterior is still reported.
		return;
	}

	Eigen::MatrixXd Sigma = ldlt_full.solve(Eigen::MatrixXd::Identity(D, D));
	if (!Sigma.allFinite()) return;

	// ── 3. β posterior covariance (marginal over dispersion) ──────
	// Replaces the Henderson conditional vcov(β | dispersion).
	Eigen::MatrixXd vcov_beta = Sigma.topLeftCorner(p, p);
	bool ok_vcov = vcov_beta.allFinite();
	for (intptr_t j = 0; ok_vcov && j < p; j++)
		if (vcov_beta(j, j) <= 0) ok_vcov = false;
	if (ok_vcov)
	{
		for (intptr_t i = 0; i < p; i++)
			for (intptr_t j = 0; j < p; j++)
				model.vcov(i + 1, j + 1) = vcov_beta(i, j);
	}

	// ── 4. β posterior summaries ────────────────────────────────
	boost::math::normal_distribution<double> normal;
	double z_975 = boost::math::quantile(normal, 0.975);

	model.posterior_mean   = Array<double>(p, 0.0);
	model.posterior_mode   = Array<double>(p, 0.0);
	model.posterior_median = Array<double>(p, 0.0);
	model.posterior_sd     = Array<double>(p, 0.0);
	model.ci_lower         = Array<double>(p, 0.0);
	model.ci_upper         = Array<double>(p, 0.0);
	model.pd               = Array<double>(p, 0.0);

	for (intptr_t j = 0; j < p; j++)
	{
		double mean = beta_hat[j];                                        // joint MAP
		double sd   = std::sqrt(std::max(vcov_beta(j, j), 0.0));
		model.posterior_mean[j + 1]   = mean;
		model.posterior_mode[j + 1]   = mean;                              // Gaussian: mode = mean
		model.posterior_median[j + 1] = mean;                              // Gaussian: median = mean
		model.posterior_sd[j + 1]     = sd;
		model.ci_lower[j + 1]         = mean - z_975 * sd;
		model.ci_upper[j + 1]         = mean + z_975 * sd;
		model.pd[j + 1]               = (sd > 0)
		                                  ? boost::math::cdf(normal, std::abs(mean) / sd)
		                                  : 1.0;
	}

	// Update se / stat / p for compatibility with older display code.
	for (intptr_t j = 0; j < p; j++)
	{
		model.se[j + 1]   = model.posterior_sd[j + 1];
		model.stat[j + 1] = (model.se[j + 1] > 0)
		                      ? model.beta[j + 1] / model.se[j + 1]
		                      : 0.0;
		model.p[j + 1]    = std::numeric_limits<double>::quiet_NaN();
	}

	// ── 5. Dispersion posterior summaries (lognormal approximation) ──
	//
	// log X ~ N(μ, σ_log²) ⇒ X ~ Lognormal:
	//   E[X]   = exp(μ + σ_log²/2)
	//   Var[X] = (exp(σ_log²) − 1) · exp(2μ + σ_log²)
	// Median = exp(μ); CI = exp(μ ∓ z·σ_log) (symmetric on log scale,
	// asymmetric on direct scale — appropriate for positive params).
	auto fill_lognormal = [&](intptr_t idx, double log_mean, double log_sd,
	                          const String &name, double lower_bound = 0.0,
	                          double upper_bound = std::numeric_limits<double>::infinity())
	{
		double s2 = log_sd * log_sd;
		double mean_X = std::exp(log_mean + 0.5 * s2);
		double var_X  = (std::exp(s2) - 1.0) * std::exp(2.0 * log_mean + s2);
		double sd_X   = std::sqrt(std::max(var_X, 0.0));
		double lo     = std::exp(log_mean - z_975 * log_sd);
		double up     = std::exp(log_mean + z_975 * log_sd);
		// Respect any clamp bounds (relevant for ν ∈ [2, 200]).
		mean_X = std::clamp(mean_X, lower_bound, upper_bound);
		lo     = std::clamp(lo,     lower_bound, upper_bound);
		up     = std::clamp(up,     lower_bound, upper_bound);

		model.hyper_names[idx]          = name;
		model.hyper_posterior_mean[idx] = mean_X;
		model.hyper_posterior_sd[idx]   = sd_X;
		model.hyper_ci_lower[idx]       = lo;
		model.hyper_ci_upper[idx]       = up;
	};

	intptr_t n_hyper = n_disp;
	model.hyper_names           = Array<String>(n_hyper, String());
	model.hyper_posterior_mean  = Array<double>(n_hyper, 0.0);
	model.hyper_posterior_sd    = Array<double>(n_hyper, 0.0);
	model.hyper_ci_lower        = Array<double>(n_hyper, 0.0);
	model.hyper_ci_upper        = Array<double>(n_hyper, 0.0);

	if (fam.name == "negbin")
	{
		double log_th  = saved_theta[0];
		double log_sd  = std::sqrt(std::max(Sigma(p, p), 0.0));
		fill_lognormal(1, log_th, log_sd, String("theta(NB)"), 1e-10);
	}
	else if (fam.name == "beta")
	{
		double log_phi = saved_theta[0];
		double log_sd  = std::sqrt(std::max(Sigma(p, p), 0.0));
		fill_lognormal(1, log_phi, log_sd, String("phi(beta)"), 1e-10);
	}
	else if (fam.name == "student")
	{
		// σ ∈ (0, ∞)
		{
			double log_sigma = saved_theta[0];
			double log_sd    = std::sqrt(std::max(Sigma(p, p), 0.0));
			fill_lognormal(1, log_sigma, log_sd, String("sigma(student)"), 1e-10);
		}
		// ν ∈ [2, 200] (the same clamp solve_pirls / LaplaceJointObjective use).
		{
			double log_nu = saved_theta[1];
			double log_sd = std::sqrt(std::max(Sigma(p + 1, p + 1), 0.0));
			fill_lognormal(2, log_nu, log_sd, String("nu(student)"), 2.0, 200.0);
		}
	}

	// ── 6. Laplace log marginal likelihood ─────────────────────
	{
		static const double log_2pi = std::log(2.0 * M_PI);
		double nlp = joint_obj.eval(phi_hat);                              // -log p(y, φ̂)
		double log_det_H = 0;
		Eigen::VectorXd diag_D = ldlt_full.vectorD();
		for (intptr_t j = 0; j < D; j++)
			log_det_H += std::log(std::max(diag_D[j], 1e-30));
		model.log_marginal = -nlp + 0.5 * D * log_2pi - 0.5 * log_det_H;
	}

	// ── 7. WAIC and LOO via joint posterior sampling ──────────
	//
	// The actual bug fix: previous behaviour drew β only from
	// N(β̂, vcov_β), holding dispersion fixed across S draws — so
	// p_waic counted only β uncertainty (~p_waic ≈ p) and missed the
	// 1–2 extra dispersion parameters. Now we draw [β, log disp]
	// jointly and pass per-draw disp to pointwise_loglik(family, disp[]).
	constexpr int S = 1000;
	constexpr unsigned int SEED = 12345;

	Eigen::LLT<Eigen::MatrixXd> chol_post(Sigma);
	if (chol_post.info() != Eigen::Success) return;

	std::function<double(double)> linkinv_fn;
	if (fam.name == "beta") {
		linkinv_fn = [](double eta) { return 1.0 / (1.0 + std::exp(-eta)); };
	} else if (fam.name == "negbin") {
		linkinv_fn = [](double eta) { return std::exp(std::clamp(eta, -30.0, 30.0)); };
	} else {
		// Student: identity link
		linkinv_fn = [](double eta) { return eta; };
	}

	std::vector<double> loglik_matrix(static_cast<size_t>(n) * S);
	std::mt19937 rng(SEED);
	std::normal_distribution<double> std_normal(0.0, 1.0);

	for (int s = 0; s < S; s++)
	{
		// Draw φ^(s) ~ N(φ̂, Σ).
		Eigen::VectorXd z(D);
		for (intptr_t j = 0; j < D; j++) z[j] = std_normal(rng);
		Eigen::VectorXd phi_s = phi_hat + chol_post.matrixL() * z;

		Eigen::VectorXd beta_s = phi_s.head(p);

		double disp[2] = {0.0, 0.0};
		if (fam.name == "negbin" || fam.name == "beta")
		{
			disp[0] = std::max(std::exp(phi_s[p]), 1e-10);
		}
		else if (fam.name == "student")
		{
			disp[0] = std::max(std::exp(phi_s[p]),     1e-10);             // σ
			disp[1] = std::clamp(std::exp(phi_s[p + 1]), 2.0, 200.0);      // ν
		}

		Eigen::VectorXd eta = Xm * beta_s;
		if (off_ptr) eta += *off_ptr;

		for (intptr_t i = 0; i < n; i++)
		{
			double mu_i = linkinv_fn(eta[i]);
			loglik_matrix[static_cast<size_t>(i) * S + s] =
				pointwise_loglik(ym[i], mu_i, fam.name, disp);
		}
	}

	compute_waic_from_loglik(model, loglik_matrix, n, S);
	compute_loo_from_loglik(model, loglik_matrix, n, S);
}


} // anonymous namespace


// =====================================================================
// Public entry point
// =====================================================================

Model mixed_model(const Array<double> &y, const Array<double> &X,
                  const std::vector<GroupingInfo> &groups, const Family &fam,
                  FittingCallback progress,
                  const PriorSpec *priors,
                  const Array<String> *coef_names,
                  int max_iter,
                  const Array<double> &offset,
                  const InitOverrides *init_overrides)
{

	if (y.ndim() != 1) {
		throw error("y must be a one-dimensional array");
	}
	if (X.ndim() != 2) {
		throw error("X must be a two-dimensional array");
	}
	if (X.nrow() != y.size()) {
		throw error("Inconsistent number of observations in y and X");
	}
	intptr_t n = y.size();
	intptr_t p = X.ncol();
	bool is_gaussian = (fam.name == "gaussian");

	// Map offset vector (nullable pointer for internal use).
	std::unique_ptr<Eigen::VectorXd> off_storage;
	const Eigen::VectorXd *off_ptr = nullptr;
	if (!offset.empty()) {
		off_storage = std::make_unique<Eigen::VectorXd>(
			Eigen::Map<const Eigen::VectorXd>(offset.data(), n));
		off_ptr = off_storage.get();
	}

	// Gaussian and standard GLMs (binomial, Poisson) without random effects
	// should be fitted via lm()/glm(); only families with a dispersion
	// parameter (NB, beta) are accepted with empty groups so that the
	// Laplace engine provides a unified optimization path.
	if (groups.empty() && !fam.has_dispersion_param()) {
		throw error("At least one grouping factor is required for family '%'", fam.name);
	}

	for (auto &g : groups)
	{
		if (g.nlevels < 2) {
			throw error("Grouping factor '%' must have at least 2 levels", g.name);
		}
		if (static_cast<intptr_t>(g.indices.size()) != n) {
			throw error("Group index vector length for '%' does not match number of observations", g.name);
		}
	}

	if (n <= p) {
		throw error("Not enough observations to fit model (% obs, % parameters)", n, p);
	}

	auto lay = GroupLayout::build(groups);
	intptr_t G = lay.G;

	Eigen::Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Eigen::Map<Vector<double>> ym(const_cast<double *>(y.data()), n);

	// ── Starting values ─────────────────────────────────────────────
	//
	// Fixed effects β are initialized from a preliminary OLS/GLM fit.
	// Variance components are initialized via method-of-moments (ANOVA-based
	// variance decomposition) on the residuals of that fit, giving each
	// grouping factor its own starting variance rather than a single blanket
	// value.  This brings the optimizer much closer to the true optimum and
	// reduces sensitivity to the choice of starting point.
	//
	// For Gaussian models: ANOVA on OLS residuals.
	// For non-Gaussian:    ANOVA on working residuals (Pearson residuals
	//                      scaled by the link derivative, i.e. (y−μ)/V(μ)).

	Model fe;
	if (is_gaussian) {
		fe = lm(y, X, offset);
	} else if (fam.name == "negbin") {
		fe = glm(y, X, Family::poisson(), false, 200, offset);
	} else if (fam.name == "beta") {
		fe = glm(y, X, Family::binomial(), false, 200, offset);
	} else if (fam.name == "student") {
		fe = lm(y, X, offset);
	} else {
		fe = glm(y, X, fam, false, 200, offset);
	}

	intptr_t outer_dim = is_gaussian ? (p + G + 1) : (p + G);
	Eigen::VectorXd phi = Eigen::VectorXd::Zero(outer_dim);

	for (intptr_t i = 0; i < p; i++) {
		phi[i] = fe.beta[i + 1];
	}

	if (is_gaussian)
	{
		// OLS residuals → per-factor ANOVA variance decomposition.
		Eigen::VectorXd resid(n);
		for (intptr_t i = 0; i < n; i++) {
			resid[i] = fe.residuals[i + 1];
		}
		double total_var = resid.squaredNorm() / std::max(n - p, (intptr_t)1);

		double sum_sigma2_u = 0;
		for (intptr_t g = 0; g < G; g++)
		{
			double s2 = anova_variance_component(resid, groups[g].indices,
			                                      groups[g].nlevels, n);
			// Floor at 1% of total variance to keep log(σ) finite and reasonable.
			s2 = std::max(s2, 0.01 * total_var);
			phi[p + g] = 0.5 * std::log(s2);   // log(σ_u_g)
			sum_sigma2_u += s2;
		}

		// Residual σ²: total − Σ σ²_u, floored at 10% of total.
		double sigma2_resid = std::max(total_var - sum_sigma2_u, 0.1 * total_var);
		phi[p + G] = 0.5 * std::log(sigma2_resid);
	}
	else
	{
		// Working residuals on the link scale: r_i = (y_i − μ̂_i) / V(μ̂_i).
		// For canonical links this equals the derivative of the deviance w.r.t. η,
		// giving residuals whose between-group variance approximates σ²_u on the
		// linear predictor scale.
		Eigen::VectorXd mu_fe(n);
		for (intptr_t i = 0; i < n; i++) {
			mu_fe[i] = fe.fitted[i + 1];
		}
		Eigen::VectorXd V = fam.variance(mu_fe);

		Eigen::VectorXd wr(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double v = std::max(V[i], 1e-8);
			wr[i] = (ym[i] - mu_fe[i]) / v;
		}

		for (intptr_t g = 0; g < G; g++)
		{
			double s2 = anova_variance_component(wr, groups[g].indices,
			                                      groups[g].nlevels, n);
			// Floor at a small positive value; no upper clamp — large random
			// effects (complete separation within groups) are common in logistic models.
			s2 = std::max(s2, 0.01);
			phi[p + g] = 0.5 * std::log(s2);
		}
	}

	// ── Outer optimisation ──────────────────────────────────────────
	//
	// Gaussian:     β is profiled out — Newton's method searches only over
	//               θ = (chol_1..G, log σ), dimension = Σ q_g(q_g+1)/2 + 1.
	//               For each θ, the joint mode (β̂, û) is found by
	//               solve_gaussian_henderson (one linear solve).  This matches
	//               the profiling strategy used by lme4 / glmmTMB internally.
	//
	// Non-Gaussian: Phase 1 — Newton over θ only, with β profiled out via PIRLS.
	//               Phase 2 — Newton over (β, θ) jointly, with u profiled out via
	//               u-only IRLS.  Phase 2 is needed because the Laplace log-det
	//               depends on β through the working weights.  Student-t skips
	//               Phase 2 (σ–ν correlation makes the joint Hessian ill-conditioned).

	Eigen::VectorXd beta_hat;
	Eigen::VectorXd sigma2_u(G);
	std::vector<Eigen::MatrixXd> D_inv_final;  // converged prior precision matrices (for SE computation)
	std::vector<double> log_det_Dg_final;       // converged log|D_g| values
	double sigma2 = 0;
	int niter = 0;
	bool converged = true;
	String optimizer_used;  // "newton" or "lbfgs" — records whichever optimizer
	                        // produced the final reported estimates (for non-Gaussian
	                        // models with Phase 2, this is the Phase 2 optimizer).
	Family fam_used = fam;  // mutable copy; updated with fitted θ_nb for negative binomial
	Eigen::VectorXd saved_theta;   // saved for INLA grid integration
	Eigen::VectorXd saved_beta_init; // saved for INLA grid integration (non-Gaussian)
	intptr_t saved_n_chol = 0;

	// Warm-start û for the FINAL reporting solve_u_given_beta call.
	//
	// PIRLS inside solve_u_given_beta uses pure Newton without step-
	// halving or trust-region.  Cold-starting from u=0 on binomial GLMMs
	// with strong random effects (e.g. schwa: many words at 0% or 100%
	// realization rate) can OVERSHOOT and converge to a non-mode point
	// with |û_j| ≈ 5–6 instead of the true mode's ≈1.5.  That inflates
	// prior_quad by ~13000 nats and collapses log|H_uu|, throwing the
	// *reported* logLik off by ~17000 nats while leaving β/θ unchanged
	// (Phase 2's optimizer warm-starts last_u between evals, so it stays
	// at the mode there — only the final report is wrong).
	//
	// We preserve Phase 2's converged û here so the final call resumes
	// from a near-mode warm-start instead of u=0.  Stays empty for
	// Gaussian (no PIRLS) and for paths that skip Phase 2.
	Eigen::VectorXd phase2_warm_u;

	if (is_gaussian)
	{
		intptr_t n_chol = total_chol_params(lay);
		intptr_t outer_dim_gauss = n_chol + 1; // Cholesky params + log σ

		GaussianCholObjective gauss_obj{Xm, ym, lay, n, p, n_chol, priors, coef_names, off_ptr};

		// ── Initialize (τ, ω) parameters from ANOVA variance estimates ──
		// New layout per group: [τ_0, ..., τ_{q-1}, ω_0, ..., ω_{q(q-1)/2-1}].
		// First q entries hold log σ_t (initialised from ANOVA s²_init,
		// with non-leading random-effect terms shrunk by 0.1 — same heuristic
		// as the previous log-Cholesky form). Remaining entries are 0,
		// which under stickbreaking corresponds to zero correlations
		// (tanh(0) = 0).
		Eigen::VectorXd theta(outer_dim_gauss);
		theta.setZero();
		{
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				double s2_init = std::exp(2.0 * phi[p + g]);
				for (intptr_t t = 0; t < qg; t++) {
					double var_init = (t == 0) ? s2_init : s2_init * 0.1;
					theta[chol_pos + t] = 0.5 * std::log(std::max(var_init, 1e-4));
				}
				chol_pos += np;
			}
		}
		theta[n_chol] = phi[p + G]; // log σ

#if defined(PHON_INLA_BAYES_DIAG)
		// ── PHON_INLA_BAYES_DIAG: Bayesian mode-finding (pre) ────────
		//
		// The grid-integration dump (earlier in this translation unit)
		// only fires after `inla_grid_integrate_gaussian` is called with
		// a θ* that may already be NaN-poisoned. This block captures the
		// state on either side of `robust_optimize` so we can see whether
		// the optimizer entered with a finite initial θ and what state it
		// returned. The try/catch is diagnostic-only: in a non-DIAG build
		// the optimizer call is bare and exceptions propagate normally.
		auto diag_decode_and_dump_theta = [&](const char *label,
		                                       const Eigen::VectorXd &th,
		                                       bool eval_obj) {
			std::fprintf(stderr, "  %s θ =", label);
			for (intptr_t j = 0; j < outer_dim_gauss; j++)
				std::fprintf(stderr, " %+.4e", th[j]);
			std::fprintf(stderr, "\n    θ all-finite: %s\n",
			             th.allFinite() ? "yes" : "NO");
			if (th.allFinite())
			{
				intptr_t cp = 0;
				for (intptr_t g = 0; g < G; g++)
				{
					intptr_t qg = lay.q[g];
					Eigen::MatrixXd L = unpack_cholesky(th.data() + cp, qg);
					Eigen::MatrixXd D = cholesky_to_cov(L);
					std::fprintf(stderr, "    group %ld σ=[", (long)g);
					for (intptr_t t = 0; t < qg; t++) {
						double v = D(t, t);
						double sd = (v >= 0 && std::isfinite(v))
						          ? std::sqrt(v) : std::nan("");
						std::fprintf(stderr, "%s%.4f", t ? ", " : "", sd);
					}
					std::fprintf(stderr, "]");
					if (qg >= 2)
						std::fprintf(stderr, "  det(D)=%.3e",
						             D.determinant());
					std::fprintf(stderr, "\n");
					cp += n_chol_params(qg);
				}
				std::fprintf(stderr, "    σ_res = %.4f\n",
				             std::exp(th[n_chol]));
				if (eval_obj) {
					double fx = gauss_obj.eval(th);
					std::fprintf(stderr,
					             "    obj.eval(θ) = %+.6e%s\n",
					             fx, std::isfinite(fx) ? "" : "  [NON-FINITE]");
				}
			}
		};

		if (priors)
		{
			std::fprintf(stderr,
				"\n=== PHON_INLA_BAYES_DIAG: Bayesian Gaussian mode-finding ===\n");
			std::fprintf(stderr,
				"  outer_dim=%ld  n_chol=%ld  n=%ld  p=%ld  G=%ld  J_total=%ld\n"
				"  priors: %s  (per-coef overrides: %zu)\n",
				(long)outer_dim_gauss, (long)n_chol, (long)n, (long)p,
				(long)G, (long)lay.J_total,
				priors ? "yes" : "no",
				priors ? priors->coefficient_priors.size() : (size_t)0);

			// Arm the one-shot prior-lookup dump. The next call to
			// add_fixed_prior_to_henderson (triggered inside the
			// diag_eval_gaussian_breakdown below) will consume the flag
			// and print the full prior/coef lookup trace inline. Subsequent
			// calls during L-BFGS iteration leave stderr quiet.
			diag_dump_prior_lookup_once = true;

			diag_decode_and_dump_theta("[pre-optimize]  θ_init =", theta, true);

			// Seven-term NLL decomposition at θ_init — identifies which
			// subterm (A: likelihood, B: u-prior, C: ½·log|H_uu|,
			// D: Laplace const, E: fixed_prior_nll(β̂), F: −variance prior,
			// G: −residual prior) first produces NaN. Bit i of nan_mask
			// is set when subterm i is non-finite.
			DiagNllBreakdown b_init = diag_eval_gaussian_breakdown(
				theta, gauss_obj, Xm, ym, lay,
				n, p, n_chol, priors, coef_names, off_ptr);

			auto tag = [](double v) {
				return std::isfinite(v) ? "" : "  [NON-FINITE]";
			};
			std::fprintf(stderr, "  NLL(θ_init) components:\n");
			std::fprintf(stderr, "    A  cond_nll            = %+.6e%s\n",
			             b_init.A_cond_nll, tag(b_init.A_cond_nll));
			std::fprintf(stderr, "    B  u_prior_nll         = %+.6e%s\n",
			             b_init.B_u_prior_nll, tag(b_init.B_u_prior_nll));
			std::fprintf(stderr, "    C  ½·log|H_uu|         = %+.6e%s\n",
			             b_init.C_half_log_det_Huu,
			             tag(b_init.C_half_log_det_Huu));
			std::fprintf(stderr, "    D  -½·J·log(2π)        = %+.6e\n",
			             b_init.D_minus_half_J_log_2pi);
			std::fprintf(stderr, "    E  fixed_prior_nll(β̂)  = %+.6e%s\n",
			             b_init.E_fixed_prior_nll,
			             tag(b_init.E_fixed_prior_nll));
			std::fprintf(stderr, "    F  -variance_prior_lp  = %+.6e%s\n",
			             b_init.F_minus_variance_prior_lp,
			             tag(b_init.F_minus_variance_prior_lp));
			std::fprintf(stderr, "    G  -residual_prior_lp  = %+.6e%s\n",
			             b_init.G_minus_residual_prior_lp,
			             tag(b_init.G_minus_residual_prior_lp));
			std::fprintf(stderr, "    Σ(A..G)                = %+.6e%s\n",
			             b_init.total_breakdown, tag(b_init.total_breakdown));
			std::fprintf(stderr, "    obj.eval(θ_init) check = %+.6e%s\n",
			             b_init.total_obj_eval, tag(b_init.total_obj_eval));
			std::fprintf(stderr, "    β̂ finite: %s   û finite: %s\n",
			             b_init.beta_finite ? "yes" : "NO",
			             b_init.u_finite    ? "yes" : "NO");
			if (b_init.nan_mask != 0) {
				std::fprintf(stderr,
				             "    >>> non-finite subterms: nan_mask=0x%x"
				             "  (bit 0=A, 1=B, 2=C, 3=D, 4=E, 5=F, 6=G)\n",
				             b_init.nan_mask);
			}
		}
#endif  // PHON_INLA_BAYES_DIAG

		NewtonResult newton_res;
#if defined(PHON_INLA_BAYES_DIAG)
		try {
			newton_res = robust_optimize(gauss_obj, theta, max_iter, 1e-8, progress);
		}
		catch (const std::exception &e)
		{
			if (priors)
			{
				std::fprintf(stderr,
					"\n  [optimizer THREW]: %s\n"
					"=== end Bayesian mode-finding dump (aborted) ===\n\n",
					e.what());
			}
			throw;
		}
#else
		newton_res = robust_optimize(gauss_obj, theta, max_iter, 1e-8, progress);
#endif
		theta = newton_res.theta;
		niter = newton_res.niter;
		converged = newton_res.converged;
		optimizer_used = newton_res.optimizer;

#if defined(PHON_INLA_BAYES_DIAG)
		if (priors)
		{
			std::fprintf(stderr,
				"\n  [post-optimize]  optimizer=%s  niter=%d  converged=%s\n",
				std::string(optimizer_used.data(),
				             optimizer_used.size()).c_str(),
				(int) niter, converged ? "yes" : "NO");
			diag_decode_and_dump_theta("                 θ_final =",
			                            theta, false);
			std::fprintf(stderr,
				"    newton_res.fx = %+.6e%s\n",
				newton_res.fx,
				std::isfinite(newton_res.fx) ? "" : "  [NON-FINITE]");
			if (!theta.allFinite() || !std::isfinite(newton_res.fx))
				std::fprintf(stderr,
					"  >>> WARNING: optimizer returned non-finite state WITHOUT\n"
					"      throwing. The NaN-guard in lbfgs_optimize should have\n"
					"      fired — if you see this, the guard is not in effect.\n");
			std::fprintf(stderr, "=== end Bayesian mode-finding dump ===\n\n");
		}
#endif  // PHON_INLA_BAYES_DIAG

		// ── Unpack converged Cholesky → D_inv, sigma2_u, sigma2 ──
		D_inv_final.resize(G);
		log_det_Dg_final.resize(G);
		{
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
				Eigen::MatrixXd D = cholesky_to_cov(L);
				D_inv_final[g] = cholesky_to_precision(L);
				log_det_Dg_final[g] = log_det_D(theta.data() + chol_pos, qg);
				sigma2_u[g] = D(0, 0);
				chol_pos += n_chol_params(qg);
			}
		}
		sigma2 = std::exp(2.0 * theta[n_chol]);

		auto final_gauss = solve_gaussian_henderson(D_inv_final, log_det_Dg_final, sigma2,
		                                             Xm, ym, lay, n, p,
		                                             priors, coef_names, off_ptr);
		beta_hat = final_gauss.beta;

		// Save for INLA grid integration (after Model is built).
		saved_theta = theta;
		saved_n_chol = n_chol;
	}
	else
	{
		// Non-Gaussian: PIRLS profiling — Newton over θ = (chol_1, ..., chol_G, [log disp...]).
		// β is concentrated out via PIRLS at each θ evaluation.
		// Outer dimension: Σ q_g(q_g+1)/2 + n_dispersion_params.
		bool is_nb = (fam.name == "negbin");
		bool is_beta = (fam.name == "beta");
		bool is_student = (fam.name == "student");
		intptr_t n_chol = total_chol_params(lay);
		intptr_t n_disp = fam.n_dispersion_params();
		intptr_t outer_dim_pirls = n_chol + n_disp;

		Eigen::VectorXd beta_init = phi.head(p);

		// ── Initialize Cholesky parameters ────────────────────────────
		Eigen::VectorXd theta(outer_dim_pirls);

		// For NB, the ANOVA-based initialization of variance components is
		// poor because NB overdispersion absorbs variance that Poisson
		// attributes to random effects.  A much better starting point is
		// to fit a Poisson mixed model with the same random structure and
		// use its converged Cholesky parameters.  This applies to all NB
		// mixed models (intercepts and/or slopes), not just slope models.

		if (is_nb && G > 0)
		{
			auto pois_model = mixed_model(y, X, groups, Family::poisson());

			// Use Poisson β as starting fixed effects
			for (intptr_t i = 0; i < p; i++) {
				beta_init[i] = pois_model.beta[i + 1];
			}

			// Use Poisson covariance as starting outer theta — convert
			// physical L = chol(D) into the (τ, ω) layout via pack_chol_to_theta.
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				auto &re = pois_model.random_effects[g + 1]; // 1-based Array

				// Reconstruct physical L from re.cov_chol (1-based, packed
				// row-by-row lower triangle, stored as the actual Cholesky
				// factor of D — parameterization-independent).
				Eigen::MatrixXd L_phys = Eigen::MatrixXd::Zero(qg, qg);
				for (intptr_t r = 0; r < qg; r++) {
					for (intptr_t c = 0; c <= r; c++) {
						intptr_t pack_idx = r * (r + 1) / 2 + c;
						L_phys(r, c) = re.cov_chol[pack_idx + 1];
					}
				}
				pack_chol_to_theta(L_phys, theta.data() + chol_pos, qg);
				chol_pos += np;
			}
		}
		else
		{
			// Standard initialization: ANOVA variance decomposition.
			// (τ, ω) layout: first q entries log σ_t, rest zero (no corr).
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				for (intptr_t i = 0; i < np; i++) theta[chol_pos + i] = 0.0;
				double s2_init = std::exp(2.0 * phi[p + g]);
				for (intptr_t t = 0; t < qg; t++) {
					double var_init = (t == 0) ? s2_init : s2_init * 0.1;
					theta[chol_pos + t] = 0.5 * std::log(std::max(var_init, 1e-4));
				}
				chol_pos += np;
			}
		}

		if (is_nb)
		{
			// Initialize θ_nb from method-of-moments
			double ybar = ym.mean();
			double yvar = (ym.array() - ybar).square().sum() / std::max(n - 1, (intptr_t)1);
			double theta_nb_init = (yvar > ybar) ? ybar * ybar / (yvar - ybar) : 10.0;
			theta_nb_init = std::clamp(theta_nb_init, 0.01, 1e6);
			theta[n_chol] = std::log(theta_nb_init);
		}
		else if (is_beta)
		{
			// Initialize φ from method-of-moments: φ = μ̄(1-μ̄)/Var(y) - 1
			double ybar = ym.mean();
			double yvar = (ym.array() - ybar).square().sum() / std::max(n - 1, (intptr_t)1);
			double phi_init = 10.0;
			if (yvar > 0 && yvar < ybar * (1.0 - ybar)) {
				phi_init = ybar * (1.0 - ybar) / yvar - 1.0;
			}
			phi_init = std::clamp(phi_init, 0.1, 1e6);
			theta[n_chol] = std::log(phi_init);
		}
		else if (is_student)
		{
			// Student-t σ initialization — three corrections to the raw
			// OLS-based scale estimate:
			//
			//   (1) Heavy-tail robustness. The OLS residual-standard-error is
			//       inflated by outliers — the very thing t-regression handles.
			//       We use MAD × 1.4826 instead, which is a consistent estimator
			//       of σ under Gaussianity but is far less inflated by heavy
			//       tails (MAD uses the median, not the mean, of |residuals|).
			//
			//   (2) Random-effects variance. In a mixed model, MAD of the OLS
			//       residuals conflates σ with Σ σ_u,g.  We subtract the ANOVA-
			//       based variance components already computed above (phi[p+g])
			//       to isolate the within-group residual variance.
			//
			//   (3) t-distribution tail inflation. For t_ν residuals,
			//       Var(r) = σ² · ν/(ν−2).  With ν₀ = 5 as the initial value,
			//       σ² = Var(r) · 3/5 ≈ 0.6 · Var(r).
			//
			// The previous "σ *= 0.5 if G > 0" heuristic applied only a partial,
			// dimensionless correction that did not adapt to the actual RE
			// structure or to the initial ν. This version is based on explicit
			// variance decomposition and is invariant to the number of grouping
			// factors and the magnitude of their variance components.

			Eigen::VectorXd ols_resid(n);
			for (intptr_t i = 0; i < n; i++) {
				ols_resid[i] = fe.residuals[i + 1];
			}
			// MAD = median(|r − median(r)|), × 1.4826 for Gaussian consistency.
			std::vector<double> abs_dev(n);
			std::nth_element(ols_resid.data(), ols_resid.data() + n / 2, ols_resid.data() + n);
			double median_r = ols_resid[n / 2];
			for (intptr_t i = 0; i < n; i++) {
				abs_dev[i] = std::abs(ols_resid[i] - median_r);
			}
			std::nth_element(abs_dev.data(), abs_dev.data() + n / 2, abs_dev.data() + n);
			double mad = abs_dev[n / 2];
			double sigma_total = std::max(mad * 1.4826, 0.01);
			double var_total = sigma_total * sigma_total;

			// Subtract the ANOVA-based RE variance contributions.
			double sum_sigma2_u = 0;
			for (intptr_t g = 0; g < G; g++) {
				sum_sigma2_u += std::exp(2.0 * phi[p + g]);
			}
			// Floor at 1% of total variance to keep log(σ) finite if the RE
			// components collectively explain nearly all the variance.
			double var_resid = std::max(var_total - sum_sigma2_u, 0.01 * var_total);

			// Adjust for t-distribution variance inflation at initial ν.
			constexpr double nu_init_default = 5.0;
			double nu_init = nu_init_default;
			double sigma2_init = var_resid * (nu_init - 2.0) / nu_init;
			double sigma_init = std::sqrt(std::max(sigma2_init, 1e-4));

			// Apply multi-start overrides if supplied (Student only).
			if (init_overrides) {
				if (init_overrides->has_nu_init) {
					nu_init = std::max(2.5, init_overrides->nu_init);
					// Re-derive σ from the same residual-variance formula
					// using the supplied ν, unless σ was *also* overridden.
					if (!init_overrides->has_sigma_init) {
						sigma2_init = var_resid * (nu_init - 2.0) / nu_init;
						sigma_init = std::sqrt(std::max(sigma2_init, 1e-4));
					}
				}
				if (init_overrides->has_sigma_init) {
					sigma_init = std::max(init_overrides->sigma_init, 1e-3);
				}
			}

			theta[n_chol] = std::log(sigma_init);
			theta[n_chol + 1] = std::log(nu_init);
		}

		// Create PirlsObjective after beta_init is finalized

		PirlsObjective pirls_obj{fam, Xm, ym, lay, n, p, beta_init, n_chol, priors, coef_names, off_ptr};

		// Polish mode (used by multi-start wrapper for diagnostics):
		// tighten gradient tolerance by ~3 orders and shrink FD step by
		// 100×. Tests whether the default-tol convergence was limited by
		// FD gradient noise. If polish moves the loglik appreciably,
		// switching to AD gradients would close the same gap stably.
		double pirls_grad_tol = 1e-8;
		double pirls_h_scale  = 1e-2;
		if (init_overrides && init_overrides->tight_tolerance) {
			pirls_grad_tol = 1e-11;
			pirls_h_scale  = 1e-4;
		}

		auto newton_res = robust_optimize(pirls_obj, theta, max_iter,
		                                   pirls_grad_tol, progress, pirls_h_scale);
		theta = newton_res.theta;
		niter = newton_res.niter;
		converged = newton_res.converged;
		optimizer_used = newton_res.optimizer;

		// ── Phase 2: joint (β, θ) optimization ─────────────────
		// For non-Gaussian, the Laplace log-det depends on β through
		// the working weights W = μ(1−μ). Phase 1 PIRLS profiles β
		// out ignoring ∂log|H|/∂β; Phase 2 re-optimizes (β, θ)
		// jointly with u profiled out, matching lme4/glmmTMB.
		//
		// For Student t, Phase 2 is skipped: the σ–ν correlation
		// makes the joint (β, σ, ν) Hessian ill-conditioned, and
		// Phase 1 PIRLS profiling already gives accurate β̂.
		{
			// Get Phase 1 β̂ via one PIRLS call at converged θ
			std::vector<Eigen::MatrixXd> D_inv_p1(G);
			std::vector<double> log_det_p1(G);
			intptr_t cp = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				Eigen::MatrixXd L = unpack_cholesky(theta.data() + cp, qg);
				D_inv_p1[g] = cholesky_to_precision(L);
				log_det_p1[g] = log_det_D(theta.data() + cp, qg);
				cp += np;
			}

			Family fam_p1 = fam;
			if (is_nb)
				fam_p1 = Family::negbin(std::exp(theta[n_chol]));
			else if (is_beta)
				fam_p1 = Family::beta(std::exp(theta[n_chol]));
			else if (is_student)
				fam_p1 = Family::student(std::exp(theta[n_chol]), std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0));

			auto p1_pirls = solve_pirls(D_inv_p1, log_det_p1, fam_p1,
			                             Xm, ym, lay, n, p, beta_init,
			                             Eigen::VectorXd(), priors, coef_names, off_ptr);

			// Seed the final-call warm-start with Phase 1's converged û.
			// Updated below to Phase 2's converged û if Phase 2 runs.
			phase2_warm_u = p1_pirls.u;

			if (is_student)
			{
				// Phase 2 is normally skipped for Student-t: the σ-ν
				// correlation can make the joint (β, σ, ν) Hessian
				// ill-conditioned, and Phase 1 PIRLS profiling already
				// gives accurate β̂. The override flag (set via
				// FitOptions.phase2_student) re-enables Phase 2 for
				// diagnostic comparison against engines like glmmTMB
				// that always do joint optimization.
				bool run_phase2 = (init_overrides
				                    && init_overrides->phase2_student);

				if (!run_phase2)
				{
					beta_hat = p1_pirls.beta;
				}
				else
				{
					// Build Phase 2 parameter vector: [β, θ_chol, log σ, log ν]
					intptr_t outer_dim2 = p + (intptr_t)theta.size();
					Eigen::VectorXd phi2(outer_dim2);
					phi2.head(p) = p1_pirls.beta;
					phi2.tail(theta.size()) = theta;

					LaplaceJointObjective joint_obj{fam, Xm, ym, lay, n, p,
					                                 n_chol, priors, coef_names, off_ptr};
					joint_obj.last_u = std::move(p1_pirls.u);

					// Use looser tolerance and FD step than the negbin/beta
					// Phase 2 above, in case σ-ν correlation makes line
					// search delicate. If it diverges or fails to improve
					// over Phase 1, the wrapper will catch it and restore
					// Phase 1 estimates externally.
					double phase2_grad_tol = 1e-7;
					double phase2_h_scale  = 1e-3;

					try
					{
						auto res2 = robust_optimize(joint_obj, phi2, max_iter,
						                             phase2_grad_tol, progress,
						                             phase2_h_scale);

						if (res2.converged && std::isfinite(res2.theta[0]))
						{
							beta_hat = res2.theta.head(p);
							theta = Eigen::VectorXd(res2.theta.tail(theta.size()));
							niter += res2.niter;
							converged = res2.converged;
							optimizer_used = res2.optimizer;
						}
						else
						{
							// Phase 2 didn't converge. Fall back to Phase 1
							// β̂ to avoid contaminating the result with
							// half-converged values.
							beta_hat = p1_pirls.beta;
						}
					}
					catch (std::exception &)
					{
						beta_hat = p1_pirls.beta;
					}

					// Preserve the warm-started û from joint_obj before
					// it goes out of scope, for use in the final call.
					phase2_warm_u = std::move(joint_obj.last_u);
				}
			}
			else
			{
				// Build Phase 2 parameter vector: [β, θ]
				intptr_t outer_dim2 = p + (intptr_t)theta.size();
				Eigen::VectorXd phi2(outer_dim2);
				phi2.head(p) = p1_pirls.beta;
				phi2.tail(theta.size()) = theta;

				LaplaceJointObjective joint_obj{fam, Xm, ym, lay, n, p, n_chol, priors, coef_names, off_ptr};
				joint_obj.last_u = std::move(p1_pirls.u);

				// ── Per-coordinate FD scaling for β coords ──────────
				// The default rule h_j = h_scale × max(|β_j|, 1) is
				// adequate when predictors X_·j are O(1), but breaks
				// when columns span several orders of magnitude (e.g.
				// formant frequencies in Hz where |X| ≈ 3000 produces
				// ∂³f/∂β_j³ ~ Σ X_ij³ ~ 10¹¹ for binomial logit). The
				// resulting FD truncation (h²/6)·∂³f swamps the
				// gradient signal at the optimum. Just shrinking
				// h_scale uniformly doesn't help — truncation drops
				// O(h²) but signal drops O(h), and arithmetic noise
				// rises O(1/h).
				//
				// We rescale each β coord by its column-max-abs so
				// that all β coords become roughly O(1) in the rescaled
				// parameterization. The existing h_scale rule then
				// produces well-conditioned FD steps regardless of
				// predictor units. θ coords pass through unchanged
				// (scale = 1) — the log-Cholesky and log-dispersion
				// parameterizations already give O(1) magnitude.
				//
				// Column-max ≈ 1/SE for binomial GLMMs (where SE ≈
				// 1/(sqrt(n)·||X_j||_RMS·sqrt(p̄(1−p̄)))), so this is
				// a cheap proxy for the principled choice of scaling
				// each coord by its posterior precision.
				Eigen::VectorXd scale = Eigen::VectorXd::Ones(outer_dim2);
				for (intptr_t j = 0; j < p; j++)
				{
					double col_max = Xm.col(j).cwiseAbs().maxCoeff();
					scale[j] = std::max(col_max, 1.0);
				}
				Eigen::VectorXd inv_scale = scale.cwiseInverse();

				// Wrapper: optimizer works in scaled coords; we map
				// back inside eval (one cwiseProduct per call, cheap
				// vs. the inner PIRLS solve) and at the end.
				struct ScaledJoint
				{
					const LaplaceJointObjective &base;
					const Eigen::VectorXd &inv_scale;
					double eval(const Eigen::VectorXd &phi_s) const
					{
						return base.eval(phi_s.cwiseProduct(inv_scale));
					}
				};
				ScaledJoint scaled_obj{joint_obj, inv_scale};
				Eigen::VectorXd phi2_scaled = phi2.cwiseProduct(scale);

				// Phase 2 outer dimension p + n_chol + n_disp is effectively
				// always > 3, so robust_optimize routes this to L-BFGS.
				auto res2 = robust_optimize(scaled_obj, phi2_scaled, max_iter, 1e-8, progress, 1e-2);

				// Unscale converged estimate back to original coords.
				Eigen::VectorXd phi2_final = res2.theta.cwiseProduct(inv_scale);

				// Update β and θ from Phase 2
				beta_hat = phi2_final.head(p);
				theta = Eigen::VectorXd(phi2_final.tail(theta.size()));
				niter += res2.niter;
				converged = res2.converged;
				optimizer_used = res2.optimizer;  // Phase 2 produces the final estimates;
				                                   // its optimizer is the one to report.

				// Preserve the warm-started û from joint_obj before it
				// goes out of scope, for use in the final call.
				phase2_warm_u = std::move(joint_obj.last_u);
			}
		}

		// ── Unpack converged Cholesky → D_inv, sigma2_u, log_det_Dg ──
		D_inv_final.resize(G);
		log_det_Dg_final.resize(G);
		{
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
				Eigen::MatrixXd D = cholesky_to_cov(L);
				D_inv_final[g] = cholesky_to_precision(L);
				log_det_Dg_final[g] = log_det_D(theta.data() + chol_pos, qg);
				// For backward compat, store the intercept variance in sigma2_u
				sigma2_u[g] = D(0, 0);
				chol_pos += np;
			}
		}

		// For NB, update the Family with the converged θ_nb
		if (is_nb) {
			double theta_nb = std::exp(theta[n_chol]);
			fam_used = Family::negbin(theta_nb);
		}
		// For beta, update the Family with the converged φ
		else if (is_beta) {
			double phi_beta = std::exp(theta[n_chol]);
			fam_used = Family::beta(phi_beta);
		}
		// For Student t, update the Family with the converged σ and ν
		else if (is_student) {
			double sigma_t = std::exp(theta[n_chol]);
			double nu_t = std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0);
			fam_used = Family::student(sigma_t, nu_t);
		}

		// beta_hat already set by Phase 2

		// Save for INLA grid integration (after Model is built).
		// Use converged beta_hat as the PIRLS warm-start for grid
		// evaluation — much closer to the mode than the original beta_init.
		saved_theta = theta;
		saved_n_chol = n_chol;
		saved_beta_init = beta_hat;
	}

	// ── Final result at converged estimates ──────────────────────────
	//
	// Gaussian:     one final Henderson solve at the converged (D_inv, σ²) —
	//               exact because the Gaussian Henderson system is linear.
	// Non-Gaussian: one final u-only IRLS at the converged β̂ — this preserves
	//               the Phase 2 β̂ without modifying it, while refining û.

	ProfiledResult final_inner;

	if (is_gaussian)
	{
		auto gauss_final = solve_gaussian_henderson(D_inv_final, log_det_Dg_final, sigma2,
		                                            Xm, ym, lay, n, p,
		                                            priors, coef_names, off_ptr);
		final_inner.u = std::move(gauss_final.u);
		final_inner.mu = std::move(gauss_final.mu);
		final_inner.laplace_nll = gauss_final.laplace_nll;
	}
	else
	{
		// Use solve_u_given_beta to ensure β̂ from Phase 2 is not modified.
		// Pass phase2_warm_u (Phase 2's converged û, or Phase 1's û if
		// Phase 2 was skipped) as initial value — see comment at the
		// declaration of phase2_warm_u above for why cold-start (u=0)
		// gives a wrong û and corrupts the reported logLik.
		auto pirls_final = solve_u_given_beta(D_inv_final, log_det_Dg_final, fam_used,
		                                       Xm, ym, lay, n, p, beta_hat,
		                                       phase2_warm_u, off_ptr,
		                                       /*is_final_report=*/true);
		final_inner.u = std::move(pirls_final.u);
		final_inner.mu = std::move(pirls_final.mu);
		final_inner.laplace_nll = pirls_final.laplace_nll;
	}

	// ── Standard errors ─────────────────────────────────────────────
	//
	// Conditional Var(β̂ | θ̂) from the inverse of the Henderson matrix:
	//   C = [X'WX     X'WZ       ]
	//       [Z'WX   Z'WZ + D⁻¹   ]
	// For Gaussian: W = (1/σ²)I.
	// For Student t: W = diag(w_i) with custom IWLS weights.
	// For non-Gaussian: W = diag(w_i) with w_i = (dμ/dη)²/V(μ).
	// The top-left p×p block of C⁻¹ is the conditional covariance
	// of β̂.  This is what lme4/glmmTMB report.

	Eigen::MatrixXd vcov;

	{
		Eigen::VectorXd w_se(n);
		if (is_gaussian)
		{
			w_se.setConstant(1.0 / sigma2);
		}
		else if (fam_used.name == "beta")
		{
			// Beta: expected Fisher info weight for Var(β|θ).
			// I_ββ = φ² · X' diag(w_i) X,
			//   w_i = [μ_i(1-μ_i)]² · [ψ'(μ_i φ) + ψ'((1-μ_i) φ)]
			// The IRLS quasi-weight μ(1-μ)(1+φ) used during estimation
			// under-estimates I_ββ when μ is far from 0.5 (Ferrari &
			// Cribari-Neto 2004, eq. 5). Using the true Fisher info weight
			// here aligns Var(β|θ) with glmmTMB's vcov block.
			double phi_b = fam_used.phi;
			double phi_sq = phi_b * phi_b;
			for (intptr_t i = 0; i < n; i++)
			{
				double mi = std::clamp(final_inner.mu[i], 1e-10, 1.0 - 1e-10);
				double mu1m = mi * (1.0 - mi);
				double tri = boost::math::trigamma(mi * phi_b)
				             + boost::math::trigamma((1.0 - mi) * phi_b);
				w_se[i] = phi_sq * mu1m * mu1m * tri;
			}
		}
		else if (fam_used.custom_weights)
		{
			w_se = fam_used.custom_weights(ym, final_inner.mu);
		}
		else
		{
			Eigen::VectorXd V_se = fam_used.variance(final_inner.mu);
			Eigen::VectorXd me_se = fam_used.mu_eta(final_inner.mu);
			for (intptr_t i = 0; i < n; i++) {
				double v = std::max(V_se[i], 1e-10);
				double d = std::max(me_se[i], 1e-10);
				w_se[i] = d * d / v;
			}
		}

		intptr_t J = lay.J_total;
		intptr_t sdim = p + J;

		Eigen::MatrixXd C = Eigen::MatrixXd::Zero(sdim, sdim);

		// X'WX (p × p)
		for (intptr_t i = 0; i < n; i++)
		{
			for (intptr_t j1 = 0; j1 < p; j1++)
			{
				double wx = Xm(i, j1) * w_se[i];
				for (intptr_t j2 = j1; j2 < p; j2++) {
					C(j1, j2) += wx * Xm(i, j2);
				}
			}
		}
		for (intptr_t j1 = 0; j1 < p; j1++) {
			for (intptr_t j2 = j1 + 1; j2 < p; j2++) {
				C(j2, j1) = C(j1, j2);
			}
		}

		// Prior precision on fixed effects (Bayesian posterior covariance).
		if (priors && coef_names)
		{
			for (intptr_t j = 0; j < p; j++)
			{
				const auto &pr = priors->prior_for((*coef_names)[j + 1]);
				C(j, j) += 1.0 / (pr.sd * pr.sd);
			}
		}

		// D⁻¹ blocks
		for (intptr_t g = 0; g < G; g++)
		{
			intptr_t qg = lay.q[g];
			auto &Dinv_g = D_inv_final[g];
			for (intptr_t j = 0; j < lay.J[g]; j++)
			{
				intptr_t base = p + lay.offset[g] + j * qg;
				for (intptr_t t1 = 0; t1 < qg; t1++) {
					for (intptr_t t2 = 0; t2 < qg; t2++) {
						C(base + t1, base + t2) += Dinv_g(t1, t2);
					}
				}
			}
		}

		// X'WZ, Z'WX, Z'WZ
		for (intptr_t i = 0; i < n; i++)
		{
			for (intptr_t g1 = 0; g1 < G; g1++)
			{
				intptr_t j1 = groups[g1].indices[i];
				intptr_t q1 = lay.q[g1];
				intptr_t base1 = p + lay.offset[g1] + j1 * q1;

				for (intptr_t t = 0; t < q1; t++)
				{
					double z_val = lay.Z(g1, i, t);
					double wz_val = w_se[i] * z_val;

					for (intptr_t j = 0; j < p; j++)
					{
						double val = Xm(i, j) * wz_val;
						C(j, base1 + t) += val;
						C(base1 + t, j) += val;
					}
				}

				// Z'WZ within-group
				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = w_se[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = t1; t2 < q1; t2++)
					{
						double val = wz1 * lay.Z(g1, i, t2);
						C(base1 + t1, base1 + t2) += val;
						if (t1 != t2) C(base1 + t2, base1 + t1) += val;
					}
				}

				// Z'WZ cross-group
				for (intptr_t g2 = g1 + 1; g2 < G; g2++)
				{
					intptr_t j2 = groups[g2].indices[i];
					intptr_t q2 = lay.q[g2];
					intptr_t base2 = p + lay.offset[g2] + j2 * q2;

					for (intptr_t t1 = 0; t1 < q1; t1++)
					{
						double wz1 = w_se[i] * lay.Z(g1, i, t1);
						for (intptr_t t2 = 0; t2 < q2; t2++)
						{
							double val = wz1 * lay.Z(g2, i, t2);
							C(base1 + t1, base2 + t2) += val;
							C(base2 + t2, base1 + t1) += val;
						}
					}
				}
			}
		}

		// Var(β̂) = top-left p×p block of C⁻¹
		Eigen::LDLT<Eigen::MatrixXd> ldlt(C);
		Eigen::MatrixXd Cinv = ldlt.solve(Eigen::MatrixXd::Identity(sdim, sdim));
		vcov = Cinv.topLeftCorner(p, p);

		// ── Beta: profile φ out by extending C with a φ row/column ──────
		//
		// The Henderson inverse above gives Var(β̂|θ̂,φ̂).  glmmTMB reports
		// the marginal variance accounting for φ uncertainty, obtained by
		// inverting the full joint information matrix.  For beta, the cross
		// blocks are given by Ferrari–Cribari-Neto (2004, eqs. 6–7):
		//
		//   K_βφ_j = Σ_i X_{ij} · c_i
		//   K_uφ_k = Σ_i Z_{ik} · c_i
		//   K_φφ   = Σ_i [μ_i² ψ'(μ_i φ) + (1-μ_i)² ψ'((1-μ_i) φ) − ψ'(φ)]
		//
		// where c_i = μ_i(1-μ_i) · φ · [μ_i ψ'(μ_i φ) − (1-μ_i) ψ'((1-μ_i) φ)].
		//
		// Var(β̂) = top-left p×p of (extended C)⁻¹.  If the extended system
		// is indefinite we keep the conditional vcov above.
		if (fam_used.name == "beta")
		{
			double phi_b = fam_used.phi;
			double tri_phi = boost::math::trigamma(phi_b);
			intptr_t ext = sdim + 1;
			Eigen::MatrixXd C_ext = Eigen::MatrixXd::Zero(ext, ext);
			C_ext.topLeftCorner(sdim, sdim) = C;
			double K_ff = 0.0;

			for (intptr_t i = 0; i < n; i++)
			{
				double mi = std::clamp(final_inner.mu[i], 1e-10, 1.0 - 1e-10);
				double mu1m = mi * (1.0 - mi);
				double a = mi * phi_b;
				double b = (1.0 - mi) * phi_b;
				double tri_a = boost::math::trigamma(a);
				double tri_b = boost::math::trigamma(b);
				double c_i = mu1m * phi_b * (mi * tri_a - (1.0 - mi) * tri_b);
				K_ff += mi * mi * tri_a + (1.0 - mi) * (1.0 - mi) * tri_b - tri_phi;

				// K_βφ contribution
				for (intptr_t j = 0; j < p; j++)
				{
					double v = Xm(i, j) * c_i;
					C_ext(j, ext - 1) += v;
					C_ext(ext - 1, j) += v;
				}
				// K_uφ contribution
				for (intptr_t g = 0; g < G; g++)
				{
					intptr_t gj = groups[g].indices[i];
					intptr_t qg = lay.q[g];
					intptr_t base = p + lay.offset[g] + gj * qg;
					for (intptr_t t = 0; t < qg; t++)
					{
						double v = lay.Z(g, i, t) * c_i;
						C_ext(base + t, ext - 1) += v;
						C_ext(ext - 1, base + t) += v;
					}
				}
			}
			C_ext(ext - 1, ext - 1) = K_ff;

			Eigen::LDLT<Eigen::MatrixXd> ldlt_ext(C_ext);
			if (ldlt_ext.info() == Eigen::Success && ldlt_ext.isPositive())
			{
				Eigen::MatrixXd Ce_inv = ldlt_ext.solve(
					Eigen::MatrixXd::Identity(ext, ext));
				Eigen::MatrixXd vcov_ext = Ce_inv.topLeftCorner(p, p);
				bool ok = vcov_ext.allFinite();
				for (intptr_t j = 0; ok && j < p; j++)
					if (vcov_ext(j, j) <= 0) ok = false;
				if (ok) vcov = std::move(vcov_ext);
			}
		}
	}

	// ── Full Laplace vcov for random-slope non-Gaussian models ─────────
	//
	// The Henderson conditional vcov Var(β̂ | θ̂) computed above matches
	// lme4 and agrees with glmmTMB to high precision for random-intercept-
	// only models: H_uu = Z'WZ + D⁻¹ is dominated by D⁻¹ when Z is a
	// group-indicator matrix, so the Laplace correction ½ log|H_uu(β)|
	// is essentially β-independent and the Henderson formula captures
	// the full marginal Hessian of β.
	//
	// For random-slope models the working weights W = μ(1−μ) vary within
	// group, making H_uu genuinely β-dependent.  The full Laplace
	// marginal Hessian then has additional β-block contributions that
	// Henderson misses, and Henderson underestimates Var(β̂) — notably
	// for the intercept when the random intercept and slope are
	// correlated.  glmmTMB reports the full Laplace vcov by inverting
	// TMB's joint (β, θ) Hessian; we do the same by finite-differencing
	// LaplaceJointObjective (the Phase 2 objective, which already
	// includes ½ log|H_uu|) at the converged φ̂ = (β̂, θ̂), inverting,
	// and extracting the top-left p×p block.
	//
	// We skip this pass in the cases where Henderson is already exact:
	// Gaussian (no log-det correction), Student-t (Phase 2 skipped),
	// and intercept-only models (H_uu is β-free in practice).  We also
	// fall back to the Henderson vcov if the FD Hessian is numerically
	// indefinite — rare, but can occur at quasi-singular random-slope
	// fits where the variance component is at the boundary.
	bool has_random_slopes = false;
	for (intptr_t g = 0; g < G; g++) {
		if (lay.q[g] > 1) { has_random_slopes = true; break; }
	}

	// Local flags populated by the identifiability diagnostic below.
	// Default: optimistic.  Set to false when the model is structurally
	// unidentified (random slope aliased with random intercept).
	bool well_identified_flag = true;
	String identifiability_msg;

	// ── Structural identifiability check ──────────────────────────
	// The most common cause of weak identifiability in mixed models is
	// a random-slope predictor that is constant within every level of
	// its grouping factor.  Example: the formula `(1 + age | speaker)`
	// where `age` is a between-subject variable — every observation of
	// a given speaker has the same `age` value, so the random slope
	// `u_age` enters the linear predictor only through `u_age · age_i`
	// with constant age_i, which is aliased with the random intercept
	// `u_0i`.  The data therefore contains no curvature information
	// about σ²_age or its correlation with σ²_0, and the posterior mode
	// is determined entirely by the priors on those hyperparameters.
	//
	// glmmTMB catches this case by returning NA for logLik because its
	// unregularized Hessian is singular.  Phonometrica's weakly-
	// informative priors make the regularized objective smooth, so the
	// optimizer reports convergence; without an explicit diagnostic,
	// the user has no signal that the fit is non-informative in that
	// direction.
	//
	// This structural test inspects the design directly: for each
	// random-slope term, check whether its Z column is constant within
	// every level of the grouping factor.  It is deterministic (no FD
	// noise), catches the condition exactly, and does not depend on
	// prior strength.  Tolerance 1e-10 on the within-level range
	// handles floating-point exact-equality while letting any genuine
	// within-level variation pass.
	//
	// Applies to every mixed-model family (Gaussian, Poisson, NB, Beta,
	// binomial, Student) — the diagnostic is a property of the design,
	// not of the likelihood.
	if (has_random_slopes && G > 0)
	{
		for (intptr_t g = 0; g < G && well_identified_flag; g++)
		{
			if (lay.q[g] <= 1) continue;    // intercept-only group, no slopes
			const std::vector<intptr_t> &gi = *lay.group_indices[g];
			intptr_t J = lay.J[g];

			for (intptr_t t = 1; t < lay.q[g] && well_identified_flag; t++)
			{
				// Track min/max of slope term t's Z values within
				// each level of group g.
				std::vector<double> z_min(J, std::numeric_limits<double>::infinity());
				std::vector<double> z_max(J, -std::numeric_limits<double>::infinity());
				for (intptr_t i = 0; i < n; i++)
				{
					intptr_t j = gi[i];
					double z = lay.Z(g, i, t);
					if (z < z_min[j]) z_min[j] = z;
					if (z > z_max[j]) z_max[j] = z;
				}

				bool constant_within_all_levels = true;
				for (intptr_t j = 0; j < J; j++)
				{
					if (z_max[j] - z_min[j] > 1e-10) {
						constant_within_all_levels = false;
						break;
					}
				}

				if (constant_within_all_levels)
				{
					well_identified_flag = false;
					identifiability_msg = String(
						"A random-slope predictor is constant within "
						"every level of its grouping factor, so the "
						"corresponding variance component is not "
						"identified by the data (the random slope is "
						"aliased with the random intercept). The "
						"reported mode is sustained by prior "
						"regularization alone; glmmTMB would typically "
						"return NA for this fit. Consider removing the "
						"random slope or moving the predictor to a finer "
						"grouping factor. Parameter estimates and "
						"standard errors along that direction are "
						"prior-driven and should be interpreted with "
						"caution.");
				}
			}
		}
	}

	if (!is_gaussian && fam.name != "student" && has_random_slopes && G > 0
	    && saved_theta.size() > 0)
	{
		intptr_t D_p2 = p + saved_theta.size();

		// Assemble φ̂ = [β̂, θ̂] — the point at which Phase 2 converged.
		Eigen::VectorXd phi_hat(D_p2);
		for (intptr_t i = 0; i < p; i++) phi_hat[i] = beta_hat[i];
		for (intptr_t i = 0; i < saved_theta.size(); i++) {
			phi_hat[p + i] = saved_theta[i];
		}

		// ── Regularized Hessian for SE computation ─────────────────
		// When the joint (β, θ) Hessian inverts cleanly, use its
		// top-left p×p block as the fixed-effect vcov; otherwise fall
		// back silently to the Henderson conditional vcov already in
		// `vcov`.  Numerical failures here are unrelated to the
		// structural identifiability test above — they reflect FD
		// noise on the outer Hessian, not model mis-specification.
		LaplaceJointObjective joint_obj_vc{fam, Xm, ym, lay, n, p,
		                                    saved_n_chol, priors, coef_names, off_ptr};
		joint_obj_vc.last_u = final_inner.u;

		Eigen::MatrixXd H_full = compute_fd_hessian(joint_obj_vc, phi_hat, 1e-3);
		Eigen::LDLT<Eigen::MatrixXd> ldlt_full(H_full);
		if (ldlt_full.info() == Eigen::Success && ldlt_full.isPositive())
		{
			Eigen::MatrixXd Hinv = ldlt_full.solve(
				Eigen::MatrixXd::Identity(D_p2, D_p2));
			if (Hinv.allFinite())
			{
				Eigen::MatrixXd vcov_full = Hinv.topLeftCorner(p, p);
				bool ok = true;
				for (intptr_t i = 0; i < p; i++) {
					double v = vcov_full(i, i);
					if (!std::isfinite(v) || v <= 0) { ok = false; break; }
				}
				if (ok) vcov = std::move(vcov_full);
			}
		}
		// else: keep the Henderson vcov already in `vcov`.
	}

	// ── Build the Model ─────────────────────────────────────────────

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
	model.theta = fam_used.theta;
	model.phi = fam_used.phi;
	model.sigma = fam_used.sigma;
	model.nu = fam_used.nu;
	model.nobs = n;
	model.nfixed = p;
	model.y = y;
	model.X = X;

	model.beta = Array<double>(p, 0.0);
	model.se = Array<double>(p, 0.0);
	model.stat = Array<double>(p, 0.0);
	model.p = Array<double>(p, 0.0);

	boost::math::normal_distribution<double> normal;

	for (intptr_t i = 1; i <= p; i++)
	{
		model.beta[i] = beta_hat[i - 1];
		double var_i = vcov(i - 1, i - 1);
		model.se[i] = (var_i > 0) ? std::sqrt(var_i) : 0.0;
		model.stat[i] = (model.se[i] > 0) ? model.beta[i] / model.se[i] : 0.0;
		model.p[i] = 2.0 * (1.0 - boost::math::cdf(normal, std::abs(model.stat[i])));
	}

	// Store full variance-covariance matrix of fixed effects
	{
		model.vcov = Array<double>(p, p, 0.0);
		for (intptr_t i = 0; i < p; i++) {
			for (intptr_t j = 0; j < p; j++) {
				model.vcov(i + 1, j + 1) = vcov(i, j);
			}
		}
	}

	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);
	for (intptr_t i = 0; i < n; i++)
	{
		model.fitted[i + 1] = final_inner.mu[i];
		model.residuals[i + 1] = ym[i] - final_inner.mu[i];
	}

	if (is_gaussian)
	{
		model.rse = std::sqrt(sigma2);
	}
	// df_residual is set consistently for all families.  For mixed models,
	// this is not the "true" residual degrees of freedom (which would need
	// to account for random-effects shrinkage via edf), but rather the same
	// n − p convention that lme4 reports for GLMMs.  Keeping it populated
	// means the `model.df` scripting key returns a sensible value for both
	// Gaussian and non-Gaussian mixed models.
	model.df_residual = n - p;

	for (intptr_t g = 0; g < G; g++)
	{
		RandomEffectGroup reg;
		reg.group_name = groups[g].name;
		reg.nlevels = groups[g].nlevels;
		reg.level_names = groups[g].levels;

		intptr_t qg = lay.q[g];

		// Term names from the GroupingInfo (e.g. "Intercept", "vowel[i]", "vowel[u]")
		reg.term_names = groups[g].term_names;

		// Covariance matrix D_g: from D_inv_final (non-Gaussian/Cholesky path)
		// or from scalar sigma2_u (Gaussian intercept-only path).
		Eigen::MatrixXd D_g;
		if (!D_inv_final.empty())
		{
			// Invert D_inv to get D (small matrix, at most q × q)
			D_g = D_inv_final[g].inverse();
		}
		else
		{
			// Gaussian scalar path: diagonal covariance
			D_g = Eigen::MatrixXd::Zero(qg, qg);
			D_g(0, 0) = sigma2_u[g];
		}

		// Variance for each term (diagonal of D_g)
		for (intptr_t t = 0; t < qg; t++) {
			reg.variance.append(D_g(t, t));
		}

		// Cholesky factor (packed lower triangle) for the covariance display
		Eigen::LLT<Eigen::MatrixXd> llt(D_g);
		Eigen::MatrixXd L_out = llt.matrixL();
		for (intptr_t r = 0; r < qg; r++) {
			for (intptr_t c = 0; c <= r; c++) {
				reg.cov_chol.append(L_out(r, c));
			}
		}

		// Conditional modes: u[j*q + t] for each level j and term t
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			for (intptr_t t = 0; t < qg; t++) {
				reg.conditional_modes.append(final_inner.u[lay.offset[g] + j * qg + t]);
			}
		}

		// Z design info for simulation-based diagnostics (DHARMa-style scaled residuals).
		// This is transient fitting-time data; not serialised.
		reg.nterms = groups[g].nterms;
		reg.indices = groups[g].indices;
		reg.Z_design = groups[g].Z_design;

		model.random_effects.append(std::move(reg));
	}

	model.loglik = -final_inner.laplace_nll;

	// ── Student-t Laplace correction: hybrid exact / Fisher-info ──
	//
	// final_inner.laplace_nll was computed inside solve_u_given_beta /
	// solve_pirls using IRLS / Fisher-information weights. For
	// Student-t we now recompute ½ log|H_uu| with the hybrid helper:
	// try the exact Hessian weights first, fall back to Fisher info if
	// the resulting H_uu is non-PD. This typically gives a tighter
	// loglik (matching glmmTMB / TMB) without sacrificing robustness.
	//
	// Per design (Q2 / 2a), this only affects the *reported* loglik —
	// optimization itself continues to use IRLS for stability. The
	// converged (β, θ, σ, ν) values are therefore unchanged; only the
	// Laplace correction term is replaced.
	if (fam.name == "student" && lay.J_total > 0)
	{
		// Recompute the IRLS log_det at the converged μ, so we know
		// what to subtract from final_inner.laplace_nll.
		Eigen::VectorXd w_irls(n);
		for (intptr_t i = 0; i < n; i++) {
			double r = ym[i] - final_inner.mu[i];
			w_irls[i] = (fam_used.nu + 1.0)
			           / (fam_used.nu * fam_used.sigma * fam_used.sigma + r * r);
		}
		double log_det_irls = full_log_det_H(w_irls, D_inv_final, lay, n);

		LaplaceMethod method;
		double log_det_hybrid = student_full_log_det_H_hybrid(
			ym, final_inner.mu, fam_used.sigma, fam_used.nu,
			D_inv_final, lay, n, method);

		// Replace the Laplace term: -½ log_det_irls + ½ log_det_hybrid
		double delta_nll = 0.5 * (log_det_hybrid - log_det_irls);
		model.loglik -= delta_nll;

		model.laplace_method = (method == LaplaceMethod::Exact)
		                        ? String("exact")
		                        : String("fisher_info");
	}

	model.compute_information_criteria();

	model.niter = niter;
	model.converged = converged;
	model.optimizer = optimizer_used;
	model.well_identified = well_identified_flag;
	model.fit_warning = identifiability_msg;

	// ── Bayesian posterior ──────────────────────────────────────────
	//
	// When priors are supplied, run INLA grid integration over the
	// hyperparameters to get mixture-of-Gaussians posteriors for β
	// and marginal posteriors for variance components.
	//   - Gaussian: θ = (chol_params, log σ)
	//   - Non-Gaussian: θ = (chol_params, [log disp...])

	if (priors)
	{
		model.estimation = Estimation::Bayesian;
		model.priors = *priors;

		if (is_gaussian && saved_theta.size() > 0 && G > 0)
		{
			GaussianCholObjective gauss_obj{Xm, ym, lay, n, p,
			                                 saved_n_chol, priors, coef_names, off_ptr};
			inla_grid_integrate_gaussian(model, gauss_obj, saved_theta,
			                              Xm, ym, lay, n, p, saved_n_chol,
			                              priors, coef_names, off_ptr);
		}
		else if (!is_gaussian && saved_theta.size() > 0 && G > 0)
		{
			PirlsObjective pirls_obj{fam, Xm, ym, lay, n, p,
			                          saved_beta_init, saved_n_chol, priors, coef_names, off_ptr};
			// Warm-start û from the joint optimizer's converged value
			// (final_inner.u, populated above by solve_u_given_beta).
			// Without this, both compute_fd_hessian and the per-grid-point
			// solve_pirls calls cold-start at u=0, which on binomial GLMMs
			// with strong random effects overshoots to a non-mode point —
			// inflating the FD Hessian (eigenvalues ~1e17 observed) so
			// T = 1/√λ collapses to 0, all grid points degenerate to θ*,
			// and conditional β̂(θ_k) lands at the overshoot mode rather
			// than the true MAP.  See the declaration of phase2_warm_u.
			inla_grid_integrate_pirls(model, pirls_obj, saved_theta,
			                           fam, Xm, ym, lay, n, p, saved_n_chol,
			                           saved_beta_init, priors, coef_names, off_ptr,
			                           final_inner.u);
		}
		else if (!is_gaussian && saved_theta.size() > 0 && G == 0)
		{
			// Fixed-effects-only Bayesian non-Gaussian (Student-t, NB, Beta).
			// The INLA paths above are gated on G > 0 because the CCD grid
			// integrates over (chol_params, log dispersion) — for G == 0
			// chol_params is empty and the grid would be 1-D (NB/Beta) or
			// 2-D (Student-t) over log-dispersion only.
			//
			// Rather than degenerate inla_grid_integrate_pirls, run a
			// dedicated joint Laplace approximation on φ = [β, log disp]
			// directly (see no_re_bayesian_laplace for details). This
			// fixes the WAIC dispersion-fixed bug — previously
			// bayesian_summaries() in fitting.cpp drew β samples but read
			// model.sigma / model.nu / model.theta / model.phi unchanged
			// across all S draws, undercounting p_waic by the number of
			// dispersion params.
			no_re_bayesian_laplace(model, fam, Xm, ym, lay, n, p,
			                        saved_n_chol, saved_theta, beta_hat,
			                        priors, coef_names, off_ptr);
			// On success, model.posterior_mean is now non-empty, which
			// short-circuits fitting.cpp:1657's bayesian_summaries fallback.
			// On failure (Hessian non-PD), we leave model.posterior_mean
			// empty so fitting.cpp falls back to the previous β-only WAIC
			// (with the documented underestimate). This preserves at-least
			// existing behaviour rather than silently producing nothing.
		}
	}

	if (!offset.empty()) model.offset = offset;

	return model;
}


// =====================================================================
// mixed_model_multistart: orchestration wrapper for multi-start fits
// =====================================================================
//
// For families like Student-t where the inner û-problem is non-log-concave,
// a single PIRLS run from a deterministic start can converge to a local —
// not global — minimum of the marginal Laplace NLL. This wrapper runs the
// optimization from N different starting points and returns the deepest
// converged fit.
//
// Perturbation strategy (Student only): vary initial σ and ν, the two
// parameters most directly responsible for whether the inner problem is
// heavy-tailed (small ν) or near-Gaussian (large ν). Other families pass
// through to single-start.
//
// Default policy: Student-t gets n_starts = 4 unless the caller overrides;
// other families always run single-start (n_starts ≤ 1 short-circuits to a
// direct mixed_model() call).
//
// Diagnostic: if multiple starts converge to *different* basins (Laplace
// NLL spread > 0.01), or if any start fails, a summary is appended to
// model.fit_warning.

Model mixed_model_multistart(const Array<double> &y, const Array<double> &X,
                              const std::vector<GroupingInfo> &groups, const Family &fam,
                              FittingCallback progress,
                              const PriorSpec *priors,
                              const Array<String> *coef_names,
                              int max_iter,
                              const Array<double> &offset,
                              const FitOptions &opts)
{
	int n_starts = opts.n_starts;
	bool is_student = (fam.name == "student");

	// Family-specific defaults: Student-t gets multi-start by default.
	if (n_starts == 0) {
		n_starts = is_student ? 4 : 1;
	}
	// Multi-start currently has no perturbation strategy for non-Student
	// families. Silently clamp.
	if (!is_student) {
		n_starts = 1;
	}
	if (n_starts < 1) n_starts = 1;

	// Fast path: no multi-start. Exact pre-existing behavior.
	if (n_starts == 1) {
		return mixed_model(y, X, groups, fam, progress,
		                    priors, coef_names, max_iter, offset, nullptr);
	}

	// ── Perturbation table for Student-t ─────────────────────────────
	// Index 0 is always the unperturbed default (matches single-start
	// behavior byte-for-byte). Indices 1..n_starts-1 vary ν across the
	// regime that matters: from heavy-tailed (ν≈3) to near-Gaussian
	// (ν≈30). σ is co-derived from the residual-variance formula at
	// each ν, so explicit σ overrides are not used here.
	std::vector<double> nu_grid;
	nu_grid.push_back(0.0);   // sentinel: index 0 = no override
	if (n_starts >= 2) nu_grid.push_back(3.0);
	if (n_starts >= 3) nu_grid.push_back(10.0);
	if (n_starts >= 4) nu_grid.push_back(30.0);
	// For n_starts > 4, fill remaining slots by interpolating between
	// 3 and 60 — diminishing returns but available for users who ask.
	for (int k = 5; k <= n_starts; k++) {
		double t = double(k - 4) / double(std::max(n_starts - 4, 1));
		nu_grid.push_back(3.0 + t * 57.0);
	}

	struct StartResult {
		Model model;
		bool succeeded = false;
		double loglik = -std::numeric_limits<double>::infinity();
		double nu_init = 0;
	};
	std::vector<StartResult> results(n_starts);

	int n_succ = 0;
	for (int k = 0; k < n_starts; k++)
	{
		InitOverrides ov;
		if (k > 0) {
			ov.has_nu_init = true;
			ov.nu_init = nu_grid[k];
		}
		results[k].nu_init = (k == 0) ? 5.0 : nu_grid[k];

		try {
			Model m = mixed_model(y, X, groups, fam,
			                       (k == 0) ? progress : nullptr,
			                       priors, coef_names, max_iter, offset,
			                       k == 0 ? nullptr : &ov);
			if (m.converged && std::isfinite(m.loglik)) {
				results[k].model = std::move(m);
				results[k].loglik = results[k].model.loglik;
				results[k].succeeded = true;
				n_succ++;
			}
		}
		catch (std::exception &) {
			// Leave results[k].succeeded = false. Continue to next start.
		}
	}

	if (n_succ == 0) {
		throw error("All % multi-start fits failed; the model could not be fitted "
		             "from any tested starting point.", n_starts);
	}

	// Pick best (highest logLik = lowest Laplace NLL).
	int best_k = -1;
	double best_ll = -std::numeric_limits<double>::infinity();
	for (int k = 0; k < n_starts; k++) {
		if (results[k].succeeded && results[k].loglik > best_ll) {
			best_ll = results[k].loglik;
			best_k = k;
		}
	}

	// Cluster successes into basins. Two starts are in the same basin
	// if their logLiks differ by less than basin_tol.
	constexpr double basin_tol = 0.01;
	double min_ll = best_ll, max_ll = best_ll;
	for (int k = 0; k < n_starts; k++) {
		if (results[k].succeeded) {
			min_ll = std::min(min_ll, results[k].loglik);
			max_ll = std::max(max_ll, results[k].loglik);
		}
	}
	int n_basins;
	{
		std::vector<double> sorted_lls;
		for (int k = 0; k < n_starts; k++) {
			if (results[k].succeeded) sorted_lls.push_back(results[k].loglik);
		}
		std::sort(sorted_lls.begin(), sorted_lls.end());
		n_basins = sorted_lls.empty() ? 0 : 1;
		for (size_t i = 1; i < sorted_lls.size(); i++) {
			if (sorted_lls[i] - sorted_lls[i - 1] > basin_tol) n_basins++;
		}
	}

	Model best = std::move(results[best_k].model);

	// ── Diagnostic polish pass (opt-in) ──────────────────────────────
	// Re-fit from the converged best with tighter gradient tolerance
	// and FD step. If FD noise was the convergence bottleneck, this
	// will move the loglik. If it doesn't move, the optimum is genuinely
	// at the FD-noise-floor of the function value, not the gradient.
	double polish_delta = 0.0;
	bool polish_attempted = false;
	bool polish_succeeded = false;
	if (opts.polish && is_student) {
		polish_attempted = true;
		InitOverrides polish_ov;
		polish_ov.has_sigma_init = true;
		polish_ov.sigma_init = best.sigma;
		polish_ov.has_nu_init = true;
		polish_ov.nu_init = best.nu;
		polish_ov.tight_tolerance = true;

		try {
			Model polished = mixed_model(y, X, groups, fam, nullptr,
			                              priors, coef_names, max_iter, offset,
			                              &polish_ov);
			if (polished.converged && std::isfinite(polished.loglik)) {
				polish_delta = polished.loglik - best.loglik;
				if (polished.loglik > best.loglik) {
					// Polish improved on the multi-start best.
					best = std::move(polished);
				}
				polish_succeeded = true;
			}
		}
		catch (std::exception &) {
			// Polish failed; keep the unpolished best. We'll note this
			// in fit_warning below.
		}
	}

	// Attach diagnostic summary if relevant.
	bool emit_summary = (n_basins > 1) || (n_succ < n_starts)
	                    || opts.report_starts || polish_attempted;
	if (emit_summary) {
		String msg;
		msg.append(String::format(
			"Multi-start fit: %d of %d starts converged; %d distinct basin(s); "
			"logLik range [",
			n_succ, n_starts, n_basins));
		msg.append(String::format("%.4f", min_ll));
		msg.append(String(", "));
		msg.append(String::format("%.4f", max_ll));
		msg.append(String("]; selected start "));
		msg.append(String::format("%d (initial nu = %.1f).",
			best_k, results[best_k].nu_init));

		if (polish_attempted) {
			if (polish_succeeded) {
				msg.append(String::format(
					" Polish: ΔlogLik = %+.4f.", polish_delta));
			} else {
				msg.append(String(" Polish: failed."));
			}
		}

		if (!best.fit_warning.empty()) {
			best.fit_warning.append(String("\n"));
		}
		best.fit_warning.append(msg);
	}

	return best;
}


// =====================================================================
// evaluate_at: diagnostic Laplace-NLL evaluation harness
// =====================================================================
//
// Computes the four Laplace components at a user-supplied (β, θ, û)
// without running outer optimization. The four components sum to the
// Laplace NLL:
//
//   laplace_nll = cond_nll + prior_nll + ½ log|H_uu| + (-½ J log 2π)
//
// Used to cross-validate against reference R packages: feed glmmTMB's
// converged parameters in and read which component differs to localize
// any formula discrepancy.

EvaluationResult evaluate_at(const Model &model, const EvaluationOverrides &ov)
{
	EvaluationResult res;

	// ── Validate model state ─────────────────────────────────────────
	intptr_t G = model.random_effects.size();
	intptr_t n = model.nobs;
	intptr_t p = model.nfixed;

	if (model.X.empty() || model.y.empty() || n == 0 || p == 0) {
		res.error = "evaluate() requires a model with stored X and y.";
		return res;
	}

	// Z_design / indices are NOT serialized. Required to assemble Zu.
	for (intptr_t g = 1; g <= G; g++)
	{
		auto &re = model.random_effects[g];
		if (re.indices.empty() || re.Z_design.empty()) {
			res.error = "evaluate() requires a model fitted in the current "
			            "session (Z design info is not serialized to file).";
			return res;
		}
	}

	// ── Build GroupLayout from model.random_effects ──────────────────
	GroupLayout lay;
	lay.G = G;
	lay.J_total = 0;
	lay.J.resize(G);
	lay.q.resize(G);
	lay.offset.resize(G);
	lay.group_indices.resize(G);
	lay.Z_data.resize(G);
	lay.n_obs = n;

	for (intptr_t g = 0; g < G; g++)
	{
		auto &re = model.random_effects[g + 1];   // 1-based Array
		lay.offset[g] = lay.J_total;
		lay.J[g] = re.nlevels;
		lay.q[g] = re.nterms;
		lay.J_total += lay.J[g] * lay.q[g];
		lay.group_indices[g] = &re.indices;
		lay.Z_data[g] = re.Z_design.data();
	}

	intptr_t J_total = lay.J_total;

	// ── Build Eigen maps over X, y ───────────────────────────────────
	Eigen::Map<Matrix<double>> Xm(const_cast<double *>(model.X.data()), n, p);
	Eigen::Map<Vector<double>> ym(const_cast<double *>(model.y.data()), n);

	// ── Override fixed effects (β) ───────────────────────────────────
	Eigen::VectorXd beta(p);
	if (ov.beta) {
		if (ov.beta->size() != p) {
			res.error = String::format(
				"beta override has length %lld; expected %lld.",
				(long long)ov.beta->size(), (long long)p);
			return res;
		}
		for (intptr_t i = 0; i < p; i++) beta[i] = ov.beta->data()[i];
	} else {
		for (intptr_t i = 0; i < p; i++) beta[i] = model.beta.data()[i];
	}

	// ── Build D_inv and log_det_Dg per group ────────────────────────
	std::vector<Eigen::MatrixXd> D_inv(G);
	std::vector<double> log_det_Dg(G);

	for (intptr_t g = 0; g < G; g++)
	{
		auto &re = model.random_effects[g + 1];
		intptr_t qg = lay.q[g];
		Eigen::MatrixXd Sigma_g(qg, qg);

		bool have_override = (ov.Sigma != nullptr
		                      && (intptr_t)ov.Sigma->size() > g
		                      && !(*ov.Sigma)[g].empty());

		if (have_override)
		{
			const Array<double> &S = (*ov.Sigma)[g];
			intptr_t total = S.size();
			if (total != qg * qg) {
				res.error = String::format(
					"Sigma[%lld] has %lld elements; expected %lld (q_g = %lld).",
					(long long)g, (long long)total,
					(long long)(qg * qg), (long long)qg);
				return res;
			}
			// Σ is symmetric, so layout (row- vs column-major) does not
			// matter. Read element-by-element using column-major
			// indexing (Phonometrica's 2D Array convention).
			const double *sd = S.data();
			for (intptr_t r = 0; r < qg; r++) {
				for (intptr_t c = 0; c < qg; c++) {
					Sigma_g(r, c) = sd[c * qg + r];
				}
			}
			Sigma_g = 0.5 * (Sigma_g + Sigma_g.transpose());
		}
		else
		{
			// Reconstruct Σ_g from packed cov_chol: Σ = L L'.
			// cov_chol is 1-indexed, packed row-by-row.
			const Array<double> &cc = re.cov_chol;
			Eigen::MatrixXd L = Eigen::MatrixXd::Zero(qg, qg);
			for (intptr_t r = 0; r < qg; r++) {
				for (intptr_t c = 0; c <= r; c++) {
					intptr_t idx = r * (r + 1) / 2 + c + 1;
					if (idx <= cc.size()) L(r, c) = cc[idx];
				}
			}
			Sigma_g = L * L.transpose();
		}

		// D_inv = Σ⁻¹ via Cholesky; log_det_Dg = 2 Σ log L_kk.
		Eigen::LLT<Eigen::MatrixXd> llt(Sigma_g);
		if (llt.info() != Eigen::Success) {
			res.error = String::format(
				"Sigma[%lld] is not positive-definite.", (long long)g);
			return res;
		}
		Eigen::MatrixXd L = llt.matrixL();
		Eigen::MatrixXd Linv = L.triangularView<Eigen::Lower>().solve(
			Eigen::MatrixXd::Identity(qg, qg));
		D_inv[g] = Linv.transpose() * Linv;
		double ld = 0;
		for (intptr_t r = 0; r < qg; r++) ld += std::log(L(r, r));
		log_det_Dg[g] = 2.0 * ld;
	}

	// ── Build Family with overrides applied ─────────────────────────
	Family fam;
	if (model.family == "gaussian") {
		fam = Family::gaussian();
	} else if (model.family == "binomial") {
		fam = Family::binomial();
	} else if (model.family == "poisson") {
		fam = Family::poisson();
	} else if (model.family == "negbin") {
		double th = ov.has_theta_nb ? ov.theta_nb_val : model.theta;
		fam = Family::negbin(th);
	} else if (model.family == "beta") {
		double phi = ov.has_phi ? ov.phi_val : model.phi;
		fam = Family::beta(phi);
	} else if (model.family == "student") {
		double sigma = ov.has_sigma ? ov.sigma_val : model.sigma;
		double nu    = ov.has_nu    ? ov.nu_val    : model.nu;
		fam = Family::student(sigma, nu);
	} else {
		String msg("evaluate() does not support family: ");
		msg.append(model.family);
		res.error = std::move(msg);
		return res;
	}

	bool is_gaussian = (fam.name == "gaussian");

	// For Gaussian, σ² appears explicitly in cond_nll and in W = (1/σ²)I.
	double sigma2_gauss = 0;
	if (is_gaussian) {
		double sigma = ov.has_sigma ? ov.sigma_val : model.rse;
		sigma2_gauss = sigma * sigma;
	}

	// Refit-u for Gaussian needs the σ²-aware Henderson u-only solve,
	// not yet wired in here. Reject explicitly to avoid silently
	// returning a σ²=1 result.
	if (ov.refit_u && is_gaussian) {
		res.error = "evaluate() with refit_u=true is not supported for "
		            "Gaussian models in this iteration.";
		return res;
	}

	// ── Determine û ──────────────────────────────────────────────────
	Eigen::VectorXd u(J_total);

	if (ov.refit_u)
	{
		// PIRLS from u=0 at the supplied (β, θ).
		ProfiledResult pr = solve_u_given_beta(D_inv, log_det_Dg, fam,
		                                        Xm, ym, lay, n, p, beta);
		u = pr.u;
		res.u_refit = true;
	}
	else if (ov.u)
	{
		if (ov.u->size() != J_total) {
			res.error = String::format(
				"u override has length %lld; expected %lld.",
				(long long)ov.u->size(), (long long)J_total);
			return res;
		}
		for (intptr_t i = 0; i < J_total; i++) u[i] = ov.u->data()[i];
		res.u_refit = false;
	}
	else
	{
		// Fall through to the model's stored conditional_modes.
		// re.conditional_modes is row-major j*q + t (raw 0-based via data()).
		intptr_t off_u = 0;
		for (intptr_t g = 0; g < G; g++)
		{
			auto &re = model.random_effects[g + 1];
			intptr_t qg = lay.q[g];
			intptr_t Jg = lay.J[g];
			const Array<double> &cm = re.conditional_modes;
			if (cm.size() < Jg * qg) {
				res.error = String::format(
					"Group %lld: conditional_modes has %lld entries; "
					"expected %lld.",
					(long long)(g + 1), (long long)cm.size(),
					(long long)(Jg * qg));
				return res;
			}
			for (intptr_t k = 0; k < Jg * qg; k++) {
				u[off_u + k] = cm.data()[k];
			}
			off_u += Jg * qg;
		}
		res.u_refit = false;
	}

	// ── Offset support ──────────────────────────────────────────────
	Eigen::VectorXd offset_vec;
	const Eigen::VectorXd *off_ptr = nullptr;
	if (!model.offset.empty()) {
		offset_vec = Eigen::Map<const Eigen::VectorXd>(
			model.offset.data(), model.offset.size());
		off_ptr = &offset_vec;
	}

	// ── η = Xβ + Zu + offset, then μ ────────────────────────────────
	Eigen::VectorXd eta = Xm * beta;
	if (off_ptr) eta += *off_ptr;
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++) {
				eta[i] += lay.Z(g, i, t) * u[base + t];
			}
		}
	}
	Eigen::VectorXd mu = (fam.link_name == "identity")
	    ? fam.linkinv(eta)
	    : fam.linkinv(eta.cwiseMax(-30.0).cwiseMin(30.0));

	// ── cond_nll = -log p(y | β, û) ─────────────────────────────────
	double cond_nll;
	if (is_gaussian) {
		double rss = (ym - mu).squaredNorm();
		cond_nll = rss / (2.0 * sigma2_gauss)
		           + 0.5 * n * std::log(2.0 * M_PI * sigma2_gauss);
	} else {
		cond_nll = -fam.loglik(ym, mu);
	}

	// ── prior_nll = -log p(û | θ) ───────────────────────────────────
	double prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					quad += u[base + t1] * D_inv[g](t1, t2) * u[base + t2];
				}
			}
			prior_nll += quad / 2.0;
		}
		prior_nll += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI)
		                          + 0.5 * log_det_Dg[g]);
	}

	// ── ½ log|H_uu| (Laplace correction) ────────────────────────────
	double half_log_det_Huu;
	bool is_student = (fam.name == "student");

	if (is_student && J_total > 0) {
		// Hybrid: try exact Hessian first, fall back to Fisher info.
		LaplaceMethod method;
		double log_det = student_full_log_det_H_hybrid(
			ym, mu, fam.sigma, fam.nu, D_inv, lay, n, method);
		half_log_det_Huu = 0.5 * log_det;
		res.laplace_method = (method == LaplaceMethod::Exact)
		                      ? String("exact")
		                      : String("fisher_info");
	}
	else {
		// Non-Student: use the standard IRLS-style weights as before.
		Eigen::VectorXd w_final(n);
		if (is_gaussian) {
			w_final.setConstant(1.0 / sigma2_gauss);
		} else if (fam.custom_weights) {
			w_final = fam.custom_weights(ym, mu);
		} else {
			Eigen::VectorXd V  = fam.variance(mu);
			Eigen::VectorXd me = fam.mu_eta(mu);
			for (intptr_t i = 0; i < n; i++) {
				double v = std::max(V[i], 1e-10);
				double d = std::max(me[i], 1e-10);
				w_final[i] = d * d / v;
			}
		}
		half_log_det_Huu = 0.5 * full_log_det_H(w_final, D_inv, lay, n);
	}

	// ── Constant term ───────────────────────────────────────────────
	double const_term = -0.5 * J_total * std::log(2.0 * M_PI);

	// ── Assemble ─────────────────────────────────────────────────────
	double laplace_nll = cond_nll + prior_nll + half_log_det_Huu + const_term;

	res.ok           = true;
	res.cond_nll     = cond_nll;
	res.prior_nll    = prior_nll;
	res.log_det_Huu  = half_log_det_Huu;
	res.const_term   = const_term;
	res.laplace_nll  = laplace_nll;

	res.u_used = Array<double>(J_total, 0.0);
	for (intptr_t i = 0; i < J_total; i++) {
		res.u_used.data()[i] = u[i];
	}

	return res;
}

} // namespace phonometrica::stats
