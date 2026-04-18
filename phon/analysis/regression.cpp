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
 * Created: 08/11/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
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

#include <boost/math/distributions/students_t.hpp>
#include <boost/math/distributions/chi_squared.hpp>
#include <boost/math/distributions/fisher_f.hpp>
#include <boost/math/special_functions/digamma.hpp>
#include <boost/math/special_functions/trigamma.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/analysis/statistics.hpp>
#include <phon/utils/matrix.hpp>
#include <phon/third_party/LBFGSpp/LBFGS.h>

namespace phonometrica::stats {

// =====================================================================
// Validation
// =====================================================================

static void validate_inputs(const Array<double> &y, const Array<double> &X)
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
	if (X.nrow() <= X.ncol()) {
		throw error("Not enough data points to perform regression");
	}
}

// =====================================================================
// Store design matrices in model (for diagnostics and predict)
// =====================================================================

static void store_matrices(Model &model, const Array<double> &y, const Array<double> &X)
{
	model.y = y;
	model.X = X;
	model.nobs = y.size();
	model.nfixed = X.ncol();
}

// Store the variance-covariance matrix of fixed-effect coefficients in the model.
static void store_vcov(Model &model, const Eigen::MatrixXd &cov)
{
	intptr_t p = cov.rows();
	model.vcov = Array<double>(p, p, 0.0);
	for (intptr_t i = 0; i < p; i++) {
		for (intptr_t j = 0; j < p; j++) {
			model.vcov(i + 1, j + 1) = cov(i, j);
		}
	}
}

// =====================================================================
// Linear model (OLS)
// =====================================================================

Model lm(const Array<double> &y, const Array<double> &X, const Array<double> &offset)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t m = X.ncol();
	intptr_t n = X.nrow();

	Model model;
	model.family = "gaussian";
	model.link = "identity";
	store_matrices(model, y, X);
	if (!offset.empty()) model.offset = offset;

	// If an offset is present, solve OLS on y_adj = y - offset.
	Map<Vector<double>> y1(const_cast<double*>(y.data()), n);
	Vector<double> y_work = y1;
	if (!offset.empty()) {
		Map<const Vector<double>> off(offset.data(), n);
		y_work -= off;
	}

	// Solve via SVD
	model.beta = Array<double>(m, 0.0);
	Map<Matrix<double>> X1(const_cast<double*>(X.data()), n, m);
	Map<Vector<double>> b1(model.beta.data(), m);

	BDCSVD<Matrix<double>, ComputeThinU | ComputeThinV> svd(X1);
	b1 = svd.solve(y_work);

	// Fitted values and residuals
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);

	for (intptr_t i = 1; i <= n; i++)
	{
		double val = 0.0;
		for (intptr_t j = 1; j <= m; j++) {
			val += X(i, j) * model.beta[j];
		}
		if (!offset.empty()) val += offset[i];
		model.fitted[i] = val;
	}

	// Residual variance
	intptr_t df = n - m;
	model.df_residual = df;
	long double sse = 0.0;

	for (intptr_t i = 1; i <= n; i++)
	{
		auto e = y[i] - model.fitted[i];
		model.residuals[i] = e;
		sse += e * e;
	}
	auto rv = sse / df;
	model.rse = sqrt(static_cast<double>(rv));

	// Standard errors, t-values, p-values
	auto var = (X1.transpose() * X1).inverse();
	model.se = Array<double>(m, 0.0);
	model.stat = Array<double>(m, 0.0);
	model.p = Array<double>(m, 0.0);

	boost::math::students_t_distribution<double> dist(df);

	for (intptr_t i = 1; i <= m; i++)
	{
		model.se[i] = sqrt(rv * var(i - 1, i - 1));
		model.stat[i] = model.beta[i] / model.se[i];
		model.p[i] = 2 * (1 - cdf(dist, std::abs(model.stat[i])));
	}

	// Store full variance-covariance matrix: σ²(X'X)⁻¹
	store_vcov(model, static_cast<double>(rv) * var);

	// R²
	double ybar = mean(y);
	long double ssr = 0.0;
	long double sst = 0.0;

	for (intptr_t i = 1; i <= n; i++)
	{
		ssr += model.residuals[i] * model.residuals[i];
		sst += (y[i] - ybar) * (y[i] - ybar);
	}
	model.r2 = 1 - double(ssr / sst);
	int np = (int)(m - 1); // number of predictors (excluding intercept)
	model.adj_r2 = 1 - (1 - model.r2) * (double(n - 1) / (n - np - 1));

	// Log-likelihood (Gaussian profile log-likelihood)
	Map<Vector<double>> y_eig(const_cast<double*>(y.data()), n);
	Map<Vector<double>> mu_eig(model.fitted.data(), n);
	model.loglik = detail::gaussian_loglik(y_eig, mu_eig);
	model.compute_information_criteria();

	// OLS always converges
	model.niter = 0;
	model.converged = true;

	return model;
}


// =====================================================================
// Generalized linear model (L-BFGS)
// =====================================================================

// Compute cost (negative log-likelihood / n) and gradient for L-BFGS.
// The gradient X'(μ-y)/n is exact for canonical links (identity, logit, log).
static double glm_cost(const Array<double> &y, const Array<double> &X,
                       const Family &fam, const Eigen::VectorXd &beta, Eigen::VectorXd &grad,
                       const Array<double> &offset)
{
	intptr_t n = X.nrow();
	intptr_t m = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, m);
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);

	Vector<double> eta = Xm * beta;
	if (!offset.empty()) {
		Eigen::Map<const Vector<double>> off(offset.data(), n);
		eta += off;
	}
	Vector<double> mu = fam.linkinv(eta);

	grad = (Xm.transpose() * (mu - ym)).array() / n;

	return -fam.loglik(ym, mu) / n;
}


// Model-based covariance: (X'WX)^{-1}
static Matrix<double> glm_covariance(const Array<double> &X, const Family &fam,
                                      const Eigen::VectorXd &beta,
                                      const Array<double> &offset)
{
	intptr_t n = X.nrow();
	intptr_t m = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, m);

	Vector<double> eta = Xm * beta;
	if (!offset.empty()) {
		Eigen::Map<const Vector<double>> off(offset.data(), n);
		eta += off;
	}
	Vector<double> mu = fam.linkinv(eta);
	Vector<double> W = fam.variance(mu);

	return (Xm.transpose() * W.asDiagonal() * Xm).inverse();
}


