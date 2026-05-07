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
 * Purpose: Posterior predictive checks (PPC) for Bayesian models.                                                     *
 *                                                                                                                     *
 * For each of R = 200 posterior draws, sample (β, u) from the approximate posterior and simulate a full y_rep dataset *
 * from the model's likelihood.  The resulting replicate datasets are summarised into plot-ready data structures       *
 * appropriate for the response family:                                                                                *
 *                                                                                                                     *
 *   • Continuous (Gaussian / Student t / Beta) — kernel density overlay: one bold KDE for y_obs and a sample of       *
 *     replicate KDEs drawn from the same posterior, on a common evaluation grid.                                      *
 *   • Bernoulli binomial — bar plot: observed proportions of 0 / 1 with 5 % / 50 % / 95 % quantile bands across       *
 *     replicate proportions.                                                                                          *
 *   • Counts (Poisson / negative binomial) — standing rootogram on the √-scale: bars at √(observed count)             *
 *     per integer value, with 5 % / 50 % / 95 % quantile bands at √(expected count) connected by a thin curve.        *
 *                                                                                                                     *
 * Posterior draws come from the grid summary stored on the Model when available (see GridSummary in model.hpp); in    *
 * that path β is drawn at a sampled grid point with diagonal Σ_k, and dispersion is read from the grid point's       *
 * outer θ_k vector via family-specific extraction.  Without a grid summary (fixed-effects Bayesian, no random         *
 * effects) the fallback path uses the full posterior covariance m.vcov via Cholesky, or the marginal posterior_sd    *
 * with independent draws if vcov is unavailable.                                                                      *
 *                                                                                                                     *
 * For mixed models, random-effect contributions are re-drawn unconditionally from N(0, ΣL') for every replicate —    *
 * the same convention used by the scaled-residuals path, and what bayesplot's ppc_dens_overlay produces with brms     *
 * defaults for a refit-style check.                                                                                   *
 *                                                                                                                     *
 * Reference: Gelman, A., Meng, X.-L. & Stern, H. (1996). Posterior predictive assessment of model fitness via         *
 * realized discrepancies. Statistica Sinica 6(4), 733–760.                                                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_POSTERIOR_PREDICTIVE_HPP
#define PHONOMETRICA_POSTERIOR_PREDICTIVE_HPP

#include <vector>
#include <phon/string.hpp>
#include <phon/analysis/model.hpp>

namespace phonometrica::stats {

// One curve of (x, density) pairs evaluated on a common grid.
struct PpcDensityCurve
{
	std::vector<double> x;
	std::vector<double> y;
};


// One x-position for the discrete plot variants.  The interpretation of the
// numerical fields depends on `PosteriorPredictiveResult::kind`:
//
//   BinomialBars: x ∈ {0, 1}; obs / exp_* are proportions in [0, 1].
//   Rootogram   : x is an integer count; obs / exp_* are √(count).
struct PpcDiscretePoint
{
	double x = 0;          // category position on the x axis
	double obs = 0;        // observed value
	double exp_mean = 0;   // posterior predictive median (50 % quantile across replicates)
	double exp_lo = 0;     // posterior predictive 5 % quantile
	double exp_hi = 0;     // posterior predictive 95 % quantile
};


enum class PpcKind
{
	Density,        // Gaussian / Student t / Beta
	BinomialBars,   // Bernoulli (n_trials = 1)
	Rootogram       // Poisson / negative binomial
};


struct PosteriorPredictiveResult
{
	PpcKind kind = PpcKind::Density;
	String family;

	int n_replicates = 0;   // total posterior draws used in the simulation
	int n_overlay = 0;      // number of replicate curves stored in rep_densities

	// ── Density mode ───────────────────────────────────────────────
	PpcDensityCurve obs_density;
	std::vector<PpcDensityCurve> rep_densities;

	// ── BinomialBars / Rootogram modes ─────────────────────────────
	std::vector<PpcDiscretePoint> discrete;

	// Plot labels (computed once in the engine, used by the GUI).
	String x_label;
	String y_label;
	String title;
};


// Compute a posterior predictive summary suitable for direct display.
// Throws std::runtime_error when the model lacks the structure needed for
// posterior simulation (frequentist fit, no design matrix, missing random-
// effect design info, etc.).  The error message is user-facing and is
// expected to be relayed to the GUI's diagnostic panel.
PosteriorPredictiveResult compute_posterior_predictive(const Model &m);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_POSTERIOR_PREDICTIVE_HPP
