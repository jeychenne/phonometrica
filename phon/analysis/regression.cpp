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
#include <boost/math/quadrature/gauss_kronrod.hpp>
#include <boost/math/special_functions/digamma.hpp>
#include <boost/math/special_functions/trigamma.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/analysis/statistics.hpp>
#include <phon/utils/matrix.hpp>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <optional>
#include <string>

namespace phonometrica::stats {

// =====================================================================
// Diagnostic env-var: PHON_DIAG_FIXED_LOG_LAMBDA
// =====================================================================
//
// When set, this env-var overrides the GCV smoothing-parameter search in
// penalized_lm and penalized_glm. Value is a comma-separated list of
// log10(λ) values. penalized_lm expects exactly K values (one per
// penalty block); penalized_glm uses only the first value (it carries a
// single global λ). On parse failure or count mismatch, a warning is
// printed to stderr and the override is ignored.
//
// Same gating discipline as PHON_DIAG_LAPLACE: read once at fitter entry,
// no perf cost when unset.
static std::optional<std::vector<double>> read_diag_fixed_log_lambda()
{
	const char *env = std::getenv("PHON_DIAG_FIXED_LOG_LAMBDA");
	if (!env || !*env) return std::nullopt;
	std::vector<double> out;
	std::string s = env;
	size_t pos = 0;
	while (pos <= s.size())
	{
		size_t comma = s.find(',', pos);
		size_t end = (comma == std::string::npos) ? s.size() : comma;
		std::string tok = s.substr(pos, end - pos);
		// trim whitespace
		while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front()))) tok.erase(tok.begin());
		while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back())))  tok.pop_back();
		if (tok.empty())
		{
			std::fprintf(stderr,
				"[PHON_DIAG_FIXED_LOG_LAMBDA] empty token in '%s'; ignoring override.\n", env);
			return std::nullopt;
		}
		char *endp = nullptr;
		double v = std::strtod(tok.c_str(), &endp);
		if (endp == tok.c_str() || *endp != '\0')
		{
			std::fprintf(stderr,
				"[PHON_DIAG_FIXED_LOG_LAMBDA] invalid value '%s'; ignoring override.\n", tok.c_str());
			return std::nullopt;
		}
		out.push_back(v);
		if (comma == std::string::npos) break;
		pos = comma + 1;
	}
	return out;
}

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
			model.vcov(i, j) = cov(i, j);
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
	Map<ColVector<double>> y1(const_cast<double*>(y.data()), n);
	ColVector<double> y_work = y1;
	if (!offset.empty()) {
		Map<const ColVector<double>> off(offset.data(), n);
		y_work -= off;
	}

	// Solve via SVD
	model.beta = Array<double>(m, 0.0);
	Map<Matrix<double>> X1(const_cast<double*>(X.data()), n, m);
	Map<ColVector<double>> b1(model.beta.data(), m);

	BDCSVD<Matrix<double>, ComputeThinU | ComputeThinV> svd(X1);
	b1 = svd.solve(y_work);

	// Fitted values and residuals
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);

	for (intptr_t i = 0; i < n; i++)
	{
		double val = 0.0;
		for (intptr_t j = 0; j < m; j++) {
			val += X(i, j) * model.beta[j];
		}
		if (!offset.empty()) val += offset[i];
		model.fitted[i] = val;
	}

	// Residual variance
	intptr_t df = n - m;
	model.df_residual = df;
	long double sse = 0.0;

	for (intptr_t i = 0; i < n; i++)
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

	for (intptr_t i = 0; i < m; i++)
	{
		model.se[i] = sqrt(rv * var(i, i));
		model.stat[i] = model.beta[i] / model.se[i];
		model.p[i] = 2 * (1 - cdf(dist, std::abs(model.stat[i])));
	}

	// Store full variance-covariance matrix: σ²(X'X)⁻¹
	store_vcov(model, static_cast<double>(rv) * var);

	// R²
	double ybar = mean(y);
	long double ssr = 0.0;
	long double sst = 0.0;

	for (intptr_t i = 0; i < n; i++)
	{
		ssr += model.residuals[i] * model.residuals[i];
		sst += (y[i] - ybar) * (y[i] - ybar);
	}
	model.r2 = 1 - double(ssr / sst);
	int np = (int)(m - 1); // number of predictors (excluding intercept)
	model.adj_r2 = 1 - (1 - model.r2) * (double(n - 1) / (n - np - 1));

	// Log-likelihood (Gaussian profile log-likelihood)
	Map<ColVector<double>> y_eig(const_cast<double*>(y.data()), n);
	Map<ColVector<double>> mu_eig(model.fitted.data(), n);
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
// Model-based covariance: (X'WX)^{-1}
static Matrix<double> glm_covariance(const Array<double> &X, const Family &fam,
                                      const Eigen::VectorXd &beta,
                                      const Array<double> &offset)
{
	intptr_t n = X.nrow();
	intptr_t m = X.ncol();

	Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), n, m);

	ColVector<double> eta = Xm * beta;
	if (!offset.empty()) {
		Eigen::Map<const ColVector<double>> off(offset.data(), n);
		eta += off;
	}
	ColVector<double> mu = fam.linkinv(eta);
	ColVector<double> W = fam.variance(mu);

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
	Eigen::Map<ColVector<double>> ym(const_cast<double*>(y.data()), n);

	ColVector<double> eta = Xm * beta;
	if (!offset.empty()) {
		Eigen::Map<const ColVector<double>> off(offset.data(), n);
		eta += off;
	}
	ColVector<double> mu = fam.linkinv(eta);
	ColVector<double> W = fam.variance(mu);

	ColVector<double> e2(n);
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
	validate_inputs(y, X);

	intptr_t m = X.ncol();
	intptr_t n = X.nrow();

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
	store_matrices(model, y, X);
	if (!offset.empty()) model.offset = offset;

	// Eigen views over the input arrays
	Eigen::Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, m);
	Eigen::Map<ColVector<double>> ym(const_cast<double *>(y.data()), n);
	const bool has_off = !offset.empty();
	Eigen::VectorXd off_eig;
	if (has_off) {
		off_eig = Eigen::Map<const ColVector<double>>(offset.data(), n);
	}

	// Diagnostic flag (PHON_DIAG_IRLS=1) — one-shot env-var read,
	// matches the PHON_DIAG_LAPLACE / PHON_DIAG_PIRLS idiom in
	// mixed_model.cpp. Zero cost when off.
	static const bool diag_irls_on = []() {
		const char *e = std::getenv("PHON_DIAG_IRLS");
		return e && e[0] && e[0] != '0';
	}();

	// ── R-style data-informed initialization ────────────────────────
	//
	// The previous L-BFGS path cold-started at β=0, which combined
	// with L-BFGS's quasi-Newton Hessian approximation makes the
	// solver sensitive to predictor magnitude ratios (lowbwt with
	// AGE≈25, LWT≈130 hits max_iterations without converging).
	//
	// IRLS itself is scale-invariant (the Hessian X'WX rescales
	// with X), so the predictor-magnitude problem goes away. But
	// we follow R's `mustart` idiom anyway: start from a data-
	// informed μ, take its link-scale, then regress on X to get
	// β₀. Reduces typical IRLS iteration count by 1–2 and matches
	// what users debugging against stats::glm() expect.
	//
	//   Gaussian: μ_start = y                       (identity link)
	//   Binomial: μ_start = (y + 0.5) / 2           (avoids 0/1 boundary)
	//   Poisson:  μ_start = y + 0.1                 (avoids log(0))
	//
	// Falls back to β=0 if X'X is singular (perfectly collinear
	// predictors); IRLS will then either converge from β=0 or fail
	// the singular-X'WX check below.
	//
	// custom_weights families (Student-t) and dispersion-parameter
	// families (NB, Beta) never reach glm() — they route through
	// mixed_model_multistart in fit_impl (see fitting.cpp:1378
	// "routes through mixed_model_multistart() with empty groups").
	// So we only handle canonical-link families here.
	Eigen::VectorXd mu_start(n);
	if (fam.name == "binomial") {
		for (intptr_t i = 0; i < n; i++) mu_start[i] = (ym[i] + 0.5) / 2.0;
	}
	else if (fam.name == "poisson") {
		for (intptr_t i = 0; i < n; i++) mu_start[i] = ym[i] + 0.1;
	}
	else {
		// gaussian and any other identity-link family routed here
		mu_start = ym;
	}
	Eigen::VectorXd eta_start = fam.link(mu_start);
	if (has_off) eta_start -= off_eig;

	Eigen::VectorXd beta;
	{
		Eigen::MatrixXd XtX = Xm.transpose() * Xm;
		Eigen::VectorXd XtEta = Xm.transpose() * eta_start;
		Eigen::LDLT<Eigen::MatrixXd> ldlt(XtX);
		if (ldlt.info() == Eigen::Success) {
			beta = ldlt.solve(XtEta);
		}
		else {
			beta = Eigen::VectorXd::Zero(m);
		}
	}

	// ── Deviance at current iterate (for the line search) ──────────
	// Frequentist GLM: just −loglik. No β prior, no random-effect
	// prior. +∞ on any non-finite intermediate (e.g. μ saturating
	// → log 0 in loglik) so the step-halver treats overflow
	// identically to a rejected step.
	auto deviance_at = [&](const Eigen::VectorXd &b) -> double {
		Eigen::VectorXd eta = Xm * b;
		if (has_off) eta += off_eig;
		Eigen::VectorXd mu = fam.linkinv(eta);
		double d = -fam.loglik(ym, mu);
		return std::isfinite(d) ? d : std::numeric_limits<double>::infinity();
	};
	double dev_curr = deviance_at(beta);

	// ── IRLS with step-halving ────────────────────────────────────
	// One outer iteration:
	//   * compute η = Xβ + offset, μ = linkinv(η)
	//   * w_i  = (dμ/dη)² / V(μ)                  [IRLS weight]
	//   * z_i  = (η − offset)_i + (y_i − μ_i) / (dμ/dη)_i   [working response, β-space]
	//   * solve  (X'WX) β_proposed = X'Wz         [Newton step]
	//   * line-search (β + α·(β_proposed − β)), α ∈ {1, 0.5, …, 2⁻¹⁰}
	//   * accept on finite, non-catastrophic deviance change
	//
	// Step-halving uses the same acceptance machinery as solve_pirls
	// (see mixed_model.cpp). Generous slack absorbs FP-noise + Newton
	// quadratic-approximation oscillation near a flat optimum;
	// catastrophic overshoot (μ saturating → loglik = ±Inf) is caught
	// by the std::isfinite check in deviance_at and triggers halving.
	constexpr int MAX_HALVINGS = 10;
	int niter = 0;
	bool converged = false;

	for (int iter = 0; iter < max_iter; iter++)
	{
		niter = iter + 1;

		Eigen::VectorXd eta = Xm * beta;
		if (has_off) eta += off_eig;
		Eigen::VectorXd mu = fam.linkinv(eta);
		Eigen::VectorXd mu_eta_v = fam.mu_eta(mu);
		Eigen::VectorXd V = fam.variance(mu);

		Eigen::VectorXd w(n), z(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double me = mu_eta_v[i];
			double v = V[i];
			// μ_η and V both → 0 at the boundary of μ's support
			// (binomial μ=0/1; Poisson μ=0). For canonical-link
			// families the algebraic limit of w·z is finite, but
			// the intermediate z would be ±∞ from y/0. We zero
			// the contribution of degenerate observations and let
			// step-halving / next iteration correct.
			if (std::abs(me) < 1e-15 || v < 1e-30) {
				w[i] = 0;
				z[i] = eta[i] - (has_off ? off_eig[i] : 0.0);
			}
			else {
				w[i] = (me * me) / v;
				z[i] = eta[i] - (has_off ? off_eig[i] : 0.0)
				     + (ym[i] - mu[i]) / me;
			}
		}

		Eigen::MatrixXd XtWX = Xm.transpose() * w.asDiagonal() * Xm;
		Eigen::VectorXd XtWz = Xm.transpose() * (w.array() * z.array()).matrix();

		Eigen::LDLT<Eigen::MatrixXd> ldlt(XtWX);
		if (ldlt.info() != Eigen::Success) {
			// X'WX singular — perfect/quasi-separation or exact
			// collinearity. R's glm() reports "did not converge"
			// here; we do the same and return β at the last
			// accepted iterate. The caller decides what to do
			// (typically: report converged=false, refuse to use
			// SEs, suggest dropping a predictor).
			converged = false;
			break;
		}
		Eigen::VectorXd beta_proposed = ldlt.solve(XtWz);

		Eigen::VectorXd d_beta = beta_proposed - beta;
		double max_change_full = d_beta.cwiseAbs().maxCoeff();

		// ── Step-halving line search ────────────────────────────
		double alpha = 1.0;
		int halvings = 0;
		bool accepted = false;
		Eigen::VectorXd beta_try;
		double dev_try = 0;
		for (; halvings <= MAX_HALVINGS; halvings++)
		{
			beta_try = beta + alpha * d_beta;
			dev_try = deviance_at(beta_try);
			constexpr double LS_TOL_ABS = 1e-3;
			constexpr double LS_TOL_REL = 1e-6;
			double ls_slack = std::max(LS_TOL_ABS,
			                            LS_TOL_REL * std::abs(dev_curr));
			if (std::isfinite(dev_try) && (dev_try - dev_curr) < ls_slack)
			{
				accepted = true;
				break;
			}
			alpha *= 0.5;
		}

		if (diag_irls_on) {
			std::fprintf(stderr,
			    "[irls_diag] iter=%d alpha=%.6g halvings=%d "
			    "dev_curr=%.6f dev_try=%.6f max_change=%.6g accepted=%d\n",
			    iter, alpha, halvings, dev_curr, dev_try,
			    max_change_full, (int)accepted);
		}

		if (!accepted) {
			// 10 halvings exhausted — solver stuck. β stays at
			// the last accepted iterate. Same propagation as the
			// singular-X'WX branch above.
			converged = false;
			break;
		}

		beta = beta_try;
		dev_curr = dev_try;

		if (max_change_full < 1e-8) {
			converged = true;
			break;
		}
	}

	model.niter = niter;
	model.converged = converged;

	// ── Coefficients ──
	model.beta = Array<double>(m, 0.0);
	std::copy(beta.data(), beta.data() + m, model.beta.data());

	// ── Variance-covariance matrix ──
	Matrix<double> cov;
	if (robust) {
		cov = glm_robust_covariance(y, X, fam, beta, offset);
	}
	else {
		cov = glm_covariance(X, fam, beta, offset);
	}

	store_vcov(model, cov);

	// ── Standard errors, Wald statistics, p-values ──
	model.se = Array<double>(m, 0.0);
	for (intptr_t i = 0; i < m; i++) {
		model.se[i] = sqrt(cov(i, i));
	}

	model.stat = Array<double>(m, 0.0);
	for (intptr_t i = 0; i < m; i++) {
		model.stat[i] = model.beta[i] / model.se[i];
	}

	boost::math::chi_squared dist(1);
	model.p = Array<double>(m, 0.0);
	for (intptr_t i = 0; i < m; i++)
	{
		auto wald = (model.beta[i] * model.beta[i]) / cov(i, i);
		model.p[i] = 1 - boost::math::cdf(dist, wald);
	}

	// ── Fitted values, log-likelihood, information criteria ──
	model.compute_fitted(fam.linkinv);
	Eigen::Map<ColVector<double>> mu_eig(model.fitted.data(), n);
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
	Eigen::Map<ColVector<double>> ym(const_cast<double*>(y.data()), n);

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
			beta[j] = pois.beta[j];
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
		model.beta[j] = beta[j];
	}

	// Fitted values
	model.compute_fitted(fam.linkinv);

	// Log-likelihood
	Eigen::Map<ColVector<double>> mu_eig(model.fitted.data(), n);
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
		for (intptr_t i = 0; i < p; i++)
		{
			model.se[i] = std::sqrt(std::max(cov(i, i), 0.0));
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
	Eigen::Map<ColVector<double>> ym(const_cast<double*>(y.data()), n);

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
			beta[j] = logit_fit.beta[j];
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
		model.beta[j] = beta[j];
	}

	// Fitted values
	model.compute_fitted(fam.linkinv);

	// Log-likelihood
	Eigen::Map<ColVector<double>> mu_eig(model.fitted.data(), n);
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
		for (intptr_t i = 0; i < p; i++)
		{
			model.se[i] = std::sqrt(std::max(cov(i, i), 0.0));
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

// Search range for log10(λ), shared by the GAM engines. With penalties
// scaled as in mgcv (scale.penalty) the optimum grows roughly with n, and
// smooths at their linear limit have very large optima (mgcv reports
// log10(sp) > 10 for some), hence the asymmetric range. The upper bound also
// stops the searches from drifting on rounding noise: as λ → ∞ the criteria
// flatten out, and under REML log|X'WX + S_λ| − log|S_λ|₊ only cancels to
// within rounding error, while (X'WX + S_λ) becomes too ill-conditioned for
// an accurate EDF. At 10¹² a smooth is already a straight line (EDF = null
// space dimension to 4 decimals on every model in the test suite).
static constexpr double LOG_LAMBDA_MIN = -5.0;
static constexpr double LOG_LAMBDA_MAX = 12.0;

// Nelder–Mead minimization of f from x0 (with f(x0) = f0), initial simplex
// step `step` along each axis. Stops when the simplex is smaller than
// x_tol and its values agree to f_rtol, or after max_iter iterations.
// Used to polish the smoothing-parameter searches: the coordinate searches
// stop at a 0.05 resolution in log10(λ), and their coordinates can be
// strongly correlated. f is expected to record its best point itself.
static void nelder_mead(const std::function<double(const Eigen::VectorXd &)> &f,
                        const Eigen::VectorXd &x0, double f0, double step,
                        double x_tol = 1e-3, double f_rtol = 1e-9, int max_iter = 300)
{
	using Eigen::VectorXd;
	const intptr_t d = x0.size();
	if (d == 0) return;
	std::vector<VectorXd> simplex(d + 1, x0);
	std::vector<double> fv(d + 1);
	fv[0] = f0;
	for (intptr_t j = 0; j < d; j++)
	{
		simplex[j + 1][j] += step;
		fv[j + 1] = f(simplex[j + 1]);
	}
	for (int it = 0; it < max_iter; it++)
	{
		std::vector<intptr_t> order(d + 1);
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [&](intptr_t a, intptr_t b) { return fv[a] < fv[b]; });
		std::vector<VectorXd> sx;
		std::vector<double> sf;
		for (auto o : order) { sx.push_back(simplex[o]); sf.push_back(fv[o]); }
		simplex = std::move(sx);
		fv = std::move(sf);

		double size = 0;
		for (intptr_t j = 1; j <= d; j++) size = std::max(size, (simplex[j] - simplex[0]).cwiseAbs().maxCoeff());
		if (size < x_tol && std::abs(fv[d] - fv[0]) <= f_rtol * (1.0 + std::abs(fv[0]))) break;

		VectorXd centroid = VectorXd::Zero(d);
		for (intptr_t j = 0; j < d; j++) centroid += simplex[j];
		centroid /= (double) d;
		VectorXd xr = centroid + (centroid - simplex[d]);
		double fr = f(xr);
		if (fr < fv[0])
		{
			VectorXd xe = centroid + 2.0 * (centroid - simplex[d]);
			double fe = f(xe);
			if (fe < fr) { simplex[d] = xe; fv[d] = fe; } else { simplex[d] = xr; fv[d] = fr; }
		}
		else if (fr < fv[d - 1])
		{
			simplex[d] = xr; fv[d] = fr;
		}
		else
		{
			VectorXd xc = centroid + 0.5 * (simplex[d] - centroid);
			double fc = f(xc);
			if (fc < fv[d]) { simplex[d] = xc; fv[d] = fc; }
			else
			{
				for (intptr_t j = 1; j <= d; j++)
				{
					simplex[j] = simplex[0] + 0.5 * (simplex[j] - simplex[0]);
					fv[j] = f(simplex[j]);
				}
			}
		}
	}
}

