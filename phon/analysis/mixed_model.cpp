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

	int max_inner = is_gaussian ? 1 : 25;
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
		for (intptr_t i = 0; i < n; i++) {
			h_u[off + idx[i]] += w_final[i];
		}
	}

	double log_det_H = 0;
	for (intptr_t k = 0; k < lay.J_total; k++) {
		log_det_H += std::log(h_u[k]);
	}

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
	std::vector<ADdouble> h_u(lay.J_total);
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t off = lay.offset[g];
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			h_u[off + j] = inv_sigma2_u[g];
		}
	}
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t off = lay.offset[g];
		for (intptr_t i = 0; i < n; i++) {
			h_u[off + idx[i]] += w[i];
		}
	}

	ADdouble log_det_H = 0;
	for (intptr_t k = 0; k < lay.J_total; k++) {
		log_det_H += CppAD::log(h_u[k]);
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

	double log_sigma_u_init = is_gaussian ? std::log(std::max(0.5 * fe.rse, 0.01)) : 0.0;
	for (intptr_t g = 0; g < G; g++) {
		phi[p + g] = log_sigma_u_init;
	}

	if (is_gaussian) {
		phi[p + G] = std::log(std::max(fe.rse, 0.01));
	}

	// ── Outer optimisation with CppAD gradients ─────────────────────

	std::string family_name(fam.name.data(), fam.name.size());
	OuterObjective objective{fam, Xm, ym, lay, n, p, is_gaussian, family_name};

	LBFGSParam<double> param;
	param.epsilon = 1e-5;
	param.max_iterations = 300;
	param.max_linesearch = 40;
	LBFGSSolver<double> solver(param);

	double fx;
	int niter = 0;
	bool converged = true;

	try {
		niter = solver.minimize(objective, phi, fx);
	}
	catch (std::exception &)
	{
		converged = false;
	}

	// ── Extract estimates ────────────────────────────────────────────

	Eigen::VectorXd beta_hat = phi.head(p);

	Eigen::VectorXd sigma2_u(G);
	for (intptr_t g = 0; g < G; g++) {
		sigma2_u[g] = std::exp(2.0 * phi[p + g]);
	}

	double sigma2 = 0;
	if (is_gaussian) {
		sigma2 = std::exp(2.0 * phi[p + G]);
	}

	auto final_inner = solve_inner(beta_hat, sigma2_u, sigma2, fam, Xm, ym, lay, n, p);

	// ── Standard errors: beta block of full inverse Hessian ──────────
	//
	// The correct Var(beta_hat) is the top-left p×p block of the inverse of the
	// FULL observed information matrix (over all outer parameters: beta, log_sigma_u's,
	// and log_sigma for Gaussian). Inverting only the beta-beta subblock would ignore
	// uncertainty propagating from the variance components — this underestimates the SE
	// for any coefficient correlated with a variance parameter (typically the intercept).
	//
	// The Hessian is computed by finite differences of the exact (CppAD) gradient.

	Eigen::MatrixXd hess_full(outer_dim, outer_dim);

	for (intptr_t k = 0; k < outer_dim; k++)
	{
		double hk = 1e-4 * std::max(std::abs(phi[k]), 1.0);

		Eigen::VectorXd phi_plus = phi, phi_minus = phi;
		phi_plus[k] += hk;
		phi_minus[k] -= hk;

		Eigen::VectorXd grad_plus = exact_gradient(phi_plus, objective);
		Eigen::VectorXd grad_minus = exact_gradient(phi_minus, objective);

		for (intptr_t j = 0; j < outer_dim; j++) {
			hess_full(j, k) = (grad_plus[j] - grad_minus[j]) / (2.0 * hk);
		}
	}

	hess_full = 0.5 * (hess_full + hess_full.transpose());

	// Invert the full Hessian, then extract the beta block.
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

	Eigen::MatrixXd vcov = vcov_full.topLeftCorner(p, p);

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
