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
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/math/distributions/negative_binomial.hpp>
#include <boost/math/distributions/beta.hpp>
#include <boost/math/distributions/students_t.hpp>
#include <Eigen/Dense>
#include <phon/analysis/scaled_residuals.hpp>
#include <phon/analysis/family.hpp>
#include <phon/utils/matrix.hpp>

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
	if (m.family == "student") {
		fam = Family::student(m.sigma, m.nu);
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
			else if (m.family == "student")
			{
				// t(μ, σ, ν): location-scale t distribution.
				// Draw t ~ t(ν), then y = μ + σ * t.
				double sigma_val = std::max(m.sigma, 1e-10);
				double nu_val = std::max(m.nu, 1.01);
				std::student_t_distribution<double> dist(nu_val);
				y_sim = mu_i + sigma_val * dist(rng);
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

	bool is_discrete = (m.family != "gaussian" && m.family != "beta" && m.family != "student");

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


// =====================================================================
// Posterior predictive checks (Bayesian models)
// =====================================================================
//
// For Bayesian models, replace frequentist p-values with posterior
// predictive p-values.  The approach:
//
//   1. Keep the existing scaled residuals and observed test statistics
//      T_obs (KS D, dispersion ratio, outlier count).
//
//   2. For r = 1..R:
//      a. Draw (β^(r), disp^(r)) from the posterior.
//      b. Compute μ_i^(r) = linkinv(x_i'β^(r) + zu_i).
//      c. Simulate y_rep_i ~ Family(μ_i^(r), disp^(r)).
//      d. Compute CDF-based PIT: u_i^(r) = F(y_rep_i | μ_i^(r), disp^(r)).
//      e. Compute T^(r) = KS D, dispersion ratio, outlier count on u^(r).
//
//   3. pp-value = proportion of T^(r) ≥ T_obs.
//
// Reference:
//   Gelman, A., Meng, X.-L. & Stern, H. (1996). Posterior predictive
//     assessment of model fitness via realized discrepancies.
//     Statistica Sinica 6(4), 733-760.

static constexpr int PPC_R = 200;
static constexpr unsigned int PPC_SEED = 54321;


// ── Analytical CDF for PIT computation ──────────────────────────────
//
// For continuous families: returns F(y | μ, disp).
// For discrete families: returns randomised PIT (uniform jitter within
// the tied probability mass) to ensure U(0,1) under the true model.

static double analytical_pit(double y, double mu, const String &family,
                              double disp0, double disp1, std::mt19937 &rng)
{
	if (family == "gaussian")
	{
		double sigma = std::max(disp0, 1e-10);
		boost::math::normal_distribution<double> dist(mu, sigma);
		return boost::math::cdf(dist, y);
	}
	else if (family == "binomial")
	{
		// Bernoulli: P(Y=0) = 1-p, P(Y=1) = p.
		double p = std::clamp(mu, 1e-10, 1.0 - 1e-10);
		double jitter = std::uniform_real_distribution<double>(0.0, 1.0)(rng);
		if (y >= 1.0) {
			// F(0) + jitter × P(Y=1) = (1-p) + jitter × p
			return (1.0 - p) + jitter * p;
		} else {
			// F(-1) + jitter × P(Y=0) = 0 + jitter × (1-p)
			return jitter * (1.0 - p);
		}
	}
	else if (family == "poisson")
	{
		double lambda = std::max(mu, 1e-10);
		boost::math::poisson_distribution<double> dist(lambda);
		double jitter = std::uniform_real_distribution<double>(0.0, 1.0)(rng);
		int yi = std::max((int)std::round(y), 0);
		double F_below = (yi > 0) ? boost::math::cdf(dist, yi - 1) : 0.0;
		double pmf = boost::math::pdf(dist, yi);
		return F_below + jitter * pmf;
	}
	else if (family == "negbin")
	{
		double theta = std::max(disp0, 1e-10);
		mu = std::max(mu, 1e-10);
		// Boost NB: success_fraction = p = θ/(θ+μ), successes = θ.
		double p_nb = theta / (theta + mu);
		boost::math::negative_binomial_distribution<double> dist(theta, p_nb);
		double jitter = std::uniform_real_distribution<double>(0.0, 1.0)(rng);
		int yi = std::max((int)std::round(y), 0);
		double F_below = (yi > 0) ? boost::math::cdf(dist, yi - 1) : 0.0;
		double pmf = boost::math::pdf(dist, yi);
		return F_below + jitter * pmf;
	}
	else if (family == "beta")
	{
		double phi = std::max(disp0, 1e-10);
		mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
		y = std::clamp(y, 1e-10, 1.0 - 1e-10);
		double a = mu * phi;
		double b = (1.0 - mu) * phi;
		boost::math::beta_distribution<double> dist(a, b);
		return boost::math::cdf(dist, y);
	}
	else if (family == "student")
	{
		double sigma = std::max(disp0, 1e-10);
		double nu = std::max(disp1, 1.01);
		// Standardise: z = (y - μ) / σ, then use Student-t CDF with ν df.
		double z = (y - mu) / sigma;
		boost::math::students_t_distribution<double> dist(nu);
		return boost::math::cdf(dist, z);
	}

	return 0.5; // fallback
}


// ── Simulate one observation from Family(μ, disp) ──────────────────
//
// This is the same simulation logic as in compute_simulation, extracted
// as a per-observation function for PPC.

static double simulate_one(double mu, const String &family,
                            double disp0, double disp1, std::mt19937 &rng)
{
	if (family == "gaussian")
	{
		std::normal_distribution<double> dist(mu, std::max(disp0, 1e-10));
		return dist(rng);
	}
	else if (family == "binomial")
	{
		mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
		std::bernoulli_distribution dist(mu);
		return dist(rng) ? 1.0 : 0.0;
	}
	else if (family == "poisson")
	{
		mu = std::max(mu, 1e-10);
		std::poisson_distribution<int> dist(mu);
		return (double)dist(rng);
	}
	else if (family == "negbin")
	{
		mu = std::max(mu, 1e-10);
		double shape = std::max(disp0, 1e-10);
		double scale = mu / shape;
		std::gamma_distribution<double> gamma_dist(shape, scale);
		double g = gamma_dist(rng);
		std::poisson_distribution<int> pois_dist(std::max(g, 1e-10));
		return (double)pois_dist(rng);
	}
	else if (family == "beta")
	{
		mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
		double phi = std::max(disp0, 1e-10);
		double a = mu * phi;
		double b = (1.0 - mu) * phi;
		std::gamma_distribution<double> g1(a, 1.0), g2(b, 1.0);
		double v1 = g1(rng), v2 = g2(rng);
		return std::clamp(v1 / (v1 + v2), 1e-10, 1.0 - 1e-10);
	}
	else if (family == "student")
	{
		double sigma_val = std::max(disp0, 1e-10);
		double nu_val = std::max(disp1, 1.01);
		std::student_t_distribution<double> dist(nu_val);
		return mu + sigma_val * dist(rng);
	}
	return mu;
}


// ── Number of dispersion parameters for a given family ──────────────

static int n_disp_params(const String &family)
{
	if (family == "gaussian" || family == "negbin" || family == "beta") return 1;
	if (family == "student") return 2;
	return 0;
}


// ── Extract dispersion from θ_k at a grid point ────────────────────

static void disp_from_theta(const String &family, const double *theta_k,
                             int n_theta, double *disp)
{
	int nd = n_disp_params(family);
	int n_chol = n_theta - nd;

	if (family == "gaussian" || family == "negbin" || family == "beta") {
		disp[0] = std::exp(theta_k[n_chol]);
	}
	else if (family == "student") {
		disp[0] = std::exp(theta_k[n_chol]);
		disp[1] = std::clamp(std::exp(theta_k[n_chol + 1]), 2.0, 200.0);
	}
}


// ── Scalar link / inverse link ──────────────────────────────────────

static double link_fn(double mu, const String &family)
{
	if (family == "binomial" || family == "beta") {
		mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
		return std::log(mu / (1.0 - mu));
	}
	if (family == "poisson" || family == "negbin") {
		return std::log(std::max(mu, 1e-10));
	}
	return mu; // identity for gaussian, student
}

static double linkinv_fn(double eta, const String &family)
{
	if (family == "binomial" || family == "beta") {
		return 1.0 / (1.0 + std::exp(-eta));
	}
	if (family == "poisson" || family == "negbin") {
		return std::exp(std::clamp(eta, -30.0, 30.0));
	}
	return eta; // identity for gaussian, student
}


// ── Main PPC function ───────────────────────────────────────────────
//
// Requires: model.X, model.y, model.fitted, model.beta, and either
// model.grid_summary (for grid-integrated models) or model.vcov
// (for fixed-effects Bayesian models).

static bool compute_ppc(ScaledResidualResult &result, const Model &m)
{
	intptr_t n = m.nobs;

	// Check prerequisites.
	if (m.X.empty() || m.y.empty() || m.fitted.empty() || m.beta.empty())
		return false;

	bool has_grid = m.grid_summary.has_value();
	bool has_vcov = m.has_vcov();

	if (!has_grid && !has_vcov)
		return false;

	// Total coefficients for posterior draws.
	// For GAMs, model.beta includes smooth basis coefficients beyond nfixed.
	// For grid models, GridSummary.n_beta already covers the full X column count.
	intptr_t p_draw;
	if (has_grid)
		p_draw = m.grid_summary->n_beta;
	else
		p_draw = m.beta.size();

	// Sanity check: X column count must match.
	if (m.X.ndim() == 2 && m.X.ncol() != p_draw)
		p_draw = m.nfixed;  // fallback

	// ── 1. Precompute random-effects offset ──────────────────────

	// zu_i = link(fitted_i) - x_i'β
	// For GAMs without RE, this is zero (all prediction is in Xβ).
	// For mixed models, this captures the BLUP contribution.
	Eigen::Map<Matrix<double>> Xm(const_cast<double *>(m.X.data()), n, p_draw);
	Eigen::Map<Vector<double>> beta_m(const_cast<double *>(m.beta.data()), p_draw);
	Eigen::VectorXd Xb = Xm * beta_m;

	std::vector<double> zu(n);
	for (intptr_t i = 0; i < n; i++)
		zu[i] = link_fn(m.fitted[i + 1], m.family) - Xb[i];

	// ── 2. Prepare posterior draw mechanism ──────────────────────

	// Grid path: per-grid-point independent draws from diag(Σ_k).
	// Fixed-effects path: correlated draws from full vcov.
	std::optional<Eigen::LLT<Eigen::MatrixXd>> chol_vcov;
	if (!has_grid && has_vcov)
	{
		Eigen::Map<Matrix<double>> vcov_m(const_cast<double *>(m.vcov.data()), p_draw, p_draw);
		Eigen::LLT<Eigen::MatrixXd> chol(vcov_m);
		if (chol.info() != Eigen::Success)
			return false;  // ill-conditioned; skip PPC
		chol_vcov.emplace(std::move(chol));
	}

	// Grid cumulative weights for sampling.
	std::vector<double> grid_cdf;
	if (has_grid)
	{
		auto &gs = *m.grid_summary;
		grid_cdf.resize(gs.n_points);
		grid_cdf[0] = gs.weights[0];
		for (int k = 1; k < gs.n_points; k++)
			grid_cdf[k] = grid_cdf[k - 1] + gs.weights[k];
	}

	// Default dispersion from model (for non-grid path or families without θ variation).
	double default_disp[2] = {0.0, 0.0};
	if (m.family == "gaussian")       default_disp[0] = std::max(m.rse, 1e-10);
	else if (m.family == "negbin")    default_disp[0] = std::max(m.theta, 1e-10);
	else if (m.family == "beta")      default_disp[0] = std::max(m.phi, 1e-10);
	else if (m.family == "student") {
		default_disp[0] = std::max(m.sigma, 1e-10);
		default_disp[1] = std::max(m.nu, 1.01);
	}

	// ── 3. Observed test statistics (already computed) ───────────

	double D_obs = result.ks_statistic;
	double disp_obs = std::abs(result.dispersion_ratio - 1.0);
	int outlier_obs = result.n_outliers;

	// Outlier thresholds (same as in compute_simulation).
	const double outlier_lo = 1.0 / (NSIM + 1);
	const double outlier_hi = 1.0 - outlier_lo;

	// ── 4. Generate R replicates and compute test statistics ─────

	int count_ks = 0;
	int count_disp = 0;
	int count_outlier = 0;

	std::mt19937 rng(PPC_SEED);
	std::uniform_real_distribution<double> unif(0.0, 1.0);
	std::normal_distribution<double> std_normal(0.0, 1.0);

	std::vector<double> pit_rep(n);

	for (int r = 0; r < PPC_R; r++)
	{
		// ── Draw (β, disp) from posterior ────────────────────────

		Eigen::VectorXd beta_draw(p_draw);
		double disp[2] = {default_disp[0], default_disp[1]};

		if (has_grid)
		{
			auto &gs = *m.grid_summary;
			// Sample grid point k.
			double u = unif(rng);
			int k = (int)(std::lower_bound(grid_cdf.begin(), grid_cdf.end(), u) - grid_cdf.begin());
			k = std::min(k, gs.n_points - 1);

			// Draw β ~ N(β̂_k, diag(Σ_k))  [independent draws]
			for (intptr_t j = 0; j < p_draw; j++)
			{
				double sd = std::sqrt(std::max(gs.vcov_diag[k * p_draw + j], 1e-20));
				beta_draw[j] = gs.beta[k * p_draw + j] + sd * std_normal(rng);
			}

			// Dispersion from θ_k.
			if (n_disp_params(m.family) > 0) {
				disp_from_theta(m.family, gs.theta.data() + k * gs.n_theta,
				                 gs.n_theta, disp);
			}
		}
		else
		{
			// Draw β ~ N(β_post, Σ_post)  [correlated draws]
			Eigen::VectorXd z(p_draw);
			for (intptr_t j = 0; j < p_draw; j++)
				z[j] = std_normal(rng);
			beta_draw = beta_m + chol_vcov->matrixL() * z;
		}

		// ── Compute μ and simulate y_rep, then CDF-PIT ──────────

		Eigen::VectorXd eta = Xm * beta_draw;
		int outlier_count = 0;

		for (intptr_t i = 0; i < n; i++)
		{
			double eta_i = eta[i] + zu[i];
			double mu_i = linkinv_fn(eta_i, m.family);

			// Simulate y_rep.
			double y_rep = simulate_one(mu_i, m.family, disp[0], disp[1], rng);

			// CDF-based PIT of y_rep against its own generating distribution.
			double u_rep = analytical_pit(y_rep, mu_i, m.family, disp[0], disp[1], rng);
			u_rep = std::clamp(u_rep, 1e-10, 1.0 - 1e-10);
			pit_rep[i] = u_rep;

			if (u_rep < outlier_lo || u_rep > outlier_hi)
				outlier_count++;
		}

		// ── Compute test statistics on the replicate ─────────────

		auto [D_rep, ks_p_rep] = ks_test_uniform(pit_rep.data(), n);
		auto [ratio_rep, disp_p_rep] = dispersion_test(pit_rep.data(), n);

		if (D_rep >= D_obs)
			count_ks++;
		if (std::abs(ratio_rep - 1.0) >= disp_obs)
			count_disp++;
		if (outlier_count >= outlier_obs)
			count_outlier++;
	}

	// ── 5. Posterior predictive p-values ─────────────────────────

	result.ks_pvalue = (double)count_ks / PPC_R;
	result.dispersion_pvalue = (double)count_disp / PPC_R;
	result.outlier_pvalue = (double)count_outlier / PPC_R;
	result.is_ppc = true;

	return true;
}


} // anonymous namespace


// =====================================================================
// Public API
// =====================================================================

ScaledResidualResult compute_scaled_residuals(const Model &m)
{
	if (m.nobs == 0 || m.y.empty() || m.fitted.empty())
		throw std::runtime_error("Model has no data for scaled residual computation.");

	auto result = compute_simulation(m);

	// For Bayesian models, replace frequentist p-values with posterior
	// predictive p-values.  The scaled residuals themselves are unchanged;
	// only the calibration tests (KS, dispersion, outlier) are recomputed.
	if (m.is_bayesian())
		compute_ppc(result, m);

	return result;
}

} // namespace phonometrica::stats
