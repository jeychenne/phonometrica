/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * Portions of this file (scaled-residual construction, randomized PIT, and simulation-based tests for uniformity,     *
 * dispersion, and outliers) are derived from the DHARMa R package, whose authors are Florian Hartig, Lukas Lohse,     *
 * Melina de Souza Leite, and Cosmina Werneke.  DHARMa is distributed under the GPL-3 license, and those portions are  *
 * included here under the same terms.  Reference: Hartig, F. et al. DHARMa: Residual Diagnostics for Hierarchical     *
 * (Multi-Level / Mixed) Regression Models. https://CRAN.R-project.org/package=DHARMa                                  *
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
 * Note: The core architecture and integration logic were designed and authored by Julien Eychenne. Portions of the    *
 * statistical estimation logic in this file were developed with the assistance of Claude Opus 4.6 (Anthropic), based  *
 * on published statistical literature and reference R implementations (including DHARMa, as noted above).             *
 * All AI-assisted logic has been manually audited, refactored, and validated against a diverse suite of datasets and  *
 * reference R packages to ensure mathematical accuracy and implementation integrity.                                  *
 * While every effort has been made to ensure reliability, this software is provided without a guarantee of being      *
 * bug-free. In the event that discrepancies or errors are discovered, the author will do his best to address them.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <random>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <boost/math/distributions/binomial.hpp>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/math/distributions/negative_binomial.hpp>
#include <boost/math/distributions/beta.hpp>
#include <boost/math/distributions/students_t.hpp>
#include <Eigen/Dense>
#include <phon/analysis/scaled_residuals.hpp>
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
// Dispersion test (DHARMa simulation-based)
// =====================================================================
//
// Derived from DHARMa::testDispersion(type = "DHARMa", refit = FALSE):
//   expectedVar <- sd(simulatedResponse)^2
//   spread <- function(x) var(x - fittedPredictedResponse) / expectedVar
//   observed  = spread(observedResponse)
//   simulated = apply(simulatedResponse, 2, spread)
//   p = getP(simulated, observed, alternative = "two.sided")
//
// The test statistic is the variance of the raw residuals (y - fitted) on
// the response scale, normalised by the total variance of all simulated
// responses.  Under a correctly specified model, observed and simulated
// replicates share the same distribution, so T_obs should lie within the
// empirical distribution {T_k}.  Over-dispersion → T_obs > most T_k.
//
// The p-value is computed on the empirical distribution of T_k using
// DHARMa's getP() convention for "two.sided":
//   p = min(2 * min(P(T_k >= T_obs), P(T_k <= T_obs)), 1)
// where P is estimated by relative frequency across the NSIM replicates.
//
// Parameters:
//   y_obs:        observed response, length n
//   fitted:       response-scale conditional predictions (Xβ̂ + Zb̂), length n
//   sim_y:        simulated responses, layout sim_y[i * nsim + s]
//   n:            number of observations
//   nsim:         number of simulations
//
// Returns (ratio, p_value) where ratio = var(y_obs - fitted) / mean_k[T_k_unnorm]
// (with T_k_unnorm = var(y_sim_k - fitted)); the normaliser in the statistic
// itself cancels, but the reported ratio is expressed relative to the mean
// simulated spread for interpretability (DHARMa reports ratioObsSim likewise).