// Rank and log pseudo-determinant of each penalty block S_j (λ_j = 1).
//
// Every smooth (or by-factor level) penalizes its own columns only, so
// S_λ = Σ_j λ_j S_j is block diagonal and
//   log|S_λ|₊ = Σ_j ( r_j log λ_j + log|S_j|₊ ),
// which the REML criterion needs at every candidate λ. Computing r_j and
// log|S_j|₊ once avoids an eigendecomposition per evaluation.
struct PenaltyBlockDet
{
	intptr_t rank = 0;
	double logdet = 0;   // log|S_j|₊ (product of the positive eigenvalues)
};

static std::vector<PenaltyBlockDet>
penalty_block_dets(const Eigen::MatrixXd &S, const std::vector<SmoothColumnRange> &smooth_ranges)
{
	std::vector<PenaltyBlockDet> out;
	for (auto &sr : smooth_ranges)
	{
		Eigen::MatrixXd Sj = S.block(sr.col_start, sr.col_start, sr.col_count, sr.col_count);
		Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(Sj, Eigen::EigenvaluesOnly);
		auto ev = es.eigenvalues();
		double tol = std::max(ev.maxCoeff(), 0.0) * 1e-10;
		PenaltyBlockDet d;
		for (intptr_t i = 0; i < ev.size(); i++)
		{
			if (ev[i] > tol) { d.rank++; d.logdet += std::log(ev[i]); }
		}
		out.push_back(d);
	}
	return out;
}

