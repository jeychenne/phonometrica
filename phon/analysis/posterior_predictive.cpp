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
 * Created: 07/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 * Note: The core architecture and integration logic were designed and authored by Julien Eychenne. Portions of the    *
 * statistical estimation logic in this file were developed with the assistance of Claude Opus 4.7 (Anthropic), based  *
 * on published statistical literature and reference implementations (notably bayesplot's ppc_dens_overlay,            *
 * ppc_bars and ppc_rootogram).  All AI-assisted logic has been manually audited, refactored, and validated against a  *
 * diverse suite of datasets and reference R packages to ensure mathematical accuracy and implementation integrity.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <random>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <Eigen/Dense>
#include <Eigen/Cholesky>
#include <phon/analysis/posterior_predictive.hpp>
#include <phon/analysis/student_bounds.hpp>

namespace phonometrica::stats {
namespace {

// ── Tunables ─────────────────────────────────────────────────────────

constexpr int    R_REPLICATES   = 200;     // total posterior draws
constexpr int    N_OVERLAY      = 30;      // replicate density curves shown
constexpr int    N_KDE_GRID     = 200;     // KDE evaluation grid points
constexpr int    ROOTOGRAM_CAP  = 50;      // cap integer support for readability
constexpr unsigned int SEED     = 42;


// ── Family-specific dispersion extraction (mirrors mixed_model.cpp) ──
//
// The grid summary stores the outer θ_k vector for each grid point.  The
// dispersion parameters live in the trailing slots of θ — after the n_chol
// Cholesky parameters.  The exact slot count and unpacking convention is
// the one used inside mixed_model.cpp::compute_grid_waic; we replicate it
// here rather than expose a shared helper because the logic is small,
// declarative and changes rarely.

static int n_disp_params(const String &family)
{
	if (family == "gaussian") return 1;  // residual σ
	if (family == "negbin")   return 1;  // θ_NB
	if (family == "beta")     return 1;  // φ
	if (family == "student")  return 2;  // σ, ν
	return 0;                            // binomial, poisson
}


static void disp_from_theta(const String &family, const double *theta_k,
                             int n_theta, double *disp)
{
	int n_disp = n_disp_params(family);
	if (n_disp == 0 || n_theta < n_disp) return;
	int n_chol = n_theta - n_disp;

	if (family == "student") {
		disp[0] = std::exp(theta_k[n_chol]);
		disp[1] = std::clamp(std::exp(theta_k[n_chol + 1]), NU_MIN, NU_MAX);
	} else {
		// gaussian, negbin, beta
		disp[0] = std::exp(theta_k[n_chol]);
	}
}


// ── Scalar inverse link ──────────────────────────────────────────────

static double linkinv_fn(double eta, const String &family)
{
	if (family == "binomial" || family == "beta")
		return 1.0 / (1.0 + std::exp(-eta));
	if (family == "poisson" || family == "negbin")
		return std::exp(std::clamp(eta, -30.0, 30.0));
	return eta; // identity for gaussian, student
}


// ── Simulate a single observation from Family(μ, disp) ───────────────

static double simulate_one(double mu, const String &family,
                            double disp0, double disp1, std::mt19937 &rng)
{
	if (family == "gaussian")
	{
		std::normal_distribution<double> dist(mu, std::max(disp0, 1e-10));
		return dist(rng);
	}
	if (family == "binomial")
	{
		mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
		std::bernoulli_distribution dist(mu);
		return dist(rng) ? 1.0 : 0.0;
	}
	if (family == "poisson")
	{
		mu = std::max(mu, 1e-10);
		std::poisson_distribution<int> dist(mu);
		return (double)dist(rng);
	}
	if (family == "negbin")
	{
		mu = std::max(mu, 1e-10);
		double shape = std::max(disp0, 1e-10);
		double scale = mu / shape;
		std::gamma_distribution<double> g(shape, scale);
		double v = g(rng);
		std::poisson_distribution<int> p(std::max(v, 1e-10));
		return (double)p(rng);
	}
	if (family == "beta")
	{
		mu = std::clamp(mu, 1e-10, 1.0 - 1e-10);
		double phi = std::max(disp0, 1e-10);
		double a = mu * phi;
		double b = (1.0 - mu) * phi;
		std::gamma_distribution<double> g1(a, 1.0), g2(b, 1.0);
		double v1 = g1(rng), v2 = g2(rng);
		return std::clamp(v1 / (v1 + v2), 1e-10, 1.0 - 1e-10);
	}
	if (family == "student")
	{
		double sigma = std::max(disp0, 1e-10);
		double nu = std::max(disp1, 1.01);
		std::student_t_distribution<double> dist(nu);
		return mu + sigma * dist(rng);
	}
	return mu;
}


// ── KDE helpers ──────────────────────────────────────────────────────

// Robust dispersion: σ_robust = IQR / 1.34, falling back to the sample SD
// when the support is degenerate (constant / near-constant data).
static double robust_sd(const std::vector<double> &y)
{
	if (y.size() < 4) {
		// Plain SD for tiny samples.
		double m = 0; for (double v : y) m += v; m /= std::max<size_t>(y.size(), 1);
		double ss = 0; for (double v : y) ss += (v - m) * (v - m);
		return (y.size() > 1) ? std::sqrt(ss / (y.size() - 1)) : 0.0;
	}
	std::vector<double> s = y;
	std::sort(s.begin(), s.end());
	intptr_t n = (intptr_t)s.size();
	double q1 = s[n / 4];
	double q3 = s[(3 * n) / 4];
	double iqr = (q3 - q1) / 1.34;
	if (iqr > 0) return iqr;

	// IQR collapsed (e.g. a heavy-mode binomial proportion vector): fall
	// back to the plain SD so the bandwidth is still finite.
	double m = 0; for (double v : y) m += v; m /= y.size();
	double ss = 0; for (double v : y) ss += (v - m) * (v - m);
	return std::sqrt(ss / std::max<intptr_t>(n - 1, 1));
}


static PpcDensityCurve compute_kde(const std::vector<double> &y,
                                    double xlo, double xhi, double bandwidth)
{
	PpcDensityCurve curve;
	curve.x.resize(N_KDE_GRID);
	curve.y.resize(N_KDE_GRID, 0.0);
	if (y.empty() || bandwidth <= 0) return curve;

	double dx = (N_KDE_GRID > 1) ? (xhi - xlo) / (N_KDE_GRID - 1) : 0.0;
	intptr_t n = (intptr_t)y.size();
	double norm = 1.0 / (n * bandwidth * std::sqrt(2.0 * M_PI));

	for (int g = 0; g < N_KDE_GRID; g++)
	{
		double xg = xlo + g * dx;
		curve.x[g] = xg;
		double sum = 0;
		for (intptr_t i = 0; i < n; i++) {
			double z = (xg - y[i]) / bandwidth;
			sum += std::exp(-0.5 * z * z);
		}
		curve.y[g] = norm * sum;
	}
	return curve;
}


// Empirical quantile via sorted linear interpolation.  Operates on a copy
// so the caller's data is untouched.
static double quantile_q(std::vector<double> v, double q)
{
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	double pos = q * (v.size() - 1);
	intptr_t lo = (intptr_t)std::floor(pos);
	intptr_t hi = (intptr_t)std::ceil(pos);
	if (lo == hi) return v[(size_t)lo];
	double frac = pos - lo;
	return v[(size_t)lo] * (1 - frac) + v[(size_t)hi] * frac;
}


// ── Per-RE-group Cholesky bundle (used during simulation) ────────────

struct GroupChol
{
	intptr_t nterms = 0;
	intptr_t nlevels = 0;
	Eigen::MatrixXd L;
	const std::vector<intptr_t> *indices = nullptr;
	const std::vector<double> *Z_design = nullptr;
};


// =====================================================================
// Main implementation
// =====================================================================

PosteriorPredictiveResult compute_posterior_predictive_impl(const Model &m)
{
	if (!m.is_bayesian())
		throw std::runtime_error(
			"Posterior predictive checks are only available for Bayesian models.");

	if (m.nobs == 0 || m.y.empty())
		throw std::runtime_error(
			"This model carries no fitted data — posterior predictive checks "
			"are unavailable.");

	if (m.X.empty())
		throw std::runtime_error(
			"Posterior predictive checks need the original design matrix, which "
			"is not stored in saved analyses.  Please refit the model to enable "
			"them.");

	intptr_t n = m.nobs;
	intptr_t p = m.nfixed;
	if (p <= 0)
		throw std::runtime_error(
			"This model has no fixed-effect coefficients — posterior predictive "
			"checks are unavailable.");

	// ── Posterior draw mechanism ─────────────────────────────────────

	bool use_grid = m.grid_summary.has_value()
	             && m.grid_summary->n_points > 0
	             && (intptr_t)m.grid_summary->n_beta == p
	             && !m.grid_summary->weights.empty()
	             && !m.grid_summary->beta.empty()
	             && !m.grid_summary->vcov_diag.empty();

	bool have_post_summary = !m.posterior_mean.empty()
	                      && !m.posterior_sd.empty()
	                      && m.posterior_mean.size() >= p
	                      && m.posterior_sd.size() >= p;

	if (!use_grid && !have_post_summary)
		throw std::runtime_error(
			"This Bayesian model does not carry posterior summaries.  Please "
			"refit before running posterior predictive checks.");

	// Grid point CDF for sampling proportional to weights.
	std::vector<double> grid_cdf;
	if (use_grid)
	{
		const auto &gs = *m.grid_summary;
		grid_cdf.resize(gs.n_points);
		double cum = 0;
		for (int k = 0; k < gs.n_points; k++) {
			cum += gs.weights[k];
			grid_cdf[k] = cum;
		}
		if (cum > 0)
			for (auto &c : grid_cdf) c /= cum;
		else
			throw std::runtime_error(
				"Posterior grid weights are degenerate; cannot draw from the "
				"posterior.  Please refit the model.");
	}

	// Cholesky of the full posterior covariance (non-grid path).  Falling
	// back to independent draws on posterior_sd is fine for visual diagnostics
	// but loses correlation across coefficients; we attempt the full Cholesky
	// whenever m.vcov is available.
	Eigen::MatrixXd L_vcov;     // materialised lower-triangular factor
	bool have_full_chol = false;
	if (!use_grid && !m.vcov.empty()
	    && m.vcov.ndim() == 2 && m.vcov.nrow() == p && m.vcov.ncol() == p)
	{
		Eigen::Map<const Eigen::MatrixXd> Vm(m.vcov.data(), p, p);
		Eigen::LLT<Eigen::MatrixXd> llt(Vm);
		if (llt.info() == Eigen::Success) {
			L_vcov = llt.matrixL();
			have_full_chol = true;
		}
	}

	// ── Random-effect design ─────────────────────────────────────────

	bool has_re = m.has_random_effects();
	std::vector<GroupChol> group_chols;

	if (has_re)
	{
		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			const auto &re = m.random_effects[g];
			if (re.indices.empty() || re.Z_design.empty() || re.cov_chol.empty())
				throw std::runtime_error(
					"Random-effects design info is missing — please refit the "
					"model to enable posterior predictive checks.");

			GroupChol gc;
			gc.nterms = re.nterms;
			gc.nlevels = re.nlevels;
			gc.L = Eigen::MatrixXd::Zero(re.nterms, re.nterms);
			intptr_t idx = 0;
			for (intptr_t r = 0; r < re.nterms; r++)
				for (intptr_t c = 0; c <= r; c++)
					gc.L(r, c) = re.cov_chol.data()[idx++];
			gc.indices = &re.indices;
			gc.Z_design = &re.Z_design;
			group_chols.push_back(std::move(gc));
		}
	}

