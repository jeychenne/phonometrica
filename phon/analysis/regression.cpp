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

// =====================================================================
// Linear model (OLS)
// =====================================================================

Model lm(const Array<double> &y, const Array<double> &X)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t m = X.ncol();
	intptr_t n = X.nrow();

	Model model;
	model.family = "gaussian";
	model.link = "identity";
	store_matrices(model, y, X);

	// Solve via SVD
	model.beta = Array<double>(m, 0.0);
	Map<Matrix<double>> X1(const_cast<double*>(X.data()), n, m);
	Map<Vector<double>> y1(const_cast<double*>(y.data()), n);
	Map<Vector<double>> b1(model.beta.data(), m);

	BDCSVD<Matrix<double>, ComputeThinU | ComputeThinV> svd(X1);
	b1 = svd.solve(y1);

	// Fitted values and residuals
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);

	for (intptr_t i = 1; i <= n; i++)
	{
		double val = 0.0;
		for (intptr_t j = 1; j <= m; j++) {
			val += X(i, j) * model.beta[j];
		}
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
                       const Family &fam, const Eigen::VectorXd &beta, Eigen::VectorXd &grad)
{
	intptr_t n = X.nrow();
	intptr_t m = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, m);
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);

	Vector<double> eta = Xm * beta;
	Vector<double> mu = fam.linkinv(eta);

	grad = (Xm.transpose() * (mu - ym)).array() / n;

	return -fam.loglik(ym, mu) / n;
}


// Model-based covariance: (X'WX)^{-1}
static Matrix<double> glm_covariance(const Array<double> &X, const Family &fam,
                                      const Eigen::VectorXd &beta)
{
	intptr_t n = X.nrow();
	intptr_t m = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, m);

	Vector<double> eta = Xm * beta;
	Vector<double> mu = fam.linkinv(eta);
	Vector<double> W = fam.variance(mu);

	return (Xm.transpose() * W.asDiagonal() * Xm).inverse();
}


// Sandwich (robust) covariance: (X'WX)^{-1} X'diag(e²)X (X'WX)^{-1}
static Matrix<double> glm_robust_covariance(const Array<double> &y, const Array<double> &X,
                                             const Family &fam, const Eigen::VectorXd &beta)
{
	intptr_t n = X.nrow();
	intptr_t m = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, m);
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);

	Vector<double> eta = Xm * beta;
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