// Sandwich (robust) covariance: (X'WX)^{-1} X'diag(e²)X (X'WX)^{-1}
static Matrix<double> glm_robust_covariance(const Array<double> &y, const Array<double> &X,
                                             const Family &fam, const Eigen::VectorXd &beta,
                                             const Array<double> &offset)
{
	intptr_t n = X.nrow();
	intptr_t m = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, m);
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);

	Vector<double> eta = Xm * beta;
	if (!offset.empty()) {
		Eigen::Map<const Vector<double>> off(offset.data(), n);
		eta += off;
	}
	Vector<double> mu = fam.linkinv(eta);
	Vector<double> W = fam.variance(mu);

	Vector<double> e2(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double e = ym[i] - mu[i];
		e2[i] = e * e;
	}

	auto XT = Xm.transpose();
	auto bread = (XT * W.asDiagonal() * Xm).inverse();

	return bread * (XT * e2.asDiagonal() * Xm) * bread;
}


Model glm(const Array<double> &y, const Array<double> &X, const Family &fam, bool robust, int max_iter,
          const Array<double> &offset)
{
	using namespace LBFGSpp;

	validate_inputs(y, X);

	intptr_t m = X.ncol();
	intptr_t n = X.nrow();

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
	store_matrices(model, y, X);
	if (!offset.empty()) model.offset = offset;

	// L-BFGS optimization
	Eigen::VectorXd weights = Eigen::VectorXd::Zero(m);
	LBFGSParam<double> param;
	param.epsilon = 1e-6;
	param.max_iterations = max_iter;
	LBFGSSolver<double> solver(param);

	auto cost = [&](const Eigen::VectorXd &b, Eigen::VectorXd &grad)
	{
		return glm_cost(y, X, fam, b, grad, offset);
	};

	double fx;
	int niter = solver.minimize(cost, weights, fx);
	model.niter = niter;
	model.converged = (niter < param.max_iterations);

	// Copy coefficients
	model.beta = Array<double>(m, 0.0);
	std::copy(weights.data(), weights.data() + m, model.beta.data());

	// Variance-covariance matrix
	Matrix<double> cov;
	if (robust) {
		cov = glm_robust_covariance(y, X, fam, weights, offset);
	} else {
		cov = glm_covariance(X, fam, weights, offset);
	}

	// Store full variance-covariance matrix
	store_vcov(model, cov);

	// Standard errors
	model.se = Array<double>(m, 0.0);
	for (intptr_t i = 0; i < m; i++) {
		model.se[i + 1] = sqrt(cov(i, i));
	}

	// z-values (Wald statistics)
	model.stat = Array<double>(m, 0.0);
	for (intptr_t i = 1; i <= m; i++) {
		model.stat[i] = model.beta[i] / model.se[i];
	}

	// p-values (Wald chi-squared test)
	boost::math::chi_squared dist(1);
	model.p = Array<double>(m, 0.0);
	for (intptr_t i = 1; i <= m; i++)
	{
		auto wald = (model.beta[i] * model.beta[i]) / cov(i - 1, i - 1);
		model.p[i] = 1 - boost::math::cdf(dist, wald);
	}

	// Fitted values and residuals
	model.compute_fitted(fam.linkinv);

	// Log-likelihood at converged values
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);
	Eigen::Map<Vector<double>> mu_eig(model.fitted.data(), n);
	model.loglik = fam.loglik(ym, mu_eig);
	model.compute_information_criteria();

	return model;
}


// =====================================================================
// Convenience wrappers
// =====================================================================

Model logit(const Array<double> &y, const Array<double> &X, int max_iter, const Array<double> &offset)
{
	for (auto value : y)
	{
		if (value != 0 && value != 1) {
			throw error("Response array can only contain the values 0 and 1");
		}
	}

	return glm(y, X, Family::binomial(), false, max_iter, offset);
}


Model poisson(const Array<double> &y, const Array<double> &X, bool robust, int max_iter,
              const Array<double> &offset)
{
	return glm(y, X, Family::poisson(), robust, max_iter, offset);
}


// =====================================================================
// Negative binomial regression (IWLS + alternating θ profile)
// =====================================================================
//
// Algorithm (following MASS::glm.nb):
//   1. Initialize θ from method-of-moments: Var(y)/mean(y) ≈ 1 + mean(y)/θ
//   2. Outer loop:
//      a. Given θ, fit β via IWLS with NB working weights
//      b. Given μ = exp(Xβ), update θ by Newton on the NB profile log-likelihood
//   3. Converge when both β and θ stabilise
//
// The IWLS uses the correct non-canonical weights for the log link:
//   w_i = μ_i θ / (θ + μ_i)      (not V(μ) = μ + μ²/θ)
//   z_i = η_i + (y_i − μ_i) / μ_i
//
// Mathematical references:
//   Lawless (1987). Negative binomial and mixed Poisson regression. Can J Stat 15(3).
//   Venables & Ripley (2002). Modern Applied Statistics with S. §7.4.