static std::pair<double, double> dharma_dispersion_test(const double *y_obs,
                                                         const double *fitted,
                                                         const double *sim_y,
                                                         intptr_t n, int nsim)
{
	if (n < 2 || nsim < 2) return {1.0, 1.0};

	// ── expectedVar: variance of all simulated responses (flattened) ──
	// Matches DHARMa's sd(simulatedResponse)^2 — uses the sample variance
	// of the n * nsim flattened matrix, divisor (n*nsim - 1).
	double grand_sum = 0;
	intptr_t N_total = n * (intptr_t)nsim;
	for (intptr_t k = 0; k < N_total; k++) grand_sum += sim_y[k];
	double grand_mean = grand_sum / double(N_total);
	double grand_ss = 0;
	for (intptr_t k = 0; k < N_total; k++) {
		double d = sim_y[k] - grand_mean;
		grand_ss += d * d;
	}
	double expected_var = grand_ss / double(N_total - 1);
	if (expected_var <= 0) return {1.0, 1.0};  // degenerate (e.g. all zeros)

	// ── spread(y_obs) ────────────────────────────────────────────────
	double obs_sum = 0;
	for (intptr_t i = 0; i < n; i++) obs_sum += (y_obs[i] - fitted[i]);
	double obs_mean = obs_sum / double(n);
	double obs_ss = 0;
	for (intptr_t i = 0; i < n; i++) {
		double d = (y_obs[i] - fitted[i]) - obs_mean;
		obs_ss += d * d;
	}
	double T_obs_unnorm = obs_ss / double(n - 1);
	double T_obs = T_obs_unnorm / expected_var;

	// ── spread(sim_y[,k]) for each k ─────────────────────────────────
	std::vector<double> T_sim((size_t)nsim);
	double mean_T_sim_unnorm = 0;
	for (int s = 0; s < nsim; s++) {
		double s_sum = 0;
		for (intptr_t i = 0; i < n; i++)
			s_sum += (sim_y[i * nsim + s] - fitted[i]);
		double s_mean = s_sum / double(n);
		double s_ss = 0;
		for (intptr_t i = 0; i < n; i++) {
			double d = (sim_y[i * nsim + s] - fitted[i]) - s_mean;
			s_ss += d * d;
		}
		double T_s_unnorm = s_ss / double(n - 1);
		T_sim[s] = T_s_unnorm / expected_var;
		mean_T_sim_unnorm += T_s_unnorm;
	}
	mean_T_sim_unnorm /= double(nsim);

	// ── Two-sided empirical p-value (DHARMa getP convention) ─────────
	int count_ge = 0, count_le = 0;
	for (int s = 0; s < nsim; s++) {
		if (T_sim[s] >= T_obs) count_ge++;
		if (T_sim[s] <= T_obs) count_le++;
	}
	double p_ge = double(count_ge) / double(nsim);
	double p_le = double(count_le) / double(nsim);
	double pval = std::min(2.0 * std::min(p_ge, p_le), 1.0);

	// ── Reported ratio: var(y_obs - fitted) / mean_k[var(y_sim_k - fitted)] ──
	// Matches DHARMa's ratioObsSim = observed / mean(simulated) statistic.
	double ratio = (mean_T_sim_unnorm > 0) ? (T_obs_unnorm / mean_T_sim_unnorm) : 1.0;

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
// Diagnostics (KS only — dispersion is computed inside compute_simulation
// where the full sim_y matrix is in scope)
// =====================================================================

static void run_diagnostics(ScaledResidualResult &result, intptr_t n)
{
	auto [D, ks_p] = ks_test_uniform(result.residuals.data(), n);
	result.ks_statistic = D;
	result.ks_pvalue = ks_p;
}


// =====================================================================
// Scalar link / inverse link (used by simulation and PPC)
// =====================================================================

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


// =====================================================================
// Simulate one observation from Family(μ, disp)
// =====================================================================

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


// =====================================================================
// Simulation engine
// =====================================================================

static constexpr int NSIM = 1000;
static constexpr unsigned int SEED = 42;


static ScaledResidualResult compute_simulation(const Model &m)
{
	intptr_t n = m.nobs;

	// Simulation strategy: UNCONDITIONAL (marginal) simulation for mixed models.
	//
	// DHARMa with glmmTMB uses unconditional simulation by default: TMB's
	// simulate() re-draws random effects from N(0, Σ̂) for each replicate,
	// rather than conditioning on the estimated BLUPs.  This is critical
	// because BLUPs are functions of y (they absorb part of the residual
	// noise), so conditioning on them produces a predictive distribution
	// that is systematically too wide relative to the actual conditional
	// residuals.  The result is underdispersed PIT residuals and inflated
	// KS statistics — false positives in model diagnostics.
	//
	// The unconditional path requires the RE design info (Z_design, indices,
	// cov_chol) that is populated at fit time.  If unavailable (e.g. model
	// loaded from file), we fall back to the conditional path using
	// model.fitted directly.

	// ── Determine simulation mode ───────────────────────────────────
	bool use_unconditional = false;
	if (m.has_random_effects() && !m.X.empty() && !m.beta.empty())
	{
		use_unconditional = true;
		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			const auto &re = m.random_effects[g];
			if (re.indices.empty() || re.Z_design.empty() || re.cov_chol.empty()) {
				use_unconditional = false;
				break;
			}
		}
	}

	// ── Precompute fixed-effects linear predictor and Cholesky factors ──
	std::vector<double> eta_fixed;   // Xβ̂ on the link scale (no BLUPs)

	struct GroupChol {
		intptr_t nterms;
		intptr_t nlevels;
		Eigen::MatrixXd L;                        // nterms × nterms Cholesky factor
		const std::vector<intptr_t> *indices;      // per-obs level index [0, nlevels)
		const std::vector<double> *Z_design;       // n × nterms, row-major
	};
	std::vector<GroupChol> group_chols;

	if (use_unconditional)
	{
		// η_fixed = Xβ̂ (includes fixed effects + smooth basis, but NOT random effects).
		intptr_t p = m.beta.size();
		// Sanity: X column count must match β length. If not (shouldn't happen
		// for freshly fitted models), fall back to nfixed.
		if (m.X.ndim() == 2 && m.X.ncol() != p)
			p = m.nfixed;
		Eigen::Map<const Eigen::MatrixXd> Xm(m.X.data(), n, p);
		Eigen::Map<const Eigen::VectorXd> bm(m.beta.data(), p);
		Eigen::VectorXd Xb = Xm * bm;
		eta_fixed.resize(n);
		for (intptr_t i = 0; i < n; i++)
		{
			eta_fixed[i] = Xb[i];
			if (!m.offset.empty()) eta_fixed[i] += m.offset[i + 1];
		}

		// Unpack Cholesky factors from each RE group's cov_chol (packed lower triangle).
		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			const auto &re = m.random_effects[g];
			GroupChol gc;
			gc.nterms = re.nterms;
			gc.nlevels = re.nlevels;
			gc.L = Eigen::MatrixXd::Zero(re.nterms, re.nterms);
			intptr_t idx = 0;  // 0-based into data()
			for (intptr_t r = 0; r < re.nterms; r++) {
				for (intptr_t c = 0; c <= r; c++) {
					gc.L(r, c) = re.cov_chol.data()[idx];
					idx++;
				}
			}
			gc.indices = &re.indices;
			gc.Z_design = &re.Z_design;
			group_chols.push_back(std::move(gc));
		}
	}

	// ── Dispersion parameters ───────────────────────────────────────
	double disp0 = 0, disp1 = 0;
	if (m.family == "gaussian")       disp0 = std::max(m.rse, 1e-10);
	else if (m.family == "negbin")    disp0 = std::max(m.theta, 1e-10);
	else if (m.family == "beta")      disp0 = std::max(m.phi, 1e-10);
	else if (m.family == "student") {
		disp0 = std::max(m.sigma, 1e-10);
		disp1 = std::max(m.nu, 1.01);
	}

	// ── Simulation loop ─────────────────────────────────────────────
	std::vector<double> sim_y(n * NSIM);
	std::mt19937 rng(SEED);
	std::normal_distribution<double> std_normal(0.0, 1.0);

	for (int s = 0; s < NSIM; s++)
	{
		// For unconditional simulation: draw fresh random effects for each group.
		// b_g ~ N(0, L_g L_g') for each level j of each group g.
		std::vector<std::vector<double>> group_b;
		if (use_unconditional)
		{
			group_b.resize(group_chols.size());
			for (size_t g = 0; g < group_chols.size(); g++)
			{
				auto &gc = group_chols[g];
				group_b[g].resize(gc.nlevels * gc.nterms);
				for (intptr_t j = 0; j < gc.nlevels; j++)
				{
					// Draw z ~ N(0, I), then b_j = L * z.
					if (gc.nterms == 1)
					{
						// Scalar RE: avoid Eigen overhead.
						group_b[g][j] = gc.L(0, 0) * std_normal(rng);
					}
					else
					{
						Eigen::VectorXd z(gc.nterms);
						for (intptr_t t = 0; t < gc.nterms; t++)
							z[t] = std_normal(rng);
						Eigen::VectorXd b_j = gc.L * z;
						for (intptr_t t = 0; t < gc.nterms; t++)
							group_b[g][j * gc.nterms + t] = b_j[t];
					}
				}
			}
		}

		for (intptr_t i = 0; i < n; i++)
		{
			double mu_i;
			if (use_unconditional)
			{
				// η_i = Xβ̂_i + Σ_g z_{g,i}' b_{g,level(i)}
				double eta_i = eta_fixed[i];
				for (size_t g = 0; g < group_chols.size(); g++)
				{
					auto &gc = group_chols[g];
					intptr_t j = (*gc.indices)[i];
					if (gc.nterms == 1)
					{
						double z_val = (*gc.Z_design)[i];
						eta_i += z_val * group_b[g][j];
					}
					else
					{
						for (intptr_t t = 0; t < gc.nterms; t++)
						{
							double z_val = (*gc.Z_design)[i * gc.nterms + t];
							eta_i += z_val * group_b[g][j * gc.nterms + t];
						}
					}
				}
				mu_i = linkinv_fn(eta_i, m.family);
			}
			else
			{
				// Conditional fallback: use fitted values (Xβ̂ + Zb̂) directly.
				mu_i = m.fitted[i + 1];
			}

			sim_y[i * NSIM + s] = simulate_one(mu_i, m.family, disp0, disp1, rng);
		}
	}

	// ── Rank observed y among simulated values (PIT) ────────────────
	ScaledResidualResult result;
	result.residuals.resize(n);

	// DHARMa outlier thresholds: residuals in the extreme tails of U(0,1).
	// Under H0, P(U < lo) + P(U > hi) = 2/(nsim+1) per observation.
	const double outlier_lo = 1.0 / (NSIM + 1);
	const double outlier_hi = 1.0 - outlier_lo;
	int n_outliers = 0;

	std::uniform_real_distribution<double> unif01(0.0, 1.0);

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

		// DHARMa getQuantile (method = "PIT") convention: randomize only
		// when ties exist for this observation (count_equal > 0).
		// lower        = count_below / NSIM
		// lowerOrEqual = (count_below + count_equal) / NSIM
		// if ties: scaled residual ~ U(lower, lowerOrEqual); else = lower.
		// This is tie-driven rather than family-driven, which matches DHARMa
		// and correctly handles Gaussian fits to low-precision (rounded) data.
		double pit;
		if (count_equal > 0) {
			double jitter = unif01(rng);
			pit = (double(count_below) + jitter * double(count_equal)) / double(NSIM);
		} else {
			pit = double(count_below) / double(NSIM);
		}
		pit = std::clamp(pit, 1e-10, 1.0 - 1e-10);
		result.residuals[i + 1] = pit;

		// Outlier: scaled residual in the extreme tails.
		if (pit < outlier_lo || pit > outlier_hi)
			n_outliers++;
	}

	result.n_outliers = n_outliers;
	result.outlier_pvalue = outlier_test(n_outliers, n, NSIM);

	run_diagnostics(result, n);

	// ── Dispersion test (DHARMa simulation-based) ───────────────────
	//
	// DHARMa's spread statistic, var(y - fitted) / expectedVar, uses the
	// MARGINAL fit as the reference (predict(re.form = ~0) in DHARMa's
	// getFitted.default), NOT the conditional fit Xβ̂ + Zb̂.  Using the
	// conditional fit introduces a degrees-of-freedom bias: var(y_obs - (Xβ̂+Zb̂))
	// is systematically smaller than σ² because the BLUPs absorb part of the
	// residual noise (the familiar (n - edf)/(n - 1) shrinkage of sample
	// residual variance).  The marginal reference avoids this because it
	// does not subtract any quantity that was fitted to y:
	//
	//   var(y_obs - Xβ̂)     ≈ var(Zu_true) + σ²   (no dof bias)
	//   var(sim_k  - Xβ̂)    ≈ var(Zu) + σ²       (under either conditional
	//                                              or unconditional simulation,
	//                                              because var(Zb̂) ≈ var(Zu))
	//
	// Both match ≈ 1 under a correctly specified model.  A further benefit:
	// this works directly with the existing unconditional sim_y — no separate
	// conditional simulation pass is required.
	//
	// Implementation note: for non-identity links, linkinv(Xβ̂) is not the true
	// marginal mean (Jensen's inequality); DHARMa uses the same approximation,
	// so we match their convention for consistency.

	std::vector<double> y_obs_vec(n), fitted_marginal(n);
	for (intptr_t i = 0; i < n; i++) y_obs_vec[i] = m.y[i + 1];

	if (!m.X.empty() && !m.beta.empty())
	{
		intptr_t p = m.beta.size();
		if (m.X.ndim() == 2 && m.X.ncol() != p) p = m.nfixed;
		Eigen::Map<const Eigen::MatrixXd> Xm(m.X.data(), n, p);
		Eigen::Map<const Eigen::VectorXd> bm(m.beta.data(), p);
		Eigen::VectorXd Xb = Xm * bm;
		for (intptr_t i = 0; i < n; i++) {
			double eta_i = Xb[i];
			if (!m.offset.empty()) eta_i += m.offset[i + 1];
			fitted_marginal[i] = linkinv_fn(eta_i, m.family);
		}
	}
	else
	{
		// X / β unavailable (model loaded from file without design info).
		// For fixed-effects-only models, m.fitted IS the marginal fit.
		// For mixed models in this state, this is an approximation — the
		// dispersion test may show residual dof bias.
		for (intptr_t i = 0; i < n; i++)
			fitted_marginal[i] = m.fitted[i + 1];
	}

	auto [disp_ratio, disp_p] = dharma_dispersion_test(
		y_obs_vec.data(), fitted_marginal.data(), sim_y.data(), n, NSIM);
	result.dispersion_ratio = disp_ratio;
	result.dispersion_pvalue = disp_p;

	return result;
}


