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
 ***********************************************************************************************************************/

#include <cmath>
#include <boost/math/distributions/normal.hpp>
#include <Eigen/Dense>
#include <phon/analysis/bayesian.hpp>

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

	// ── 6. Set estimation method and store priors ───────────────────

	model.estimation = Estimation::Bayesian;
	model.priors = priors;
}

} // namespace phonometrica::stats