	// ── Map design matrix and observed response ──────────────────────

	Eigen::Map<const Eigen::MatrixXd> Xm(m.X.data(), n, p);

	std::vector<double> y_obs(n);
	for (intptr_t i = 0; i < n; i++) y_obs[(size_t)i] = m.y[i + 1];

	// Posterior point estimate of β / posterior_sd for the non-grid path.
	Eigen::VectorXd beta_post(p), post_sd(p);
	if (have_post_summary)
	{
		for (intptr_t j = 0; j < p; j++) {
			beta_post[j] = m.posterior_mean[j + 1];
			post_sd[j]   = m.posterior_sd[j + 1];
		}
	}

	// Fallback dispersion (used in the non-grid path or as a defensive default).
	double disp_fallback[2] = {0, 0};
	if (m.is_gaussian())     disp_fallback[0] = std::max(m.rse,   1e-10);
	else if (m.is_negbin())  disp_fallback[0] = std::max(m.theta, 1e-10);
	else if (m.is_beta())    disp_fallback[0] = std::max(m.phi,   1e-10);
	else if (m.is_student()) {
		disp_fallback[0] = std::max(m.sigma, 1e-10);
		disp_fallback[1] = std::max(m.nu,    1.01);
	}

	// ── Replicate simulation loop ────────────────────────────────────

