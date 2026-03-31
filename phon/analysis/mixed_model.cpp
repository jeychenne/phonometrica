/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 * The outer gradient of the Laplace-approximated marginal log-likelihood is computed exactly                          *
 * by algorithmic differentiation (CppAD). The inner Newton solve (which finds the random-effects                      *
 * mode u_hat for given outer parameters) runs with plain doubles. At the converged u_hat, the                         *
 * stationarity condition df/du = 0 means that du_hat/dphi does not contribute to the gradient                         *
 * (implicit function theorem), so we can treat u_hat as a constant when taping the nll w.r.t.                         *
 * the outer parameters phi = (beta, log_sigma_u_1, ..., log_sigma_u_G, [log_sigma]).                                *
 *                                                                                                                     *
 * Mathematical references:                                                                                            *
 *   Breslow & Clayton (1993). JASA 88(421), 9–25.                                                                    *
 *   Kristensen et al. (2016). JSS 70(5), 1–21.                                                                       *
 *   Skaug & Fournier (2006). Automatic approximation of the marginal likelihood in non-Gaussian                      *
 *       hierarchical models. Computational Statistics & Data Analysis, 51, 699–709.                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <cppad/cppad.hpp>
#include <boost/math/distributions/normal.hpp>
#include <phon/analysis/mixed_model.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/utils/matrix.hpp>
#include <phon/third_party/LBFGSpp/LBFGS.h>

namespace phonometrica::stats {

namespace {

using ADdouble = CppAD::AD<double>;

// =====================================================================
// Multi-group random effects layout (unchanged)
// =====================================================================

struct GroupLayout
{
	intptr_t G;
	intptr_t J_total;
	std::vector<intptr_t> J;
	std::vector<intptr_t> offset;
	std::vector<const std::vector<intptr_t> *> group_indices;

	static GroupLayout build(const std::vector<GroupingInfo> &groups)
	{
		GroupLayout lay;
		lay.G = (intptr_t)groups.size();
		lay.J_total = 0;
		lay.J.resize(lay.G);
		lay.offset.resize(lay.G);
		lay.group_indices.resize(lay.G);

		for (intptr_t g = 0; g < lay.G; g++)
		{
			lay.offset[g] = lay.J_total;
			lay.J[g] = groups[g].nlevels;
			lay.J_total += lay.J[g];
			lay.group_indices[g] = &groups[g].indices;
		}
		return lay;
	}
};


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
// Full log-determinant of the random-effects Hessian
// =====================================================================
//
// For crossed random intercepts the Hessian H_uu of the penalized
// log-likelihood w.r.t. u is NOT diagonal: observation i couples
// u_{g1,k1} and u_{g2,k2} with entry w_i whenever i belongs to
// both group k1 of factor g1 and group k2 of factor g2.
//
// The diagonal approximation log det(diag(H)) ≥ log det(H) by
// Hadamard's inequality, which biases the Laplace nll upward.
// For a single grouping factor (G=1) H is truly diagonal, so
// both give the same answer.

static double full_log_det_H(const Eigen::VectorXd &w,
                               const Eigen::VectorXd &inv_sigma2_u,
                               const GroupLayout &lay,
                               intptr_t n)
{
	intptr_t J = lay.J_total;

	// For a single grouping factor the Hessian is diagonal — fast path.
	if (lay.G == 1)
	{
		Eigen::VectorXd h(J);
		intptr_t off = lay.offset[0];
		for (intptr_t j = 0; j < J; j++) {
			h[j] = inv_sigma2_u[0];
		}
		auto &idx = *lay.group_indices[0];
		for (intptr_t i = 0; i < n; i++) {
			h[idx[i]] += w[i];
		}
		double ld = 0;
		for (intptr_t j = 0; j < J; j++) {
			ld += std::log(h[j]);
		}
		return ld;
	}

	// General case: build the full J × J matrix and factorize.
	Eigen::MatrixXd H = Eigen::MatrixXd::Zero(J, J);

	// Prior precision on the diagonal
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			H(off + j, off + j) = inv_sigma2_u[g];
		}
	}

	// Data contributions: observation i adds w[i] to every (k1,k2) pair
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < lay.G; g1++)
		{
			intptr_t k1 = lay.offset[g1] + (*lay.group_indices[g1])[i];
			H(k1, k1) += w[i];

			for (intptr_t g2 = g1 + 1; g2 < lay.G; g2++)
			{
				intptr_t k2 = lay.offset[g2] + (*lay.group_indices[g2])[i];
				H(k1, k2) += w[i];
				H(k2, k1) += w[i];
			}
		}
	}

	Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
	return ldlt.vectorD().array().log().sum();
}


// AD version: manual LDLT for CppAD types.
// Only called when G > 1 (crossed effects); for G = 1 the diagonal
// formula is used directly in laplace_nll_ad.

