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
#include <algorithm>
#include <random>
#include <boost/math/distributions/normal.hpp>
#include <phon/third_party/LBFGSpp/LBFGS.h>
#include <phon/analysis/mixed_model.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/analysis/waic.hpp>
#include <phon/analysis/psis.hpp>
#include <phon/utils/matrix.hpp>

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

// Unpack a Cholesky parameter slice into a lower-triangular Eigen matrix.
// theta_g: packed vector of length q(q+1)/2.
// Returns the q × q lower Cholesky factor L (with exp applied to diagonal).
static Eigen::MatrixXd unpack_cholesky(const double *theta_g, intptr_t q)
{
	Eigen::MatrixXd L = Eigen::MatrixXd::Zero(q, q);
	for (intptr_t r = 0; r < q; r++)
	{
		for (intptr_t c = 0; c <= r; c++)
		{
			intptr_t idx = r * (r + 1) / 2 + c;
			if (r == c) {
				L(r, c) = std::exp(theta_g[idx]); // diagonal: exponentiate
			} else {
				L(r, c) = theta_g[idx];            // off-diagonal: as-is
			}
		}
	}
	return L;
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

// log|D| = 2 Σ log L_kk = 2 Σ θ_diag (since diag elements are stored as log).
static double log_det_D(const double *theta_g, intptr_t q)
{
	double ld = 0;
	for (intptr_t r = 0; r < q; r++) {
		ld += theta_g[r * (r + 1) / 2 + r]; // the log L_rr value
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
static void add_fixed_prior_to_henderson(Eigen::MatrixXd &H, Eigen::VectorXd &rhs,
                                          const PriorSpec &priors,
                                          const Array<String> &coef_names,
                                          intptr_t p)
{
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
static double variance_prior_log_density(const std::vector<Eigen::MatrixXd> &D_cov,
                                          const PriorSpec &priors,
                                          const GroupLayout &lay)
{
	double lp = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];

		// Marginal prior on each random-effect SD (plus log-σ Jacobian).
		for (intptr_t t = 0; t < qg; t++)
		{
			double sd = std::sqrt(std::max(D_cov[g](t, t), 1e-20));
			// Prior on SD + Jacobian for the exp transform (log σ → σ).
			lp += priors.variance_components.log_density(sd) + std::log(sd);
		}

		// LKJ-style prior on the correlation structure when q_g ≥ 2.
		// Density: p(R | η) ∝ |R|^(η − 1) where R is the correlation matrix
		// obtained from D = σ R σ, so R_ij = D_ij / (σ_i σ_j) and therefore
		//     |R| = |D| / ∏_i D_ii.
		// Taking logs:  log|R| = log|D| − Σ_i log D_ii.
		// With η = 1 (the default), this contribution is identically zero,
		// so we skip the computation entirely — preserves exact backward
		// compatibility when the user does not set an LKJ prior.
		if (qg >= 2 && priors.lkj_eta != 1.0)
		{
			double log_det_D = std::log(std::max(D_cov[g].determinant(), 1e-20));
			double log_det_diag = 0;
			for (intptr_t t = 0; t < qg; t++) {
				log_det_diag += std::log(std::max(D_cov[g](t, t), 1e-20));
			}
			double log_det_R = log_det_D - log_det_diag;
			lp += (priors.lkj_eta - 1.0) * log_det_R;
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

	// General case (G > 1): build the full J × J matrix and factorize.
	Eigen::MatrixXd H = Eigen::MatrixXd::Zero(J, J);

	// Prior precision: D_g⁻¹ block at each level
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					H(base + t1, base + t2) = D_inv[g](t1, t2);
				}
			}
		}
	}

	// Data contributions
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < lay.G; g1++)
		{
			intptr_t j1 = (*lay.group_indices[g1])[i];
			intptr_t base1 = lay.offset[g1] + j1 * lay.q[g1];

			// Within-group: w[i] * z_g1 ⊗ z_g1
			for (intptr_t t1 = 0; t1 < lay.q[g1]; t1++)
			{
				double wz1 = w[i] * lay.Z(g1, i, t1);
				for (intptr_t t2 = t1; t2 < lay.q[g1]; t2++)
				{
					double val = wz1 * lay.Z(g1, i, t2);
					H(base1 + t1, base1 + t2) += val;
					if (t1 != t2) H(base1 + t2, base1 + t1) += val;
				}
			}

			// Cross-group: w[i] * z_g1 ⊗ z_g2
			for (intptr_t g2 = g1 + 1; g2 < lay.G; g2++)
			{
				intptr_t j2 = (*lay.group_indices[g2])[i];
				intptr_t base2 = lay.offset[g2] + j2 * lay.q[g2];

				for (intptr_t t1 = 0; t1 < lay.q[g1]; t1++)
				{
					double wz1 = w[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = 0; t2 < lay.q[g2]; t2++)
					{
						double val = wz1 * lay.Z(g2, i, t2);
						H(base1 + t1, base2 + t2) += val;
						H(base2 + t2, base1 + t1) += val;
					}
				}
			}
		}
	}

	Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
	return ldlt.vectorD().array().log().sum();
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

		Eigen::MatrixXd H = Eigen::MatrixXd::Zero(sdim, sdim);
		Eigen::VectorXd rhs = Eigen::VectorXd::Zero(sdim);

		// X'WX (p × p) and X'Wz (p)
		for (intptr_t i = 0; i < n; i++)
		{
			double wz = w[i] * z[i];
			for (intptr_t j1 = 0; j1 < p; j1++)
			{
				rhs[j1] += Xm(i, j1) * wz;
				double wx = Xm(i, j1) * w[i];
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

		// D⁻¹: q_g × q_g block at each level
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

		// Data contributions to X'WZ, Z'WX, Z'WZ, Z'Wz
		for (intptr_t i = 0; i < n; i++)
		{
			double wz = w[i] * z[i];

			for (intptr_t g1 = 0; g1 < G; g1++)
			{
				intptr_t j1 = (*lay.group_indices[g1])[i];
				intptr_t q1 = lay.q[g1];
				intptr_t base1 = p + lay.offset[g1] + j1 * q1;

				// X'WZ and Z'WX: for each random term t
				for (intptr_t t = 0; t < q1; t++)
				{
					double z_val = lay.Z(g1, i, t);
					double wz_val = w[i] * z_val;

					for (intptr_t j = 0; j < p; j++)
					{
						double val = Xm(i, j) * wz_val;
						H(j, base1 + t) += val;
						H(base1 + t, j) += val;
					}

					// Z'Wz
					rhs[base1 + t] += wz * z_val;
				}

				// Z'WZ within-group: w[i] * z_g1 ⊗ z_g1
				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = w[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = t1; t2 < q1; t2++)
					{
						double val = wz1 * lay.Z(g1, i, t2);
						H(base1 + t1, base1 + t2) += val;
						if (t1 != t2) H(base1 + t2, base1 + t1) += val;
					}
				}

				// Z'WZ cross-group: w[i] * z_g1 ⊗ z_g2
				for (intptr_t g2 = g1 + 1; g2 < G; g2++)
				{
					intptr_t j2 = (*lay.group_indices[g2])[i];
					intptr_t q2 = lay.q[g2];
					intptr_t base2 = p + lay.offset[g2] + j2 * q2;

					for (intptr_t t1 = 0; t1 < q1; t1++)
					{
						double wz1 = w[i] * lay.Z(g1, i, t1);
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

		// ── Fixed-effect prior (Bayesian mode) ──────────────────
		if (priors && coef_names)
			add_fixed_prior_to_henderson(H, rhs, *priors, *coef_names, p);

		// ── Solve ───────────────────────────────────────────────
		Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
		Eigen::VectorXd sol = ldlt.solve(rhs);

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
    const Eigen::VectorXd *off_ptr = nullptr)
{
	ProfiledResult res;
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;

	res.beta = beta;  // fixed — not updated
	res.u = (u_init.size() == J) ? u_init : Eigen::VectorXd::Zero(J);

	Eigen::VectorXd Xbeta = Xm * beta;
	add_offset(Xbeta, off_ptr);

	// When there are no random effects (G=0, J=0), skip the u-update loop entirely.
	if (J > 0)
	{
	for (int iter = 0; iter < 100; iter++)
	{
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
			// ── Crossed groups: full J × J system ──
			// (Z'WZ + D⁻¹) u = Z'W r
			Eigen::MatrixXd H = Eigen::MatrixXd::Zero(J, J);
			Eigen::VectorXd rhs = Eigen::VectorXd::Zero(J);

			// D⁻¹ blocks on diagonal
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				for (intptr_t j = 0; j < lay.J[g]; j++)
				{
					intptr_t base = lay.offset[g] + j * qg;
					for (intptr_t t1 = 0; t1 < qg; t1++)
						for (intptr_t t2 = 0; t2 < qg; t2++)
							H(base + t1, base + t2) = D_inv[g](t1, t2);
				}
			}

			// Data contributions: Z'WZ and Z'Wr
			for (intptr_t i = 0; i < n; i++)
			{
				for (intptr_t g1 = 0; g1 < G; g1++)
				{
					intptr_t j1 = (*lay.group_indices[g1])[i];
					intptr_t q1 = lay.q[g1];
					intptr_t base1 = lay.offset[g1] + j1 * q1;

					for (intptr_t t = 0; t < q1; t++)
						rhs[base1 + t] += w[i] * lay.Z(g1, i, t) * r[i];

					// Within-group Z'WZ
					for (intptr_t t1 = 0; t1 < q1; t1++)
					{
						double wz1 = w[i] * lay.Z(g1, i, t1);
						for (intptr_t t2 = t1; t2 < q1; t2++)
						{
							double val = wz1 * lay.Z(g1, i, t2);
							H(base1 + t1, base1 + t2) += val;
							if (t1 != t2) H(base1 + t2, base1 + t1) += val;
						}
					}

					// Cross-group Z'WZ
					for (intptr_t g2 = g1 + 1; g2 < G; g2++)
					{
						intptr_t j2 = (*lay.group_indices[g2])[i];
						intptr_t q2 = lay.q[g2];
						intptr_t base2 = lay.offset[g2] + j2 * q2;

						for (intptr_t t1 = 0; t1 < q1; t1++)
						{
							double wz1 = w[i] * lay.Z(g1, i, t1);
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

			// Solve
			u_new = Eigen::LDLT<Eigen::MatrixXd>(H).solve(rhs);
		}

		double max_change = (u_new - res.u).cwiseAbs().maxCoeff();
		res.u = u_new;
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

	double prior_nll = 0;
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
			prior_nll += quad / 2.0;
		}
		prior_nll += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI) + 0.5 * log_det_Dg[g]);
	}

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

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_Huu
	                  - 0.5 * J * std::log(2.0 * M_PI);
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
		// empirically: compute the gradient at the final theta; if small,
		// declare convergence.
		threw = true;
	}

	if (threw)
	{
		// Evaluate gradient at the final theta to determine whether the
		// exception reflected real failure or merely a near-optimum
		// line-search issue.  This uses the same FD scheme as the optimizer
		// would have used internally.
		Eigen::VectorXd grad(theta.size());
		fx = wrapper(theta, grad);
		double gnorm = grad.norm();
		// Use a generous tolerance (1000× grad_tol) because the exception
		// path is often triggered by FD noise, not by genuine non-convergence.
		res.converged = (gnorm < 1000.0 * grad_tol);
		if (niter < 1) niter = std::max(1, wrapper.call_count() / 3);
	}

	// Final progress callback (100% bar — wrapper already reported per call).
	if (progress) progress(max_iter, max_iter);

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
// search keeps steps in-range.  The threshold of 3 keeps Newton for
// trivial problems (random-intercept-only models with one or two
// grouping factors, where dim ≤ 3 and no ridge is possible) and
// switches to L-BFGS as soon as random slopes or multiple grouping
// factors introduce the geometry that causes Newton trouble.
template<typename Objective>
static NewtonResult robust_optimize(const Objective &obj,
                                     Eigen::VectorXd theta,
                                     int max_iter = 200,
                                     double grad_tol = 1e-8,
                                     FittingCallback progress = nullptr,
                                     double h_scale_hint = 0)
{
	constexpr intptr_t LBFGS_THRESHOLD = 3;
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
	// Skip if d > 6 to avoid exponential blowup (64 corners).
	if (d <= 6)
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


// Evaluate a Gaussian LMM at a single grid point θ:
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
	const Eigen::VectorXd *off_ptr = nullptr)
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
		                   beta_init, Eigen::VectorXd(), priors, coef_names, off_ptr);
	}
	else if (fam.name == "beta")
	{
		double phi_beta = std::exp(theta[n_chol]);
		auto fam_beta = Family::beta(phi_beta);
		res = solve_pirls(D_inv, log_det_Dg, fam_beta, Xm, ym, lay, n, p,
		                   beta_init, Eigen::VectorXd(), priors, coef_names, off_ptr);
	}
	else if (fam.name == "student")
	{
		double sigma_t = std::exp(theta[n_chol]);
		double nu_t = std::clamp(std::exp(theta[n_chol + 1]), 2.0, 200.0);
		auto fam_t = Family::student(sigma_t, nu_t);
		res = solve_pirls(D_inv, log_det_Dg, fam_t, Xm, ym, lay, n, p,
		                   beta_init, Eigen::VectorXd(), priors, coef_names, off_ptr);
	}
	else
	{
		// Binomial, Poisson: no extra dispersion parameters.
		res = solve_pirls(D_inv, log_det_Dg, fam, Xm, ym, lay, n, p,
		                   beta_init, Eigen::VectorXd(), priors, coef_names, off_ptr);
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

	if (fam_gp.custom_weights)
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
//   3. Computes μ_i^(s) = linkinv(x_i' β^(s) + zu_i)  [zu_i from BLUPs at mode]
//   4. Evaluates pointwise log-likelihood using dispersion params from θ^(s)
//   5. Calls compute_waic_from_loglik to populate model.waic/p_waic/lppd/se_waic
//
// Parameters:
//   results, w: the per-grid-point β̂/Σ and normalised weights
//   theta_star, T, z_points: for reconstructing θ_k = θ* + T z_k
//   n_chol: number of Cholesky parameters (needed to find dispersion in θ)
//   linkinv_scalar: scalar inverse link function (identity for Gaussian, exp for log, etc.)
//   disp_from_theta: extracts dispersion params from a θ vector (family-dependent)
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
	const Eigen::VectorXd *off_ptr = nullptr)
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

	// ── 2. Precompute RE offset from BLUPs at mode θ* ───────────────

	std::vector<double> zu = compute_re_offset(model, n);

	// ── 3. Precompute Cholesky factors of Σ_k for correlated draws ──
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

	for (int s = 0; s < WAIC_S; s++)
	{
		// Sample grid point k with probability w_k.
		double u = unif(rng);
		intptr_t k = (intptr_t)(std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin());
		k = std::min(k, n_grid - 1);

		// Draw β^(s) ~ N(β̂_k, Σ_k)
		Eigen::VectorXd z(p);
		for (intptr_t j = 0; j < p; j++)
			z[j] = std_normal(rng);

		Eigen::VectorXd beta_s = results[k].beta;
		if (chol_ok[k])
			beta_s += chol_vcov[k].matrixL() * z;

		// Dispersion parameters from θ_k.
		Eigen::VectorXd theta_k = theta_star + T * z_points[k];
		double disp[2] = {0.0, 0.0};
		disp_from_theta(theta_k, disp);

		// Compute pointwise log-likelihoods.
		for (intptr_t i = 0; i < n; i++)
		{
			// η_i = x_i' β^(s) + zu_i + offset_i
			double eta_i = zu[i];
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

	// ── 4b. Discard invalid grid points ──────────────────────────
	sanitise_grid_points(results, log_posterior, n_grid, p);

	// ── 5. Integration weights ───────────────────────────────────
	//
	// For deterministic CCD integration, weights are proportional to the
	// unnormalized posterior density at each grid point (not the p/q
	// importance ratio, which gives equal weights for a Gaussian posterior
	// and produces over-dispersed mixtures).
	//
	// w_k ∝ p̃(θ_k | y) = exp(-neg_log_posterior_k)
	//
	// Log-sum-exp trick for numerical stability.

	std::vector<double> log_w(n_grid);
	double max_log_w = -1e300;
	for (intptr_t k = 0; k < n_grid; k++)
	{
		log_w[k] = log_posterior[k];  // = -neg_log_posterior_k
		max_log_w = std::max(max_log_w, log_w[k]);
	}

	double sum_w = 0;
	std::vector<double> w(n_grid);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		w[k] = std::exp(log_w[k] - max_log_w);
		sum_w += w[k];
	}
	for (intptr_t k = 0; k < n_grid; k++) {
		w[k] /= sum_w;
	}

	// ── 5b. SLA-corrected conditional means ─────────────────────
	//
	// For Gaussian LMMs, ℓ'''(η) = 0 for all observations, so d₃ = 0
	// and sla_beta[k] == results[k].beta identically.  We include the
	// same structure as the PIRLS path for code consistency.

	std::vector<Eigen::VectorXd> sla_beta(n_grid);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		sla_beta[k] = results[k].beta;
		if (results[k].d3.size() == p)
		{
			for (intptr_t j = 0; j < p; j++)
			{
				double s2 = results[k].vcov_beta(j, j);
				sla_beta[k][j] += 0.5 * results[k].d3[j] * s2 * s2;
			}
		}
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
	                   off_ptr);
}
// Populates the Model's posterior fields with mixture-based estimates.
//
// Outer θ layout: (chol_1, ..., chol_G, [log disp...])
// where n_disp = 0 (binomial/Poisson), 1 (NB/beta), or 2 (Student t).
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
	const Eigen::VectorXd *off_ptr = nullptr)
{
	intptr_t d = theta_star.size();  // n_chol + n_disp

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
		                                    priors, coef_names, off_ptr);
		log_posterior[k] = -results[k].neg_log_posterior;
	}

	// ── 4b. Discard invalid grid points ──────────────────────────
	sanitise_grid_points(results, log_posterior, n_grid, p);

	// ── 5. Integration weights ───────────────────────────────────
	// w_k ∝ exp(-neg_log_posterior_k)
	std::vector<double> log_w(n_grid);
	double max_log_w = -1e300;
	for (intptr_t k = 0; k < n_grid; k++)
	{
		log_w[k] = log_posterior[k];
		max_log_w = std::max(max_log_w, log_w[k]);
	}

	double sum_w = 0;
	std::vector<double> w(n_grid);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		w[k] = std::exp(log_w[k] - max_log_w);
		sum_w += w[k];
	}
	for (intptr_t k = 0; k < n_grid; k++) {
		w[k] /= sum_w;
	}

	// ── 5b. SLA-corrected conditional means ─────────────────────
	//
	// Simplified Laplace correction (Rue, Martino & Chopin 2009 §3.2.2):
	// The Gaussian approximation π̃_G(β_j | θ_k) = N(μ_j, σ²_j) is corrected
	// by shifting the component mean to account for third-derivative skewness:
	//
	//   μ̃_j(θ_k) = μ_j(θ_k) + ½ d₃_j(θ_k) σ⁴_j(θ_k)
	//
	// For Gaussian families d₃ = 0, so no correction.  For non-Gaussian
	// families (binomial, Poisson, NB, beta, Student), this captures the
	// skewness of the log-full-conditional and matters for small-sample
	// GLMMs with skewed variance-component posteriors.
	//
	// The variance correction is O(n⁻²) and is not applied.

	std::vector<Eigen::VectorXd> sla_beta(n_grid);
	for (intptr_t k = 0; k < n_grid; k++)
	{
		sla_beta[k] = results[k].beta;
		if (results[k].d3.size() == p)
		{
			for (intptr_t j = 0; j < p; j++)
			{
				double s2 = results[k].vcov_beta(j, j);
				sla_beta[k][j] += 0.5 * results[k].d3[j] * s2 * s2;
			}
		}
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
	                   linkinv_fn, disp_fn, off_ptr);
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
                  const Array<double> &offset)
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

	if (is_gaussian)
	{
		intptr_t n_chol = total_chol_params(lay);
		intptr_t outer_dim_gauss = n_chol + 1; // Cholesky params + log σ

		GaussianCholObjective gauss_obj{Xm, ym, lay, n, p, n_chol, priors, coef_names, off_ptr};

		// ── Initialize Cholesky from ANOVA variance estimates ──
		Eigen::VectorXd theta(outer_dim_gauss);
		{
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				double s2_init = std::exp(2.0 * phi[p + g]);
				for (intptr_t r = 0; r < qg; r++)
				{
					for (intptr_t c = 0; c <= r; c++)
					{
						intptr_t idx = chol_pos + r * (r + 1) / 2 + c;
						if (r == c) {
							double var_init = (r == 0) ? s2_init : s2_init * 0.1;
							theta[idx] = 0.5 * std::log(std::max(var_init, 1e-4));
						} else {
							theta[idx] = 0.0;
						}
					}
				}
				chol_pos += np;
			}
		}
		theta[n_chol] = phi[p + G]; // log σ

		auto newton_res = robust_optimize(gauss_obj, theta, max_iter, 1e-8, progress);
		theta = newton_res.theta;
		niter = newton_res.niter;
		converged = newton_res.converged;
		optimizer_used = newton_res.optimizer;

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

			// Use Poisson Cholesky as starting outer theta
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				auto &re = pois_model.random_effects[g + 1]; // 1-based Array

				for (intptr_t r = 0; r < qg; r++)
				{
					for (intptr_t c = 0; c <= r; c++)
					{
						intptr_t pack_idx = r * (r + 1) / 2 + c;
						double val = re.cov_chol[pack_idx + 1]; // 1-based Array
						if (r == c) {
							theta[chol_pos + pack_idx] = std::log(std::max(val, 1e-6));
						} else {
							theta[chol_pos + pack_idx] = val;
						}
					}
				}
				chol_pos += np;
			}
		}
		else
		{
			// Standard initialization: ANOVA variance decomposition.
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				double s2_init = std::exp(2.0 * phi[p + g]);
				for (intptr_t r = 0; r < qg; r++)
				{
					for (intptr_t c = 0; c <= r; c++)
					{
						intptr_t idx = chol_pos + r * (r + 1) / 2 + c;
						if (r == c) {
							double var_init = (r == 0) ? s2_init : s2_init * 0.1;
							theta[idx] = 0.5 * std::log(std::max(var_init, 1e-4));
						} else {
							theta[idx] = 0.0;
						}
					}
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
			constexpr double nu_init = 5.0;
			double sigma2_init = var_resid * (nu_init - 2.0) / nu_init;
			double sigma_init = std::sqrt(std::max(sigma2_init, 1e-4));

			theta[n_chol] = std::log(sigma_init);
			theta[n_chol + 1] = std::log(nu_init);
		}

		// Create PirlsObjective after beta_init is finalized

		PirlsObjective pirls_obj{fam, Xm, ym, lay, n, p, beta_init, n_chol, priors, coef_names, off_ptr};

		auto newton_res = robust_optimize(pirls_obj, theta, max_iter, 1e-8, progress, 1e-2);
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

			if (is_student)
			{
				// Student t: use Phase 1 β̂ directly (no Phase 2).
				beta_hat = p1_pirls.beta;
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

				// Phase 2 outer dimension p + n_chol + n_disp is effectively
				// always > 3, so robust_optimize routes this to L-BFGS.
				auto res2 = robust_optimize(joint_obj, phi2, max_iter, 1e-8, progress, 1e-2);

				// Update β and θ from Phase 2
				beta_hat = res2.theta.head(p);
				theta = Eigen::VectorXd(res2.theta.tail(theta.size()));
				niter += res2.niter;
				converged = res2.converged;
				optimizer_used = res2.optimizer;  // Phase 2 produces the final estimates;
				                                   // its optimizer is the one to report.
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

		// Assemble full φ = (β̂, θ̂) for SE computation
		phi.resize(outer_dim);
		phi.head(p) = beta_hat;
		for (intptr_t g = 0; g < G; g++) {
			phi[p + g] = theta[g];
		}

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
		// Use solve_u_given_beta to ensure β̂ from Phase 2 is not modified
		auto pirls_final = solve_u_given_beta(D_inv_final, log_det_Dg_final, fam_used,
		                                       Xm, ym, lay, n, p, beta_hat,
		                                       Eigen::VectorXd(), off_ptr);
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

		// Term names from the GroupingInfo (e.g. "(Intercept)", "vowel[i]", "vowel[u]")
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
	model.compute_information_criteria();

	model.niter = niter;
	model.converged = converged;
	model.optimizer = optimizer_used;

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
			inla_grid_integrate_pirls(model, pirls_obj, saved_theta,
			                           fam, Xm, ym, lay, n, p, saved_n_chol,
			                           saved_beta_init, priors, coef_names, off_ptr);
		}
	}

	if (!offset.empty()) model.offset = offset;

	return model;
}

} // namespace phonometrica::stats