// =====================================================================
// Posterior predictive checks (Bayesian models)
// =====================================================================
//
// For Bayesian models, replace frequentist p-values for KS and dispersion
// with posterior predictive p-values, following the classical construction
// of Gelman, Meng & Stern (1996).  The scaled residuals themselves (stored
// in result.residuals) are always the simulation-based unconditional
// residuals from compute_simulation and are kept unchanged — only the
// calibration tests are recomputed.
//
// For each posterior draw r = 1..R:
//   a. Draw θ^(r) = (β^(r), disp^(r)) from the posterior (grid or vcov).
//   b. Compute μ_i^(r) = linkinv(x_i'β^(r) + zu_i), where zu_i is the
//      BLUP contribution held at its MAP value.  We deliberately do NOT
//      re-draw u ~ N(0, LL') per replicate: the analytical PIT is only
//      approximately uniform under the correct model when μ_i is close
//      to the true conditional mean of y_i, and fresh RE draws put μ on
//      a different realisation of u than y_obs — inflating var(y_obs - μ)
//      by an extra var(Zu) term.  MAP BLUPs keep μ close to the true
//      conditional mean (up to BLUP estimation error) and make y_rep's
//      analytical PIT exactly U(0,1) by construction, so both sides of
//      the ppp carry the same small bias and it cancels.
//   c. Simulate y_rep_i ~ Family(μ_i^(r), disp^(r)).
//   d. Compute T_obs^(r) = T(y_obs, θ^(r)) and T_rep^(r) = T(y_rep^(r), θ^(r))
//      from the SAME draw, ensuring observed and replicate statistics
//      share the same null distribution.
//
// Test statistics:
//   - KS:         T(y, θ) = KS D of analytical-PIT(y_i | θ) against U(0,1)
//   - Dispersion: T(y, θ) = var(y - μ(θ)) on the response scale; this is
//                 the Monte Carlo analogue of the frequentist dispersion
//                 test (same statistic, with posterior-drawn disp propagated
//                 through the simulate step).
//
// p-values:
//   - KS:         one-sided P(D_rep ≥ D_obs), because KS D only detects
//                 deviation from uniformity in one direction.
//   - Dispersion: two-sided 2·min(P(T_rep ≥ T_obs), P(T_rep ≤ T_obs)).
//
// The outlier p-value keeps its exact binomial form from compute_simulation:
// the outlier probability is 2/(NSIM+1) per observation by construction and
// does not depend on θ, so a PPC version would only add Monte Carlo noise.
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


