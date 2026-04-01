/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 31/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <random>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <boost/math/distributions/binomial.hpp>
#include <boost/math/distributions/chi_squared.hpp>
#include <phon/analysis/scaled_residuals.hpp>
#include <phon/analysis/family.hpp>

namespace phonometrica::stats {
namespace {

// =====================================================================
// Kolmogorov-Smirnov test against U(0,1)
// =====================================================================

static std::pair<double, double> ks_test_uniform(const double *data, intptr_t n)
{
	if (n <= 0) return {0.0, 1.0};

	std::vector<double> sorted(data, data + n);
	std::sort(sorted.begin(), sorted.end());

	double d_plus = 0, d_minus = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double fn      = double(i + 1) / n;
		double fn_prev = double(i)     / n;

		d_plus  = std::max(d_plus,  fn - sorted[i]);
		d_minus = std::max(d_minus, sorted[i] - fn_prev);
	}
	double D = std::max(d_plus, d_minus);

	// Asymptotic p-value via Kolmogorov distribution.
	double lambda = std::sqrt(double(n)) * D;
	double pval = 0;
	for (int k = 1; k <= 200; k++)
	{
		double term = std::exp(-2.0 * k * k * lambda * lambda);
		if (k % 2 == 1)
			pval += term;
		else
			pval -= term;
		if (term < 1e-15) break;
	}
	pval *= 2.0;
	pval = std::clamp(pval, 0.0, 1.0);

	return {D, pval};
}

// =====================================================================
// Dispersion test
// =====================================================================

static std::pair<double, double> dispersion_test(const double *data, intptr_t n)
{
	if (n < 2) return {1.0, 1.0};

	double sum = 0;
	for (intptr_t i = 0; i < n; i++) sum += data[i];
	double mean = sum / n;

	double ss = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double d = data[i] - mean;
		ss += d * d;
	}
	double s2 = ss / (n - 1);

	constexpr double expected_var = 1.0 / 12.0;
	double ratio = s2 / expected_var;

	double chi2 = (n - 1) * s2 / expected_var;
	boost::math::chi_squared_distribution<double> dist(double(n - 1));
	double p_lower = boost::math::cdf(dist, chi2);
	double p_upper = 1.0 - p_lower;
	double pval = 2.0 * std::min(p_lower, p_upper);
	pval = std::clamp(pval, 0.0, 1.0);

	return {ratio, pval};
}

// =====================================================================
// Outlier test
// =====================================================================

// Two-sided exact binomial test.
// Under H0, each observation has probability p0 = 2/(nsim+1) of being an outlier.
static double outlier_test(int n_outliers, intptr_t n, int nsim)
{
	if (n <= 0 || nsim <= 0) return 1.0;

	double p0 = 2.0 / (nsim + 1);
	double expected = n * p0;

	boost::math::binomial_distribution<double> binom(double(n), p0);

	double pval;
	if (n_outliers >= expected)
		pval = (n_outliers > 0) ? (1.0 - boost::math::cdf(binom, n_outliers - 1)) : 1.0;
	else
		pval = boost::math::cdf(binom, n_outliers);

	return std::min(2.0 * pval, 1.0);
}

// =====================================================================
// Diagnostics
// =====================================================================

static void run_diagnostics(ScaledResidualResult &result, intptr_t n)
{
	auto [D, ks_p] = ks_test_uniform(result.residuals.data(), n);
	result.ks_statistic = D;
	result.ks_pvalue = ks_p;

	auto [ratio, disp_p] = dispersion_test(result.residuals.data(), n);
	result.dispersion_ratio = ratio;
	result.dispersion_pvalue = disp_p;
}


// =====================================================================
// Simulation engine
// =====================================================================

static constexpr int NSIM = 250;
static constexpr unsigned int SEED = 42;


static Eigen::MatrixXd unpack_cholesky(const Array<double> &packed, intptr_t q)
{
	Eigen::MatrixXd L = Eigen::MatrixXd::Zero(q, q);
	intptr_t idx = 1; // 1-based Array
	for (intptr_t r = 0; r < q; r++) {
		for (intptr_t c = 0; c <= r; c++) {
			L(r, c) = packed[idx++];
		}
	}
	return L;
}

static bool has_z_info(const Model &m)
{
	for (intptr_t g = 1; g <= m.random_effects.size(); g++)
	{
		auto &re = m.random_effects[g];
		if (re.indices.empty() || re.Z_design.empty())
			return false;
	}
	return true;
}


