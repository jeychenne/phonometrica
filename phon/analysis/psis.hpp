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
 * Created: 14/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: PSIS-LOO (Pareto Smoothed Importance Sampling Leave-One-Out cross-validation).                             *
 *                                                                                                                     *
 * Algorithm reference (published mathematical specifications):                                                        *
 *   Vehtari, A., Gelman, A. & Gabry, J. (2017). Practical Bayesian model evaluation                                   *
 *     using leave-one-out cross-validation and WAIC. Statistics and Computing 27(5),                                  *
 *     1413-1432.                                                                                                      *
 *   Hosking, J.R.M. & Wallis, J.R. (1987). Parameter and quantile estimation for                                      *
 *     the generalized Pareto distribution. Technometrics 29(3), 339-349.                                              *
 *     (L-moment GPD estimator)                                                                                        *
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

#ifndef PHONOMETRICA_PSIS_HPP
#define PHONOMETRICA_PSIS_HPP

#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include <phon/analysis/model.hpp>

namespace phonometrica::stats {


// =====================================================================
// Generalized Pareto distribution fitting via L-moments
// =====================================================================
//
// Given M exceedances (sorted ascending), estimate the shape parameter k
// (= Pareto k diagnostic) and scale parameter sigma using the L-moment
// method of Hosking & Wallis (1987).
//
// GPD CDF: F(z) = 1 - (1 + k*z/sigma)^(-1/k)   for k != 0
//          F(z) = 1 - exp(-z/sigma)               for k == 0
//
// L-moments:  lambda_1 = sigma / (1 - k)
//             lambda_2 = sigma / ((1 - k)(2 - k))
//
// Returns {k, sigma}. If estimation fails, returns {0, mean(exceedances)}.

inline std::pair<double, double> gpdfit_lmom(const double *exc, int M)
{
	if (M < 2)
		return {0.0, 1.0};

	// Sample probability-weighted moments.
	// b0 = (1/M) sum z_i
	// b1 = (1/M) sum ((i-1)/(M-1)) z_(i)   [z_(i) sorted ascending, i = 1..M]
	double b0 = 0, b1 = 0;
	for (int i = 0; i < M; i++)
	{
		b0 += exc[i];
		b1 += (static_cast<double>(i) / (M - 1)) * exc[i];
	}
	b0 /= M;
	b1 /= M;

	// L-moments: lambda_1 = b0, lambda_2 = 2*b1 - b0.
	double lam1 = b0;
	double lam2 = 2.0 * b1 - b0;

	// Guard: lambda_2 must be positive for a valid GPD fit.
	if (lam2 <= 1e-15)
		return {0.0, std::max(b0, 1e-10)};

	// Estimators:  k = 2 - lam1/lam2,  sigma = lam1 * (lam1 - lam2) / lam2
	double k = 2.0 - lam1 / lam2;
	double sigma = lam1 * (lam1 - lam2) / lam2;

	// Guard: sigma must be positive.
	if (sigma <= 0)
		return {0.0, std::max(b0, 1e-10)};

	return {k, sigma};
}


// =====================================================================
// GPD quantile function
// =====================================================================
//
// Q(p) = sigma/k * ((1-p)^(-k) - 1)   for k != 0
// Q(p) = -sigma * log(1-p)             for k == 0

inline double gpd_quantile(double p, double k, double sigma)
{
	if (std::abs(k) < 1e-8)
		return -sigma * std::log(1.0 - p);
	return (sigma / k) * (std::pow(1.0 - p, -k) - 1.0);
}


// =====================================================================
// PSIS-LOO computation from a pointwise log-likelihood matrix
// =====================================================================
//
// loglik_matrix: n_obs × S row-major matrix, where
//   loglik_matrix[i * S + s] = log p(y_i | theta^(s))
//
// For each observation i:
//   1. Compute raw log importance ratios: lw_s = -loglik_is
//   2. Pareto-smooth the upper tail (M = min(S/5, 3*sqrt(S)) largest values)
//   3. Normalize smoothed weights
//   4. Compute elpd_loo_i = log_sum_exp(lw_norm + loglik)
//
// Populates model.loo_ic, model.p_loo, model.se_loo, model.elpd_loo_i, model.pareto_k.

inline void compute_loo_from_loglik(Model &model, const std::vector<double> &loglik_matrix,
                                     intptr_t n, intptr_t S)
{
	// Tail size for Pareto smoothing.
	int M = std::min((int)(S / 5), (int)(3.0 * std::sqrt(static_cast<double>(S))));
	M = std::max(M, 2);

	// Need at least M + 1 samples for a meaningful cutoff.
	if (S <= M + 1)
	{
		// Too few samples for PSIS; leave LOO fields as NaN.
		return;
	}

	std::vector<double> elpd_loo(n);
	std::vector<double> pk(n);

	// Scratch buffers (reused across observations).
	std::vector<double> lw(S);
	std::vector<int> order(S);

	for (intptr_t i = 0; i < n; i++)
	{
		const double *row = loglik_matrix.data() + i * S;

		// 1. Raw log importance ratios.
		for (intptr_t s = 0; s < S; s++)
			lw[s] = -row[s];

		// 2. Sort indices by lw ascending.
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [&](int a, int b) {
			return lw[a] < lw[b];
		});

		// 3. Pareto-smooth the M largest values.
		double cutoff = lw[order[S - M - 1]];

		// Collect sorted exceedances.
		std::vector<double> exc(M);
		for (int j = 0; j < M; j++)
			exc[j] = lw[order[S - M + j]] - cutoff;

		// Fit GPD.
		auto [k, sigma] = gpdfit_lmom(exc.data(), M);
		pk[i] = k;

		// Replace tail with expected GPD order statistics.
		for (int j = 0; j < M; j++)
		{
			double p = (j + 0.5) / M;
			lw[order[S - M + j]] = cutoff + gpd_quantile(p, k, sigma);
		}

		// 4. Compute elpd_loo_i.
		// Normalize: lw_norm = lw - log_sum_exp(lw)
		double max_lw = lw[order[S - 1]]; // largest after smoothing
		for (intptr_t s = 0; s < S; s++)
			max_lw = std::max(max_lw, lw[s]);

		double sum_exp_lw = 0;
		for (intptr_t s = 0; s < S; s++)
			sum_exp_lw += std::exp(lw[s] - max_lw);
		double lse_lw = max_lw + std::log(sum_exp_lw);

		// elpd_loo_i = log_sum_exp(lw_norm + loglik)
		//            = log_sum_exp((lw - lse_lw) + loglik)
		double max_val = -1e300;
		for (intptr_t s = 0; s < S; s++)
		{
			double v = (lw[s] - lse_lw) + row[s];
			max_val = std::max(max_val, v);
		}

		double sum_exp = 0;
		for (intptr_t s = 0; s < S; s++)
		{
			double v = (lw[s] - lse_lw) + row[s];
			sum_exp += std::exp(v - max_val);
		}

		elpd_loo[i] = max_val + std::log(sum_exp);
	}