Model glm(const Array<double> &y, const Array<double> &X, const Family &fam, bool robust, int max_iter)
{
	using namespace LBFGSpp;

	validate_inputs(y, X);

	intptr_t m = X.ncol();
	intptr_t n = X.nrow();

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
	store_matrices(model, y, X);

	// L-BFGS optimization
	Eigen::VectorXd weights = Eigen::VectorXd::Zero(m);
	LBFGSParam<double> param;
	param.epsilon = 1e-6;
	param.max_iterations = max_iter;
	LBFGSSolver<double> solver(param);

	auto cost = [&](const Eigen::VectorXd &b, Eigen::VectorXd &grad)
	{
		return glm_cost(y, X, fam, b, grad);
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
		cov = glm_robust_covariance(y, X, fam, weights);
	} else {
		cov = glm_covariance(X, fam, weights);
	}

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

Model logit(const Array<double> &y, const Array<double> &X, int max_iter)
{
	for (auto value : y)
	{
		if (value != 0 && value != 1) {
			throw error("Response array can only contain the values 0 and 1");
		}
	}

	return glm(y, X, Family::binomial(), false, max_iter);
}


Model poisson(const Array<double> &y, const Array<double> &X, bool robust, int max_iter)
{
	return glm(y, X, Family::poisson(), robust, max_iter);
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

Model negbin(const Array<double> &y, const Array<double> &X, int max_iter)
{
	validate_inputs(y, X);

	intptr_t n = X.nrow();
	intptr_t p = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, p);
	Eigen::Map<Vector<double>> ym(const_cast<double*>(y.data()), n);

	// ── Initial θ from method-of-moments ─────────────────────────────
	//
	// E[Y] = μ, Var(Y) = μ + μ²/θ  →  θ = μ² / (Var(Y) − μ)
	// Use sample mean and variance as plug-in estimators.

	double ybar = ym.mean();
	double yvar = (ym.array() - ybar).square().sum() / (n - 1);
	double theta;
	if (yvar > ybar) {
		theta = ybar * ybar / (yvar - ybar);
	} else {
		theta = 10.0; // low overdispersion fallback
	}
	theta = std::clamp(theta, 0.01, 1e6);

	// ── Initial β from Poisson GLM (quick starting point) ────────────

	Eigen::VectorXd beta = Eigen::VectorXd::Zero(p);
	{
		auto pois = glm(y, X, Family::poisson(), false, 50);
		for (intptr_t j = 0; j < p; j++) {
			beta[j] = pois.beta[j + 1];
		}
	}

	// ── Precompute X'X factorisation for IWLS solve ──────────────────

	// (Recomputed each iteration with weights, but we keep the structure)

	int outer_iter = 0;
	bool converged = false;

	for (int iter = 0; iter < max_iter; iter++)
	{
		Eigen::VectorXd beta_old = beta;
		double theta_old = theta;

		// ── (a) IWLS for β given θ ───────────────────────────────────
		//
		// Iterate IWLS until β converges (typically 3–8 iterations).

		for (int iwls = 0; iwls < 50; iwls++)
		{
			Eigen::VectorXd eta = Xm * beta;
			Eigen::VectorXd mu = eta.array().exp().matrix();

			// Working weights and response for log link + NB variance
			Eigen::VectorXd w(n), z(n);
			for (intptr_t i = 0; i < n; i++)
			{
				double mi = std::max(mu[i], 1e-10);
				// w_i = (dμ/dη)² / V(μ) = μ² / (μ + μ²/θ) = μθ/(θ+μ)
				w[i] = mi * theta / (theta + mi);
				// z_i = η_i + (y_i − μ_i) / (dμ/dη) = η_i + (y_i − μ_i) / μ_i
				z[i] = eta[i] + (ym[i] - mi) / mi;
			}

			// Solve (X'WX) β = X'Wz
			Eigen::MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
			Eigen::VectorXd XtWz = Xm.transpose() * (w.array() * z.array()).matrix();

			Eigen::LDLT<Eigen::MatrixXd> ldlt(XtWX);
			Eigen::VectorXd beta_new = ldlt.solve(XtWz);

			double max_change = (beta_new - beta).cwiseAbs().maxCoeff();
			beta = beta_new;

			if (max_change < 1e-8) break;
		}

		// ── (b) Update θ given β (1D Newton on profile log-likelihood) ──
		//
		// ℓ(θ) = Σ [lgamma(y_i+θ) − lgamma(θ) + θ log(θ/(θ+μ_i))
		//         + y_i log(μ_i/(θ+μ_i)) − lgamma(y_i+1)]
		//
		// Score:     s = Σ [digamma(y_i+θ) − digamma(θ) + log(θ/(θ+μ_i)) + 1 − (y_i+θ)/(θ+μ_i)]
		// Info (−s'): I = Σ [−trigamma(y_i+θ) + trigamma(θ) − 1/θ + 2/(θ+μ_i) − (y_i+θ)/(θ+μ_i)²]

		Eigen::VectorXd eta = Xm * beta;
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
		Eigen::VectorXd mu_vec = (Xm * beta).array().exp().matrix();
		Eigen::VectorXd w(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double mi = std::max(mu_vec[i], 1e-10);
			w[i] = mi * theta / (theta + mi);
		}
		Eigen::MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
		Eigen::MatrixXd cov = XtWX.inverse();

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
                   FittingCallback progress)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t n = y.size();
	intptr_t p = X.ncol();

	Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Map<Vector<double>> ym(const_cast<double *>(y.data()), n);
	Map<Matrix<double>> Sm(const_cast<double *>(S.data()), p, p);

	MatrixXd XtX = Xm.transpose() * Xm;
	VectorXd Xty = Xm.transpose() * ym;

	// ── GCV grid search for optimal λ ────────────────────────────

	double best_log_lambda = 0;
	double best_gcv = std::numeric_limits<double>::max();

	// Total steps: coarse grid (141) + fine grid (31) = 172.
	int progress_step = 0;
	constexpr int total_steps = 172;

	// Coarse grid: log10(λ) from -7 to 7
	for (int k = -70; k <= 70; k++)
	{
		double log_lam = 0.1 * k;
		double lam = std::pow(10.0, log_lam);
		auto [g, edf, rss] = gcv_score(XtX, Xty, Sm, Xm, ym, lam, n, p);
		if (g < best_gcv)
		{
			best_gcv = g;
			best_log_lambda = log_lam;
		}
		if (++progress_step % 5 == 0 && progress)
			progress(progress_step, total_steps);
	}

	// Fine grid: refine around the best
	double lo = best_log_lambda - 0.15;
	double hi = best_log_lambda + 0.15;
	for (int k = 0; k <= 30; k++)
	{
		double log_lam = lo + (hi - lo) * k / 30.0;
		double lam = std::pow(10.0, log_lam);
		auto [g, edf, rss] = gcv_score(XtX, Xty, Sm, Xm, ym, lam, n, p);
		if (g < best_gcv)
		{
			best_gcv = g;
			best_log_lambda = log_lam;
		}
		if (++progress_step % 5 == 0 && progress)
			progress(progress_step, total_steps);
	}

	if (progress) progress(total_steps, total_steps);

	double lambda_opt = std::pow(10.0, best_log_lambda);

	// ── Fit at optimal λ ─────────────────────────────────────────

	MatrixXd M = XtX + lambda_opt * Sm;
	LLT<MatrixXd> llt(M);
	VectorXd beta_vec = llt.solve(Xty);
	VectorXd resid = ym - Xm * beta_vec;
	double rss = resid.squaredNorm();

	// EDF and influence matrix: A = (X'X + λS)⁻¹ X'X
	MatrixXd Minv_XtX = llt.solve(XtX);
	double edf_total = Minv_XtX.trace();

	// Bayesian covariance: Vβ = σ² (X'X + λS)⁻¹ X'X (X'X + λS)⁻¹
	double sigma2 = rss / ((double)n - edf_total);
	if (sigma2 <= 0) sigma2 = 1e-10;
	MatrixXd Minv = llt.solve(MatrixXd::Identity(p, p));
	MatrixXd Vb = sigma2 * Minv * XtX * Minv;

	// ── Build Model ──────────────────────────────────────────────

	Model model;
	model.family = "gaussian";
	model.link = "identity";
	store_matrices(model, y, X);

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
		sm.basis = sr.basis;
		sm.k = sr.k;
		sm.col_start = sr.col_start;
		sm.col_count = sr.col_count;

		// EDF: sum of diagonal of influence matrix for this smooth's columns.
		sm.edf = 0;
		for (intptr_t j = sr.col_start; j < sr.col_start + sr.col_count; j++) {
			sm.edf += Minv_XtX(j, j);
		}

		// Reference df: use EDF (Wood 2013b uses a correction, but EDF is adequate).
		sm.ref_df = sm.edf;

		// F-test: β_s' Vb_s⁻¹ β_s / edf_s, where β_s and Vb_s are the sub-blocks.
		if (sm.edf > 0.001)
		{
			VectorXd beta_s = beta_vec.segment(sr.col_start, sr.col_count);
			MatrixXd Vb_s = Vb.block(sr.col_start, sr.col_start, sr.col_count, sr.col_count);

			// Use pseudoinverse via eigendecomposition for numerical stability.
			SelfAdjointEigenSolver<MatrixXd> es(Vb_s);
			VectorXd evals = es.eigenvalues();
			MatrixXd evecs = es.eigenvectors();
			double tol = evals.maxCoeff() * 1e-10;
			VectorXd evals_inv = VectorXd::Zero(sr.col_count);
			for (intptr_t j = 0; j < sr.col_count; j++) {
				if (evals[j] > tol) {
					evals_inv[j] = 1.0 / evals[j];
				}
			}
			double quad = (evecs.transpose() * beta_s).array().square().matrix().dot(evals_inv);
			sm.F_stat = quad / sm.ref_df;

			// p-value from F(ref_df, residual_df) distribution.
			double resid_df = std::max(1.0, (double)n - edf_total);
			try {
				boost::math::fisher_f fdist(sm.ref_df, resid_df);
				sm.p_value = 1.0 - boost::math::cdf(fdist, sm.F_stat);
			} catch (...) {
				sm.p_value = 0; // extreme case
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
		model.fitted[i + 1] = yhat;
		model.residuals[i + 1] = ym[i] - yhat;
	}

	// Fit statistics
	model.rse = std::sqrt(sigma2);
	model.df_residual = std::max((intptr_t)1, (intptr_t)std::round((double)n - edf_total));
	model.nobs = n;
	model.nfixed = p;

	// Log-likelihood (Gaussian profile)
	model.loglik = -0.5 * n * (std::log(2.0 * M_PI) + std::log(sigma2) + 1.0);
	model.aic = -2.0 * model.loglik + 2.0 * edf_total;
	model.bic = -2.0 * model.loglik + std::log((double)n) * edf_total;
	model.deviance = rss;

	// R² and adjusted R²
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


// =====================================================================
// Penalized GLM (non-Gaussian GAM) via Penalized IRLS with GCV
// =====================================================================

Model penalized_glm(const Array<double> &y, const Array<double> &X,
                    const Array<double> &S, const Family &fam,
                    intptr_t n_parametric,
                    const std::vector<SmoothColumnRange> &smooth_ranges,
                    FittingCallback progress,
                    int max_iter)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t n = y.size();
	intptr_t p = X.ncol();

	Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Map<Vector<double>> ym(const_cast<double *>(y.data()), n);
	Map<Matrix<double>> Sm(const_cast<double *>(S.data()), p, p);

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
		VectorXd w(n), z(n);

		for (intptr_t i = 0; i < n; i++)
		{
			double dmu = std::max(std::abs(mu_eta_vec[i]), 1e-10);
			double v = std::max(var_vec[i], 1e-10);
			w[i] = dmu * dmu / v;
			z[i] = eta[i] + (ym[i] - mu[i]) / dmu;
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

	// Bayesian covariance
	MatrixXd Vb = Minv * XtWX * Minv;

	// ── Per-smooth EDF and F-test ────────────────────────────────

	Array<Model::SmoothResult> smooth_results;

	for (auto &sr : smooth_ranges)
	{
		Model::SmoothResult sm;
		sm.variable = sr.variable;
		sm.basis = sr.basis;
		sm.k = sr.k;
		sm.col_start = sr.col_start;
		sm.col_count = sr.col_count;

		sm.edf = 0;
		for (intptr_t j = sr.col_start; j < sr.col_start + sr.col_count; j++) {
			sm.edf += Minv_XtWX(j, j);
		}
		sm.ref_df = sm.edf;

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
