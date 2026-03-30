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
 * The algorithm implemented here is the Laplace approximation to the marginal likelihood of a generalized linear      *
 * mixed model (GLMM) with random intercepts. The mathematical framework is described in:                             *
 *                                                                                                                     *
 *   Breslow & Clayton (1993). Approximate inference in generalized linear mixed models. JASA 88(421), 9–25.          *
 *   Kristensen et al. (2016). TMB: Automatic Differentiation and Laplace Approximation. JSS 70(5), 1–21.            *
 *                                                                                                                     *
 * Outline:                                                                                                            *
 *                                                                                                                     *
 *   The model is  y_i | u ~ Family(mu_i),  eta_i = x_i' beta + u_{g(i)},  u_j ~ N(0, sigma^2_u).                   *
 *                                                                                                                     *
 *   Define the joint negative log-likelihood:                                                                         *
 *                                                                                                                     *
 *     f(u, phi) = -sum_i log p(y_i | eta_i)  +  ||u||^2 / (2 sigma^2_u)  +  (J/2) log(2 pi sigma^2_u)             *
 *                                                                                                                     *
 *   where phi = (beta, log sigma_u) for non-Gaussian, or (beta, log sigma_u, log sigma) for Gaussian.                *
 *                                                                                                                     *
 *   The Laplace approximation to the marginal negative log-likelihood is:                                             *
 *                                                                                                                     *
 *     nll(phi) = f(u_hat, phi)  +  (1/2) log det H_uu  -  (J/2) log(2 pi)                                          *
 *                                                                                                                     *
 *   where u_hat minimises f over u for fixed phi, and H_uu = d^2 f / du du' at u_hat.                               *
 *                                                                                                                     *
 *   For random intercepts, H_uu is diagonal, so:                                                                      *
 *     - The inner optimisation decomposes into J independent scalar problems (Newton-Raphson)                         *
 *     - log det H = sum_j log h_j, with h_j = sum_{i in group j} w_i + 1/sigma^2_u                                 *
 *     - w_i is the GLM working weight: d^2(-log p) / d eta^2                                                        *
 *                                                                                                                     *
 *   For Gaussian family, the inner problem is quadratic and Newton converges in one step.                             *
 *   The Laplace approximation is then exact (no approximation error).                                                 *
 *                                                                                                                     *
 *   The outer optimisation over phi uses L-BFGS. Gradients are currently computed by central finite differences;      *
 *   when CppAD is integrated, the outer gradient will be obtained by automatic differentiation of nll(phi),           *
 *   either by taping the inner Newton (exact implicit-function-theorem gradient) or by treating u_hat as constant     *
 *   (valid at convergence due to the stationarity of u_hat).                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <boost/math/distributions/normal.hpp>
#include <phon/analysis/mixed_model.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/utils/matrix.hpp>
#include <phon/third_party/LBFGSpp/LBFGS.h>

