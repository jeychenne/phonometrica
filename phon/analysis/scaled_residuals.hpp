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
 *   - For mixed models with Z info, fresh random effects are drawn from N(0, D).                                      *
 *   - The observed y_i is ranked among its simulated values, yielding a PIT value                                     *
 *     via the empirical CDF.                                                                                          *
 *                                                                                                                     *
 * Under the correct model, scaled residuals are uniformly distributed on (0, 1).                                      *
 * Diagnostics include:                                                                                                *
 *   - Kolmogorov-Smirnov test for uniformity                                                                          *
 *   - Dispersion test (variance ratio vs 1/12)                                                                        *
 *   - Outlier test: binomial test for excess observations falling outside                                              *
 *     the simulated range                                                                                             *
 *                                                                                                                     *
 * References:                                                                                                         *
 *   - Dunn, P.K. & Smyth, G.K. (1996). Randomized quantile residuals. JCGS 5(3), 236-244.                           *
 *   - Hartig, F. (2020). DHARMa: Residual Diagnostics for Hierarchical Models. R package.                            *
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
};

// Compute DHARMa-style simulation-based scaled residuals for a fitted model.
// For mixed models with Z info available, fresh random effects are drawn each replicate.
// For all other models (fixed-effects, or deserialized mixed without Z), simulates
// from the conditional fitted values.
ScaledResidualResult compute_scaled_residuals(const Model &m);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_SCALED_RESIDUALS_HPP