	std::mt19937 rng(SEED);
	std::normal_distribution<double> std_normal(0.0, 1.0);
	std::uniform_real_distribution<double> unif01(0.0, 1.0);

	std::vector<std::vector<double>> y_rep(R_REPLICATES, std::vector<double>(n));

	Eigen::VectorXd beta_draw(p);

	for (int r = 0; r < R_REPLICATES; r++)
	{
		// 1. Sample β and dispersion from the posterior.
		double disp[2] = {disp_fallback[0], disp_fallback[1]};

		if (use_grid)
		{
			const auto &gs = *m.grid_summary;
			double u = unif01(rng);
			auto it = std::lower_bound(grid_cdf.begin(), grid_cdf.end(), u);
			int k = (int)(it - grid_cdf.begin());
			if (k >= gs.n_points) k = gs.n_points - 1;

			for (intptr_t j = 0; j < p; j++) {
				double sd = std::sqrt(std::max(gs.vcov_diag[(size_t)k * p + j], 1e-20));
				beta_draw[j] = gs.beta[(size_t)k * p + j] + sd * std_normal(rng);
			}

			// Family-specific dispersion at this grid point.
			if (n_disp_params(m.family) > 0 && gs.n_theta > 0) {
				disp_from_theta(m.family,
				                gs.theta.data() + (size_t)k * gs.n_theta,
				                gs.n_theta, disp);
			}
		}
		else if (have_full_chol)
		{
			Eigen::VectorXd z(p);
			for (intptr_t j = 0; j < p; j++) z[j] = std_normal(rng);
			beta_draw = beta_post + L_vcov * z;
		}
		else
		{
			// Independent marginal draws — last resort fallback.
			for (intptr_t j = 0; j < p; j++)
				beta_draw[j] = beta_post[j] + post_sd[j] * std_normal(rng);
		}

		// 2. Linear predictor: η = X β + offset (RE contribution added below).
		Eigen::VectorXd eta = Xm * beta_draw;
		if (!m.offset.empty())
			for (intptr_t i = 0; i < n; i++)
				eta[i] += m.offset[i + 1];

		// 3. Draw fresh random-effect contributions: u_g ~ N(0, L_g L_g').
		if (has_re)
		{
			for (auto &gc : group_chols)
			{
				std::vector<double> u_g((size_t)gc.nlevels * gc.nterms);
				for (intptr_t lv = 0; lv < gc.nlevels; lv++)
				{
					if (gc.nterms == 1) {
						u_g[(size_t)lv] = gc.L(0, 0) * std_normal(rng);
					} else {
						Eigen::VectorXd z(gc.nterms);
						for (intptr_t t = 0; t < gc.nterms; t++) z[t] = std_normal(rng);
						Eigen::VectorXd b = gc.L * z;
						for (intptr_t t = 0; t < gc.nterms; t++)
							u_g[(size_t)lv * gc.nterms + t] = b[t];
					}
				}
				for (intptr_t i = 0; i < n; i++)
				{
					intptr_t lv = (*gc.indices)[(size_t)i];
					if (gc.nterms == 1) {
						eta[i] += (*gc.Z_design)[(size_t)i] * u_g[(size_t)lv];
					} else {
						for (intptr_t t = 0; t < gc.nterms; t++)
							eta[i] += (*gc.Z_design)[(size_t)i * gc.nterms + t]
							        * u_g[(size_t)lv * gc.nterms + t];
					}
				}
			}
		}

		// 4. Simulate y_rep_i ~ Family(linkinv(η_i), disp).
		for (intptr_t i = 0; i < n; i++)
		{
			double mu_i = linkinv_fn(eta[i], m.family);
			y_rep[(size_t)r][(size_t)i] = simulate_one(mu_i, m.family,
			                                            disp[0], disp[1], rng);
		}
	}