Model negbin(const Array<double> &y, const Array<double> &X, int max_iter, const Array<double> &offset)
{
	validate_inputs(y, X);

	intptr_t n = X.nrow();
	intptr_t p = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, p);
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);

	// Map offset (may be empty).
	bool has_off = !offset.empty();
	Eigen::Map<const Eigen::VectorXd> off(
		has_off ? offset.data() : nullptr, has_off ? n : 0);

	// ── Initial θ from method-of-moments ─────────────────────────────

	double ybar = ym.mean();
	double yvar = (ym.array() - ybar).square().sum() / (n - 1);
	double theta;
	if (yvar > ybar) {
		theta = ybar * ybar / (yvar - ybar);
	} else {
		theta = 10.0;
	}
	theta = std::clamp(theta, 0.01, 1e6);

	// ── Initial β from Poisson GLM ──────────────────────────────────

	Eigen::VectorXd beta = Eigen::VectorXd::Zero(p);
	{
		auto pois = glm(y, X, Family::poisson(), false, 50, offset);
		for (intptr_t j = 0; j < p; j++) {
			beta[j] = pois.beta[j + 1];
		}
	}

	int outer_iter = 0;
	bool converged = false;

	for (int iter = 0; iter < max_iter; iter++)
	{
		Eigen::VectorXd beta_old = beta;
		double theta_old = theta;

		// ── (a) IWLS for β given θ ───────────────────────────────────

		for (int iwls = 0; iwls < 50; iwls++)
		{
			Eigen::VectorXd eta_xb = Xm * beta;
			Eigen::VectorXd eta = eta_xb;
			if (has_off) eta += off;
			Eigen::VectorXd mu = eta.array().exp().matrix();

			Eigen::VectorXd w(n), z(n);
			for (intptr_t i = 0; i < n; i++)
			{
				double mi = std::max(mu[i], 1e-10);
				w[i] = mi * theta / (theta + mi);
				// Working response on the Xβ scale (without offset)
				z[i] = eta_xb[i] + (ym[i] - mi) / mi;
			}

			Eigen::MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
			Eigen::VectorXd XtWz = Xm.transpose() * (w.array() * z.array()).matrix();

			Eigen::LDLT<Eigen::MatrixXd> ldlt(XtWX);
			Eigen::VectorXd beta_new = ldlt.solve(XtWz);

			double max_change = (beta_new - beta).cwiseAbs().maxCoeff();
			beta = beta_new;

			if (max_change < 1e-8) break;
		}

		// ── (b) Update θ given β ─────────────────────────────────────

		Eigen::VectorXd eta = Xm * beta;
		if (has_off) eta += off;
		Eigen::VectorXd mu = eta.array().exp().matrix();

		for (int newton = 0; newton < 20; newton++)
		{
			double score = 0, info = 0;
			for (intptr_t i = 0; i < n; i++)
			{
				double yi = ym[i];
				double mi = std::max(mu[i], 1e-10);
				double tpm = theta + mi;

				score += boost::math::digamma(yi + theta) - boost::math::digamma(theta)
				         + std::log(theta / tpm) + 1.0 - (yi + theta) / tpm;

				info += -boost::math::trigamma(yi + theta) + boost::math::trigamma(theta)
				        - 1.0 / theta + 2.0 / tpm - (yi + theta) / (tpm * tpm);
			}

			// Newton step: Δθ = score / info (info = observed Fisher information = -d²ℓ/dθ²)
			// Working on θ directly with clamping for positivity.
			if (std::abs(info) < 1e-15) break;
			double step = score / info;
			// Clamp step to avoid wild jumps
			step = std::clamp(step, -theta * 0.5, theta * 2.0);
			double theta_new = theta + step;
			theta_new = std::max(theta_new, 0.001);
			theta = theta_new;

			if (std::abs(step) < 1e-6 * theta) break;
		}

		outer_iter = iter + 1;

		// ── Convergence check ────────────────────────────────────────
		double beta_change = (beta - beta_old).cwiseAbs().maxCoeff();
		double theta_change = std::abs(theta - theta_old);

		if (beta_change < 1e-6 && theta_change < 1e-6 * theta)
		{
			converged = true;
			break;
		}
	}

	// ── Build the model ──────────────────────────────────────────────

	auto fam = Family::negbin(theta);

	Model model;
	model.family = "negbin";
	model.link = "log";
	model.theta = theta;
	store_matrices(model, y, X);
	if (has_off) model.offset = offset;

	model.beta = Array<double>(p, 0.0);
	for (intptr_t j = 0; j < p; j++) {
		model.beta[j + 1] = beta[j];
	}

	// Fitted values
	model.compute_fitted(fam.linkinv);

	// Log-likelihood
	Eigen::Map<Vector<double>> mu_eig(model.fitted.data(), n);
	model.loglik = fam.loglik(ym, mu_eig);
	model.compute_information_criteria();

	// Covariance: (X'WX)⁻¹ with NB working weights
	{
		Eigen::VectorXd eta_cov = Xm * beta;
		if (has_off) eta_cov += off;
		Eigen::VectorXd mu_vec = eta_cov.array().exp().matrix();
		Eigen::VectorXd w(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double mi = std::max(mu_vec[i], 1e-10);
			w[i] = mi * theta / (theta + mi);
		}
		Eigen::MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
		Eigen::MatrixXd cov = XtWX.inverse();

		// Store full variance-covariance matrix
		store_vcov(model, cov);

		model.se = Array<double>(p, 0.0);
		model.stat = Array<double>(p, 0.0);
		model.p = Array<double>(p, 0.0);

		boost::math::chi_squared dist(1);
		for (intptr_t i = 1; i <= p; i++)
		{
			model.se[i] = std::sqrt(std::max(cov(i - 1, i - 1), 0.0));
			model.stat[i] = (model.se[i] > 0) ? model.beta[i] / model.se[i] : 0.0;
			double wald = model.stat[i] * model.stat[i];
			model.p[i] = 1.0 - boost::math::cdf(dist, wald);
		}
	}

	model.niter = outer_iter;
	model.converged = converged;

	return model;
}


// =====================================================================
// Beta regression (IWLS + alternating φ profile)
// =====================================================================
//
// Algorithm (following betareg, Ferrari & Cribari-Neto 2004):
//   1. Initialize φ from method-of-moments: Var(y) = μ̄(1-μ̄)/(1+φ)
//   2. Outer loop:
//      a. Given φ, fit β via IWLS with beta working weights
//      b. Given μ = logistic(Xβ), update φ by Newton on the profile log-likelihood
//   3. Converge when both β and φ stabilise
//
// The logit link gives the same inverse link / dμ/dη as binomial:
//   w_i = μ_i(1-μ_i)(1+φ) = φ · μ_i(1-μ_i)
//   z_i = η_i + (y_i − μ_i) / [μ_i(1 − μ_i)]
//
// Mathematical references:
//   Ferrari, S. L. P. & Cribari-Neto, F. (2004). Beta regression for modelling
//   rates and proportions. J. Applied Statistics, 31(7), 799–815.
//   Smithson, M. & Verkuilen, J. (2006). A better lemon squeezer? Maximum-
//   likelihood regression with beta-distributed dependent variables.
//   Psychological Methods, 11(1), 54–71.

