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
 * mixed model (GLMM) with one or more random intercepts (crossed or nested).                                         *
 *                                                                                                                     *
 * For G grouping factors with J_1, ..., J_G levels, the random effects vector is                                     *
 *   u = (u_{1,1}, ..., u_{1,J_1}, u_{2,1}, ..., u_{2,J_2}, ..., u_{G,1}, ..., u_{G,J_G})                           *
 *                                                                                                                     *
 * Each u_{g,j} ~ N(0, sigma^2_{u,g}) independently. The linear predictor is                                          *
 *   eta_i = x_i' beta  +  u_{1, k_1(i)}  +  u_{2, k_2(i)}  +  ...  +  u_{G, k_G(i)}                               *
 *                                                                                                                     *
 * The joint Hessian H_uu is diagonal (each u_{g,j} appears in a disjoint set of terms),                              *
 * so the inner Newton and the log-det computation decompose into J_total = sum J_g independent                        *
 * scalar problems, exactly as in the single-group case.                                                               *
 *                                                                                                                     *
 * Mathematical framework:                                                                                             *
 *   Breslow & Clayton (1993). JASA 88(421), 9–25.                                                                    *
 *   Kristensen et al. (2016). JSS 70(5), 1–21.                                                                       *
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
// Multi-group random effects layout
// =====================================================================

// Precomputed layout for the concatenated random effects vector.
// u = [ group_0 levels | group_1 levels | ... ]
//       ^offset[0]       ^offset[1]
struct GroupLayout
{
	intptr_t G;                        // number of grouping factors
	intptr_t J_total;                  // sum of all J_g
	std::vector<intptr_t> J;           // J[g] = number of levels in group g
	std::vector<intptr_t> offset;      // offset[g] = start index in the concatenated u vector

	// group_indices[g][i] = level index (0-based) for observation i in group g
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
// Helper functions (unchanged from single-group version)
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
// Inner Newton solve + Laplace nll (multi-group)
// =====================================================================

struct InnerResult
{
	Eigen::VectorXd u;           // converged random intercepts (J_total)
	Eigen::VectorXd eta;         // linear predictor at convergence (n)
	Eigen::VectorXd mu;          // fitted means at convergence (n)
	double laplace_nll;
	bool converged;
};


// sigma2_u: one variance per grouping factor (length G)
// sigma2:   residual variance (Gaussian only; 0 for others)
static InnerResult solve_laplace(const Eigen::VectorXd &beta,
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
	res.converged = true;

	bool is_gaussian = (fam.name == "gaussian");
	double inv_sigma2 = is_gaussian ? (1.0 / sigma2) : 0.0;

	// Inverse variance per group
	Eigen::VectorXd inv_sigma2_u(lay.G);
	for (intptr_t g = 0; g < lay.G; g++) {
		inv_sigma2_u[g] = 1.0 / sigma2_u[g];
	}

	int max_inner = is_gaussian ? 1 : 25;
	double inner_tol = 1e-8;

	Eigen::VectorXd Xbeta = Xm * beta;

	for (int iter = 0; iter < max_inner; iter++)
	{
		// eta = X beta + sum_g Z_g u_g
		res.eta = Xbeta;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++) {
				res.eta[i] += res.u[off + idx[i]];
			}
		}
		res.mu = fam.linkinv(res.eta);

		Eigen::VectorXd g_eta = nll_gradient_eta(ym, res.mu, fam, inv_sigma2);
		Eigen::VectorXd w = working_weights(res.mu, fam, inv_sigma2);

		// Accumulate per-element gradient and Hessian for each group
		// g_u and h_u are indexed into the concatenated u vector
		Eigen::VectorXd g_u = Eigen::VectorXd::Zero(lay.J_total);
		Eigen::VectorXd h_u = Eigen::VectorXd::Zero(lay.J_total);

