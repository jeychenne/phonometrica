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
 * Created: 12/04/2026                                                                                                 *
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

#include <algorithm>
#include <cmath>
#include <random>
#include <boost/math/distributions/normal.hpp>
#include <Eigen/Dense>
#include <phon/analysis/bayesian.hpp>
#include <phon/analysis/waic.hpp>

namespace phonometrica::stats {

void bayesian_adjust(Model &model, const PriorSpec &priors)
{
	intptr_t p = model.nfixed;
	if (p <= 0) return;
	if (!model.has_vcov()) {
		throw error("Cannot compute Bayesian posterior: variance-covariance matrix not available");
	}

	// ── 1. Build the prior precision matrix for fixed effects ────────
	//
	// For Normal priors, the prior precision of β_j is 1/σ²_prior_j.
	// The prior precision matrix is diagonal: Λ_prior = diag(1/σ²_prior_j).
	// The prior mean vector is μ_prior = (μ_prior_1, ..., μ_prior_p).

	Eigen::VectorXd prior_mean(p);
	Eigen::VectorXd prior_precision(p);  // diagonal

	for (intptr_t j = 0; j < p; j++)
	{
		const auto &pr = priors.prior_for(model.coef_names[j + 1]);
		prior_mean[j] = pr.mean;
		prior_precision[j] = 1.0 / (pr.sd * pr.sd);
	}

	// ── 2. Compute posterior covariance and mean ─────────────────────
	//
	// H_lik = vcov⁻¹ (Fisher information from the frequentist fit)
	// H_post = H_lik + Λ_prior
	// Σ_post = H_post⁻¹
	// β̂_post = Σ_post (H_lik β̂_MLE + Λ_prior μ_prior)

	Eigen::Map<Matrix<double>> vcov_m(const_cast<double *>(model.vcov.data()), p, p);

	// Invert vcov to get the Fisher information.
	Eigen::LDLT<Eigen::MatrixXd> ldlt(vcov_m);
	Eigen::MatrixXd H_lik = ldlt.solve(Eigen::MatrixXd::Identity(p, p));

	// Add prior precision (diagonal).
	Eigen::MatrixXd H_post = H_lik;
	for (intptr_t j = 0; j < p; j++) {
		H_post(j, j) += prior_precision[j];
	}

	// Posterior covariance.
	Eigen::LDLT<Eigen::MatrixXd> post_ldlt(H_post);
	Eigen::MatrixXd Sigma_post = post_ldlt.solve(Eigen::MatrixXd::Identity(p, p));

	// Posterior mean.
	Eigen::VectorXd beta_mle(p);
	for (intptr_t j = 0; j < p; j++) {
		beta_mle[j] = model.beta[j + 1];
	}

	Eigen::VectorXd info_weighted = H_lik * beta_mle;
	for (intptr_t j = 0; j < p; j++) {
		info_weighted[j] += prior_precision[j] * prior_mean[j];
	}
	Eigen::VectorXd beta_post = post_ldlt.solve(info_weighted);

	// ── 3. Extract posterior summaries ───────────────────────────────

	boost::math::normal_distribution<double> normal;
	double z_975 = boost::math::quantile(normal, 0.975); // ≈ 1.96

	model.posterior_mean = Array<double>(p, 0.0);
	model.posterior_mode = Array<double>(p, 0.0);
	model.posterior_median = Array<double>(p, 0.0);
	model.posterior_sd = Array<double>(p, 0.0);
	model.ci_lower = Array<double>(p, 0.0);
	model.ci_upper = Array<double>(p, 0.0);
	model.pd = Array<double>(p, 0.0);

	for (intptr_t j = 0; j < p; j++)
	{
		double mean = beta_post[j];
		double var = Sigma_post(j, j);
		double sd = (var > 0) ? std::sqrt(var) : 0.0;

		model.posterior_mean[j + 1] = mean;
		model.posterior_mode[j + 1] = mean;    // Gaussian: mode = mean
		model.posterior_median[j + 1] = mean;  // Gaussian: median = mean
		model.posterior_sd[j + 1] = sd;
		model.ci_lower[j + 1] = mean - z_975 * sd;
		model.ci_upper[j + 1] = mean + z_975 * sd;

		// Probability of direction: P(sign(β) = sign(β̂_post))
		// = Φ(|β̂_post| / σ_post)   for the Gaussian approximation.
		if (sd > 0) {
			model.pd[j + 1] = boost::math::cdf(normal, std::abs(mean) / sd);
		} else {
			model.pd[j + 1] = 1.0;
		}
	}

	// ── 4. Update vcov to posterior covariance ───────────────────────

	for (intptr_t i = 0; i < p; i++) {
		for (intptr_t j = 0; j < p; j++) {
			model.vcov(i + 1, j + 1) = Sigma_post(i, j);
		}
	}

	// Also update beta to the posterior mean (used by EMMs, predict, etc.)
	for (intptr_t j = 0; j < p; j++) {
		model.beta[j + 1] = beta_post[j];
	}

	// Recompute se/stat from posterior (these are now posterior SD / posterior z-score).
	// We keep se and stat populated for compatibility with existing display code,
	// but they represent posterior SD and posterior mean / posterior SD, not
	// frequentist standard errors and Wald statistics.
	for (intptr_t j = 0; j < p; j++)
	{
		model.se[j + 1] = model.posterior_sd[j + 1];
		model.stat[j + 1] = (model.se[j + 1] > 0) ? model.beta[j + 1] / model.se[j + 1] : 0.0;
		// p-values are not meaningful in Bayesian context; set to NaN.
		model.p[j + 1] = std::numeric_limits<double>::quiet_NaN();
	}

	// ── 5. Hyperparameter posteriors (variance components) ──────────
	//
	// For random-effect SDs, we report the MLE point estimate with a
	// rough posterior SD derived from the delta method on the outer
	// Hessian (when available). Full hyperparameter posteriors require
	// the grid integration of Phase 2.

	if (model.has_random_effects())
	{
		intptr_t n_hyper = 0;

		// Count: one SD per random-effect term.
		for (intptr_t g = 1; g <= model.random_effects.size(); g++) {
			n_hyper += model.random_effects[g].term_names.size();
		}
		// Add residual SD for Gaussian.
		if (model.is_gaussian()) {
			n_hyper += 1;
		}

		model.hyper_names = Array<String>(n_hyper, String());
		model.hyper_posterior_mean = Array<double>(n_hyper, 0.0);
		model.hyper_posterior_sd = Array<double>(n_hyper, std::numeric_limits<double>::quiet_NaN());
		model.hyper_ci_lower = Array<double>(n_hyper, std::numeric_limits<double>::quiet_NaN());
		model.hyper_ci_upper = Array<double>(n_hyper, std::numeric_limits<double>::quiet_NaN());

		intptr_t idx = 1;
		for (intptr_t g = 1; g <= model.random_effects.size(); g++)
		{
			auto &re = model.random_effects[g];
			for (intptr_t t = 1; t <= re.term_names.size(); t++)
			{
				// Name: "sd(term|group)"
				std::string name = "sd(" + std::string(re.term_names[t].data(), re.term_names[t].size())
				                 + "|" + std::string(re.group_name.data(), re.group_name.size()) + ")";
				model.hyper_names[idx] = String(name);
				model.hyper_posterior_mean[idx] = std::sqrt(std::max(re.variance[t], 0.0));
				idx++;
			}
		}

		if (model.is_gaussian())
		{
			model.hyper_names[idx] = "sd(residual)";
			model.hyper_posterior_mean[idx] = model.rse;
			idx++;
		}
	}

	// ── 6. Laplace log marginal likelihood ───────────────────────────
	//
	// log p(y) ≈ log f(β̂_post) + (p/2) log(2π) - 0.5 log det(H_post)
	//
	// where f(β̂_post) = p(y|β̂_post) × π(β̂_post).
	{
		static const double log_2pi = std::log(2.0 * M_PI);

		// Log-likelihood at the posterior mode (quadratic approx from MLE).
		Eigen::VectorXd shift = beta_post - beta_mle;
		double loglik_post = model.loglik - 0.5 * shift.dot(H_lik * shift);

		// Log-prior at the posterior mode.
		double log_prior = 0;
		for (intptr_t j = 0; j < p; j++)
		{
			const auto &pr = priors.prior_for(model.coef_names[j + 1]);
			double z = (beta_post[j] - pr.mean) / pr.sd;
			log_prior += -0.5 * log_2pi - std::log(pr.sd) - 0.5 * z * z;
		}

		// log det(H_post) from the LDLT decomposition.
		double log_det_H = 0;
		Eigen::VectorXd diag_D = post_ldlt.vectorD();
		for (intptr_t j = 0; j < p; j++)
			log_det_H += std::log(std::max(diag_D[j], 1e-30));

		model.log_marginal = loglik_post + log_prior
		                   + 0.5 * p * log_2pi
		                   - 0.5 * log_det_H;
	}

	// ── 7. WAIC ─────────────────────────────────────────────────────────
	//
	// Draw S posterior samples of β from the full posterior, compute
	// pointwise log-likelihoods, and call compute_waic_from_loglik.
	//
	// For GAMs, model.beta and model.vcov include smooth basis coefficients
	// (the parametric block has been updated above; the smooth block retains
	// the penalized MLE, which IS the posterior under the implicit smoothing
	// prior).  We draw the full coefficient vector so that smooth uncertainty
	// is properly reflected in WAIC.

	if (!model.X.empty() && !model.y.empty())
	{
		constexpr int S = 1000;
		constexpr unsigned int SEED = 12345;

		intptr_t n = model.nobs;

		// Total coefficients: parametric + smooth basis (for GAMs).
		// For non-GAMs, p_draw == nfixed.
		intptr_t p_draw = model.beta.size();
		if (model.X.ndim() == 2 && model.X.ncol() != p_draw)
			p_draw = p;  // fallback to parametric-only

		Eigen::Map<Matrix<double>> Xm(const_cast<double *>(model.X.data()), n, p_draw);
		Eigen::Map<Vector<double>> ym(const_cast<double *>(model.y.data()), n);

		// Full posterior mean and covariance (parametric block adjusted,
		// smooth block unchanged from penalized MLE).
		Eigen::Map<Vector<double>> beta_full(const_cast<double *>(model.beta.data()), p_draw);
		Eigen::Map<Matrix<double>> vcov_full(const_cast<double *>(model.vcov.data()), p_draw, p_draw);

		// Cholesky of full posterior covariance for correlated draws.
		Eigen::LLT<Eigen::MatrixXd> chol_full(vcov_full);

		if (chol_full.info() == Eigen::Success)
		{
			// Scalar inverse link.
			std::function<double(double)> linkinv_fn;
			if (model.family == "binomial" || model.family == "beta") {
				linkinv_fn = [](double eta) { return 1.0 / (1.0 + std::exp(-eta)); };
			} else if (model.family == "poisson" || model.family == "negbin") {
				linkinv_fn = [](double eta) { return std::exp(std::clamp(eta, -30.0, 30.0)); };
			} else {
				// Gaussian, Student: identity link
				linkinv_fn = [](double eta) { return eta; };
			}

			std::vector<double> loglik_matrix(n * S);
			std::mt19937 rng(SEED);
			std::normal_distribution<double> std_normal(0.0, 1.0);

			for (int s = 0; s < S; s++)
			{
				// Draw β^(s) ~ N(β_full, Σ_full)
				Eigen::VectorXd z(p_draw);
				for (intptr_t j = 0; j < p_draw; j++)
					z[j] = std_normal(rng);

				Eigen::VectorXd beta_s = beta_full + chol_full.matrixL() * z;

				// η = X β^(s) + offset
				Eigen::VectorXd eta = Xm * beta_s;
				if (!model.offset.empty()) {
					Eigen::Map<const Eigen::VectorXd> off(model.offset.data(), n);
					eta += off;
				}

				for (intptr_t i = 0; i < n; i++)
				{
					double mu_i = linkinv_fn(eta[i]);
					loglik_matrix[i * S + s] = pointwise_loglik(ym[i], mu_i, model);
				}
			}

			compute_waic_from_loglik(model, loglik_matrix, n, S);
		}
		// If Cholesky fails (ill-conditioned penalty matrix), WAIC is skipped.
	}

	// ── 8. Set estimation method and store priors ───────────────────

	model.estimation = Estimation::Bayesian;
	model.priors = priors;
}

} // namespace phonometrica::stats
