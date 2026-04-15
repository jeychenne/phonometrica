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
 * Purpose: DHARMa-style scaled residuals for model diagnostics.                                                       *
 *                                                                                                                     *
 * All models use simulation-based PIT (the approach used by DHARMa):                                                  *
 *   - For each of N replicates, a response is simulated from the fitted model.                                        *
 *   - For mixed models, simulation is UNCONDITIONAL (marginal): random effects                                        *
 *     are re-drawn from N(0, Σ̂) for each replicate, matching DHARMa's default                                        *
 *     behavior with glmmTMB.  This is critical because BLUPs are functions of y                                       *
 *     (they absorb part of the residual noise via shrinkage), so conditioning                                         *
 *     on them produces systematically biased PIT residuals — typically under-                                          *
 *     dispersed — even when the model is correct.                                                                     *
 *   - The unconditional path requires Z_design, indices, and cov_chol (populated                                      *
 *     at fit time).  If unavailable (e.g. model loaded from file), the code falls                                     *
 *     back to conditional simulation using fitted values directly.                                                    *
 *   - The observed y_i is ranked among its simulated values, yielding a PIT value                                     *
 *     via the empirical CDF.  For discrete families (binomial, Poisson, NB),                                          *
 *     randomized PIT is used (uniform jitter within the tied range) to ensure                                         *
 *     uniformity under the correct model.  For continuous families (Gaussian),                                        *
 *     the midpoint is used since ties are negligible.                                                                 *
 *                                                                                                                     *
 * Under the correct model, scaled residuals are uniformly distributed on (0, 1).                                      *
 * Diagnostics include:                                                                                                *
 *   - Kolmogorov-Smirnov test for uniformity                                                                          *
 *   - Dispersion test (variance ratio vs 1/12)                                                                        *
 *   - Outlier test: binomial test for observations whose scaled residual                                              *
 *     falls below 1/(nsim+1) or above 1-1/(nsim+1), matching DHARMa's                                                 *
 *     testOutliers(type = "binomial") threshold definition.                                                           *
 *                                                                                                                     *
 * References:                                                                                                         *
 *   - Dunn, P.K. & Smyth, G.K. (1996). Randomized quantile residuals. JCGS 5(3), 236-244.                             *
 *   - Hartig, F. (2020). DHARMa: Residual Diagnostics for Hierarchical Models. R package.                             *
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

#ifndef PHONOMETRICA_SCALED_RESIDUALS_HPP
#define PHONOMETRICA_SCALED_RESIDUALS_HPP

#include <phon/array.hpp>
#include <phon/analysis/model.hpp>

namespace phonometrica::stats {

struct ScaledResidualResult
{
	// Scaled residuals in (0, 1).  Should be U(0,1) if model is correct.
	// 1-based Array (n observations).
	Array<double> residuals;

	// One-sample Kolmogorov-Smirnov test against U(0,1).
	double ks_statistic = 0;
	double ks_pvalue = 1;

	// Dispersion test: ratio = Var(u) / (1/12).
	// Values near 1 are good; > 1 suggests overdispersion, < 1 underdispersion.
	double dispersion_ratio = 1;
	double dispersion_pvalue = 1;

	// Outlier test.
	// An outlier is an observation whose y falls entirely outside the simulated range
	// (scaled residual = 0 or 1 before clamping).
	// Under H0, P(outlier) = 2/(nsim+1) per observation.
	int n_outliers = 0;
	double outlier_pvalue = 1;

	// When true, the p-values above are posterior predictive p-values
	// rather than frequentist p-values.  Posterior predictive p-values
	// represent the proportion of replicate datasets (drawn from the
	// posterior predictive distribution) that produce a test statistic
	// at least as extreme as the observed one.
	bool is_ppc = false;
};

// Compute DHARMa-style simulation-based scaled residuals for a fitted model.
// For mixed models, simulation is unconditional (re-draws random effects),
// matching DHARMa's default.  For discrete families (binomial, Poisson, NB),
// randomized PIT is used; for continuous families (Gaussian), midpoint PIT.
ScaledResidualResult compute_scaled_residuals(const Model &m);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_SCALED_RESIDUALS_HPP