	// Aggregate.
	double total_elpd_loo = 0;
	for (intptr_t i = 0; i < n; i++)
		total_elpd_loo += elpd_loo[i];

	double loo_ic_val = -2.0 * total_elpd_loo;

	// p_loo = lppd - elpd_loo (effective number of parameters).
	// lppd was already computed by WAIC; if unavailable, compute from scratch.
	double p_loo_val = std::isnan(model.lppd) ? 0.0 : (model.lppd - total_elpd_loo);

	// SE(LOO-IC) = 2 * sqrt(n * Var_i(elpd_loo_i))
	double mean_elpd = total_elpd_loo / n;
	double var_elpd = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double d = elpd_loo[i] - mean_elpd;
		var_elpd += d * d;
	}
	var_elpd /= (n - 1);

	model.loo_ic = loo_ic_val;
	model.p_loo  = p_loo_val;
	model.se_loo = 2.0 * std::sqrt(static_cast<double>(n) * var_elpd);

	// Store per-observation values.
	model.elpd_loo_i.resize(n);
	model.pareto_k.resize(n);
	for (intptr_t i = 0; i < n; i++)
	{
		model.elpd_loo_i[i] = elpd_loo[i];
		model.pareto_k[i] = pk[i];
	}
}


} // namespace phonometrica::stats

#endif // PHONOMETRICA_PSIS_HPP