namespace phonometrica::stats {

namespace {

// =====================================================================
// Inner Newton solve + Laplace nll evaluation
// =====================================================================
//
// For given outer parameters phi = (beta, sigma^2_u, [sigma^2]),
// find u_hat by Newton-Raphson on the conditional posterior, then
// evaluate the Laplace-approximated marginal negative log-likelihood.

struct InnerResult
{
	Eigen::VectorXd u;           // converged random intercepts (J)
	Eigen::VectorXd eta;         // linear predictor at convergence (n)
	Eigen::VectorXd mu;          // fitted means at convergence (n)
	double laplace_nll;          // Laplace-approximated marginal nll
	bool converged;
};


// Compute the per-observation GLM working weight  w_i = d^2(-log p)/d eta^2.
// For canonical links this equals V(mu_i), optionally scaled by 1/sigma^2 for Gaussian.
static Eigen::VectorXd working_weights(const Eigen::VectorXd &mu, const Family &fam,
                                        double inv_sigma2)
{
	// fam.variance returns V(mu); for canonical links, d^2(-log p)/d eta^2 = V(mu).
	// For Gaussian, we additionally divide by sigma^2 (since -log p = (y-mu)^2 / (2 sigma^2)).
	Eigen::VectorXd V = fam.variance(mu);
	if (fam.name == "gaussian") {
		V *= inv_sigma2;
	}
	return V;
}


// Compute the per-observation gradient  d(-log p)/d eta.
// For canonical links: -(y_i - mu_i), scaled by 1/sigma^2 for Gaussian.
static Eigen::VectorXd nll_gradient_eta(const Eigen::VectorXd &y, const Eigen::VectorXd &mu,
                                          const Family &fam, double inv_sigma2)
{
	Eigen::VectorXd g = -(y - mu);
	if (fam.name == "gaussian") {
		g *= inv_sigma2;
	}
	return g;
}


static InnerResult solve_laplace(const Eigen::VectorXd &beta,
                                  double sigma2_u,
                                  double sigma2,   // residual variance (Gaussian only; 0 for others)
                                  const Family &fam,
                                  const Eigen::Map<Matrix<double>> &Xm,
                                  const Eigen::Map<Vector<double>> &ym,
                                  const std::vector<intptr_t> &group_idx,
                                  intptr_t n, intptr_t p, intptr_t J)
{
	InnerResult res;
	res.u = Eigen::VectorXd::Zero(J);
	res.converged = true;

	bool is_gaussian = (fam.name == "gaussian");
	double inv_sigma2 = is_gaussian ? (1.0 / sigma2) : 0.0;
	double inv_sigma2_u = 1.0 / sigma2_u;

	// Maximum inner Newton iterations.
	// For Gaussian, the problem is quadratic and 1 iteration is exact.
	int max_inner = is_gaussian ? 1 : 25;
	double inner_tol = 1e-8;

	// ── Fixed part of linear predictor: Xβ ──────────────────────────
	Eigen::VectorXd Xbeta = Xm * beta;

	// ── Inner Newton: solve for u_hat ────────────────────────────────
	//
	// For each group j, we solve the 1D problem:
	//   min_{u_j}  -sum_{i in j} log p(y_i | eta_i)  +  u_j^2 / (2 sigma^2_u)
	//
	// Gradient w.r.t. u_j:
	//   g_j = sum_{i in j} d(-log p)/d eta_i  +  u_j / sigma^2_u
	//
	// Hessian w.r.t. u_j:
	//   h_j = sum_{i in j} w_i  +  1 / sigma^2_u
	//
	// Newton step:  u_j  <-  u_j - g_j / h_j

	for (int iter = 0; iter < max_inner; iter++)
	{
		// Compute eta, mu at current u
		res.eta = Xbeta;
		for (intptr_t i = 0; i < n; i++) {
			res.eta[i] += res.u[group_idx[i]];
		}
		res.mu = fam.linkinv(res.eta);

		// Per-observation gradient and weights
		Eigen::VectorXd g_eta = nll_gradient_eta(ym, res.mu, fam, inv_sigma2);
		Eigen::VectorXd w = working_weights(res.mu, fam, inv_sigma2);

		// Accumulate per-group gradient and Hessian diagonal
		Eigen::VectorXd g_u = Eigen::VectorXd::Zero(J);
		Eigen::VectorXd h_u = Eigen::VectorXd::Constant(J, inv_sigma2_u);

		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t j = group_idx[i];
			g_u[j] += g_eta[i];
			h_u[j] += w[i];
		}
		// Add the prior gradient
		for (intptr_t j = 0; j < J; j++) {
			g_u[j] += res.u[j] * inv_sigma2_u;
		}

		// Newton update
		double max_change = 0;
		for (intptr_t j = 0; j < J; j++)
		{
			double step = g_u[j] / h_u[j];
			res.u[j] -= step;
			max_change = std::max(max_change, std::abs(step));
		}

		if (max_change < inner_tol) break;
	}

	// ── Recompute eta, mu at converged u ─────────────────────────────
	res.eta = Xbeta;
	for (intptr_t i = 0; i < n; i++) {
		res.eta[i] += res.u[group_idx[i]];
	}
	res.mu = fam.linkinv(res.eta);

	// ── Joint nll at u_hat ───────────────────────────────────────────
	//
	// f = -loglik(y, mu) + ||u||^2 / (2 sigma^2_u) + (J/2) log(2 pi sigma^2_u)
	//
	// For Gaussian, we use the explicit nll with sigma^2 (not the profiled version):
	//   -loglik = sum (y-mu)^2 / (2 sigma^2) + (n/2) log(2 pi sigma^2)

	double cond_nll;   // -log p(y | eta)
	if (is_gaussian)
	{
		double rss = (ym - res.mu).squaredNorm();
		cond_nll = rss / (2.0 * sigma2) + 0.5 * n * std::log(2.0 * M_PI * sigma2);
	}
	else
	{
		cond_nll = -fam.loglik(ym, res.mu);
	}