static ADdouble full_log_det_H_ad(const std::vector<ADdouble> &w,
                                    const std::vector<ADdouble> &inv_sigma2_u,
                                    const GroupLayout &lay,
                                    intptr_t n)
{
	intptr_t J = lay.J_total;

	// Build H with AD entries
	std::vector<std::vector<ADdouble>> H(J, std::vector<ADdouble>(J, ADdouble(0)));

	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			H[off + j][off + j] = inv_sigma2_u[g];
		}
	}
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < lay.G; g1++)
		{
			intptr_t k1 = lay.offset[g1] + (*lay.group_indices[g1])[i];
			H[k1][k1] += w[i];

			for (intptr_t g2 = g1 + 1; g2 < lay.G; g2++)
			{
				intptr_t k2 = lay.offset[g2] + (*lay.group_indices[g2])[i];
				H[k1][k2] += w[i];
				H[k2][k1] += w[i];
			}
		}
	}

	// LDLT decomposition: H = L D L' (no sqrt needed)
	std::vector<ADdouble> D(J);
	// L stored in-place in the lower triangle of H
	for (intptr_t j = 0; j < J; j++)
	{
		ADdouble s = H[j][j];
		for (intptr_t k = 0; k < j; k++) {
			s -= H[j][k] * H[j][k] * D[k];
		}
		D[j] = s;

		for (intptr_t i = j + 1; i < J; i++)
		{
			ADdouble s2 = H[i][j];
			for (intptr_t k = 0; k < j; k++) {
				s2 -= H[i][k] * D[k] * H[j][k];
			}
			H[i][j] = s2 / D[j];   // store L[i][j] in H[i][j]
		}
	}

	ADdouble log_det = ADdouble(0);
	for (intptr_t j = 0; j < J; j++) {
		log_det += CppAD::log(D[j]);
	}
	return log_det;
}


// =====================================================================
// Plain-double helpers for the inner Newton
// =====================================================================

static Eigen::VectorXd working_weights(const Eigen::VectorXd &mu, const Family &fam,
                                        double inv_sigma2)
{
	Eigen::VectorXd V = fam.variance(mu);
	if (fam.name == "gaussian") {
		V *= inv_sigma2;
	}
	return V;
}

static Eigen::VectorXd nll_gradient_eta(const Eigen::VectorXd &y, const Eigen::VectorXd &mu,
                                          const Family &fam, double inv_sigma2)
{
	Eigen::VectorXd g = -(y - mu);
	if (fam.name == "gaussian") {
		g *= inv_sigma2;
	}
	return g;
}


// =====================================================================
// Inner Newton solve (plain double — unchanged)
// =====================================================================

struct InnerResult
{
	Eigen::VectorXd u;
	Eigen::VectorXd mu;
	double laplace_nll;
};

static InnerResult solve_inner(const Eigen::VectorXd &beta,
                                const Eigen::VectorXd &sigma2_u,
                                double sigma2,
                                const Family &fam,
                                const Eigen::Map<Matrix<double>> &Xm,
                                const Eigen::Map<Vector<double>> &ym,
                                const GroupLayout &lay,
                                intptr_t n, intptr_t p)
{
	InnerResult res;
	res.u = Eigen::VectorXd::Zero(lay.J_total);

	bool is_gaussian = (fam.name == "gaussian");
	double inv_sigma2 = is_gaussian ? (1.0 / sigma2) : 0.0;

	Eigen::VectorXd inv_sigma2_u(lay.G);
	for (intptr_t g = 0; g < lay.G; g++) {
		inv_sigma2_u[g] = 1.0 / sigma2_u[g];
	}

	int max_inner = 25;   // converges in 1 step for single-factor Gaussian;
	                      // needs multiple steps for crossed random effects
	double inner_tol = 1e-8;

	Eigen::VectorXd Xbeta = Xm * beta;
	Eigen::VectorXd eta(n), mu(n);

	for (int iter = 0; iter < max_inner; iter++)
	{
		eta = Xbeta;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++) {
				eta[i] += res.u[off + idx[i]];
			}
		}
		mu = fam.linkinv(eta);

		Eigen::VectorXd g_eta = nll_gradient_eta(ym, mu, fam, inv_sigma2);
		Eigen::VectorXd w = working_weights(mu, fam, inv_sigma2);

		Eigen::VectorXd g_u = Eigen::VectorXd::Zero(lay.J_total);
		Eigen::VectorXd h_u = Eigen::VectorXd::Zero(lay.J_total);

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				h_u[off + j] = inv_sigma2_u[g];
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t k = off + idx[i];
				g_u[k] += g_eta[i];
				h_u[k] += w[i];
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				g_u[off + j] += res.u[off + j] * inv_sigma2_u[g];
			}
		}

		double max_change = 0;
		for (intptr_t k = 0; k < lay.J_total; k++)
		{
			double step = g_u[k] / h_u[k];
			res.u[k] -= step;
			max_change = std::max(max_change, std::abs(step));
		}

		if (max_change < inner_tol) break;
	}

	// Recompute mu at converged u
	eta = Xbeta;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t off = lay.offset[g];
		for (intptr_t i = 0; i < n; i++) {
			eta[i] += res.u[off + idx[i]];
		}
	}
	res.mu = fam.linkinv(eta);

	// Also compute the plain-double nll (used as the return value for L-BFGS)
	double cond_nll;
	if (is_gaussian)
	{
		double rss = (ym - res.mu).squaredNorm();
		cond_nll = rss / (2.0 * sigma2) + 0.5 * n * std::log(2.0 * M_PI * sigma2);
	}
	else
	{
		cond_nll = -fam.loglik(ym, res.mu);
	}

	double prior_nll = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		double sq = 0;
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			sq += res.u[off + j] * res.u[off + j];
		}
		prior_nll += sq / (2.0 * sigma2_u[g])
		             + 0.5 * lay.J[g] * std::log(2.0 * M_PI * sigma2_u[g]);
	}

	Eigen::VectorXd w_final = working_weights(res.mu, fam, inv_sigma2);
	double log_det_H = full_log_det_H(w_final, inv_sigma2_u, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_H
	                  - 0.5 * lay.J_total * std::log(2.0 * M_PI);

	return res;
}


