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
	// that is systematically too narrow relative to the actual marginal
	// distribution of y.  The result is overdispersed PIT residuals and
	// inflated KS statistics — false positives in model diagnostics.
	//
	// The unconditional path requires the RE design info (Z_design, indices,
	// cov_chol) that is populated at fit time and serialised to disk.  When
	// this info is missing — which can happen for models loaded from save
	// files written before it was serialised — we throw rather than fall
	// back to the conditional path; see "Determine simulation mode" below.

	// ── Determine simulation mode ───────────────────────────────────
	//
	// Mixed models require unconditional simulation: random effects are
	// re-drawn from N(0, ΣL') for each replicate, rather than conditioning
	// on the estimated BLUPs. Conditioning on BLUPs would systematically
	// underestimate residual variance (BLUPs absorb part of the noise) and
	// inflate the KS statistic, producing false positives. The unconditional
	// path requires the RE design info (Z_design, indices, cov_chol) that
	// is populated at fit time and serialised to disk.
	//
	// If a model has random effects but the design info is missing — which
	// typically indicates the model was loaded from a save file written
	// before this information was serialised — we refuse to compute
	// residuals rather than silently produce biased numbers via a
	// conditional fallback. The user is asked to refit the model.

	bool use_unconditional = false;
	if (m.has_random_effects() && !m.beta.empty() && !m.fitted.empty())
	{
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			const auto &re = m.random_effects[g];
			if (re.indices.empty() || re.Z_design.empty() || re.cov_chol.empty()
			    || re.conditional_modes.empty()) {
				throw std::runtime_error(
					"Residual diagnostics are not available for this model. "
					"The random-effects design info needed for unconditional "
					"simulation is missing — this typically indicates the "
					"model was loaded from a save file written before this "
					"information was serialised. Please refit the model to "
					"enable residual diagnostics.");
			}
		}
		use_unconditional = true;
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
		eta_fixed.resize(n);

		if (!m.X.empty())
		{
			// Standard path (freshly-fit model): compute η_fixed = X β̂ + offset
			// directly from the design matrix.
			intptr_t p = m.beta.size();
			// Sanity: X column count must match β length. If not (shouldn't happen
			// for freshly fitted models), fall back to nfixed.
			if (m.X.ndim() == 2 && m.X.ncol() != p)
				p = m.nfixed;
			Eigen::Map<const Eigen::MatrixXd> Xm(m.X.data(), n, p);
			Eigen::Map<const Eigen::VectorXd> bm(m.beta.data(), p);
			Eigen::VectorXd Xb = Xm * bm;
			for (intptr_t i = 0; i < n; i++)
			{
				eta_fixed[i] = Xb[i];
				if (!m.offset.empty()) eta_fixed[i] += m.offset[i];
			}
		}
		else
		{
			// Reload path: X is not serialised, so reconstruct η_fixed from the
			// saved fitted values minus the BLUP contribution:
			//   η_full     = link(fitted)            (fitted = linkinv(Xβ + offset + Zu_BLUP))
			//   η_fixed    = η_full − Z·u_BLUP      (= Xβ + offset; offset already in fitted)
			// This produces numerically the same η_fixed as the standard path
			// for any cleanly fitted model, and is the default path for models
			// loaded from disk.
			for (intptr_t i = 0; i < n; i++)
			{
				double eta_full_i = link_fn(m.fitted[i], m.family);
				double Zu_BLUP_i = 0.0;
				for (intptr_t g = 0; g < m.random_effects.size(); g++)
				{
					const auto &re = m.random_effects[g];
					intptr_t j = re.indices[i];  // 0-based level index
					for (intptr_t t = 0; t < re.nterms; t++)
					{
						double z = re.Z_design[i * re.nterms + t];
						double b = re.conditional_modes.data()[j * re.nterms + t];
						Zu_BLUP_i += z * b;
					}
				}
				eta_fixed[i] = eta_full_i - Zu_BLUP_i;
			}
		}

		// Unpack Cholesky factors from each RE group's cov_chol (packed lower triangle).
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
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
				mu_i = m.fitted[i];
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
		double y_obs = m.y[i];
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
		result.residuals[i] = pit;

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
	for (intptr_t i = 0; i < n; i++) y_obs_vec[i] = m.y[i];

	if (use_unconditional)
	{
		// Mixed model: eta_fixed has already been computed by the simulation
		// precompute step — either as X·β + offset (freshly-fit, X populated)
		// or as link(fitted) − Z·u_BLUP (reloaded, X not serialised). Both
		// give the marginal linear predictor, so linkinv is the right
		// reference for the dispersion test in either case.
		for (intptr_t i = 0; i < n; i++)
			fitted_marginal[i] = linkinv_fn(eta_fixed[i], m.family);
	}
	else if (!m.X.empty() && !m.beta.empty())
	{
		// Fixed-effects model with X available: standard X·β + offset path.
		intptr_t p = m.beta.size();
		if (m.X.ndim() == 2 && m.X.ncol() != p) p = m.nfixed;
		Eigen::Map<const Eigen::MatrixXd> Xm(m.X.data(), n, p);
		Eigen::Map<const Eigen::VectorXd> bm(m.beta.data(), p);
		Eigen::VectorXd Xb = Xm * bm;
		for (intptr_t i = 0; i < n; i++) {
			double eta_i = Xb[i];
			if (!m.offset.empty()) eta_i += m.offset[i];
			fitted_marginal[i] = linkinv_fn(eta_i, m.family);
		}
	}
	else
	{
		// Fixed-effects model loaded from disk: m.fitted = linkinv(X·β + offset)
		// already, so it IS the marginal fit (no BLUPs to subtract because
		// there are no random effects).
		for (intptr_t i = 0; i < n; i++)
			fitted_marginal[i] = m.fitted[i];
	}

	auto [disp_ratio, disp_p] = dharma_dispersion_test(
		y_obs_vec.data(), fitted_marginal.data(), sim_y.data(), n, NSIM);
	result.dispersion_ratio = disp_ratio;
	result.dispersion_pvalue = disp_p;

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

	// Bayesian and frequentist models share the same DHARMa-style residual
	// diagnostics: simulation-based KS, dispersion and outlier tests on the
	// marginal scaled residuals.  Earlier versions added a posterior predictive
	// layer for Bayesian fits, but the conditional-PIT-with-frozen-BLUP test
	// statistic it used carried a built-in upward bias on KS that produced
	// false positives even when the marginal residuals were clean (see the
	// documentation comment in compute_simulation).  We now report the same
	// frequentist diagnostics regardless of estimation method.
	return compute_simulation(m);
}

} // namespace phonometrica::stats