Model beta_regression(const Array<double> &y, const Array<double> &X, int max_iter,
                      const Array<double> &offset)
{
	validate_inputs(y, X);

	intptr_t n = X.nrow();
	intptr_t p = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, p);
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);

	// Map offset (may be empty).
	bool has_off = !offset.empty();
	Eigen::Map<const Eigen::VectorXd> off(
		has_off ? offset.data() : nullptr, has_off ? n : 0);

	// ── Validate response ────────────────────────────────────────────
	for (intptr_t i = 0; i < n; i++)
	{
		if (ym[i] <= 0.0 || ym[i] >= 1.0) {
			throw error("Beta regression requires response values strictly in (0, 1); "
			            "found y = % at observation %", ym[i], i + 1);
		}
	}

	// ── Initial φ from method-of-moments ─────────────────────────────

	double ybar = ym.mean();
	double yvar = (ym.array() - ybar).square().sum() / (n - 1);
	double phi;
	if (yvar > 0 && yvar < ybar * (1.0 - ybar)) {
		phi = ybar * (1.0 - ybar) / yvar - 1.0;
	} else {
		phi = 10.0;
	}
	phi = std::clamp(phi, 0.1, 1e6);

	// ── Initial β from logistic regression ──────────────────────────

	Eigen::VectorXd beta = Eigen::VectorXd::Zero(p);
	{
		auto logit_fit = glm(y, X, Family::binomial(), false, 50, offset);
		for (intptr_t j = 0; j < p; j++) {
			beta[j] = logit_fit.beta[j + 1];
		}
	}

	// ── Outer loop: alternate IWLS for β and Newton for φ ────────────

	int outer_iter = 0;
	bool converged = false;

	for (int iter = 0; iter < max_iter; iter++)
	{
		Eigen::VectorXd beta_old = beta;
		double phi_old = phi;

		// ── (a) IWLS for β given φ ───────────────────────────────────

		for (int iwls = 0; iwls < 50; iwls++)
		{
			Eigen::VectorXd eta_xb = Xm * beta;
			Eigen::VectorXd eta = eta_xb;
			if (has_off) eta += off;
			Eigen::VectorXd mu = (1.0 / (1.0 + (-eta.array()).exp())).matrix();

			Eigen::VectorXd w(n), z(n);
			for (intptr_t i = 0; i < n; i++)
			{
				double mi = std::clamp(mu[i], 1e-10, 1.0 - 1e-10);
				double mu1m = mi * (1.0 - mi);
				w[i] = mu1m * (1.0 + phi);
				// Working response on the Xβ scale (without offset)
				z[i] = eta_xb[i] + (ym[i] - mi) / mu1m;
			}

			Eigen::MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
			Eigen::VectorXd XtWz = Xm.transpose() * (w.array() * z.array()).matrix();

			Eigen::LDLT<Eigen::MatrixXd> ldlt(XtWX);
			Eigen::VectorXd beta_new = ldlt.solve(XtWz);

			double max_change = (beta_new - beta).cwiseAbs().maxCoeff();
			beta = beta_new;

			if (max_change < 1e-8) break;
		}

		// ── (b) Update φ given β ─────────────────────────────────────

		Eigen::VectorXd eta = Xm * beta;
		if (has_off) eta += off;
		Eigen::VectorXd mu = (1.0 / (1.0 + (-eta.array()).exp())).matrix();

		for (int newton = 0; newton < 30; newton++)
		{
			double score = 0, info = 0;
			for (intptr_t i = 0; i < n; i++)
			{
				double mi = std::clamp(mu[i], 1e-10, 1.0 - 1e-10);
				double yi = std::clamp(ym[i], 1e-10, 1.0 - 1e-10);
				double a = mi * phi;           // shape1
				double b = (1.0 - mi) * phi;   // shape2

				score += boost::math::digamma(phi) - mi * boost::math::digamma(a)
				         - (1.0 - mi) * boost::math::digamma(b)
				         + mi * std::log(yi) + (1.0 - mi) * std::log(1.0 - yi);

				info += -boost::math::trigamma(phi) + mi * mi * boost::math::trigamma(a)
				        + (1.0 - mi) * (1.0 - mi) * boost::math::trigamma(b);
			}

			// Newton step: Δφ = score / info  (info = −d²ℓ/dφ²)
			if (std::abs(info) < 1e-15) break;
			double step = score / info;
			step = std::clamp(step, -phi * 0.5, phi * 2.0);
			double phi_new = phi + step;
			phi_new = std::max(phi_new, 0.01);
			phi = phi_new;

			if (std::abs(step) < 1e-6 * phi) break;
		}

		outer_iter = iter + 1;

		// ── Convergence check ────────────────────────────────────────
		double beta_change = (beta - beta_old).cwiseAbs().maxCoeff();
		double phi_change = std::abs(phi - phi_old);

		if (beta_change < 1e-6 && phi_change < 1e-6 * phi)
		{
			converged = true;
			break;
		}
	}

	// ── Build the model ──────────────────────────────────────────────

	auto fam = Family::beta(phi);

	Model model;
	model.family = "beta";
	model.link = "logit";
	model.phi = phi;
	store_matrices(model, y, X);
	if (has_off) model.offset = offset;

	model.beta = Array<double>(p, 0.0);
	for (intptr_t j = 0; j < p; j++) {
		model.beta[j + 1] = beta[j];
	}

	// Fitted values
	model.compute_fitted(fam.linkinv);

	// Log-likelihood
	Eigen::Map<Vector<double>> mu_eig(model.fitted.data(), n);
	model.loglik = fam.loglik(ym, mu_eig);
	model.compute_information_criteria();

	// ── Covariance from expected Fisher information, profiled over φ ──────
	//
	// Under the Ferrari–Cribari parameterisation with the logit link, the
	// expected information blocks are (Ferrari & Cribari-Neto 2004, eqs. 5–7):
	//
	//   K_ββ   = φ² · X' · diag(w_i) · X
	//            w_i = [μ_i(1-μ_i)]² · [ψ'(μ_i φ) + ψ'((1-μ_i) φ)]
	//
	//   K_βφ_j = Σ_i X_{ij} · μ_i(1-μ_i) · φ ·
	//            [μ_i ψ'(μ_i φ) − (1-μ_i) ψ'((1-μ_i) φ)]
	//
	//   K_φφ   = Σ_i [μ_i² ψ'(μ_i φ) + (1-μ_i)² ψ'((1-μ_i) φ) − ψ'(φ)]
	//
	// Profiled variance of β̂ (accounting for φ uncertainty at the MLE):
	//
	//   Cov(β̂) = [K_ββ − K_βφ K_φφ⁻¹ K_βφ']⁻¹
	//
	// The IRLS working weight μ(1-μ)(1+φ) used during estimation is the
	// quasi-likelihood weight; it equals K_ββ only in the φ → ∞ limit and
	// under-estimates the ML Fisher information elsewhere — notably when μ
	// is far from 0.5, where the trigamma factor dominates.
	//
	// Matches glmmTMB's vcov (which computes the observed information by AD
	// of the exact beta log-likelihood).
	{
		Eigen::VectorXd eta_cov = Xm * beta;
		if (has_off) eta_cov += off;
		Eigen::VectorXd mu_vec = (1.0 / (1.0 + (-eta_cov).array().exp())).matrix();

		Eigen::VectorXd w_bb(n);     // diagonal for K_ββ (without φ² factor)
		Eigen::VectorXd c_bphi(n);   // column for K_βφ = X' · c_bphi
		double K_ff = 0.0;            // scalar K_φφ
		double tri_phi = boost::math::trigamma(phi);

		for (intptr_t i = 0; i < n; i++)
		{
			double mi = std::clamp(mu_vec[i], 1e-10, 1.0 - 1e-10);
			double mu1m = mi * (1.0 - mi);
			double a = mi * phi;
			double b = (1.0 - mi) * phi;
			double tri_a = boost::math::trigamma(a);
			double tri_b = boost::math::trigamma(b);

			w_bb[i]   = mu1m * mu1m * (tri_a + tri_b);
			c_bphi[i] = mu1m * phi * (mi * tri_a - (1.0 - mi) * tri_b);
			K_ff     += mi * mi * tri_a + (1.0 - mi) * (1.0 - mi) * tri_b - tri_phi;
		}

		Eigen::MatrixXd K_bb = phi * phi * (Xm.transpose() * w_bb.asDiagonal() * Xm);
		Eigen::VectorXd K_bf = Xm.transpose() * c_bphi;

		// Profile out φ: I_β = K_ββ − K_βφ K_φφ⁻¹ K_φβ
		Eigen::MatrixXd I_profile = K_bb;
		if (K_ff > 1e-15) {
			I_profile.noalias() -= (K_bf * K_bf.transpose()) / K_ff;
		}

		Eigen::MatrixXd cov = I_profile.inverse();

		store_vcov(model, cov);

		model.se = Array<double>(p, 0.0);
		model.stat = Array<double>(p, 0.0);
		model.p = Array<double>(p, 0.0);

		boost::math::chi_squared dist(1);
		for (intptr_t i = 1; i <= p; i++)
		{
			model.se[i] = std::sqrt(std::max(cov(i - 1, i - 1), 0.0));
			model.stat[i] = (model.se[i] > 0) ? model.beta[i] / model.se[i] : 0.0;
			double wald = model.stat[i] * model.stat[i];
			model.p[i] = 1.0 - boost::math::cdf(dist, wald);
		}
	}

	model.niter = outer_iter;
	model.converged = converged;

	return model;
}