// log|S_λ|₊ for log10(λ) values ll (see penalty_block_dets).
static double log_pdet_S(const std::vector<PenaltyBlockDet> &dets, const std::vector<double> &ll)
{
	double v = 0;
	for (size_t j = 0; j < dets.size(); j++) {
		v += (double) dets[j].rank * ll[j] * M_LN10 + dets[j].logdet;
	}
	return v;
}

// Log-determinant of a symmetric positive definite matrix, or +∞ if the
// Cholesky factorization fails (not positive definite).
static double log_det_spd(const Eigen::MatrixXd &M)
{
	Eigen::LLT<Eigen::MatrixXd> llt(M);
	if (llt.info() != Eigen::Success) return std::numeric_limits<double>::infinity();
	return 2.0 * llt.matrixL().toDenseMatrix().diagonal().array().log().sum();
}


// ── Smooth-term significance tests (mgcv's summary.gam) ──────────────
//
// Spline smooths use Wood (2013a, Biometrika 100(1):221–228), as mgcv's
// testStat(): a pseudo-inverse of the smooth's covariance whose rank
// follows the EDF, with the fractional part of the rank handled by a
// rank-2 block, and p-values from the matching weighted sum of χ²
// (or its ratio with a χ²/df for an estimated scale). Random-effect smooths
// (bs = "re", no unpenalized null space) use the random-effect test of
// Wood (2013b, Biometrika 100(4):1005–1010), as mgcv's reTest().

// Upper tail P(Σ_j λ_j χ²_{h_j} > q) of a weighted sum of independent
// central χ² variables, by numerical inversion of the characteristic
// function (Imhof 1961):
//   P = ½ + (1/π) ∫₀^∞ sin θ(u) / (u ρ(u)) du
//   θ(u) = ½ Σ h_j atan(λ_j u) − ½ q u,   ρ(u) = Π (1 + λ_j² u²)^{h_j/4}.
// mgcv's psum.chisq() evaluates the same probability by Davies' (1980)
// algorithm with absolute accuracy 2e-5; this agrees with it to that
// accuracy and is accurate to about 1e-10 itself.
static double psum_chisq(double q, const std::vector<double> &lambda_in, const std::vector<double> &df_in)
{
	namespace bq = boost::math::quadrature;
	std::vector<double> lb, h;
	double lmax = 0;
	for (size_t j = 0; j < lambda_in.size(); j++)
	{
		if (lambda_in[j] != 0 && df_in[j] > 0)
		{
			lb.push_back(lambda_in[j]);
			h.push_back(df_in[j]);
			lmax = std::max(lmax, std::abs(lambda_in[j]));
		}
	}
	if (lb.empty()) return q < 0 ? 1.0 : 0.0;
	bool any_neg = false;
	for (double l : lb) any_neg |= (l < 0);
	if (!any_neg && q <= 0) return 1.0;

	auto theta = [&](double u) {
		double s = 0;
		for (size_t j = 0; j < lb.size(); j++) s += h[j] * std::atan(lb[j] * u);
		return 0.5 * (s - q * u);
	};
	auto dtheta = [&](double u) {
		double s = 0;
		for (size_t j = 0; j < lb.size(); j++) s += h[j] * lb[j] / (1 + lb[j] * lb[j] * u * u);
		return 0.5 * (s - q);
	};
	// Envelope 1/(u ρ(u)). Its log-slope −1 − s(u), s(u) = ½ Σ h λ²u²/(1+λ²u²),
	// steepens with u, so ∫_U^∞ envelope ≤ envelope(U) · U / s(U).
	auto envelope = [&](double u) {
		double lr = 0;
		for (size_t j = 0; j < lb.size(); j++) lr += 0.25 * h[j] * std::log1p(lb[j] * lb[j] * u * u);
		return std::exp(-lr) / u;
	};
	auto tail_bound = [&](double u) {
		double s = 0;
		for (size_t j = 0; j < lb.size(); j++) {
			double z = lb[j] * lb[j] * u * u;
			s += 0.5 * h[j] * z / (1 + z);
		}
		return s > 0 ? envelope(u) * u / s : HUGE_VAL;
	};
	auto f = [&](double u) {
		if (u <= 0) return dtheta(0.0);
		return std::sin(theta(u)) * envelope(u);
	};
	auto integrate = [&](double a, double b) {
		return bq::gauss_kronrod<double, 31>::integrate(f, a, b, 15, 1e-12);
	};
	auto prob = [](double integral) { return std::clamp(0.5 + integral / M_PI, 0.0, 1.0); };

	const double eps = 1e-12;
	double total = 0;

	if (q <= 0)
	{
		// No linear phase (the estimated-scale tests, where q = 0): the
		// integrand does not oscillate in the tail. Geometric segments.
		double a = 0, b = 1.0 / lmax;
		for (int it = 0; it < 200; it++)
		{
			total += integrate(a, b);
			if (tail_bound(b) < eps) break;
			a = b;
			b *= 2;
		}
		return prob(total);
	}

	// q > 0: θ eventually decreases like −q u/2, so the tail oscillates with
	// half-period → 2π/q and decays slowly when Σ h is small. Integrate up
	// to the point u_m past which θ is monotone, then between successive
	// zeros of sin θ, and sum that alternating series with Wynn's ε-algorithm.
	double um = 0;
	if (dtheta(0) >= 0)
	{
		double lo = 0, hi = 1.0 / lmax;
		while (dtheta(hi) >= 0) { lo = hi; hi *= 2; }
		for (int it = 0; it < 100; it++) {
			double mid = 0.5 * (lo + hi);
			(dtheta(mid) >= 0 ? lo : hi) = mid;
		}
		um = hi;
	}
	const double half = 2 * M_PI / q;
	double a = 0;
	while (a < um)
	{
		double b = std::min(um, a + half);
		total += integrate(a, b);
		a = b;
	}
	if (a > 0 && tail_bound(a) < eps) return prob(total);

	// θ is strictly decreasing on [a, ∞): find u ≥ from with θ(u) = target.
	auto next_zero = [&](double from, double target) {
		double lo = from, hi = from + half;
		while (theta(hi) > target) { lo = hi; hi += half; }
		for (int it = 0; it < 200 && hi - lo > 1e-15 * hi; it++) {
			double mid = 0.5 * (lo + hi);
			(theta(mid) > target ? lo : hi) = mid;
		}
		return 0.5 * (lo + hi);
	};
	double m = std::floor(theta(a) / M_PI);
	if (m * M_PI == theta(a)) m -= 1;

	std::vector<double> sums;
	double S = total, best = S, prev_best = HUGE_VAL;
	int stable = 0;
	for (int it = 0; it < 400; it++)
	{
		double b = next_zero(a, m * M_PI);
		S += integrate(a, b);
		a = b;
		m -= 1;
		if (tail_bound(a) < eps) { best = S; break; }
		sums.push_back(S);

		// Wynn's ε-algorithm on the last (up to) 40 partial sums:
		// ε_{k+1}(i) = ε_{k−1}(i+1) + 1 / (ε_k(i+1) − ε_k(i)); even columns
		// are the accelerated estimates.
		size_t start = sums.size() > 40 ? sums.size() - 40 : 0;
		std::vector<double> cur(sums.begin() + (std::ptrdiff_t) start, sums.end());
		std::vector<double> prevcol(cur.size() + 1, 0.0);
		double est = cur.back();
		for (size_t col = 1; cur.size() > 1; col++)
		{
			std::vector<double> nxt(cur.size() - 1);
			bool degenerate = false;
			for (size_t i = 0; i + 1 < cur.size(); i++) {
				double d = cur[i + 1] - cur[i];
				if (d == 0) { degenerate = true; break; }
				nxt[i] = prevcol[i + 1] + 1.0 / d;
			}
			if (degenerate) break;
			prevcol = std::move(cur);
			cur = std::move(nxt);
			if (col % 2 == 0) est = cur.back();
		}
		best = est;
		if (std::abs(best - prev_best) < 1e-11) { if (++stable >= 3) break; }
		else stable = 0;
		prev_best = best;
	}
	return prob(best);
}

struct SmoothTest
{
	double stat = 0;   // Chi.sq (the F statistic is stat / rank for an estimated scale)
	double pval = 1;
	double rank = 0;   // Ref.df
};