// =====================================================================
// AD evaluation of Laplace nll (for exact outer gradients)
// =====================================================================
//
// u_hat is treated as a constant (plain double). Only the outer
// parameters phi are AD-active. This is valid because at the inner
// optimum, df/du = 0, so the implicit-function-theorem correction
// du_hat/dphi vanishes from the total derivative.

static ADdouble laplace_nll_ad(const std::vector<ADdouble> &a_phi,
                                const Eigen::VectorXd &u_hat,
                                const std::string &family_name,
                                const Eigen::Map<Matrix<double>> &Xm,
                                const Eigen::Map<Vector<double>> &ym,
                                const GroupLayout &lay,
                                intptr_t n, intptr_t p,
                                bool is_gaussian)
{
	intptr_t G = lay.G;

	// ── Extract variance parameters ─────────────────────────────────

	std::vector<ADdouble> sigma2_u(G), inv_sigma2_u(G);
	for (intptr_t g = 0; g < G; g++)
	{
		sigma2_u[g] = CppAD::exp(2.0 * a_phi[p + g]);
		inv_sigma2_u[g] = 1.0 / sigma2_u[g];
	}

	ADdouble sigma2(0), inv_sigma2(0);
	if (is_gaussian)
	{
		sigma2 = CppAD::exp(2.0 * a_phi[p + G]);
		inv_sigma2 = 1.0 / sigma2;
	}

	// ── Linear predictor: eta = X*beta + Z*u_hat ─────────────────────

	std::vector<ADdouble> eta(n);
	for (intptr_t i = 0; i < n; i++)
	{
		ADdouble sum = 0;
		for (intptr_t j = 0; j < p; j++) {
			sum += Xm(i, j) * a_phi[j];   // double * AD = AD
		}
		for (intptr_t g = 0; g < G; g++) {
			sum += u_hat[lay.offset[g] + (*lay.group_indices[g])[i]];  // double + AD = AD
		}
		eta[i] = sum;
	}

	// ── mu = linkinv(eta) ────────────────────────────────────────────

	std::vector<ADdouble> mu(n);
	if (family_name == "gaussian")
	{
		for (intptr_t i = 0; i < n; i++) {
			mu[i] = eta[i];
		}
	}
	else if (family_name == "binomial")
	{
		for (intptr_t i = 0; i < n; i++) {
			mu[i] = 1.0 / (1.0 + CppAD::exp(-eta[i]));
		}
	}
	else // poisson
	{
		for (intptr_t i = 0; i < n; i++) {
			mu[i] = CppAD::exp(eta[i]);
		}
	}

	// ── Conditional nll: -log p(y | eta) ─────────────────────────────

	ADdouble cond_nll = 0;

	if (family_name == "gaussian")
	{
		ADdouble rss = 0;
		for (intptr_t i = 0; i < n; i++)
		{
			ADdouble d = ym[i] - mu[i];
			rss += d * d;
		}
		cond_nll = rss / (2.0 * sigma2) + 0.5 * n * CppAD::log(2.0 * M_PI * sigma2);
	}
	else if (family_name == "binomial")
	{
		// Numerically stable: use eta directly
		// -loglik = sum -[ y*log(mu) + (1-y)*log(1-mu) ]
		//         = sum -[ y*eta - log(1+exp(eta)) ]
		for (intptr_t i = 0; i < n; i++)
		{
			// log(1 + exp(eta)): use log1p(exp(eta)) for stability
			// For large eta, log(1+exp(eta)) ≈ eta
			// CppAD handles exp and log; we rely on the AD library for correctness
			ADdouble log1pexp = CppAD::log(1.0 + CppAD::exp(eta[i]));
			cond_nll += -ym[i] * eta[i] + log1pexp;
		}
	}
	else // poisson
	{
		for (intptr_t i = 0; i < n; i++)
		{
			// -loglik = sum [ mu - y*log(mu) + lgamma(y+1) ]
			// mu = exp(eta), so log(mu) = eta
			cond_nll += mu[i] - ym[i] * eta[i] + std::lgamma(ym[i] + 1.0);
		}
	}

	// ── Prior nll: sum_g [ ||u_g||^2 / (2 sigma2_u_g) + J_g/2 log(2pi sigma2_u_g) ]

	ADdouble prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t off = lay.offset[g];
		double sq = 0;
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			sq += u_hat[off + j] * u_hat[off + j];
		}
		// sq is plain double, sigma2_u[g] is AD
		prior_nll += sq / (2.0 * sigma2_u[g])
		             + 0.5 * lay.J[g] * CppAD::log(2.0 * M_PI * sigma2_u[g]);
	}

	// ── Working weights and log det H ────────────────────────────────

	std::vector<ADdouble> w(n);
	if (family_name == "gaussian")
	{
		for (intptr_t i = 0; i < n; i++) {
			w[i] = inv_sigma2;  // V(mu) = 1 for Gaussian, scaled by 1/sigma2
		}
	}
	else if (family_name == "binomial")
	{
		for (intptr_t i = 0; i < n; i++) {
			w[i] = mu[i] * (1.0 - mu[i]);
		}
	}
	else // poisson
	{
		for (intptr_t i = 0; i < n; i++) {
			w[i] = mu[i];
		}
	}

	// h_k = inv_sigma2_u[g] + sum_{i in group} w[i]
	// For G > 1 (crossed effects) the full Hessian has off-diagonal blocks;
	// the diagonal formula underestimates log det(H).
	ADdouble log_det_H;
	if (G > 1)
	{
		log_det_H = full_log_det_H_ad(w, inv_sigma2_u, lay, n);
	}
	else
	{
		// Single grouping factor: H is truly diagonal — fast path.
		std::vector<ADdouble> h_u(lay.J_total);
		intptr_t off = lay.offset[0];
		for (intptr_t j = 0; j < lay.J[0]; j++) {
			h_u[j] = inv_sigma2_u[0];
		}
		auto &idx = *lay.group_indices[0];
		for (intptr_t i = 0; i < n; i++) {
			h_u[idx[i]] += w[i];
		}
		log_det_H = ADdouble(0);
		for (intptr_t k = 0; k < lay.J_total; k++) {
			log_det_H += CppAD::log(h_u[k]);
		}
	}

	// ── Laplace nll ──────────────────────────────────────────────────

	return cond_nll + prior_nll + 0.5 * log_det_H
	       - 0.5 * lay.J_total * std::log(2.0 * M_PI);
}