// =====================================================================
// Penalized linear model (Gaussian GAM) with GCV
// =====================================================================

// Evaluate GCV score for a given smoothing parameter λ.
// Returns {gcv_score, edf, rss} for the penalized OLS:
//   β̂(λ) = (X'X + λS)⁻¹ X'y
//   GCV(λ) = n · RSS(λ) / (n − edf(λ))²
//   edf(λ) = tr(X (X'X + λS)⁻¹ X') = tr((X'X + λS)⁻¹ X'X)
//
static std::tuple<double, double, double>
gcv_score(const Matrix<double> &XtX, const Vector<double> &Xty,
          const Matrix<double> &Sm, const Matrix<double> &Xm,
          const Vector<double> &ym, double lambda, intptr_t n, intptr_t p)
{
	using namespace Eigen;

	// Penalized normal equations: (X'X + λS) β = X'y
	MatrixXd M = XtX + lambda * Sm;
	LLT<MatrixXd> llt(M);
	if (llt.info() != Eigen::Success)
	{
		// Fallback to LDLT for near-singular cases
		LDLT<MatrixXd> ldlt(M);
		VectorXd beta = ldlt.solve(Xty);
		VectorXd resid = ym - Xm * beta;
		double rss = resid.squaredNorm();
		// EDF via trace: solve M Z = X'X, edf = tr(Z)
		MatrixXd Z = ldlt.solve(XtX);
		double edf = Z.trace();
		double denom = (double)n - edf;
		double gcv = (denom > 0.5) ? (double)n * rss / (denom * denom) : 1e30;
		return {gcv, edf, rss};
	}

	VectorXd beta = llt.solve(Xty);
	VectorXd resid = ym - Xm * beta;
	double rss = resid.squaredNorm();

	// EDF: solve M Z = X'X, then edf = tr(Z)
	MatrixXd Z = llt.solve(XtX);
	double edf = Z.trace();

	double denom = (double)n - edf;
	double gcv = (denom > 0.5) ? (double)n * rss / (denom * denom) : 1e30;

	return {gcv, edf, rss};
}