// mgcv's testStat(p, X, V, rank, type = 0, res.df): Wood (2013a).
//   beta_s: the smooth's coefficients; Rt: any k×k factor with Rt'Rt equal
//   to the smooth's block of X'WX (mgcv: R factor of the QR of those columns
//   of the model's R); V: the smooth's block of Vp; rank: min(k, edf1);
//   res_df: residual df if the scale is estimated, ≤ 0 otherwise.
static SmoothTest wood_test_stat(const Eigen::VectorXd &beta_s, const Eigen::MatrixXd &Rt,
                                 const Eigen::MatrixXd &V, double rank, double res_df)
{
	using namespace Eigen;
	intptr_t nc = beta_s.size();
	MatrixXd Vt = Rt * V * Rt.transpose();
	Vt = 0.5 * (Vt + Vt.transpose()).eval();
	SelfAdjointEigenSolver<MatrixXd> es(Vt);
	// Decreasing order, with the first entry of each eigenvector made
	// non-negative, as mgcv (the sign matters in the fractional-rank block).
	VectorXd ev = es.eigenvalues().reverse();
	MatrixXd U = es.eigenvectors().rowwise().reverse();
	for (intptr_t j = 0; j < nc; j++) {
		if (U(0, j) < 0) U.col(j) *= -1.0;
	}

	intptr_t k = std::max<intptr_t>(0, (intptr_t) std::floor(rank));
	double nu = std::abs(rank - (double) k);
	intptr_t k1 = (nu > 0) ? k + 1 : k;
	double ev_max = ev.maxCoeff();
	intptr_t r_est = 0;
	for (intptr_t j = 0; j < nc; j++) {
		if (ev[j] > ev_max * std::pow(std::numeric_limits<double>::epsilon(), 0.9)) r_est++;
	}
	if (r_est < k1) {
		k1 = k = r_est;
		nu = 0;
		rank = (double) r_est;
	}
	if (k1 == 0) return {0.0, 1.0, 0.0};

	MatrixXd vec = U.leftCols(k1);
	MatrixXd vec1;
	if (nu > 0 && k > 0)
	{
		for (intptr_t j = 0; j < k - 1; j++) vec.col(j) /= std::sqrt(ev[j]);
		double b12 = std::sqrt(std::max(0.0, 0.5 * nu * (1 - nu)));
		Matrix2d B;
		B << 1, b12, b12, nu;
		Matrix2d ev2 = Vector2d(1.0 / std::sqrt(ev[k - 1]), 1.0 / std::sqrt(ev[k1 - 1])).asDiagonal();
		B = ev2 * B * ev2;
		SelfAdjointEigenSolver<Matrix2d> eb(B);
		Matrix2d rB = eb.eigenvectors() * eb.eigenvalues().cwiseMax(0.0).cwiseSqrt().asDiagonal()
		              * eb.eigenvectors().transpose();
		MatrixXd pair = vec.middleCols(k - 1, 2);
		vec1 = vec;
		vec1.middleCols(k - 1, 2) = pair * Vector2d(-1.0, 1.0).asDiagonal() * rB;
		vec.middleCols(k - 1, 2) = pair * rB;
	}
	else
	{
		intptr_t nscale = (k == 0) ? 1 : k;
		for (intptr_t j = 0; j < vec.cols(); j++) vec.col(j) /= std::sqrt(ev[std::min(j, nscale - 1)]);
		vec1 = vec;
		if (k == 1) rank = 1;
	}

	VectorXd Rp = Rt * beta_s;
	double d = (vec.transpose() * Rp).squaredNorm();
	double d1 = (vec1.transpose() * Rp).squaredNorm();

	double rank1 = rank;
	double pval = 2;
	if (nu > 0)
	{
		std::vector<double> val;
		if (k1 == 1) {
			rank1 = 1;
			val = {1.0};
		}
		else {
			val.assign((size_t) k1, 1.0);
			double rp = nu + 1;
			val[k - 1] = (rp + std::sqrt(rp * (2 - rp))) / 2;
			val[k1 - 1] = rp - val[k - 1];
		}
		std::vector<double> df(val.size(), 1.0);
		if (res_df <= 0) {
			pval = 0.5 * (psum_chisq(d, val, df) + psum_chisq(d1, val, df));
		}
		else {
			double k0 = std::max(1.0, std::round(res_df));
			auto with_scale = [&](double dd) {
				std::vector<double> l = val, h = df;
				l.push_back(-dd / k0);
				h.push_back(k0);
				return psum_chisq(0.0, l, h);
			};
			pval = 0.5 * (with_scale(d) + with_scale(d1));
		}
	}
	if (pval > 1)
	{
		if (res_df <= 0) {
			boost::math::chi_squared chisq(rank1);
			pval = 0.5 * (boost::math::cdf(boost::math::complement(chisq, d)) +
			              boost::math::cdf(boost::math::complement(chisq, d1)));
		}
		else {
			boost::math::fisher_f fdist(rank1, res_df);
			pval = 0.5 * (boost::math::cdf(boost::math::complement(fdist, d / rank1)) +
			              boost::math::cdf(boost::math::complement(fdist, d1 / rank1)));
		}
	}
	return {d, std::min(1.0, pval), rank};
}

static Eigen::MatrixXd submatrix(const Eigen::MatrixXd &A, const std::vector<intptr_t> &rows, const std::vector<intptr_t> &cols)
{
	Eigen::MatrixXd B((intptr_t) rows.size(), (intptr_t) cols.size());
	for (size_t i = 0; i < rows.size(); i++) {
		for (size_t j = 0; j < cols.size(); j++) B((intptr_t) i, (intptr_t) j) = A(rows[i], cols[j]);
	}
	return B;
}

// mgcv's reTest(b, m) (with recov()): Wood (2013b). The other random-effect
// smooths are integrated out, i.e. treated as part of the error, and the
// test statistic is β_m' (Rm'Rm) β_m / σ², with Rm'Rm the Schur complement
// of the remaining penalized normal matrix on smooth m. Its null
// distribution is Σ ev_j χ²₁ (ev: eigenvalues of Ve_m Rm'Rm / σ²), divided
// by χ²_k/k for an estimated scale.
static SmoothTest random_effect_test(intptr_t m, const std::vector<SmoothColumnRange> &ranges,
                                     const Eigen::VectorXd &beta, const Eigen::MatrixXd &Vp,
                                     const Eigen::MatrixXd &XtWX, const Eigen::MatrixXd &S_lambda,
                                     double sig2, bool est_scale, double res_df)
{
	using namespace Eigen;
	intptr_t p = beta.size();
	const double eps08 = std::pow(std::numeric_limits<double>::epsilon(), 0.8);
	const auto &sm = ranges[(size_t) m];

	std::vector<bool> rind((size_t) p, false);
	bool other_re = false;
	for (size_t i = 0; i < ranges.size(); i++)
	{
		if ((intptr_t) i == m || ranges[i].basis != "re") continue;
		other_re = true;
		for (intptr_t j = 0; j < ranges[i].col_count; j++) rind[(size_t) (ranges[i].col_start + j)] = true;
	}

	MatrixXd Ve, G;
	std::vector<intptr_t> keep;  // columns of G (all but the other random effects)
	if (!other_re)
	{
		// Frequentist covariance Ve = Vp X'WX Vp / σ²; G = X'WX + S_λ.
		Ve = Vp * XtWX * Vp / sig2;
		G = XtWX + S_lambda;
		for (intptr_t j = 0; j < p; j++) keep.push_back(j);
	}
	else
	{
		// R with R'R = X'WX (any square root will do: everything below is
		// invariant to R → QR), then Σ = I + R₂ S₂⁻ R₂' for the other random
		// effects (columns 2), Ve = Vp R'ΣR Vp / σ², G = R₁'ΣR₁ + S₁.
		SelfAdjointEigenSolver<MatrixXd> ex(0.5 * (XtWX + XtWX.transpose()));
		MatrixXd R = ex.eigenvalues().cwiseMax(0.0).cwiseSqrt().asDiagonal() * ex.eigenvectors().transpose();
		std::vector<intptr_t> idx2;
		for (intptr_t j = 0; j < p; j++) (rind[(size_t) j] ? idx2 : keep).push_back(j);
		std::vector<intptr_t> all(p);
		std::iota(all.begin(), all.end(), 0);
		MatrixXd S2 = submatrix(S_lambda, idx2, idx2);
		SelfAdjointEigenSolver<MatrixXd> es2(0.5 * (S2 + S2.transpose()));
		VectorXd d2 = es2.eigenvalues();
		double d2max = d2.maxCoeff();
		for (intptr_t j = 0; j < d2.size(); j++) d2[j] = (d2[j] > d2max * eps08) ? 1.0 / d2[j] : 0.0;
		MatrixXd S2inv = es2.eigenvectors() * d2.asDiagonal() * es2.eigenvectors().transpose();
		MatrixXd R2 = submatrix(R, all, idx2);
		MatrixXd Sigma = MatrixXd::Identity(p, p) + R2 * S2inv * R2.transpose();
		MatrixXd RtSR = R.transpose() * Sigma * R;
		Ve = Vp * RtSR * Vp / sig2;
		G = submatrix(RtSR, keep, keep) + submatrix(S_lambda, keep, keep);
	}
	Ve = 0.5 * (Ve + Ve.transpose()).eval();

	// Schur complement of G on smooth m's columns.
	std::vector<intptr_t> im, io;
	for (size_t i = 0; i < keep.size(); i++)
	{
		bool in_m = keep[i] >= sm.col_start && keep[i] < sm.col_start + sm.col_count;
		(in_m ? im : io).push_back((intptr_t) i);
	}
	MatrixXd Gmm = submatrix(G, im, im);
	MatrixXd Schur = Gmm;
	if (!io.empty())
	{
		MatrixXd Goo = submatrix(G, io, io);
		MatrixXd Gom = submatrix(G, io, im);
		LDLT<MatrixXd> ldoo(Goo);
		Schur -= Gom.transpose() * ldoo.solve(Gom);
	}
	Schur = 0.5 * (Schur + Schur.transpose()).eval();

	std::vector<intptr_t> cols_m;
	for (intptr_t j = 0; j < sm.col_count; j++) cols_m.push_back(sm.col_start + j);
	VectorXd b_m = beta.segment(sm.col_start, sm.col_count);
	MatrixXd Ve_m = submatrix(Ve, cols_m, cols_m);

	SmoothTest res;
	res.stat = b_m.dot(Schur * b_m) / sig2;

	// Eigenvalues of Rm Ve_m Rm' / σ², Rm'Rm = Schur.
	SelfAdjointEigenSolver<MatrixXd> esS(Schur);
	MatrixXd Rm = esS.eigenvalues().cwiseMax(0.0).cwiseSqrt().asDiagonal() * esS.eigenvectors().transpose();
	MatrixXd C = Rm * Ve_m * Rm.transpose() / sig2;
	SelfAdjointEigenSolver<MatrixXd> esC(0.5 * (C + C.transpose()));
	VectorXd ev = esC.eigenvalues().cwiseMax(0.0);
	double ev_max = ev.maxCoeff();
	std::vector<double> lam;
	for (intptr_t j = 0; j < ev.size(); j++)
	{
		if (ev[j] > ev_max * eps08) res.rank += 1;
		lam.push_back(ev[j]);
	}
	if (ev_max <= 0) return {res.stat, 1.0, 0.0};

	if (est_scale)
	{
		double k = std::max(1.0, std::round(res_df));
		std::vector<double> h(lam.size(), 1.0);
		lam.push_back(-res.stat / k);
		h.push_back(k);
		res.pval = psum_chisq(0.0, lam, h);
	}
	else
	{
		res.pval = psum_chisq(res.stat, lam, std::vector<double>(lam.size(), 1.0));
	}
	return res;
}