// =====================================================================
// Outer objective with CppAD gradients
// =====================================================================
//
// Outer parameter layout:
//   Non-Gaussian: phi = (beta_1..p, log_sigma_u_1..G)            dim = p + G
//   Gaussian:     phi = (beta_1..p, log_sigma_u_1..G, log_sigma)  dim = p + G + 1

struct OuterObjective
{
	const Family &fam;
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	bool is_gaussian;
	std::string family_name;  // cached for AD (avoids String comparison in tape)

	// Plain-double evaluation (for starting value, final nll, etc.)
	double eval(const Eigen::VectorXd &phi) const
	{
		Eigen::VectorXd beta = phi.head(p);
		Eigen::VectorXd sigma2_u(lay.G);
		for (intptr_t g = 0; g < lay.G; g++) {
			sigma2_u[g] = std::exp(2.0 * phi[p + g]);
		}
		double sigma2 = is_gaussian ? std::exp(2.0 * phi[p + lay.G]) : 0.0;

		auto res = solve_inner(beta, sigma2_u, sigma2, fam, Xm, ym, lay, n, p);
		return res.laplace_nll;
	}

	// L-BFGS callback: value and gradient via finite differences.
	//
	// Finite differences are used for the outer optimization because the
	// implicit re-solve of the inner problem at each perturbed point captures
	// curvature information that improves the L-BFGS search direction, compared
	// to the frozen-u_hat AD gradient. CppAD exact gradients are used for the
	// SE computation (see exact_gradient below), where accuracy matters more
	// than search-direction quality.
	double operator()(const Eigen::VectorXd &phi, Eigen::VectorXd &grad) const
	{
		double f0 = eval(phi);

		intptr_t dim = phi.size();
		for (intptr_t k = 0; k < dim; k++)
		{
			double hk = 1e-5 * std::max(std::abs(phi[k]), 1.0);

			Eigen::VectorXd phi_plus = phi, phi_minus = phi;
			phi_plus[k] += hk;
			phi_minus[k] -= hk;

			grad[k] = (eval(phi_plus) - eval(phi_minus)) / (2.0 * hk);
		}

		return f0;
	}
};