	// ── Family-specific summary for plotting ─────────────────────────

	PosteriorPredictiveResult res;
	res.family = m.family;
	res.n_replicates = R_REPLICATES;

	if (m.family == "binomial")
	{
		res.kind = PpcKind::BinomialBars;
		res.title   = "Posterior predictive check";
		res.x_label = "y";
		res.y_label = "Proportion";

		double inv_n = 1.0 / (double)n;
		double obs_one = 0;
		for (double v : y_obs) if (v >= 0.5) obs_one += 1;
		obs_one *= inv_n;
		double obs_zero = 1.0 - obs_one;

		std::vector<double> rep_zero(R_REPLICATES), rep_one(R_REPLICATES);
		for (int r = 0; r < R_REPLICATES; r++) {
			int ones = 0;
			for (double v : y_rep[(size_t)r]) if (v >= 0.5) ones++;
			rep_one[(size_t)r]  = (double)ones * inv_n;
			rep_zero[(size_t)r] = 1.0 - rep_one[(size_t)r];
		}

		PpcDiscretePoint p0;
		p0.x = 0; p0.obs = obs_zero;
		p0.exp_lo   = quantile_q(rep_zero, 0.05);
		p0.exp_mean = quantile_q(rep_zero, 0.50);
		p0.exp_hi   = quantile_q(rep_zero, 0.95);

		PpcDiscretePoint p1;
		p1.x = 1; p1.obs = obs_one;
		p1.exp_lo   = quantile_q(rep_one, 0.05);
		p1.exp_mean = quantile_q(rep_one, 0.50);
		p1.exp_hi   = quantile_q(rep_one, 0.95);

		res.discrete = {p0, p1};
	}
	else if (m.family == "poisson" || m.family == "negbin")
	{
		res.kind = PpcKind::Rootogram;
		res.title   = "Posterior predictive rootogram";
		res.x_label = "y";
		res.y_label = "\u221A(count)";  // √(count)

		// Determine the integer support to display.  Bound by the larger of
		// observed-max and the median replicate-max, capped at ROOTOGRAM_CAP.
		int obs_max = 0;
		for (double v : y_obs)
			obs_max = std::max(obs_max, (int)std::lround(v));

		std::vector<int> rep_max(R_REPLICATES, 0);
		for (int r = 0; r < R_REPLICATES; r++)
			for (double v : y_rep[(size_t)r])
				rep_max[(size_t)r] = std::max(rep_max[(size_t)r], (int)std::lround(v));
		std::sort(rep_max.begin(), rep_max.end());
		int rep_med = rep_max[R_REPLICATES / 2];

		int max_k = std::min(std::max(obs_max, rep_med), ROOTOGRAM_CAP);

		res.discrete.reserve((size_t)max_k + 1);
		std::vector<double> rep_counts(R_REPLICATES);
		for (int k = 0; k <= max_k; k++)
		{
			double obs_count = 0;
			for (double v : y_obs)
				if ((int)std::lround(v) == k) obs_count += 1;

			for (int r = 0; r < R_REPLICATES; r++) {
				int c = 0;
				for (double v : y_rep[(size_t)r])
					if ((int)std::lround(v) == k) c++;
				rep_counts[(size_t)r] = (double)c;
			}

			PpcDiscretePoint pt;
			pt.x = (double)k;
			pt.obs      = std::sqrt(obs_count);
			pt.exp_lo   = std::sqrt(quantile_q(rep_counts, 0.05));
			pt.exp_mean = std::sqrt(quantile_q(rep_counts, 0.50));
			pt.exp_hi   = std::sqrt(quantile_q(rep_counts, 0.95));
			res.discrete.push_back(pt);
		}
	}
	else
	{
		// Continuous: gaussian, student, beta — KDE overlay on a common grid.
		res.kind = PpcKind::Density;
		res.title   = "Posterior predictive density overlay";
		res.x_label = "y";
		res.y_label = "Density";

		double ylo = *std::min_element(y_obs.begin(), y_obs.end());
		double yhi = *std::max_element(y_obs.begin(), y_obs.end());
		// Pool with the first 10 replicates so the grid covers the bulk of the
		// posterior predictive support without having to scan all 200.
		int probe = std::min(R_REPLICATES, 10);
		for (int r = 0; r < probe; r++) {
			for (double v : y_rep[(size_t)r]) {
				ylo = std::min(ylo, v);
				yhi = std::max(yhi, v);
			}
		}
		if (yhi <= ylo) { ylo -= 0.5; yhi += 0.5; }

		double sd = robust_sd(y_obs);
		if (!(sd > 0)) sd = 0.5 * (yhi - ylo);
		if (!(sd > 0)) sd = 1.0;
		double bandwidth = 1.06 * sd * std::pow((double)n, -0.2);
		if (!(bandwidth > 0)) bandwidth = 1.0;

		// Pad the evaluation range by 3 bandwidths so the KDE tails decay
		// to (visually) zero inside the plot frame.
		ylo -= 3.0 * bandwidth;
		yhi += 3.0 * bandwidth;

		res.obs_density = compute_kde(y_obs, ylo, yhi, bandwidth);

		int n_overlay = std::min(N_OVERLAY, R_REPLICATES);
		res.rep_densities.reserve((size_t)n_overlay);
		for (int s = 0; s < n_overlay; s++)
		{
			// Evenly spread index over [0, R_REPLICATES) so we sample widely
			// across the posterior rather than just the first contiguous block.
			int idx = (int)((long long)s * R_REPLICATES / n_overlay);
			res.rep_densities.push_back(
				compute_kde(y_rep[(size_t)idx], ylo, yhi, bandwidth));
		}
		res.n_overlay = n_overlay;
	}

	return res;
}

} // anonymous namespace


// ── Public entry point ───────────────────────────────────────────────

PosteriorPredictiveResult compute_posterior_predictive(const Model &m)
{
	return compute_posterior_predictive_impl(m);
}

} // namespace phonometrica::stats