// Per-smooth EDF and significance test, as mgcv's summary.gam.
//   F_infl: (X'WX + S_λ)⁻¹ X'WX, whose diagonal gives the EDF;
//   Vp: Bayesian covariance; sig2: scale (1 if fixed);
//   est_scale: Gaussian (F test against res_df = n − Σ edf) or not (χ²).
static Array<Model::SmoothResult>
smooth_term_tests(const std::vector<SmoothColumnRange> &ranges, const Eigen::VectorXd &beta,
                  const Eigen::MatrixXd &Vp, const Eigen::MatrixXd &XtWX, const Eigen::MatrixXd &S_lambda,
                  const Eigen::MatrixXd &F_infl, double sig2, bool est_scale, double res_df)
{
	using namespace Eigen;
	Array<Model::SmoothResult> out;
	for (size_t i = 0; i < ranges.size(); i++)
	{
		auto &sr = ranges[i];
		Model::SmoothResult sm;
		sm.variable = sr.variable;
		sm.by = sr.by;
		sm.basis = sr.basis;
		sm.k = sr.k;
		sm.col_start = sr.col_start;
		sm.col_count = sr.col_count;

		// edf = Σ diag(F); edf1 = Σ (2F − F²)_jj, the alternative EDF that
		// sets the rank of the test (mgcv's edf1).
		sm.edf = 0;
		double edf1 = 0;
		for (intptr_t j = sr.col_start; j < sr.col_start + sr.col_count; j++)
		{
			sm.edf += F_infl(j, j);
			edf1 += 2 * F_infl(j, j) - F_infl.row(j).dot(F_infl.col(j));
		}

		SmoothTest t;
		if (sr.basis == "re")
		{
			t = random_effect_test((intptr_t) i, ranges, beta, Vp, XtWX, S_lambda, sig2, est_scale, res_df);
		}
		else
		{
			MatrixXd Ablk = XtWX.block(sr.col_start, sr.col_start, sr.col_count, sr.col_count);
			Ablk = 0.5 * (Ablk + Ablk.transpose()).eval();
			MatrixXd Rt;
			LLT<MatrixXd> llt(Ablk);
			if (llt.info() == Success) {
				Rt = llt.matrixU();
			}
			else {
				SelfAdjointEigenSolver<MatrixXd> ea(Ablk);
				Rt = ea.eigenvalues().cwiseMax(0.0).cwiseSqrt().asDiagonal() * ea.eigenvectors().transpose();
			}
			VectorXd b_s = beta.segment(sr.col_start, sr.col_count);
			MatrixXd V_s = Vp.block(sr.col_start, sr.col_start, sr.col_count, sr.col_count);
			double rank = std::min((double) sr.col_count, edf1);
			t = wood_test_stat(b_s, Rt, V_s, rank, est_scale ? res_df : -1.0);
		}

		// The F_stat field carries F = Chi.sq / Ref.df for an estimated
		// scale (Gaussian) and Chi.sq otherwise, as mgcv's s.table.
		sm.ref_df = t.rank;
		sm.F_stat = (est_scale && t.rank > 0) ? t.stat / t.rank : t.stat;
		sm.p_value = t.pval;
		out.append(std::move(sm));
	}
	return out;
}