// =====================================================================
// Exact gradient for Hessian estimation (SE computation)
// =====================================================================
//
// Given phi, compute the exact gradient of the Laplace nll.
// This is used to build the numerical Hessian: H_{jk} ≈ (grad_j(phi+e_k) - grad_j(phi-e_k)) / (2h).
// Since the gradient itself is exact (not a finite difference), this gives a first-order-accurate
// Hessian rather than the second-order-noisy Hessian we had before.

static Eigen::VectorXd exact_gradient(const Eigen::VectorXd &phi,
                                       const OuterObjective &obj)
{
	intptr_t dim = phi.size();
	Eigen::VectorXd grad(dim);

	Eigen::VectorXd beta = phi.head(obj.p);
	Eigen::VectorXd sigma2_u(obj.lay.G);
	for (intptr_t g = 0; g < obj.lay.G; g++) {
		sigma2_u[g] = std::exp(2.0 * phi[obj.p + g]);
	}
	double sigma2 = obj.is_gaussian ? std::exp(2.0 * phi[obj.p + obj.lay.G]) : 0.0;

	auto inner = solve_inner(beta, sigma2_u, sigma2, obj.fam, obj.Xm, obj.ym,
	                          obj.lay, obj.n, obj.p);

	std::vector<ADdouble> a_phi(dim);
	for (intptr_t i = 0; i < dim; i++) {
		a_phi[i] = phi[i];
	}
	CppAD::Independent(a_phi);

	std::vector<ADdouble> a_nll(1);
	a_nll[0] = laplace_nll_ad(a_phi, inner.u, obj.family_name,
	                           obj.Xm, obj.ym, obj.lay, obj.n, obj.p, obj.is_gaussian);

	CppAD::ADFun<double> tape;
	tape.Dependent(a_phi, a_nll);

	std::vector<double> phi_vec(phi.data(), phi.data() + dim);
	tape.Forward(0, phi_vec);

	std::vector<double> w(1, 1.0);
	std::vector<double> dw = tape.Reverse(1, w);

	for (intptr_t i = 0; i < dim; i++) {
		grad[i] = dw[i];
	}
	return grad;
}


// =====================================================================
// Profiled inner solve for Gaussian (β concentrated out)
// =====================================================================
//
// For Gaussian LMMs, given variance parameters θ = (log σ_u_1..G, log σ),
// the optimal β is the GLS estimator β̂(θ) = (X'X)⁻¹ X'(y − Zû).
// This function alternates between updating u (diagonal Newton) and
// updating β (OLS on adjusted response) until both converge.
// The result is the joint mode (β̂, û) plus the Laplace nll.

struct ProfiledResult
{
	Eigen::VectorXd beta;
	Eigen::VectorXd u;
	Eigen::VectorXd mu;
	double laplace_nll;
};