Model penalized_lm(const Array<double> &y, const Array<double> &X,
                   const Array<double> &S, intptr_t n_parametric,
                   const std::vector<SmoothColumnRange> &smooth_ranges,
                   FittingCallback progress,
                   const Array<double> &offset)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t n = y.size();
	intptr_t p = X.ncol();
	intptr_t K = (intptr_t)smooth_ranges.size(); // number of penalty blocks

	Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Map<Vector<double>> ym(const_cast<double *>(y.data()), n);
	Map<Matrix<double>> Sm(const_cast<double *>(S.data()), p, p);

	// For Gaussian with identity link, absorb offset into the response:
	// y_work = y - offset, then solve (X'X + λS)β = X'y_work.
	VectorXd ym_work = ym;
	if (!offset.empty()) {
		Map<const VectorXd> off(offset.data(), n);
		ym_work -= off;
	}

	MatrixXd XtX = Xm.transpose() * Xm;
	VectorXd Xty = Xm.transpose() * ym_work;

	// ── Extract per-smooth penalty sub-matrices from S ───────────

	// Each S_j is p×p with nonzero entries only in the block for smooth j.
	std::vector<MatrixXd> S_blocks(K, MatrixXd::Zero(p, p));
	for (intptr_t j = 0; j < K; j++)
	{
		auto &sr = smooth_ranges[j];
		for (intptr_t r = 0; r < sr.col_count; r++) {
			for (intptr_t c = 0; c < sr.col_count; c++) {
				S_blocks[j](sr.col_start + r, sr.col_start + c) = Sm(sr.col_start + r, sr.col_start + c);
			}
		}
	}

	// ── Per-smooth λ optimization via alternating GCV ────────────
	//
	// For K=1 or K=0 this reduces to the single-λ case.
	// For K>1, we cycle: optimize λ_j holding others fixed, repeat.

	std::vector<double> log_lambda(K, 0.0); // log10(λ_j), initialized to 1.0

	// Step 1: Initialize with single-λ GCV.
	if (K > 0)
	{
		double best_gcv = std::numeric_limits<double>::max();
		for (int g = -50; g <= 50; g++)
		{
			double log_lam = 0.1 * g;
			double lam = std::pow(10.0, log_lam);
			MatrixXd M = XtX + lam * Sm;
			LDLT<MatrixXd> ldlt(M);
			VectorXd beta = ldlt.solve(Xty);
			double rss = (ym_work - Xm * beta).squaredNorm();
			double edf = ldlt.solve(XtX).trace();
			double denom = (double)n - edf;
			double gcv = (denom > 0.5) ? (double)n * rss / (denom * denom) : 1e30;
			if (gcv < best_gcv)
			{
				best_gcv = gcv;
				for (intptr_t j = 0; j < K; j++) log_lambda[j] = log_lam;
			}
		}
	}

	// Step 2: Alternating optimization.
	int total_steps = std::max(K * 41 * 5, (intptr_t)1);
	int step = 0;
	if (progress) progress(0, total_steps);

	int max_cycles = (K > 1) ? 10 : 1; // single smooth needs no alternating

	for (int cycle = 0; cycle < max_cycles; cycle++)
	{
		double max_change = 0;

		for (intptr_t j = 0; j < K; j++)
		{
			// Build penalty from all OTHER smooths at their current λ.
			MatrixXd S_other = MatrixXd::Zero(p, p);
			for (intptr_t k = 0; k < K; k++)
			{
				if (k != j) {
					S_other += std::pow(10.0, log_lambda[k]) * S_blocks[k];
				}
			}

			// GCV-optimize λ_j on a grid.
			double best_gcv = std::numeric_limits<double>::max();
			double best_log_lam = log_lambda[j];

			// Coarse grid: 30 points around current value ± 3.5
			double center = log_lambda[j];
			for (int g = -15; g <= 15; g++)
			{
				double log_lam = center + 0.23 * g;
				double lam = std::pow(10.0, log_lam);
				MatrixXd M = XtX + S_other + lam * S_blocks[j];
				LDLT<MatrixXd> ldlt(M);
				VectorXd beta = ldlt.solve(Xty);
				double rss = (ym_work - Xm * beta).squaredNorm();
				double edf = ldlt.solve(XtX).trace();
				double denom = (double)n - edf;
				double gcv = (denom > 0.5) ? (double)n * rss / (denom * denom) : 1e30;
				if (gcv < best_gcv)
				{
					best_gcv = gcv;
					best_log_lam = log_lam;
				}
			}

			// Fine grid: 10 points around best
			double lo = best_log_lam - 0.25;
			double hi = best_log_lam + 0.25;
			for (int g = 0; g <= 10; g++)
			{
				double log_lam = lo + (hi - lo) * g / 10.0;
				double lam = std::pow(10.0, log_lam);
				MatrixXd M = XtX + S_other + lam * S_blocks[j];
				LDLT<MatrixXd> ldlt(M);
				VectorXd beta = ldlt.solve(Xty);
				double rss = (ym_work - Xm * beta).squaredNorm();
				double edf = ldlt.solve(XtX).trace();
				double denom = (double)n - edf;
				double gcv = (denom > 0.5) ? (double)n * rss / (denom * denom) : 1e30;
				if (gcv < best_gcv)
				{
					best_gcv = gcv;
					best_log_lam = log_lam;
				}
			}

			double change = std::abs(best_log_lam - log_lambda[j]);
			if (change > max_change) max_change = change;
			log_lambda[j] = best_log_lam;

			step += 41;
			if (progress) progress(std::min(step, total_steps), total_steps);
		}

		// Convergence: all log(λ) changed by less than 0.05 (≈ 12% in λ)
		if (max_change < 0.05 && cycle > 0) break;
	}

	if (progress) progress(total_steps, total_steps);

	// ── Final fit at converged λ values ──────────────────────────

	MatrixXd S_final = MatrixXd::Zero(p, p);
	for (intptr_t j = 0; j < K; j++) {
		S_final += std::pow(10.0, log_lambda[j]) * S_blocks[j];
	}

	MatrixXd M = XtX + S_final;
	LLT<MatrixXd> llt(M);
	VectorXd beta_vec = llt.solve(Xty);
	VectorXd resid = ym_work - Xm * beta_vec;
	double rss = resid.squaredNorm();

	// EDF and influence matrix: A = (X'X + S_final)⁻¹ X'X
	MatrixXd Minv_XtX = llt.solve(XtX);
	double edf_total = Minv_XtX.trace();

	// Bayesian posterior covariance: Vp = σ² (X'X + S)⁻¹
	// This is the default in mgcv (Wood, 2017 §6.10). It gives correct coverage
	// for parametric terms when penalized smooth/random-effect terms are present,
	// because the off-diagonal blocks of (X'X + S)⁻¹ propagate the smooth
	// uncertainty into the parametric SEs.
	double sigma2 = rss / ((double)n - edf_total);
	if (sigma2 <= 0) sigma2 = 1e-10;
	MatrixXd Minv = llt.solve(MatrixXd::Identity(p, p));
	MatrixXd Vb = sigma2 * Minv;

	// ── Build Model ──────────────────────────────────────────────

	Model model;
	model.family = "gaussian";
	model.link = "identity";
	store_matrices(model, y, X);
	if (!offset.empty()) model.offset = offset;

	// Store full variance-covariance matrix
	store_vcov(model, Vb);

	model.beta = Array<double>(p, 0.0);
	model.se = Array<double>(p, 0.0);
	model.stat = Array<double>(p, 0.0);
	model.p = Array<double>(p, 0.0);

	for (intptr_t j = 0; j < p; j++)
	{
		model.beta[j + 1] = beta_vec[j];
		model.se[j + 1] = std::sqrt(std::max(Vb(j, j), 0.0));
	}

	// t-statistics and p-values for parametric terms only.
	boost::math::students_t tdist(std::max(1.0, (double)n - edf_total));
	for (intptr_t j = 0; j < n_parametric; j++)
	{
		if (model.se[j + 1] > 0)
		{
			model.stat[j + 1] = model.beta[j + 1] / model.se[j + 1];
			model.p[j + 1] = 2.0 * boost::math::cdf(boost::math::complement(tdist, std::abs(model.stat[j + 1])));
		}
	}

	// ── Per-smooth EDF and F-test ────────────────────────────────

	for (auto &sr : smooth_ranges)
	{
		Model::SmoothResult sm;
		sm.variable = sr.variable;
		sm.by = sr.by;
		sm.basis = sr.basis;
		sm.k = sr.k;
		sm.col_start = sr.col_start;
		sm.col_count = sr.col_count;

		sm.edf = 0;
		for (intptr_t j = sr.col_start; j < sr.col_start + sr.col_count; j++) {
			sm.edf += Minv_XtX(j, j);
		}
		// For random-effect smooths (bs="re"), the reference df for the F-test
		// is the basis dimension (number of levels), not the shrunken edf.
		sm.ref_df = (sr.basis == "re") ? (double)sr.col_count : sm.edf;

		if (sm.edf > 0.001)
		{
			VectorXd beta_s = beta_vec.segment(sr.col_start, sr.col_count);
			MatrixXd Vb_s = Vb.block(sr.col_start, sr.col_start, sr.col_count, sr.col_count);

			SelfAdjointEigenSolver<MatrixXd> es(Vb_s);
			VectorXd evals = es.eigenvalues();
			MatrixXd evecs = es.eigenvectors();
			double tol = evals.maxCoeff() * 1e-10;
			VectorXd evals_inv = VectorXd::Zero(sr.col_count);
			for (intptr_t j = 0; j < sr.col_count; j++) {
				if (evals[j] > tol) evals_inv[j] = 1.0 / evals[j];
			}
			double quad = (evecs.transpose() * beta_s).array().square().matrix().dot(evals_inv);
			sm.F_stat = quad / sm.ref_df;

			double resid_df = std::max(1.0, (double)n - edf_total);
			try {
				boost::math::fisher_f fdist(sm.ref_df, resid_df);
				sm.p_value = 1.0 - boost::math::cdf(fdist, sm.F_stat);
			} catch (...) {
				sm.p_value = 0;
			}
		}

		model.smooth_terms.append(std::move(sm));
	}

	// Fitted values and residuals
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);
	for (intptr_t i = 0; i < n; i++)
	{
		double yhat = 0;
		for (intptr_t j = 0; j < p; j++) {
			yhat += Xm(i, j) * beta_vec[j];
		}
		if (!offset.empty()) yhat += offset.data()[i];
		model.fitted[i + 1] = yhat;
		model.residuals[i + 1] = ym[i] - yhat;
	}

	// Fit statistics
	model.rse = std::sqrt(sigma2);
	model.df_residual = std::max((intptr_t)1, (intptr_t)std::round((double)n - edf_total));
	model.nobs = n;
	model.nfixed = p;

	// Log-likelihood uses the ML scale estimate (RSS/n), not the unbiased estimate
	// (RSS/(n-edf)) which is used for standard errors. This matches mgcv's convention.
	double sigma2_ml = rss / (double)n;
	if (sigma2_ml <= 0) sigma2_ml = 1e-10;
	model.loglik = -0.5 * n * (std::log(2.0 * M_PI) + std::log(sigma2_ml) + 1.0);
	model.aic = -2.0 * model.loglik + 2.0 * edf_total;
	model.bic = -2.0 * model.loglik + std::log((double)n) * edf_total;
	model.deviance = rss;

	double ss_total = (ym.array() - ym.mean()).square().sum();
	model.r2 = (ss_total > 0) ? 1.0 - rss / ss_total : 0.0;
	double adj_denom = (double)n - edf_total;
	model.adj_r2 = (adj_denom > 0 && ss_total > 0)
		? 1.0 - (rss / adj_denom) / (ss_total / ((double)n - 1.0))
		: 0.0;

	model.converged = true;
	model.niter = 0;

	return model;
}