		// Prior contribution to Hessian diagonal
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				h_u[off + j] = inv_sigma2_u[g];
			}
		}

		// Data contribution
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

		// Prior contribution to gradient
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				g_u[off + j] += res.u[off + j] * inv_sigma2_u[g];
			}
		}

		// Newton update
		double max_change = 0;
		for (intptr_t k = 0; k < lay.J_total; k++)
		{
			double step = g_u[k] / h_u[k];
			res.u[k] -= step;
			max_change = std::max(max_change, std::abs(step));
		}

		if (max_change < inner_tol) break;
	}

	// ── Recompute eta, mu at converged u ─────────────────────────────
	res.eta = Xbeta;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t off = lay.offset[g];
		for (intptr_t i = 0; i < n; i++) {
			res.eta[i] += res.u[off + idx[i]];
		}
	}
	res.mu = fam.linkinv(res.eta);

	// ── Joint nll at u_hat ───────────────────────────────────────────

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

	// Prior nll: sum over groups
	double prior_nll = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		double sq_sum = 0;
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			sq_sum += res.u[off + j] * res.u[off + j];
		}
		prior_nll += sq_sum / (2.0 * sigma2_u[g])
		             + 0.5 * lay.J[g] * std::log(2.0 * M_PI * sigma2_u[g]);
	}

	double joint_nll = cond_nll + prior_nll;

	// ── Laplace correction ───────────────────────────────────────────

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

	res.laplace_nll = joint_nll + 0.5 * log_det_H - 0.5 * lay.J_total * std::log(2.0 * M_PI);

	return res;
}


// =====================================================================
// Outer objective (multi-group)
// =====================================================================
//
// Parameter layout:
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

	double eval(const Eigen::VectorXd &phi) const
	{
		Eigen::VectorXd beta = phi.head(p);

		Eigen::VectorXd sigma2_u(lay.G);
		for (intptr_t g = 0; g < lay.G; g++) {
			sigma2_u[g] = std::exp(2.0 * phi[p + g]);
		}

		double sigma2 = 0;
		if (is_gaussian) {
			sigma2 = std::exp(2.0 * phi[p + lay.G]);
		}

		auto res = solve_laplace(beta, sigma2_u, sigma2, fam, Xm, ym, lay, n, p);
		return res.laplace_nll;
	}

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

	// Initialize all log(sigma_u_g) to the same starting point
	double log_sigma_u_init = is_gaussian ? std::log(std::max(0.5 * fe.rse, 0.01)) : 0.0;
	for (intptr_t g = 0; g < G; g++) {
		phi[p + g] = log_sigma_u_init;
	}

	if (is_gaussian) {
		phi[p + G] = std::log(std::max(fe.rse, 0.01));
	}

	// ── Outer optimisation ──────────────────────────────────────────

	OuterObjective objective{fam, Xm, ym, lay, n, p, is_gaussian};

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

	auto final_res = solve_laplace(beta_hat, sigma2_u, sigma2, fam, Xm, ym, lay, n, p);

	// ── Standard errors (numerical Hessian of beta block) ────────────

	Eigen::MatrixXd hess_beta(p, p);
	double h = 1e-4;

	for (intptr_t k = 0; k < p; k++)
	{
		double hk = h * std::max(std::abs(phi[k]), 1.0);

		Eigen::VectorXd phi_plus = phi, phi_minus = phi;
		phi_plus[k] += hk;
		phi_minus[k] -= hk;

		Eigen::VectorXd grad_plus(outer_dim), grad_minus(outer_dim);
		objective(phi_plus, grad_plus);
		objective(phi_minus, grad_minus);

		for (intptr_t j = 0; j < p; j++) {
			hess_beta(j, k) = (grad_plus[j] - grad_minus[j]) / (2.0 * hk);
		}
	}

	hess_beta = 0.5 * (hess_beta + hess_beta.transpose());
	Eigen::MatrixXd vcov;
	{
		Eigen::LDLT<Eigen::MatrixXd> ldlt(hess_beta);
		if (ldlt.info() == Eigen::Success && ldlt.isPositive()) {
			vcov = ldlt.solve(Eigen::MatrixXd::Identity(p, p));
		} else {
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

	// Fitted values and residuals
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);
	for (intptr_t i = 0; i < n; i++)
	{
		model.fitted[i + 1] = final_res.mu[i];
		model.residuals[i + 1] = ym[i] - final_res.mu[i];
	}

	if (is_gaussian)
	{
		model.rse = std::sqrt(sigma2);
		model.df_residual = n - p;
	}

	// Random effects — one RandomEffectGroup per grouping factor
	for (intptr_t g = 0; g < G; g++)
	{
		RandomEffectGroup reg;
		reg.group_name = groups[g].name;
		reg.term_names.append("(Intercept)");
		reg.nlevels = groups[g].nlevels;
		reg.variance.append(sigma2_u[g]);

		intptr_t off = lay.offset[g];
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			reg.conditional_modes.append(final_res.u[off + j]);
		}

		model.random_effects.append(std::move(reg));
	}

	model.loglik = -final_res.laplace_nll;
	model.compute_information_criteria();

	model.niter = niter;
	model.converged = converged;

	return model;
}

} // namespace phonometrica::stats