static ProfiledResult solve_profiled_gaussian(
	const Eigen::VectorXd &sigma2_u, double sigma2,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p,
	const Eigen::LDLT<Eigen::MatrixXd> &XtX_ldlt,
	const Eigen::MatrixXd &Xt)          // precomputed X'
{
	ProfiledResult res;
	res.u = Eigen::VectorXd::Zero(lay.J_total);
	res.beta = XtX_ldlt.solve(Xt * ym);   // OLS starting β

	double inv_sigma2 = 1.0 / sigma2;
	Eigen::VectorXd inv_sigma2_u(lay.G);
	for (intptr_t g = 0; g < lay.G; g++) {
		inv_sigma2_u[g] = 1.0 / sigma2_u[g];
	}

	// Alternating (u | β) and (β | u) updates, both linear for Gaussian.
	// Typically converges in 2–4 passes for crossed designs.
	for (int iter = 0; iter < 50; iter++)
	{
		// ── Update u given β ────────────────────────────────────────
		Eigen::VectorXd eta = Xm * res.beta;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++) {
				eta[i] += res.u[off + idx[i]];
			}
		}

		Eigen::VectorXd g_u = Eigen::VectorXd::Zero(lay.J_total);
		Eigen::VectorXd h_u = Eigen::VectorXd::Zero(lay.J_total);

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				h_u[off + j] = inv_sigma2_u[g];
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t k = off + idx[i];
				g_u[k] += -(ym[i] - eta[i]) * inv_sigma2;
				h_u[k] += inv_sigma2;
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				g_u[off + j] += res.u[off + j] * inv_sigma2_u[g];
			}
		}

		double max_u_change = 0;
		for (intptr_t k = 0; k < lay.J_total; k++)
		{
			double step = g_u[k] / h_u[k];
			res.u[k] -= step;
			max_u_change = std::max(max_u_change, std::abs(step));
		}

		// ── Update β given u:  β = (X'X)⁻¹ X'(y − Zu) ─────────────
		Eigen::VectorXd y_adj = ym;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++) {
				y_adj[i] -= res.u[off + idx[i]];
			}
		}
		Eigen::VectorXd beta_new = XtX_ldlt.solve(Xt * y_adj);
		double max_beta_change = (beta_new - res.beta).cwiseAbs().maxCoeff();
		res.beta = beta_new;

		if (std::max(max_u_change, max_beta_change) < 1e-10) break;
	}

	// ── Final eta / mu ──────────────────────────────────────────────
	Eigen::VectorXd eta = Xm * res.beta;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t off = lay.offset[g];
		for (intptr_t i = 0; i < n; i++) {
			eta[i] += res.u[off + idx[i]];
		}
	}
	res.mu = eta;   // identity link

	// ── Laplace nll ─────────────────────────────────────────────────
	double rss = (ym - res.mu).squaredNorm();
	double cond_nll = rss / (2.0 * sigma2) + 0.5 * n * std::log(2.0 * M_PI * sigma2);

	double prior_nll = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		double sq = 0;
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			sq += res.u[off + j] * res.u[off + j];
		}
		prior_nll += sq / (2.0 * sigma2_u[g])
		             + 0.5 * lay.J[g] * std::log(2.0 * M_PI * sigma2_u[g]);
	}

	// Working weights for Gaussian: w_i = 1/σ² for all i
	Eigen::VectorXd w_prof = Eigen::VectorXd::Constant(n, inv_sigma2);
	double log_det_H = full_log_det_H(w_prof, inv_sigma2_u, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_H
	                  - 0.5 * lay.J_total * std::log(2.0 * M_PI);
	return res;
}


// =====================================================================
// Profiled outer objective (Gaussian only)
// =====================================================================
//
// θ = (log σ_u_1, ..., log σ_u_G, log σ)    dim = G + 1
//
// β is concentrated out: for each θ, the optimal (β̂, û) is computed
// by solve_profiled_gaussian.

struct ProfiledObjective
{
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	const Eigen::LDLT<Eigen::MatrixXd> &XtX_ldlt;
	const Eigen::MatrixXd &Xt;

	double eval(const Eigen::VectorXd &theta) const
	{
		Eigen::VectorXd sigma2_u(lay.G);
		for (intptr_t g = 0; g < lay.G; g++) {
			sigma2_u[g] = std::exp(2.0 * theta[g]);
		}
		double sigma2 = std::exp(2.0 * theta[lay.G]);

		auto res = solve_profiled_gaussian(sigma2_u, sigma2, Xm, ym, lay,
		                                    n, p, XtX_ldlt, Xt);
		return res.laplace_nll;
	}
};


// =====================================================================
// Newton optimizer for the profiled Gaussian case
// =====================================================================
//
// With β profiled out, the outer parameter θ has dimension G+1 (typically
// 2–4).  For such small problems full Newton (exact Hessian via finite
// differences of the objective, Armijo backtracking) converges in 5–10
// iterations to machine precision.  This is what nlminb (the default
// optimizer in glmmTMB/lme4) does — L-BFGS is designed for thousands
// of parameters and is overkill / unreliable for 3D surfaces.

struct NewtonResult
{
	Eigen::VectorXd theta;
	double fx;
	int niter;
	bool converged;
};

static NewtonResult newton_profiled(const ProfiledObjective &obj,
                                     Eigen::VectorXd theta,
                                     int max_iter = 100,
                                     double grad_tol = 1e-8)
{
	intptr_t dim = theta.size();
	NewtonResult res;
	res.converged = false;
	res.fx = obj.eval(theta);

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
			h[j] = 1e-4 * std::max(std::abs(theta[j]), 1.0);
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
		if (grad.norm() < grad_tol)
		{
			res.converged = true;
			res.theta = theta;
			res.niter = iter;
			return res;
		}

		// ── Newton direction: −H⁻¹g ─────────────────────────────
		Eigen::VectorXd step;
		{
			Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
			if (ldlt.info() == Eigen::Success && ldlt.isPositive()) {
				step = -ldlt.solve(grad);
			} else {
				// Hessian not positive-definite: steepest-descent fallback
				step = -grad * (1.0 / grad.norm());
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
	}

	res.theta = theta;
	res.niter = max_iter;
	return res;
}


} // anonymous namespace


// =====================================================================
// Public entry point
// =====================================================================