Model penalized_glm(const Array<double> &y, const Array<double> &X,
                    const Array<double> &S, const Family &fam,
                    intptr_t n_parametric,
                    const std::vector<SmoothColumnRange> &smooth_ranges,
                    FittingCallback progress,
                    int max_iter,
                    const Array<double> &offset)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t n = y.size();
	intptr_t p = X.ncol();

	Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Map<Vector<double>> ym(const_cast<double *>(y.data()), n);
	Map<Matrix<double>> Sm(const_cast<double *>(S.data()), p, p);

	bool has_off = !offset.empty();
	Map<const VectorXd> off(has_off ? offset.data() : nullptr, has_off ? n : 0);

	// Initialize beta from unpenalized GLM-style: eta = link(y)
	VectorXd beta = VectorXd::Zero(p);
	VectorXd eta = fam.link(ym);
	VectorXd mu = fam.linkinv(eta);

	double lambda = 1.0; // will be optimized
	bool converged = false;
	int iter = 0;

	for (iter = 0; iter < max_iter; iter++)
	{
		// Working weights and response
		VectorXd mu_eta_vec = fam.mu_eta(mu);
		VectorXd var_vec = fam.variance(mu);
		VectorXd eta_xb = Xm * beta;
		VectorXd w(n), z(n);

		for (intptr_t i = 0; i < n; i++)
		{
			double dmu = std::max(std::abs(mu_eta_vec[i]), 1e-10);
			double v = std::max(var_vec[i], 1e-10);
			w[i] = dmu * dmu / v;
			// Working response on Xβ scale (without offset)
			z[i] = eta_xb[i] + (ym[i] - mu[i]) / dmu;
		}

		// Penalized WLS: (X'WX + λS) β = X'Wz
		MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
		VectorXd XtWz = Xm.transpose() * (w.array() * z.array()).matrix();

		// GCV for λ selection on this working model.
		// GCV_w(λ) = Σ w_i (z_i - x_i'β)² / (n - edf)²
		double best_gcv = std::numeric_limits<double>::max();
		double best_lam = lambda;

		// Grid search near current λ
		for (int k = -50; k <= 50; k++)
		{
			double log_lam = 0.1 * k;
			double lam = std::pow(10.0, log_lam);

			MatrixXd M = XtWX + lam * Sm;
			LDLT<MatrixXd> ldlt(M);
			VectorXd b = ldlt.solve(XtWz);
			VectorXd r = z - Xm * b;
			double wrss = (w.array() * r.array().square()).sum();
			MatrixXd Z = ldlt.solve(XtWX);
			double edf = Z.trace();
			double denom = (double)n - edf;
			double g = (denom > 0.5) ? (double)n * wrss / (denom * denom) : 1e30;

			if (g < best_gcv)
			{
				best_gcv = g;
				best_lam = lam;
			}
		}
		lambda = best_lam;

		// Solve at optimal λ
		MatrixXd M = XtWX + lambda * Sm;
		LDLT<MatrixXd> ldlt(M);
		VectorXd beta_new = ldlt.solve(XtWz);

		// Check convergence
		double delta = (beta_new - beta).norm() / (beta.norm() + 1e-10);
		beta = beta_new;
		eta = Xm * beta;
		if (has_off) eta += off;
		mu = fam.linkinv(eta);

		if (delta < 1e-6)
		{
			converged = true;
			break;
		}

		if (iter % 5 == 0 && progress)
			progress(iter, max_iter);
	}

	if (progress) progress(max_iter, max_iter);

	// ── Final quantities at convergence ──────────────────────────

	VectorXd mu_eta_vec = fam.mu_eta(mu);
	VectorXd var_vec = fam.variance(mu);
	VectorXd w(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double dmu = std::max(std::abs(mu_eta_vec[i]), 1e-10);
		double v = std::max(var_vec[i], 1e-10);
		w[i] = dmu * dmu / v;
	}

	MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
	MatrixXd M = XtWX + lambda * Sm;
	LDLT<MatrixXd> ldlt(M);
	MatrixXd Minv = ldlt.solve(MatrixXd::Identity(p, p));
	MatrixXd Minv_XtWX = ldlt.solve(XtWX);
	double edf_total = Minv_XtWX.trace();

	// Bayesian posterior covariance: Vp = (X'WX + λS)⁻¹
	// Matches mgcv's default. See penalized_lm for rationale.
	MatrixXd Vb = Minv;

	// ── Per-smooth EDF and F-test ────────────────────────────────

	Array<Model::SmoothResult> smooth_results;

	for (auto &sr : smooth_ranges)
	{
		Model::SmoothResult sm;
		sm.variable = sr.variable;
		sm.by = sr.by;
		sm.basis = sr.basis;
		sm.k = sr.k;
		sm.col_start = sr.col_start;
		sm.col_count = sr.col_count;

		sm.edf = 0;
		for (intptr_t j = sr.col_start; j < sr.col_start + sr.col_count; j++) {
			sm.edf += Minv_XtWX(j, j);
		}
		sm.ref_df = (sr.basis == "re") ? (double)sr.col_count : sm.edf;

		if (sm.edf > 0.001)
		{
			VectorXd beta_s = beta.segment(sr.col_start, sr.col_count);
			MatrixXd Vb_s = Vb.block(sr.col_start, sr.col_start, sr.col_count, sr.col_count);

			SelfAdjointEigenSolver<MatrixXd> es(Vb_s);
			VectorXd evals = es.eigenvalues();
			MatrixXd evecs = es.eigenvectors();
			double tol = evals.maxCoeff() * 1e-10;
			VectorXd evals_inv = VectorXd::Zero(sr.col_count);
			for (intptr_t j = 0; j < sr.col_count; j++) {
				if (evals[j] > tol) evals_inv[j] = 1.0 / evals[j];
			}
			double quad = (evecs.transpose() * beta_s).array().square().matrix().dot(evals_inv);
			sm.F_stat = quad / sm.ref_df;

			double resid_df = std::max(1.0, (double)n - edf_total);
			try {
				boost::math::fisher_f fdist(sm.ref_df, resid_df);
				sm.p_value = 1.0 - boost::math::cdf(fdist, sm.F_stat);
			} catch (...) {
				sm.p_value = 0;
			}
		}

		smooth_results.append(std::move(sm));
	}

	// ── Build Model ──────────────────────────────────────────────

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
	store_matrices(model, y, X);
	if (has_off) model.offset = offset;

	// Store full variance-covariance matrix
	store_vcov(model, Vb);

	model.beta = Array<double>(p, 0.0);
	model.se = Array<double>(p, 0.0);
	model.stat = Array<double>(p, 0.0);
	model.p = Array<double>(p, 0.0);

	for (intptr_t j = 0; j < p; j++)
	{
		model.beta[j + 1] = beta[j];
		model.se[j + 1] = std::sqrt(std::max(Vb(j, j), 0.0));
	}

	// z-statistics and p-values for parametric terms
	boost::math::chi_squared chisq_dist(1);
	for (intptr_t j = 0; j < n_parametric; j++)
	{
		if (model.se[j + 1] > 0)
		{
			model.stat[j + 1] = model.beta[j + 1] / model.se[j + 1];
			double wald = model.stat[j + 1] * model.stat[j + 1];
			model.p[j + 1] = 1.0 - boost::math::cdf(chisq_dist, wald);
		}
	}

	// Fitted values and residuals
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);
	for (intptr_t i = 0; i < n; i++)
	{
		model.fitted[i + 1] = mu[i];
		model.residuals[i + 1] = ym[i] - mu[i];
	}

	model.nobs = n;
	model.nfixed = p;
	model.loglik = fam.loglik(ym, mu);
	model.aic = -2.0 * model.loglik + 2.0 * edf_total;
	model.bic = -2.0 * model.loglik + std::log((double)n) * edf_total;
	model.deviance = -2.0 * model.loglik;
	model.niter = iter;
	model.converged = converged;
	model.smooth_terms = std::move(smooth_results);

	return model;
}

} // namespace phonometrica::stats