	double prior_nll = res.u.squaredNorm() / (2.0 * sigma2_u)
	                   + 0.5 * J * std::log(2.0 * M_PI * sigma2_u);

	double joint_nll = cond_nll + prior_nll;

	// ── Laplace correction: + (1/2) log det H_uu - (J/2) log(2 pi) ──
	//
	// H_uu is diagonal with h_j = sum_{i in j} w_i + 1/sigma^2_u

	Eigen::VectorXd w_final = working_weights(res.mu, fam, inv_sigma2);
	Eigen::VectorXd h_u = Eigen::VectorXd::Constant(J, inv_sigma2_u);
	for (intptr_t i = 0; i < n; i++) {
		h_u[group_idx[i]] += w_final[i];
	}

	double log_det_H = 0;
	for (intptr_t j = 0; j < J; j++) {
		log_det_H += std::log(h_u[j]);
	}

	// nll(phi) = f(u_hat, phi) + (1/2) log det H - (J/2) log(2 pi)
	res.laplace_nll = joint_nll + 0.5 * log_det_H - 0.5 * J * std::log(2.0 * M_PI);

	return res;
}


// =====================================================================
// Outer objective for L-BFGS (with numerical gradient)
// =====================================================================
//
// Parameter layout:
//   Non-Gaussian: phi = (beta_1, ..., beta_p, log_sigma_u)        dim = p+1
//   Gaussian:     phi = (beta_1, ..., beta_p, log_sigma_u, log_sigma)  dim = p+2
//
// The gradient is computed by central finite differences.  When CppAD is
// available, this function should be replaced by an AD-taped version that
// calls solve_laplace with AD<double> scalars.

struct OuterObjective
{
	const Family &fam;
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const std::vector<intptr_t> &group_idx;
	intptr_t n, p, J;
	bool is_gaussian;

	// Evaluate the Laplace nll for a given outer parameter vector.
	double eval(const Eigen::VectorXd &phi) const
	{
		Eigen::VectorXd beta = phi.head(p);
		double log_sigma_u = phi[p];
		double sigma2_u = std::exp(2.0 * log_sigma_u);
		double sigma2 = 0;
		if (is_gaussian) {
			sigma2 = std::exp(2.0 * phi[p + 1]);
		}

		auto res = solve_laplace(beta, sigma2_u, sigma2, fam, Xm, ym,
		                         group_idx, n, p, J);
		return res.laplace_nll;
	}

	// L-BFGS callback: value and gradient (central finite differences).
	double operator()(const Eigen::VectorXd &phi, Eigen::VectorXd &grad) const
	{
		double f0 = eval(phi);

		// Central finite differences
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


} // anonymous namespace


// =====================================================================
// Public entry point
// =====================================================================

Model mixed_model(const Array<double> &y, const Array<double> &X,
                  const GroupingInfo &group, const Family &fam)
{
	using namespace LBFGSpp;

	// ── Validate inputs ─────────────────────────────────────────────

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
	intptr_t J = group.nlevels;
	bool is_gaussian = (fam.name == "gaussian");

	if (n <= p) {
		throw error("Not enough observations to fit model (% obs, % parameters)", n, p);
	}
	if (J < 2) {
		throw error("Grouping factor '%' must have at least 2 levels", group.name);
	}

	// ── Eigen maps ──────────────────────────────────────────────────

	Eigen::Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Eigen::Map<Vector<double>> ym(const_cast<double *>(y.data()), n);

	// ── Starting values from fixed-effects model ─────────────────────

	Model fe;
	if (is_gaussian) {
		fe = lm(y, X);
	} else {
		fe = glm(y, X, fam);
	}

	// Outer parameter vector
	intptr_t outer_dim = is_gaussian ? (p + 2) : (p + 1);
	Eigen::VectorXd phi = Eigen::VectorXd::Zero(outer_dim);

	// beta_0: from fixed-effects fit
	for (intptr_t i = 0; i < p; i++) {
		phi[i] = fe.beta[i + 1]; // 1-based Array → 0-based VectorXd
	}

	// log(sigma_u)_0: start at log(0.5 * rse) for Gaussian, log(1) = 0 otherwise
	if (is_gaussian) {
		phi[p] = std::log(std::max(0.5 * fe.rse, 0.01));
		phi[p + 1] = std::log(std::max(fe.rse, 0.01));
	} else {
		phi[p] = 0.0;
	}

	// ── Outer optimisation: L-BFGS with numerical gradients ──────────

	OuterObjective objective{fam, Xm, ym, group.indices, n, p, J, is_gaussian};

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
		// Use whatever phi we ended up with — may still be a reasonable estimate
	}