Model mixed_model(const Array<double> &y, const Array<double> &X,
                  const std::vector<GroupingInfo> &groups, const Family &fam)
{
	using namespace LBFGSpp;

	if (y.ndim() != 1) {
		throw error("y must be a one-dimensional array");
	}
	if (X.ndim() != 2) {
		throw error("X must be a two-dimensional array");
	}
	if (X.nrow() != y.size()) {
		throw error("Inconsistent number of observations in y and X");
	}
	if (groups.empty()) {
		throw error("At least one grouping factor is required");
	}

	intptr_t n = y.size();
	intptr_t p = X.ncol();
	bool is_gaussian = (fam.name == "gaussian");

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
		fe = lm(y, X);
	} else {
		fe = glm(y, X, fam);
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
			// Clamp to a reasonable range on the link scale: [0.01, 10].
			s2 = std::clamp(s2, 0.01, 10.0);
			phi[p + g] = 0.5 * std::log(s2);
		}
	}

	// ── Outer optimisation ──────────────────────────────────────────
	//
	// Gaussian:     β is profiled out — Newton's method searches only over
	//               θ = (log σ_u_1..G, log σ), dimension G+1.
	//               For each θ, the joint mode (β̂,û) is found by
	//               solve_profiled_gaussian.  This matches the profiling
	//               strategy used by lme4/glmmTMB internally.
	//
	// Non-Gaussian: L-BFGS searches over φ = (β, log σ_u_1..G),
	//               dimension p+G (original approach).

	Eigen::VectorXd beta_hat;
	Eigen::VectorXd sigma2_u(G);
	double sigma2 = 0;
	int niter = 0;
	bool converged = true;

	if (is_gaussian)
	{
		// Precompute X'X decomposition (reused on every profiled solve)
		Eigen::MatrixXd Xt = Xm.transpose();
		Eigen::MatrixXd XtX = Xt * Xm;
		Eigen::LDLT<Eigen::MatrixXd> XtX_ldlt(XtX);

		ProfiledObjective profiled{Xm, ym, lay, n, p, XtX_ldlt, Xt};

		// θ starting values from the ANOVA init
		Eigen::VectorXd theta(G + 1);
		for (intptr_t g = 0; g < G; g++) {
			theta[g] = phi[p + g];           // log σ_u_g
		}
		theta[G] = phi[p + G];               // log σ

		auto newton_res = newton_profiled(profiled, theta);
		theta = newton_res.theta;
		niter = newton_res.niter;
		converged = newton_res.converged;

		// Recover (β̂, û) at the converged θ
		for (intptr_t g = 0; g < G; g++) {
			sigma2_u[g] = std::exp(2.0 * theta[g]);
		}
		sigma2 = std::exp(2.0 * theta[G]);

		auto final_prof = solve_profiled_gaussian(sigma2_u, sigma2, Xm, ym, lay,
		                                           n, p, XtX_ldlt, Xt);
		beta_hat = final_prof.beta;

		// Assemble full φ = (β̂, θ̂) for Hessian / SE computation
		phi.resize(outer_dim);
		phi.head(p) = beta_hat;
		for (intptr_t g = 0; g < G; g++) {
			phi[p + g] = theta[g];
		}
		phi[p + G] = theta[G];
	}
	else
	{
		// Non-Gaussian: optimise over the full φ = (β, log σ_u_1..G)
		std::string family_name(fam.name.data(), fam.name.size());
		OuterObjective objective{fam, Xm, ym, lay, n, p, is_gaussian, family_name};

		LBFGSParam<double> param;
		param.epsilon = 1e-6;
		param.max_iterations = 300;
		param.max_linesearch = 40;
		LBFGSSolver<double> solver(param);

		double fx;
		try {
			niter = solver.minimize(objective, phi, fx);
		}
		catch (std::exception &) {
			converged = false;
		}

		beta_hat = phi.head(p);
		for (intptr_t g = 0; g < G; g++) {
			sigma2_u[g] = std::exp(2.0 * phi[p + g]);
		}
	}

	// ── Final inner solve at converged estimates ─────────────────────

	auto final_inner = solve_inner(beta_hat, sigma2_u, sigma2, fam, Xm, ym, lay, n, p);

	// ── Standard errors ─────────────────────────────────────────────
	//
	// Gaussian:     Conditional Var(β̂ | θ̂) = (X'V⁻¹X)⁻¹ where
	//               V = σ²I + ZDZ'. Using Woodbury on V⁻¹ and noting
	//               that the Laplace Hessian A = D⁻¹ + (1/σ²)Z'Z:
	//                  X'V⁻¹X = (1/σ²)(X'X − (1/σ²) B A⁻¹ B')
	//               with B = X'Z.  This is the standard conditional SE
	//               that lme4/glmmTMB report.
	//
	// Non-Gaussian: β block of the inverse of the full observed
	//               information matrix over all outer parameters.

	Eigen::MatrixXd vcov;

	if (is_gaussian)
	{
		double inv_s2 = 1.0 / sigma2;

		// Build A = D⁻¹ + (1/σ²)Z'Z  (same structure as the Laplace Hessian)
		intptr_t J = lay.J_total;
		Eigen::MatrixXd A = Eigen::MatrixXd::Zero(J, J);

		for (intptr_t g = 0; g < G; g++)
		{
			intptr_t off = lay.offset[g];
			double inv_s2u = 1.0 / sigma2_u[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				A(off + j, off + j) = inv_s2u;
			}
		}
		for (intptr_t i = 0; i < n; i++)
		{
			for (intptr_t g1 = 0; g1 < G; g1++)
			{
				intptr_t k1 = lay.offset[g1] + groups[g1].indices[i];
				A(k1, k1) += inv_s2;

				for (intptr_t g2 = g1 + 1; g2 < G; g2++)
				{
					intptr_t k2 = lay.offset[g2] + groups[g2].indices[i];
					A(k1, k2) += inv_s2;
					A(k2, k1) += inv_s2;
				}
			}
		}

		Eigen::LDLT<Eigen::MatrixXd> A_ldlt(A);

		// B = X'Z  (p × J_total)
		Eigen::MatrixXd B = Eigen::MatrixXd::Zero(p, J);
		for (intptr_t g = 0; g < G; g++)
		{
			intptr_t off = lay.offset[g];
			auto &idx = groups[g].indices;
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t col = off + idx[i];
				for (intptr_t k = 0; k < p; k++) {
					B(k, col) += Xm(i, k);
				}
			}
		}

		// X'V⁻¹X = (1/σ²)(X'X − (1/σ²) B A⁻¹ B')
		Eigen::MatrixXd XtX = Xm.transpose() * Xm;
		Eigen::MatrixXd AinvBt = A_ldlt.solve(B.transpose());
		Eigen::MatrixXd XtVinvX = inv_s2 * (XtX - inv_s2 * B * AinvBt);

		// Var(β̂ | θ̂) = (X'V⁻¹X)⁻¹
		Eigen::LDLT<Eigen::MatrixXd> ldlt(XtVinvX);
		vcov = ldlt.solve(Eigen::MatrixXd::Identity(p, p));
	}
	else
	{
		// Non-Gaussian: β block of the full inverse Hessian.
		std::string family_name_se(fam.name.data(), fam.name.size());
		OuterObjective se_objective{fam, Xm, ym, lay, n, p, is_gaussian, family_name_se};

		Eigen::MatrixXd hess_full(outer_dim, outer_dim);

		for (intptr_t k = 0; k < outer_dim; k++)
		{
			double hk = 1e-4 * std::max(std::abs(phi[k]), 1.0);

			Eigen::VectorXd phi_plus = phi, phi_minus = phi;
			phi_plus[k] += hk;
			phi_minus[k] -= hk;

			Eigen::VectorXd grad_plus = exact_gradient(phi_plus, se_objective);
			Eigen::VectorXd grad_minus = exact_gradient(phi_minus, se_objective);

			for (intptr_t j = 0; j < outer_dim; j++) {
				hess_full(j, k) = (grad_plus[j] - grad_minus[j]) / (2.0 * hk);
			}
		}

		hess_full = 0.5 * (hess_full + hess_full.transpose());

		Eigen::MatrixXd vcov_full;
		{
			Eigen::LDLT<Eigen::MatrixXd> ldlt(hess_full);
			if (ldlt.info() == Eigen::Success && ldlt.isPositive()) {
				vcov_full = ldlt.solve(Eigen::MatrixXd::Identity(outer_dim, outer_dim));
			} else {
				Eigen::JacobiSVD<Eigen::MatrixXd> svd(hess_full, Eigen::ComputeThinU | Eigen::ComputeThinV);
				vcov_full = svd.solve(Eigen::MatrixXd::Identity(outer_dim, outer_dim));
			}
		}

		vcov = vcov_full.topLeftCorner(p, p);
	}

	// ── Build the Model ─────────────────────────────────────────────

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
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
		model.df_residual = n - p;
	}

	for (intptr_t g = 0; g < G; g++)
	{
		RandomEffectGroup reg;
		reg.group_name = groups[g].name;
		reg.term_names.append("(Intercept)");
		reg.nlevels = groups[g].nlevels;
		reg.variance.append(sigma2_u[g]);

		intptr_t off = lay.offset[g];
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			reg.conditional_modes.append(final_inner.u[off + j]);
		}

		model.random_effects.append(std::move(reg));
	}

	model.loglik = -final_inner.laplace_nll;
	model.compute_information_criteria();

	model.niter = niter;
	model.converged = converged;

	return model;
}

} // namespace phonometrica::stats