// ── Main PPC function ───────────────────────────────────────────────
//
// Requires: model.X, model.y, model.fitted, model.beta, and either
// model.grid_summary (for grid-integrated models) or model.vcov
// (for fixed-effects Bayesian models).
//
// Uses the classical Gelman-Meng-Stern PPC construction: for each posterior
// draw r, compute T_obs^(r) = T(y_obs, θ^(r)) and T_rep^(r) = T(y_rep^(r), θ^(r))
// from the SAME draw, then count the fraction with T_rep^(r) ≥ T_obs^(r).
// This avoids the asymmetry that arose in the previous implementation where
// T_obs was fixed from simulation-based PIT while T_rep varied with θ^(r).
//
// For mixed models, fresh random effects are drawn per replicate from
// N(0, L L') using the stored MAP Cholesky factor (re.cov_chol).  This
// replaces the previous frozen-BLUP offset, which was biasing PPC p-values
// upward.  The exact posterior over θ is only integrated in β through the
// grid / vcov path; the RE covariance is held at its MAP value.  This is an
// approximation but matches what the frequentist unconditional path does
// and removes the dominant source of bias.
//
// Test statistics used:
//   - KS:         T(y, θ) = KS D of analytical-PIT(y_i | θ) against U(0,1)
//   - Dispersion: T(y, θ) = var(y - μ(θ)) on the response scale
// Both are computed identically for observed y and replicate y_rep.
//
// The outlier test p-value is kept from the frequentist path: outlier rate
// is 2/(NSIM+1) by construction regardless of θ, so a PPC version would
// only add Monte Carlo noise.

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

	Eigen::Map<Matrix<double>> Xm(const_cast<double *>(m.X.data()), n, p_draw);
	Eigen::Map<Vector<double>> beta_m(const_cast<double *>(m.beta.data()), p_draw);

	// ── 1. Precompute the MAP BLUP offset ────────────────────────
	//
	// zu_i = link(m.fitted_i) - x_i'β_MAP = the BLUP contribution at the MAP.
	// For GAMs without RE, this is zero (all prediction is in Xβ).
	// For mixed models, this captures Zb̂ at the MAP.
	//
	// We deliberately hold BLUPs at their MAP values across all posterior
	// draws rather than re-drawing u ~ N(0, LL') per replicate.  The reason
	// is that the PPC uses ANALYTICAL PIT against f(· | μ_i, disp), which
	// is only approximately uniform under the correct model when μ_i is
	// close to the true conditional mean of y_i.  Fresh RE draws per
	// replicate put μ_i on a different realisation of u than y_obs was
	// generated from, inflating var(y_obs - μ^(r)) by an extra var(Zu)
	// term — the same bias that, on the frequentist side, required us to
	// draw a separate conditional pass for the dispersion test.  MAP BLUPs
	// keep μ^(r) close to the true conditional mean of y_obs (up to BLUP
	// estimation error), and make y_rep's analytical PIT exactly U(0,1)
	// by construction, so both sides of the ppp carry the same small bias
	// and it cancels.
	//
	// Posterior uncertainty in θ still enters μ^(r) through the β^(r) draw
	// (full grid or vcov path) and the simulation through the disp^(r)
	// draw; only the RE covariance is held at its MAP value.

	Eigen::VectorXd Xb_map = Xm * beta_m;
	std::vector<double> zu_frozen(n);
	for (intptr_t i = 0; i < n; i++)
		zu_frozen[i] = link_fn(m.fitted[i + 1], m.family) - Xb_map[i];

	// ── 2. Prepare β posterior draw mechanism ────────────────────

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

	// ── 3. Observed response vector ──────────────────────────────

	std::vector<double> y_obs_vec(n);
	for (intptr_t i = 0; i < n; i++) y_obs_vec[i] = m.y[i + 1];

	// ── 4. Generate R replicates and count T_rep ≥ T_obs ─────────

	int count_ks = 0;
	int count_disp = 0;

	std::mt19937 rng(PPC_SEED);
	std::uniform_real_distribution<double> unif(0.0, 1.0);
	std::normal_distribution<double> std_normal(0.0, 1.0);

	std::vector<double> pit_obs(n), pit_rep(n);
	std::vector<double> mu_draw(n), mu_marg(n), y_rep(n);

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

		Eigen::VectorXd eta_fixed_draw = Xm * beta_draw;

		// ── Compute μ and simulate y_rep; compute PIT of both ────
		//
		// Two references are used:
		//   mu_cond   = linkinv(x_i'β^(r) + zu_frozen) — conditional fit at
		//               draw r.  Used for analytical PIT (approximately
		//               uniform under the correct model) and as the mean
		//               for simulating y_rep.
		//   mu_marg   = linkinv(x_i'β^(r)) — marginal fit at draw r, used
		//               as the reference for the dispersion statistic.
		//               This mirrors DHARMa's getFitted.default behaviour
		//               (predict with re.form = ~0) and avoids the dof
		//               shrinkage bias that would appear if we subtracted
		//               the BLUP contribution from y_obs.

		for (intptr_t i = 0; i < n; i++)
		{
			double eta_fixed_i = eta_fixed_draw[i];
			if (!m.offset.empty()) eta_fixed_i += m.offset[i + 1];
			double mu_marg_i = linkinv_fn(eta_fixed_i, m.family);

			double eta_cond_i = eta_fixed_draw[i] + zu_frozen[i];
			double mu_cond_i = linkinv_fn(eta_cond_i, m.family);
			mu_draw[i] = mu_cond_i;          // for PIT
			mu_marg[i] = mu_marg_i;          // for dispersion

			// Simulate y_rep conditional on μ_cond^(r) and disp^(r).
			y_rep[i] = simulate_one(mu_cond_i, m.family, disp[0], disp[1], rng);

			// Analytical PIT of y_obs and y_rep against the SAME conditional
			// distribution f(· | μ_cond_i, θ^(r)).  Both should be U(0,1)
			// under the correct model (exactly for y_rep by construction;
			// approximately for y_obs, up to BLUP estimation error).
			double u_obs = analytical_pit(y_obs_vec[i], mu_cond_i, m.family, disp[0], disp[1], rng);
			double u_rep = analytical_pit(y_rep[i],     mu_cond_i, m.family, disp[0], disp[1], rng);
			pit_obs[i] = std::clamp(u_obs, 1e-10, 1.0 - 1e-10);
			pit_rep[i] = std::clamp(u_rep, 1e-10, 1.0 - 1e-10);
		}

		// ── Compute test statistics on this replicate ────────────

		// KS D under θ^(r), symmetrically for y_obs and y_rep.
		double D_obs_r = ks_test_uniform(pit_obs.data(), n).first;
		double D_rep_r = ks_test_uniform(pit_rep.data(), n).first;

		// Dispersion statistic: var(y - Xβ^(r)) on the response scale,
		// using the MARGINAL fit as reference (no BLUP subtraction).
		// Under the correct model, var(y_obs - Xβ^(r)) and var(y_rep - Xβ^(r))
		// both approximate var(Zu) + σ², so the ratio is centred near 1.
		double obs_sum = 0, rep_sum = 0;
		for (intptr_t i = 0; i < n; i++) {
			obs_sum += (y_obs_vec[i] - mu_marg[i]);
			rep_sum += (y_rep[i]     - mu_marg[i]);
		}
		double obs_mean = obs_sum / double(n);
		double rep_mean = rep_sum / double(n);
		double obs_ss = 0, rep_ss = 0;
		for (intptr_t i = 0; i < n; i++) {
			double d_o = (y_obs_vec[i] - mu_marg[i]) - obs_mean;
			double d_r = (y_rep[i]     - mu_marg[i]) - rep_mean;
			obs_ss += d_o * d_o;
			rep_ss += d_r * d_r;
		}
		double T_disp_obs = obs_ss / double(n - 1);
		double T_disp_rep = rep_ss / double(n - 1);

		if (D_rep_r >= D_obs_r)     count_ks++;
		if (T_disp_rep >= T_disp_obs) count_disp++;
	}

	// ── 5. Posterior predictive p-values ─────────────────────────
	//
	// KS p-value: one-sided "replicate at least as extreme as observed" —
	// larger KS D means greater deviation from uniformity, so the natural
	// ppp is P(D_rep ≥ D_obs | θ^(r)).
	//
	// Dispersion p-value: two-sided — T_rep should neither systematically
	// exceed nor fall below T_obs under the correct model.  We convert the
	// one-sided count to a two-sided p-value in the usual way.
	//
	// The outlier p-value is unchanged (frequentist exact binomial).

	double p_ks_one_sided = double(count_ks) / double(PPC_R);
	double p_disp_upper = double(count_disp) / double(PPC_R);
	double p_disp_lower = 1.0 - p_disp_upper;
	double p_disp_two_sided = std::min(2.0 * std::min(p_disp_upper, p_disp_lower), 1.0);

	result.ks_pvalue = p_ks_one_sided;
	result.dispersion_pvalue = p_disp_two_sided;
	// result.outlier_pvalue unchanged — exact binomial from compute_simulation.
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