	// ── Extract estimates from converged phi ─────────────────────────

	Eigen::VectorXd beta_hat = phi.head(p);
	double log_sigma_u = phi[p];
	double sigma2_u = std::exp(2.0 * log_sigma_u);
	double sigma2 = 0;
	if (is_gaussian) {
		sigma2 = std::exp(2.0 * phi[p + 1]);
	}

	// Final inner solve to get BLUPs, fitted values, and Hessian
	auto final_res = solve_laplace(beta_hat, sigma2_u, sigma2, fam,
	                                Xm, ym, group.indices, n, p, J);

	// ── Standard errors of fixed effects ─────────────────────────────
	//
	// Approximate Var(beta_hat) from the observed information matrix:
	// compute the Hessian of the Laplace nll w.r.t. beta by finite differences.

	Eigen::MatrixXd hess_beta(p, p);
	double h = 1e-4;

	for (intptr_t k = 0; k < p; k++)
	{
		double hk = h * std::max(std::abs(phi[k]), 1.0);

		Eigen::VectorXd phi_plus = phi, phi_minus = phi;
		phi_plus[k] += hk;
		phi_minus[k] -= hk;

		// Gradient of nll w.r.t. all beta components at phi +/- perturbation of beta_k
		// We only need the beta-block of the Hessian: d^2 nll / d beta_j d beta_k
		// Use the fact that  H_{jk} ≈ (f(+e_k) - 2 f(0) + f(-e_k)) / h^2  for diagonal,
		// and the cross-diagonal via  H_{jk} ≈ (g_j(+e_k) - g_j(-e_k)) / (2h)

		// We already have f0 from the optimization; compute gradient at perturbed phi
		// Simpler: approximate H numerically from the gradient
		Eigen::VectorXd grad_plus(outer_dim), grad_minus(outer_dim);
		objective(phi_plus, grad_plus);
		objective(phi_minus, grad_minus);

		for (intptr_t j = 0; j < p; j++) {
			hess_beta(j, k) = (grad_plus[j] - grad_minus[j]) / (2.0 * hk);
		}
	}

	// Symmetrise and invert for the variance-covariance matrix
	hess_beta = 0.5 * (hess_beta + hess_beta.transpose());
	Eigen::MatrixXd vcov;
	{
		Eigen::LDLT<Eigen::MatrixXd> ldlt(hess_beta);
		if (ldlt.info() == Eigen::Success && ldlt.isPositive()) {
			vcov = ldlt.solve(Eigen::MatrixXd::Identity(p, p));
		} else {
			// Fallback: Moore-Penrose pseudo-inverse via SVD
			Eigen::JacobiSVD<Eigen::MatrixXd> svd(hess_beta, Eigen::ComputeThinU | Eigen::ComputeThinV);
			vcov = svd.solve(Eigen::MatrixXd::Identity(p, p));
		}
	}

	// ── Build the Model ─────────────────────────────────────────────

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
	model.nobs = n;
	model.nfixed = p;

	// Design matrices and response
	model.y = y;
	model.X = X;

	// Fixed effects
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

	// Fitted values and residuals (conditional on BLUPs)
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);
	for (intptr_t i = 0; i < n; i++)
	{
		model.fitted[i + 1] = final_res.mu[i];
		model.residuals[i + 1] = ym[i] - final_res.mu[i];
	}

	// Gaussian-specific diagnostics
	if (is_gaussian)
	{
		model.rse = std::sqrt(sigma2);
		model.df_residual = n - p;
	}

	// Random effects
	RandomEffectGroup reg;
	reg.group_name = group.name;
	reg.term_names.append("(Intercept)");
	reg.nlevels = J;
	reg.variance.append(sigma2_u);

	for (intptr_t j = 0; j < J; j++) {
		reg.conditional_modes.append(final_res.u[j]);
	}

	model.random_effects.append(std::move(reg));

	// Log-likelihood and information criteria
	// The Laplace nll is the negative marginal log-likelihood.
	model.loglik = -final_res.laplace_nll;
	model.compute_information_criteria();

	// Convergence
	model.niter = niter;
	model.converged = converged;

	return model;
}

} // namespace phonometrica::stats