static ScaledResidualResult compute_simulation(const Model &m)
{
	intptr_t n = m.nobs;
	intptr_t G = m.random_effects.size();

	// Use full random-effects simulation only if we have Z info.
	bool mixed = (G > 0) && has_z_info(m);

	Family fam = Family::from_name(m.family);
	if (m.family == "negbin") {
		fam = Family::negbin(m.theta);
	}

	// For mixed models: compute X*beta and unpack Cholesky factors once.
	Vector<double> Xbeta;
	std::vector<Eigen::MatrixXd> L_factors;

	if (mixed)
	{
		intptr_t p = m.nfixed;
		Eigen::Map<const Matrix<double>> Xm(m.X.data(), n, p);
		Eigen::Map<const Vector<double>> bm(m.beta.data(), p);
		Xbeta = Xm * bm;

		L_factors.resize(G);
		for (intptr_t g = 0; g < G; g++)
		{
			auto &re = m.random_effects[g + 1];
			L_factors[g] = unpack_cholesky(re.cov_chol, re.nterms);
		}
	}

	std::vector<double> sim_y(n * NSIM);

	std::mt19937 rng(SEED);
	std::normal_distribution<double> std_normal(0.0, 1.0);

	for (int s = 0; s < NSIM; s++)
	{
		Vector<double> mu(n);

		if (mixed)
		{
			Vector<double> eta = Xbeta;

			for (intptr_t g = 0; g < G; g++)
			{
				auto &re = m.random_effects[g + 1];
				intptr_t q = re.nterms;
				intptr_t J = re.nlevels;

				std::vector<double> b(J * q);
				for (intptr_t j = 0; j < J; j++)
				{
					Eigen::VectorXd z(q);
					for (intptr_t t = 0; t < q; t++)
						z(t) = std_normal(rng);

					Eigen::VectorXd b_j = L_factors[g] * z;
					for (intptr_t t = 0; t < q; t++)
						b[j * q + t] = b_j(t);
				}

				for (intptr_t i = 0; i < n; i++)
				{
					intptr_t j = re.indices[i];
					for (intptr_t t = 0; t < q; t++)
						eta[i] += re.Z_design[i * q + t] * b[j * q + t];
				}
			}

			mu = fam.linkinv(eta);
		}
		else
		{
			for (intptr_t i = 0; i < n; i++)
				mu[i] = m.fitted[i + 1];
		}

		// Draw y_sim ~ Family(mu)
		for (intptr_t i = 0; i < n; i++)
		{
			double mu_i = mu[i];
			double y_sim;

			if (m.family == "gaussian")
			{
				std::normal_distribution<double> dist(mu_i, std::max(m.rse, 1e-10));
				y_sim = dist(rng);
			}
			else if (m.family == "binomial")
			{
				mu_i = std::clamp(mu_i, 1e-10, 1.0 - 1e-10);
				std::bernoulli_distribution dist(mu_i);
				y_sim = dist(rng) ? 1.0 : 0.0;
			}
			else if (m.family == "poisson")
			{
				mu_i = std::max(mu_i, 1e-10);
				std::poisson_distribution<int> dist(mu_i);
				y_sim = (double)dist(rng);
			}
			else if (m.family == "negbin")
			{
				mu_i = std::max(mu_i, 1e-10);
				double shape = std::max(m.theta, 1e-10);
				double scale = mu_i / shape;
				std::gamma_distribution<double> gamma_dist(shape, scale);
				double g = gamma_dist(rng);
				std::poisson_distribution<int> pois_dist(std::max(g, 1e-10));
				y_sim = (double)pois_dist(rng);
			}
			else
			{
				y_sim = mu_i;
			}

			sim_y[i * NSIM + s] = y_sim;
		}
	}

	// Rank observed y among simulated values.
	ScaledResidualResult result;
	result.residuals.resize(n);

	int n_outliers = 0;

	for (intptr_t i = 0; i < n; i++)
	{
		double y_obs = m.y[i + 1];
		int count_below = 0;
		int count_equal = 0;

		for (int s = 0; s < NSIM; s++)
		{
			double y_s = sim_y[i * NSIM + s];
			if (y_s < y_obs)
				count_below++;
			else if (y_s == y_obs)
				count_equal++;
		}

		// Outlier: y_obs falls entirely outside the simulated range.
		if ((count_below == 0 && count_equal == 0) || count_below == NSIM)
			n_outliers++;

		double pit = ((double)count_below + 0.5 * (double)count_equal) / (double)NSIM;
		result.residuals[i + 1] = std::clamp(pit, 1e-10, 1.0 - 1e-10);
	}

	result.n_outliers = n_outliers;
	result.outlier_pvalue = outlier_test(n_outliers, n, NSIM);

	run_diagnostics(result, n);
	return result;
}


} // anonymous namespace


// =====================================================================
// Public API
// =====================================================================

ScaledResidualResult compute_scaled_residuals(const Model &m)
{
	if (m.nobs == 0 || m.y.empty() || m.fitted.empty())
		throw std::runtime_error("Model has no data for scaled residual computation.");

	return compute_simulation(m);
}

} // namespace phonometrica::stats