// Evaluate GCV score for a given smoothing parameter λ.
// Returns {gcv_score, edf, rss} for the penalized OLS:
//   β̂(λ) = (X'X + λS)⁻¹ X'y
//   GCV(λ) = n · RSS(λ) / (n − edf(λ))²
//   edf(λ) = tr(X (X'X + λS)⁻¹ X') = tr((X'X + λS)⁻¹ X'X)
//
static std::tuple<double, double, double>
gcv_score(const Matrix<double> &XtX, const ColVector<double> &Xty,
          const Matrix<double> &Sm, const Matrix<double> &Xm,
          const ColVector<double> &ym, double lambda, intptr_t n, intptr_t p)
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
                   const Array<double> &offset,
                   bool reml)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t n = y.size();
	intptr_t p = X.ncol();
	intptr_t K = (intptr_t)smooth_ranges.size(); // number of penalty blocks

	Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Map<ColVector<double>> ym(const_cast<double *>(y.data()), n);
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

	// ── Per-smooth λ optimization ────────────────────────────────
	//
	// Criterion, as mgcv's gam():
	//   GCV (method = "GCV.Cp", the default):
	//     V(λ) = n RSS / (n − τ)²,  τ = tr((X'X + S_λ)⁻¹ X'X)
	//   REML (method = "REML"), with the scale φ profiled out:
	//     V(λ) = (n − M_p)/2 · (1 + log(2π φ̂)) + ½ log|X'X + S_λ| − ½ log|S_λ|₊
	//     φ̂ = (RSS + β̂'S_λβ̂) / (n − M_p)
	//   where M_p = p − rank(S_λ) is the dimension of the unpenalized space.
	//   This is mgcv's REML score (m$gcv.ubre) at its optimum: the same
	//   expression with φ free reduces to it at φ = φ̂.
	//
	// For K=1 or K=0 this reduces to the single-λ case.
	// For K>1, we cycle: optimize λ_j holding others fixed, repeat.

	std::vector<double> log_lambda(K, 0.0); // log10(λ_j), initialized to 0.0 (λ=1)

	auto build_S = [&](const std::vector<double> &ll) -> MatrixXd {
		MatrixXd Sl = MatrixXd::Zero(p, p);
		for (intptr_t j = 0; j < K; j++) Sl += std::pow(10.0, ll[j]) * S_blocks[j];
		return Sl;
	};

	auto dets = penalty_block_dets(Sm, smooth_ranges);
	intptr_t rank_S = 0;
	for (auto &dj : dets) rank_S += dj.rank;
	const double n_minus_Mp = (double) n - (double) (p - rank_S);

	auto score = [&](const std::vector<double> &ll) -> double {
		MatrixXd Sl = build_S(ll);
		MatrixXd M = XtX + Sl;
		LDLT<MatrixXd> ldlt(M);
		VectorXd b = ldlt.solve(Xty);
		double rss = (ym_work - Xm * b).squaredNorm();
		if (!reml)
		{
			double edf = ldlt.solve(XtX).trace();
			double denom = (double) n - edf;
			return (denom > 0.5) ? (double) n * rss / (denom * denom) : 1e30;
		}
		double phi = (rss + b.dot(Sl * b)) / n_minus_Mp;
		if (!(phi > 0) || n_minus_Mp <= 0) return 1e30;
		double ldM = log_det_spd(M);
		if (!std::isfinite(ldM)) return 1e30;
		return 0.5 * n_minus_Mp * (1.0 + std::log(2.0 * M_PI * phi)) + 0.5 * ldM - 0.5 * log_pdet_S(dets, ll);
	};

	auto fixed_ll = read_diag_fixed_log_lambda();
	bool gcv_overridden = false;
	if (fixed_ll.has_value())
	{
		if ((intptr_t) fixed_ll->size() != K)
		{
			std::fprintf(stderr,
				"[PHON_DIAG_FIXED_LOG_LAMBDA] expected %lld value(s) for this model, got %lld; ignoring override.\n",
				(long long) K, (long long) fixed_ll->size());
		}
		else
		{
			log_lambda = std::move(*fixed_ll);
			gcv_overridden = true;
		}
	}

	if (!gcv_overridden)
	{
	// Step 1: Initialize with a common λ.
	//
	// The penalties are scaled as in mgcv (scale.penalty), on which the optimal
	// λ grows roughly in proportion to n, and near-linear smooths have large
	// optima (mgcv reports log10(sp) > 10 for some). Hence the asymmetric range
	// log10(λ) ∈ [−5, 12].
	if (K > 0)
	{
		double best = std::numeric_limits<double>::max();
		for (int g = -50; g <= 120; g++)
		{
			std::vector<double> ll(K, 0.1 * g);
			double v = score(ll);
			if (v < best)
			{
				best = v;
				log_lambda = ll;
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
			double best = std::numeric_limits<double>::max();
			double best_log_lam = log_lambda[j];
			std::vector<double> ll = log_lambda;

			// Coarse grid: 31 points around current value ± 3.45
			double center = log_lambda[j];
			for (int g = -15; g <= 15; g++)
			{
				ll[j] = std::clamp(center + 0.23 * g, LOG_LAMBDA_MIN, LOG_LAMBDA_MAX);
				double v = score(ll);
				if (v < best)
				{
					best = v;
					best_log_lam = ll[j];
				}
			}

			// Fine grid: 11 points around best
			double lo = best_log_lam - 0.25;
			double hi = best_log_lam + 0.25;
			for (int g = 0; g <= 10; g++)
			{
				ll[j] = std::clamp(lo + (hi - lo) * g / 10.0, LOG_LAMBDA_MIN, LOG_LAMBDA_MAX);
				double v = score(ll);
				if (v < best)
				{
					best = v;
					best_log_lam = ll[j];
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

	// Step 3: Nelder–Mead polish of all log10(λ_j) jointly (see the GLM
	// path), below the coordinate search's 0.05 resolution.
	if (K > 0)
	{
		double best = score(log_lambda);
		auto eval = [&](const VectorXd &x) -> double {
			std::vector<double> ll(K);
			for (intptr_t j = 0; j < K; j++) ll[j] = std::clamp(x[j], LOG_LAMBDA_MIN, LOG_LAMBDA_MAX);
			double v = score(ll);
			if (v < best) { best = v; log_lambda = ll; }
			return v;
		};
		VectorXd x0(K);
		for (intptr_t j = 0; j < K; j++) x0[j] = log_lambda[j];
		nelder_mead(eval, x0, best, 0.1);
	}

	if (progress) progress(total_steps, total_steps);

	} // end if (!gcv_overridden)

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
	// Scale: RSS/(n − τ) under GCV; under REML the REML estimate
	// (RSS + β̂'S_λβ̂)/(n − M_p), as mgcv's sig2 for method = "REML".
	double sigma2 = reml ? (rss + beta_vec.dot(S_final * beta_vec)) / n_minus_Mp
	                     : rss / ((double)n - edf_total);
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
		model.beta[j] = beta_vec[j];
		model.se[j] = std::sqrt(std::max(Vb(j, j), 0.0));
	}

	// t-statistics and p-values for parametric terms only.
	boost::math::students_t tdist(std::max(1.0, (double)n - edf_total));
	for (intptr_t j = 0; j < n_parametric; j++)
	{
		if (model.se[j] > 0)
		{
			model.stat[j] = model.beta[j] / model.se[j];
			model.p[j] = 2.0 * boost::math::cdf(boost::math::complement(tdist, std::abs(model.stat[j])));
		}
	}

	// ── Per-smooth EDF and test (mgcv's summary.gam) ─────────────

	model.smooth_terms = smooth_term_tests(smooth_ranges, beta_vec, Vb, XtX, S_final, Minv_XtX,
	                                       sigma2, true, (double)n - edf_total);

	// Surface the GCV-selected log10(λ) per penalty block on the model so
	// callers (and the diagnostic env-var override) can introspect what the
	// inner loop converged to.
	model.smooth_log_lambda = Array<double>((intptr_t) log_lambda.size(), 0.0);
	for (intptr_t j = 0; j < (intptr_t) log_lambda.size(); j++) {
		model.smooth_log_lambda[j] = log_lambda[j];
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
		model.fitted[i] = yhat;
		model.residuals[i] = ym[i] - yhat;
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
	// The estimated scale σ² counts as one more parameter, as in mgcv's
	// logLik.gam (df = sum(edf) + 1 for the Gaussian family) and in lm().
	model.aic = -2.0 * model.loglik + 2.0 * (edf_total + 1.0);
	model.bic = -2.0 * model.loglik + std::log((double)n) * (edf_total + 1.0);
	model.deviance = rss;

	double ss_total = (ym.array() - ym.mean()).square().sum();
	model.r2 = (ss_total > 0) ? 1.0 - rss / ss_total : 0.0;
	double adj_denom = (double)n - edf_total;
	model.adj_r2 = (adj_denom > 0 && ss_total > 0)
		? 1.0 - (rss / adj_denom) / (ss_total / ((double)n - 1.0))
		: 0.0;

	model.converged = true;
	model.niter = 0;
	model.method = reml ? Method::REML : Method::ML;

	return model;
}

// Per-observation derivatives of the log-likelihood with respect to the
// linear predictor η, for each family's (only supported) default link:
//   d1     = ∂ℓ_i/∂η_i                   (score)
//   w_iter = P-IRLS weight: expected information, or for Student t the
//            EM-type weight (ν+1)/(νσ² + r²), which is always positive
//   w_obs  = −∂²ℓ_i/∂η_i²                (observed information)
//   w_exp  = E[−∂²ℓ_i/∂η_i²]             (expected information)
// P-IRLS iterates β ← (X'W X + S)⁻¹ X'W z with z = η + d1/w_iter, whose fixed
// point solves the penalized score equations X'd1 = Sβ for any positive
// weights, i.e. it converges to the penalized MLE of the family itself.
// (A working response built from V(μ) and dμ/dη alone would instead give a
// quasi-likelihood estimate for beta, and least squares for Student t.)
// For canonical links (Poisson log, binomial logit) all three weights
// coincide. w_obs is what the Laplace approximation in REML needs, as in
// mgcv's extended families (full Newton weights); w_exp is what mgcv uses
// for the covariance and EDF it reports (Dd()$EDmu2).
struct EtaDerivs
{
	Eigen::VectorXd d1, w_iter, w_obs, w_exp;
};

static void eta_derivs(const Family &fam, const Eigen::VectorXd &y, const Eigen::VectorXd &mu, EtaDerivs &o)
{
	intptr_t n = y.size();
	o.d1.resize(n);
	o.w_iter.resize(n);
	o.w_obs.resize(n);
	o.w_exp.resize(n);
	const String &f = fam.name;
	for (intptr_t i = 0; i < n; i++)
	{
		double yi = y[i], m = mu[i];
		double d1, wi, wo, we = -1;
		if (f == "poisson")
		{
			d1 = yi - m;  wi = wo = m;
		}
		else if (f == "binomial")
		{
			d1 = yi - m;  wi = wo = m * (1.0 - m);
		}
		else if (f == "negbin")
		{
			double th = fam.theta, q = m + th;
			d1 = th * (yi - m) / q;
			wi = m * th / q;
			wo = m * th * (yi + th) / (q * q);
		}
		else if (f == "beta")
		{
			double phi = fam.phi;
			m = std::clamp(m, 1e-10, 1.0 - 1e-10);
			double yc = std::clamp(yi, 1e-10, 1.0 - 1e-10);
			double a = m * phi, b = (1.0 - m) * phi, g1 = m * (1.0 - m);
			double dl_dmu = phi * (std::log(yc) - std::log1p(-yc)
			                       - boost::math::digamma(a) + boost::math::digamma(b));
			double info_mu = phi * phi * (boost::math::trigamma(a) + boost::math::trigamma(b));
			d1 = dl_dmu * g1;
			wi = info_mu * g1 * g1;
			wo = wi - dl_dmu * g1 * (1.0 - 2.0 * m);   // − d²ℓ/dη², with d²μ/dη² = g1(1 − 2μ)
		}
		else if (f == "student")
		{
			double nu = fam.nu, s2 = fam.sigma * fam.sigma, r = yi - m, q = nu * s2 + r * r;
			d1 = (nu + 1.0) * r / q;
			wi = (nu + 1.0) / q;
			wo = (nu + 1.0) * (nu * s2 - r * r) / (q * q);
			we = (nu + 1.0) / ((nu + 3.0) * s2);
		}
		else   // gaussian, identity link
		{
			d1 = yi - m;  wi = wo = 1.0;
		}
		o.d1[i] = d1;
		o.w_iter[i] = std::max(wi, 1e-10);
		o.w_obs[i] = wo;
		o.w_exp[i] = (we < 0) ? o.w_iter[i] : we;   // w_iter is the expected information but for Student t
	}
}


Model penalized_glm(const Array<double> &y, const Array<double> &X,
                    const Array<double> &S, const Family &fam,
                    intptr_t n_parametric,
                    const std::vector<SmoothColumnRange> &smooth_ranges,
                    FittingCallback progress,
                    int max_iter,
                    const Array<double> &offset,
                    bool reml_requested)
{
	using namespace Eigen;

	validate_inputs(y, X);

	intptr_t n = y.size();
	intptr_t p = X.ncol();

	Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Map<ColVector<double>> ym(const_cast<double *>(y.data()), n);
	Map<Matrix<double>> Sm(const_cast<double *>(S.data()), p, p);

	bool has_off = !offset.empty();
	Map<const VectorXd> off(has_off ? offset.data() : nullptr, has_off ? n : 0);

	// Initialize μ via family-specific mustart, mirroring mgcv's
	// family$initialize.  This avoids degenerate eta (e.g. log(0) = −∞
	// for Poisson when y has zeros, or logit(0/1) = ±∞ for binomial),
	// which otherwise produce zero-weighted observations in the first
	// PIRLS step and make convergence both slow and prone to landing
	// on a sub-optimal stationary point of the (otherwise strictly
	// convex) penalized objective.
	VectorXd mu(n);
	if (fam.name == "poisson")
	{
		// mgcv: mustart = y + 0.1
		for (intptr_t i = 0; i < n; i++) mu[i] = ym[i] + 0.1;
	}
	else if (fam.name == "binomial")
	{
		// mgcv: mustart = (y + 0.5) / 2 (works for 0/1-coded y)
		for (intptr_t i = 0; i < n; i++) mu[i] = (ym[i] + 0.5) / 2.0;
	}
	else if (fam.name == "negbin")
	{
		// Same shrink-from-zero trick as Poisson; identical link form.
		for (intptr_t i = 0; i < n; i++) mu[i] = ym[i] + 0.1;
	}
	else if (fam.name == "beta")
	{
		// Beta is constrained to (0, 1); pull strictly inside.
		for (intptr_t i = 0; i < n; i++) {
			double v = ym[i];
			if (v <= 0.0) v = 1e-3;
			else if (v >= 1.0) v = 1.0 - 1e-3;
			mu[i] = v;
		}
	}
	else
	{
		// Gaussian, Student-t, etc.: mustart = y is fine (identity link).
		mu = ym;
	}
	VectorXd eta = fam.link(mu);
	if (has_off) eta -= off;   // subtract offset so eta represents Xβ alone
	{
		VectorXd eta_full = eta;
		if (has_off) eta_full += off;
		mu = fam.linkinv(eta_full);
	}

	const VectorXd eta_start = eta;   // Xβ scale (offset removed)
	const VectorXd mu_start = mu;

	auto full_eta = [&](const VectorXd &e) -> VectorXd {
		if (has_off) return e + off;
		return e;
	};

	// ── Per-smooth penalty blocks ────────────────────────────────

	intptr_t K = (intptr_t) smooth_ranges.size();
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
	auto build_S = [&](const std::vector<double> &ll) -> MatrixXd {
		MatrixXd Sl = MatrixXd::Zero(p, p);
		for (intptr_t j = 0; j < K; j++) Sl += std::pow(10.0, ll[j]) * S_blocks[j];
		return Sl;
	};
	auto dets = penalty_block_dets(Sm, smooth_ranges);
	intptr_t rank_S = 0;
	for (auto &dj : dets) rank_S += dj.rank;
	const double M_p = (double) (p - rank_S);

	// ── Selection criterion ──────────────────────────────────────
	//
	// Smoothing parameters are selected by "outer" iteration, as in mgcv's
	// gam() (default optimizer): every candidate is scored on the fit P-IRLS
	// converges to, not on the working linear model of one IRLS step
	// ("performance iteration", which mgcv has dropped and which lands on a
	// different λ). The criteria are mgcv's:
	//
	//   UBRE (method = "GCV.Cp", known scale: Poisson, binomial)
	//     V = D/n + 2τ/n − 1,   τ = tr((X'WX + S_λ)⁻¹ X'WX)
	//   REML (method = "REML"; always for negative binomial, beta and Student
	//   t, as for mgcv's extended families), the Laplace approximate
	//   marginal likelihood with the scale fixed at 1:
	//     V = −ℓ(β̂) + ½β̂'S_λβ̂ + ½ log|X'W_obs X + S_λ| − ½ log|S_λ|₊ − (M_p/2) log 2π
	//
	// D is taken as −2ℓ, which differs from the deviance by a constant that
	// does not move the minimum. The extra parameters of the extended
	// families (log θ; log φ; log σ and log ν) are REML coordinates too and
	// are estimated jointly with λ, as mgcv does.
	const bool extended = fam.name == "negbin" || fam.name == "beta" || fam.name == "student";
	const bool reml = reml_requested || extended;
	if (!reml && fam.name != "poisson" && fam.name != "binomial")
		throw error("Smoothing-parameter selection is not defined for the '%' family", fam.name);

	// Extra-parameter coordinates (natural log scale) and the family they define.
	std::vector<double> log_theta;
	if (fam.name == "negbin" || fam.name == "beta")
	{
		// Moment-based start, ignoring the smooth (so the dispersion is
		// overstated); the search corrects it.
		double m = ym.mean();
		double v = (ym.array() - m).square().sum() / std::max<double>(1.0, (double) n - 1.0);
		double start;
		if (fam.name == "negbin")
			start = (v > m) ? m * m / (v - m) : 100.0;
		else
			start = (v > 0) ? m * (1.0 - m) / v - 1.0 : 10.0;
		log_theta.push_back(std::log(std::clamp(start, 0.05, 1e4)));
	}
	else if (fam.name == "student")
	{
		// σ from the MAD (robust to the heavy tails), ν = 5.
		std::vector<double> dev(ym.data(), ym.data() + n);
		std::nth_element(dev.begin(), dev.begin() + n / 2, dev.end());
		double med = dev[n / 2];
		for (auto &v : dev) v = std::abs(v - med);
		std::nth_element(dev.begin(), dev.begin() + n / 2, dev.end());
		double sd = 1.4826 * dev[n / 2];
		if (!(sd > 0)) sd = std::max(1e-6, std::sqrt((ym.array() - ym.mean()).square().mean()));
		log_theta.push_back(std::log(sd));
		log_theta.push_back(std::log(5.0));
	}
	const intptr_t n_theta = (intptr_t) log_theta.size();

	auto make_family = [&](const std::vector<double> &lt) -> Family {
		if (fam.name == "negbin") return Family::negbin(std::exp(lt[0]));
		if (fam.name == "beta") return Family::beta(std::exp(lt[0]));
		if (fam.name == "student")
			return Family::student(std::exp(lt[0]), std::clamp(std::exp(lt[1]), NU_MIN, NU_MAX));
		return fam;
	};

	// ── P-IRLS at fixed λ (and θ) ────────────────────────────────

	const double pirls_tol = extended ? 1e-11 : 1e-7;
	const int pirls_max_iter = extended ? std::max(max_iter, 200) : max_iter;

	struct PirlsFit
	{
		VectorXd beta, eta, mu;
		double ll = -std::numeric_limits<double>::infinity();
		double score = std::numeric_limits<double>::infinity();
		bool converged = false;
		int iter = 0;
	};

	auto pirls = [&](const Family &F, const std::vector<double> &ll_lambda, const VectorXd &eta0,
	                 const VectorXd &mu0, const VectorXd &beta0, bool have_beta0) -> PirlsFit
	{
		MatrixXd Sl = build_S(ll_lambda);
		PirlsFit f;
		f.eta = eta0;
		f.mu = mu0;
		f.beta = beta0;
		bool have_beta = have_beta0;
		double pdev_prev = std::numeric_limits<double>::infinity();
		double dev_prev = std::numeric_limits<double>::infinity();
		EtaDerivs dv;
		VectorXd z(n);

		for (f.iter = 0; f.iter < pirls_max_iter; f.iter++)
		{
			eta_derivs(F, ym, f.mu, dv);
			z = f.eta + (dv.d1.array() / dv.w_iter.array()).matrix();
			MatrixXd XtWX = Xm.transpose() * dv.w_iter.asDiagonal() * Xm;
			VectorXd XtWz = Xm.transpose() * (dv.w_iter.array() * z.array()).matrix();
			LDLT<MatrixXd> ldlt(XtWX + Sl);
			VectorXd beta_new = ldlt.solve(XtWz);

			// Step halving (as in mgcv's gam.fit3): back off towards the
			// previous β while the penalized deviance is non-finite or rises.
			VectorXd eta_new, mu_new;
			double dev_new = 0, pdev_new = 0;
			for (int half = 0; half < 30; half++)
			{
				eta_new = Xm * beta_new;
				mu_new = F.linkinv(full_eta(eta_new));
				dev_new = -2.0 * F.loglik(ym, mu_new);
				pdev_new = dev_new + beta_new.dot(Sl * beta_new);
				bool bad = !std::isfinite(pdev_new) ||
				           (have_beta && pdev_new > pdev_prev + 1e-7 * (0.1 + std::abs(pdev_prev)));
				if (!bad || !have_beta) break;
				beta_new = 0.5 * (beta_new + f.beta);
			}
			if (!std::isfinite(pdev_new)) return f;   // diverged: score stays +inf

			f.beta = beta_new;
			f.eta = eta_new;
			f.mu = mu_new;
			f.ll = -0.5 * dev_new;
			have_beta = true;

			// Convergence: relative change in deviance below pirls_tol (β can
			// plateau in flat regions before the deviance does). mgcv's
			// gam.fit3 uses 1e-7, but it takes full Newton steps; with
			// expected-information or EM weights convergence is linear, and
			// the criteria are compared to ~1e-6 absolute on deviances in the
			// thousands, so the extended families get a tighter tolerance.
			double dev_change = std::abs(dev_new - dev_prev) / (0.1 + std::abs(dev_new));
			dev_prev = dev_new;
			pdev_prev = pdev_new;
			if (f.iter > 0 && dev_change < pirls_tol)
			{
				f.converged = true;
				break;
			}
		}

		// Score the converged fit.
		eta_derivs(F, ym, f.mu, dv);
		if (reml)
		{
			MatrixXd H = Xm.transpose() * dv.w_obs.asDiagonal() * Xm + Sl;
			double ldH = log_det_spd(H);
			if (!std::isfinite(ldH)) return f;   // not a maximum: score stays +inf
			f.score = -f.ll + 0.5 * f.beta.dot(Sl * f.beta) + 0.5 * ldH
			          - 0.5 * log_pdet_S(dets, ll_lambda) - 0.5 * M_p * std::log(2.0 * M_PI);
		}
		else
		{
			MatrixXd XtWX = Xm.transpose() * dv.w_iter.asDiagonal() * Xm;
			LDLT<MatrixXd> ldlt(XtWX + Sl);
			double tau = ldlt.solve(XtWX).trace();
			f.score = -2.0 * f.ll / (double) n + 2.0 * tau / (double) n - 1.0;
		}
		return f;
	};

	// ── Search over log10(λ_j) and the extra parameters ──────────

	std::vector<double> log_lambda(K, 0.0);

	// Diagnostic override: one value per penalty block, or a single value
	// applied to all of them. The extra parameters are still estimated.
	auto fixed_ll_glm = read_diag_fixed_log_lambda();
	bool lambda_fixed = false;
	if (fixed_ll_glm.has_value())
	{
		if ((intptr_t) fixed_ll_glm->size() == K) {
			log_lambda = *fixed_ll_glm;
			lambda_fixed = true;
		} else if (fixed_ll_glm->size() == 1) {
			std::fill(log_lambda.begin(), log_lambda.end(), (*fixed_ll_glm)[0]);
			lambda_fixed = true;
		} else {
			std::fprintf(stderr,
				"[PHON_DIAG_FIXED_LOG_LAMBDA] expected 1 or %lld value(s) for this model, got %lld; ignoring override.\n",
				(long long) K, (long long) fixed_ll_glm->size());
		}
	}

	PirlsFit best;
	best.eta = eta_start;
	best.mu = mu_start;
	best.beta = VectorXd::Zero(p);
	bool have_best = false;
	Family fam_cur = make_family(log_theta);

	const intptr_t n_coord = (lambda_fixed ? 0 : K) + n_theta;
	int max_cycles = (n_coord > 1) ? 15 : 1;
	int total_steps = std::max<int>(1, 69 + (int) n_coord * 40 * std::min(max_cycles, 4));
	int step = 0;
	auto tick = [&]() {
		step++;
		if (progress && step % 5 == 0) progress(std::min(step, total_steps), total_steps);
	};

	if (!lambda_fixed && K > 0)
	{
		// Step 1: a common λ for all blocks, log10(λ) ∈ [−5, 12] (see
		// penalized_lm for the range), swept from the smoothest end so that
		// each fit starts from its neighbour's (and from a near-linear,
		// well-conditioned fit at the start).
		PirlsFit prev;
		bool have_prev = false;
		for (int g = 48; g >= -20; g--)
		{
			std::vector<double> ll(K, 0.25 * g);
			PirlsFit f = have_prev ? pirls(fam_cur, ll, prev.eta, prev.mu, prev.beta, true)
			                       : pirls(fam_cur, ll, eta_start, mu_start, VectorXd::Zero(p), false);
			tick();
			if (std::isfinite(f.ll)) { prev = f; have_prev = true; }
			if (f.score < best.score)
			{
				best = f;
				log_lambda = ll;
				have_best = true;
			}
		}
	}
	else
	{
		best = pirls(fam_cur, log_lambda, eta_start, mu_start, VectorXd::Zero(p), false);
		have_best = std::isfinite(best.score);
	}

	// Step 2: coordinate search, one coordinate at a time with the others
	// held fixed (as in penalized_lm): a coarse pass (λ: ±3.5 decades in
	// steps of 0.25; θ: ±2 on the log scale in steps of 0.25) then a fine pass
	// (±0.25 in steps of 0.05). A single smooth with no extra parameter only
	// needs the fine pass around the grid optimum.
	for (int cycle = 0; have_best && n_coord > 0 && cycle < max_cycles; cycle++)
	{
		double max_change = 0;
		for (intptr_t c = 0; c < n_coord; c++)
		{
			bool is_theta = lambda_fixed || c >= K;
			intptr_t idx = is_theta ? c - (lambda_fixed ? 0 : K) : c;
			auto get = [&]() { return is_theta ? log_theta[idx] : log_lambda[idx]; };
			const double cycle_start = get();

			std::vector<double> coarse;
			if (is_theta) {
				for (int g = -8; g <= 8; g++) coarse.push_back(0.25 * g);
			} else if (n_coord > 1) {
				for (int g = -14; g <= 14; g++) coarse.push_back(0.25 * g);
			}
			std::vector<double> fine;
			for (int g = -5; g <= 5; g++) fine.push_back(0.05 * g);

			for (int pass = 0; pass < 2; pass++)
			{
				const std::vector<double> &offsets = (pass == 0) ? coarse : fine;
				double center = get();
				PirlsFit start = best;
				for (double off_g : offsets)
				{
					if (off_g == 0.0) continue;
					std::vector<double> ll = log_lambda, lt = log_theta;
					if (is_theta) {
						lt[idx] = center + off_g;
					} else {
						ll[idx] = std::clamp(center + off_g, LOG_LAMBDA_MIN, LOG_LAMBDA_MAX);
						if (ll[idx] == log_lambda[idx]) continue;
					}
					Family F = is_theta ? make_family(lt) : fam_cur;
					PirlsFit f = pirls(F, ll, start.eta, start.mu, start.beta, true);
					tick();
					if (f.score < best.score)
					{
						best = f;
						log_lambda = ll;
						log_theta = lt;
						fam_cur = F;
					}
				}
			}
			max_change = std::max(max_change, std::abs(get() - cycle_start));
		}
		if (max_change < 0.05) break;
	}
	// Step 3: Nelder–Mead polish over all coordinates jointly. The extra
	// parameters are often strongly correlated with each other (σ and ν for
	// Student t) or with λ, which one-at-a-time search approaches only
	// slowly; the simplex settles the joint optimum well below the
	// coordinate search's 0.05 resolution.
	if (have_best && n_coord > 0)
	{
		const intptr_t nl = lambda_fixed ? 0 : K;
		auto eval = [&](const VectorXd &x) -> double {
			std::vector<double> ll = log_lambda, lt = log_theta;
			for (intptr_t j = 0; j < nl; j++) ll[j] = std::clamp(x[j], LOG_LAMBDA_MIN, LOG_LAMBDA_MAX);
			for (intptr_t j = 0; j < n_theta; j++) lt[j] = x[nl + j];
			Family F = make_family(lt);
			PirlsFit f = pirls(F, ll, best.eta, best.mu, best.beta, true);
			tick();
			if (f.score < best.score)
			{
				best = f;
				log_lambda = ll;
				log_theta = lt;
				fam_cur = F;
			}
			return f.score;
		};
		VectorXd x0(n_coord);
		for (intptr_t j = 0; j < nl; j++) x0[j] = log_lambda[j];
		for (intptr_t j = 0; j < n_theta; j++) x0[nl + j] = log_theta[j];
		nelder_mead(eval, x0, best.score, 0.1);
	}

	if (progress) progress(total_steps, total_steps);

	// Final fit at the selected λ (and θ), warm-started from the best
	// candidate, so the reported β, μ, convergence flag and iteration count
	// are those of a complete P-IRLS run at exactly these values.
	MatrixXd S_lambda = build_S(log_lambda);
	PirlsFit final_fit = have_best ? pirls(fam_cur, log_lambda, best.eta, best.mu, best.beta, true)
	                               : pirls(fam_cur, log_lambda, eta_start, mu_start, VectorXd::Zero(p), false);
	VectorXd beta = final_fit.beta;
	mu = final_fit.mu;
	bool converged = final_fit.converged;
	// Iterations of the final P-IRLS run (f.iter is the 0-based index of the
	// iteration at which it stopped).
	int iter = std::min(final_fit.iter + 1, pirls_max_iter);

	// ── Final quantities at convergence ──────────────────────────
	//
	// Covariance and EDF from the expected information (X'W_exp X + S_λ), as
	// mgcv reports them: for its extended families (nb, betar, scat) Vp, R
	// and edf use the expected weights Dd()$EDmu2 at convergence, not the
	// Newton weights of the fit (checked against mgcv 1.9-3 to 1e-12). The
	// two coincide for canonical links (Poisson, binomial). The expected
	// weights are positive, so X'W_exp X + S_λ is positive definite.

	EtaDerivs dv;
	eta_derivs(fam_cur, ym, mu, dv);
	MatrixXd XtWX = Xm.transpose() * dv.w_exp.asDiagonal() * Xm;
	MatrixXd M = XtWX + S_lambda;
	LDLT<MatrixXd> ldlt(M);
	MatrixXd Minv = ldlt.solve(MatrixXd::Identity(p, p));
	MatrixXd Minv_XtWX = ldlt.solve(XtWX);
	double edf_total = Minv_XtWX.trace();

	// Bayesian posterior covariance: Vp = (X'WX + S_λ)⁻¹
	// Matches mgcv's default. See penalized_lm for rationale.
	MatrixXd Vb = Minv;

	// ── Per-smooth EDF and test (mgcv's summary.gam) ─────────────
	//
	// The scale is fixed (1 for Poisson and binomial; θ, φ, σ enter the
	// likelihood directly for the extended families), so, as mgcv, the
	// tests report Chi.sq against a (weighted) χ² reference.

	Array<Model::SmoothResult> smooth_results =
		smooth_term_tests(smooth_ranges, beta, Vb, XtWX, S_lambda, Minv_XtWX, 1.0, false, -1.0);

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
		model.beta[j] = beta[j];
		model.se[j] = std::sqrt(std::max(Vb(j, j), 0.0));
	}

	// z-statistics and p-values for parametric terms
	boost::math::chi_squared chisq_dist(1);
	for (intptr_t j = 0; j < n_parametric; j++)
	{
		if (model.se[j] > 0)
		{
			model.stat[j] = model.beta[j] / model.se[j];
			double wald = model.stat[j] * model.stat[j];
			model.p[j] = 1.0 - boost::math::cdf(chisq_dist, wald);
		}
	}

	// Fitted values and residuals
	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);
	for (intptr_t i = 0; i < n; i++)
	{
		model.fitted[i] = mu[i];
		model.residuals[i] = ym[i] - mu[i];
	}

	// Extra parameters. They count as parameters in AIC/BIC (mgcv adds
	// n.theta to the degrees of freedom of logLik.gam for extended families).
	if (fam.name == "negbin") model.theta = fam_cur.theta;
	if (fam.name == "beta") model.phi = fam_cur.phi;
	if (fam.name == "student") { model.sigma = fam_cur.sigma; model.nu = fam_cur.nu; }
	double df_model = edf_total + (double) n_theta;

	model.nobs = n;
	model.nfixed = p;
	model.loglik = fam_cur.loglik(ym, mu);
	model.aic = -2.0 * model.loglik + 2.0 * df_model;
	model.bic = -2.0 * model.loglik + std::log((double)n) * df_model;
	model.deviance = -2.0 * model.loglik;
	model.niter = iter;
	model.converged = converged;
	model.method = reml ? Method::REML : Method::ML;
	model.smooth_terms = std::move(smooth_results);

	// One log10(λ) per penalty block, in smooth_ranges order (as penalized_lm).
	model.smooth_log_lambda = Array<double>(K, 0.0);
	for (intptr_t j = 0; j < K; j++) {
		model.smooth_log_lambda[j] = log_lambda[j];
	}

	return model;
}

} // namespace phonometrica::stats
