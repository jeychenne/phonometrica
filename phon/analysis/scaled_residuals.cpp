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

static constexpr int NSIM = 1000;
static constexpr unsigned int SEED = 42;


static ScaledResidualResult compute_simulation(const Model &m)
{
	intptr_t n = m.nobs;

	// Simulation strategy: CONDITIONAL on estimated random effects (BLUPs).
	// This matches DHARMa's default behavior with glmmTMB (re.form = NULL),
	// where simulate() conditions on the estimated random effects rather
	// than drawing fresh ones from N(0, Σ).  The conditional approach tests
	// whether the residuals, given the random effects, follow the assumed
	// distribution.  With fresh RE draws the simulated range per observation
	// becomes much wider, suppressing outlier detection and inflating the
	// marginal variance — producing systematically wrong diagnostics.
	//
	// The conditional path simply uses model.fitted (which already includes
	// the BLUPs for mixed models) as the mean for simulation.

	Family fam = Family::from_name(m.family);
	if (m.family == "negbin") {
		fam = Family::negbin(m.theta);
	}
	if (m.family == "beta") {
		fam = Family::beta(m.phi);
	}

	std::vector<double> sim_y(n * NSIM);

	std::mt19937 rng(SEED);

	for (int s = 0; s < NSIM; s++)
	{
		// Draw y_sim ~ Family(mu) where mu = conditional fitted values (including BLUPs).
		for (intptr_t i = 0; i < n; i++)
		{
			double mu_i = m.fitted[i + 1];
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
			else if (m.family == "beta")
			{
				// Beta(a, b) where a = μφ, b = (1-μ)φ.
				// Simulate via two independent gamma variates:
				//   G1 ~ Gamma(a, 1), G2 ~ Gamma(b, 1), Y = G1/(G1+G2).
				mu_i = std::clamp(mu_i, 1e-10, 1.0 - 1e-10);
				double phi_val = std::max(m.phi, 1e-10);
				double a = mu_i * phi_val;
				double b = (1.0 - mu_i) * phi_val;
				std::gamma_distribution<double> g1(a, 1.0);
				std::gamma_distribution<double> g2(b, 1.0);
				double v1 = g1(rng);
				double v2 = g2(rng);
				y_sim = v1 / (v1 + v2);
				y_sim = std::clamp(y_sim, 1e-10, 1.0 - 1e-10);
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

	bool is_discrete = (m.family != "gaussian" && m.family != "beta");

	// DHARMa outlier thresholds: residuals in the extreme tails of U(0,1).
	// Under H0, P(U < lo) + P(U > hi) = 2/(nsim+1) per observation.
	const double outlier_lo = 1.0 / (NSIM + 1);
	const double outlier_hi = 1.0 - outlier_lo;
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

		// For continuous families (Gaussian), use midpoint PIT (ties are negligible).
		// For discrete families (binomial, Poisson, NB), use randomized PIT
		// to ensure uniformity under the correct model (DHARMa convention).
		double jitter = is_discrete ? std::uniform_real_distribution<double>(0.0, 1.0)(rng) : 0.5;
		double pit = ((double)count_below + jitter * (double)count_equal) / (double)NSIM;
		pit = std::clamp(pit, 1e-10, 1.0 - 1e-10);
		result.residuals[i + 1] = pit;

		// Outlier: scaled residual in the extreme tails.
		// This matches DHARMa's testOutliers(type = "binomial"):
		//   outliers = sum(u < 1/(nSim+1)) + sum(u > 1 - 1/(nSim+1))
		if (pit < outlier_lo || pit > outlier_hi)
			n_outliers++;
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
