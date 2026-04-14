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
 * Created: 13/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: WAIC (Watanabe-Akaike Information Criterion) computation.                                                  *
 *                                                                                                                     *
 * WAIC is a fully observation-level Bayesian model comparison criterion, asymptotically                               *
 * equivalent to leave-one-out cross-validation.                                                                       *
 *                                                                                                                     *
 * Algorithm reference (published mathematical specification):                                                          *
 *   Gelman, A., Hwang, J. & Vehtari, A. (2014). Understanding predictive information                                 *
 *     criteria for Bayesian models. Statistics and Computing 24(6), 997-1016.                                         *
 *   Vehtari, A., Gelman, A. & Gabry, J. (2017). Practical Bayesian model evaluation                                  *
 *     using leave-one-out cross-validation and WAIC. Statistics and Computing 27(5),                                  *
 *     1413-1432.                                                                                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_WAIC_HPP
#define PHONOMETRICA_WAIC_HPP

#include <cmath>
#include <vector>
#include <algorithm>
#include <phon/analysis/model.hpp>

namespace phonometrica::stats {

// =====================================================================
// Scalar pointwise log-likelihood: log p(y_i | mu_i, dispersion)
// =====================================================================

// Gaussian: log N(y | mu, sigma)
inline double pointwise_loglik_gaussian(double y, double mu, double sigma)
{
	double r = (y - mu) / sigma;
	return -0.5 * std::log(2 * M_PI) - std::log(sigma) - 0.5 * r * r;
}

// Binomial (Bernoulli): y log(mu) + (1-y) log(1-mu)
inline double pointwise_loglik_binomial(double y, double mu)
{
	mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
	return y * std::log(mu) + (1.0 - y) * std::log(1.0 - mu);
}

// Poisson: y log(mu) - mu - lgamma(y+1)
inline double pointwise_loglik_poisson(double y, double mu)
{
	mu = std::max(mu, 1e-10);
	return y * std::log(mu) - mu - std::lgamma(y + 1.0);
}

// Negative binomial (NB2): log f(y | mu, theta)
inline double pointwise_loglik_negbin(double y, double mu, double theta)
{
	mu = std::max(mu, 1e-10);
	return std::lgamma(y + theta) - std::lgamma(theta) - std::lgamma(y + 1.0)
	       + theta * std::log(theta / (theta + mu))
	       + y * std::log(mu / (theta + mu));
}

// Beta: log f(y | mu, phi)
inline double pointwise_loglik_beta(double y, double mu, double phi)
{
	mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
	y = std::clamp(y, 1e-10, 1.0 - 1e-10);
	double a = mu * phi;
	double b = (1.0 - mu) * phi;
	return std::lgamma(phi) - std::lgamma(a) - std::lgamma(b)
	       + (a - 1.0) * std::log(y) + (b - 1.0) * std::log(1.0 - y);
}

// Student t: log f(y | mu, sigma, nu)
inline double pointwise_loglik_student(double y, double mu, double sigma, double nu)
{
	double r = y - mu;
	double log_const = std::lgamma(0.5 * (nu + 1.0)) - std::lgamma(0.5 * nu)
	                   - 0.5 * std::log(nu * M_PI * sigma * sigma);
	return log_const - 0.5 * (nu + 1.0) * std::log(1.0 + r * r / (nu * sigma * sigma));
}


// Dispatch pointwise log-likelihood for a given model family.
// The Model provides the family name and dispersion parameters.
inline double pointwise_loglik(double y, double mu, const Model &m)
{
	if (m.family == "gaussian")
		return pointwise_loglik_gaussian(y, mu, std::max(m.rse, 1e-10));
	if (m.family == "binomial")
		return pointwise_loglik_binomial(y, mu);
	if (m.family == "poisson")
		return pointwise_loglik_poisson(y, mu);
	if (m.family == "negbin")
		return pointwise_loglik_negbin(y, mu, std::max(m.theta, 1e-10));
	if (m.family == "beta")
		return pointwise_loglik_beta(y, mu, std::max(m.phi, 1e-10));
	if (m.family == "student")
		return pointwise_loglik_student(y, mu, std::max(m.sigma, 1e-10), std::max(m.nu, 1.01));
	return 0.0;
}

// Overload with explicit dispersion for grid points where dispersion varies with theta.
// disp[0] = sigma/theta/phi, disp[1] = nu (Student only).
inline double pointwise_loglik(double y, double mu, const String &family,
                               const double *disp)
{
	if (family == "gaussian")
		return pointwise_loglik_gaussian(y, mu, std::max(disp[0], 1e-10));
	if (family == "binomial")
		return pointwise_loglik_binomial(y, mu);
	if (family == "poisson")
		return pointwise_loglik_poisson(y, mu);
	if (family == "negbin")
		return pointwise_loglik_negbin(y, mu, std::max(disp[0], 1e-10));
	if (family == "beta")
		return pointwise_loglik_beta(y, mu, std::max(disp[0], 1e-10));
	if (family == "student")
		return pointwise_loglik_student(y, mu, std::max(disp[0], 1e-10), std::max(disp[1], 1.01));
	return 0.0;
}


// =====================================================================
// WAIC computation from a pointwise log-likelihood matrix
// =====================================================================
//
// loglik_matrix: n_obs × S row-major matrix, where
//   loglik_matrix[i * S + s] = log p(y_i | theta^(s))
//
// Populates model.waic, model.p_waic, model.lppd, model.se_waic, model.elpd_i.

inline void compute_waic_from_loglik(Model &model, const std::vector<double> &loglik_matrix,
                                     intptr_t n, intptr_t S)
{
	// Per-observation lppd and p_waic components.
	std::vector<double> lppd_i(n);
	std::vector<double> pwaic_i(n);

	for (intptr_t i = 0; i < n; i++)
	{
		const double *row = loglik_matrix.data() + i * S;

		// lppd_i = log( (1/S) sum_s exp(loglik_is) )
		//        = log_sum_exp(loglik_is) - log(S)
		double max_ll = -1e300;
		for (intptr_t s = 0; s < S; s++)
			max_ll = std::max(max_ll, row[s]);

		double sum_exp = 0;
		for (intptr_t s = 0; s < S; s++)
			sum_exp += std::exp(row[s] - max_ll);

		lppd_i[i] = max_ll + std::log(sum_exp) - std::log(static_cast<double>(S));

		// p_waic_i = Var_s(loglik_is)
		double mean_ll = 0;
		for (intptr_t s = 0; s < S; s++)
			mean_ll += row[s];
		mean_ll /= S;

		double var_ll = 0;
		for (intptr_t s = 0; s < S; s++)
		{
			double d = row[s] - mean_ll;
			var_ll += d * d;
		}
		var_ll /= (S - 1);  // sample variance

		pwaic_i[i] = var_ll;
	}

	// Aggregate.
	double total_lppd = 0;
	double total_pwaic = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		total_lppd += lppd_i[i];
		total_pwaic += pwaic_i[i];
	}

	double waic_val = -2.0 * (total_lppd - total_pwaic);

	// SE(WAIC) = sqrt(n * Var_i(2 * (lppd_i - pwaic_i)))
	std::vector<double> elpd_i(n);
	double mean_elpd = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		elpd_i[i] = 2.0 * (lppd_i[i] - pwaic_i[i]);
		mean_elpd += elpd_i[i];
	}
	mean_elpd /= n;

	double var_elpd = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double d = elpd_i[i] - mean_elpd;
		var_elpd += d * d;
	}
	var_elpd /= (n - 1);  // sample variance

	model.waic    = waic_val;
	model.p_waic  = total_pwaic;
	model.lppd    = total_lppd;
	model.se_waic = std::sqrt(static_cast<double>(n) * var_elpd);

	// Store per-observation elpd for proper SE(ΔWAIC) in model comparison.
	model.elpd_i.resize(n);
	for (intptr_t i = 0; i < n; i++)
		model.elpd_i[i + 1] = lppd_i[i] - pwaic_i[i];
}


} // namespace phonometrica::stats

#endif // PHONOMETRICA_WAIC_HPP
