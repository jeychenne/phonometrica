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
 * Created: 05/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: estimated marginal means (EMMs) and pairwise contrasts for post-hoc analysis of fitted models.             *
 *          Conceptually similar to the emmeans package in R.                                                          *
 *                                                                                                                     *
 *          EMMs are population-averaged predictions at each level of a target factor, with other categorical          *
 *          factors balanced (equal weights across levels) and numeric covariates held at their observed means.        *
 *          For GLMs and mixed models, results are computed on the link scale and back-transformed to the              *
 *          response scale via the inverse link (delta method for SEs, endpoint transformation for CIs).               *
 *                                                                                                                     *
 *          Mathematical reference:                                                                                    *
 *            Searle, Speed & Milliken (1980). Population marginal means in the linear model:                          *
 *                an alternative to least squares means. The American Statistician, 34(4), 216–221.                    *
 *            Lenth (2016). Least-squares means: the R package lsmeans. JSS 69(1), 1–33.                               *
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

#ifndef PHONOMETRICA_EMMEANS_HPP
#define PHONOMETRICA_EMMEANS_HPP

#include <phon/analysis/model.hpp>
#include <phon/analysis/family.hpp>

namespace phonometrica::stats {

// Result of estimated marginal means computation for one categorical factor.
struct EMMResult
{
	String factor;               // variable being marginalized, e.g. "vowel"
	Array<String> levels;        // level labels in order (reference first)

	// Link-scale results (always computed).
	// For Gaussian models with identity link, these are identical to response-scale.
	Array<double> emmean_link;   // estimated marginal means on the link scale
	Array<double> se_link;       // standard errors on the link scale
	Array<double> lower_link;    // lower CI on link scale
	Array<double> upper_link;    // upper CI on link scale

	// Response-scale results.
	Array<double> emmean;        // estimated marginal means on the response scale
	Array<double> se;            // standard errors on the response scale (delta method for GLMs)
	Array<double> lower_ci;      // lower confidence interval bound
	Array<double> upper_ci;      // upper confidence interval bound

	// Covariance matrix of link-scale EMMs (K × K).
	// Stored for use by pairwise_contrasts().
	Array<double> cov_link;

	// Degrees of freedom: finite for Gaussian fixed-effects models (residual df),
	// infinite for GLMs and mixed models (Wald z-tests).
	double df = std::numeric_limits<double>::infinity();

	// True if the source model used Bayesian estimation.
	// When set, CIs are credible intervals and contrasts report pd instead of p-values.
	bool is_bayesian = false;
};


// Result of pairwise contrasts between estimated marginal means.
struct ContrastResult
{
	Array<String> label;         // contrast labels, e.g. "a - i"
	Array<double> estimate;      // contrast estimates (on link scale)
	Array<double> se;            // standard errors
	Array<double> stat;          // z or t statistics
	Array<double> p_value;       // adjusted p-values (frequentist) or pd (Bayesian)
	String adjustment;           // adjustment method: "none", "bonferroni", "holm"
	double df = std::numeric_limits<double>::infinity();

	// True if the source model used Bayesian estimation.
	// When set, p_value contains probability of direction (pd), not p-values,
	// and no multiplicity adjustment is applied.
	bool is_bayesian = false;
};


// Result of emmeans_by(): EMMs and contrasts for each level of a conditioning factor.
struct ByEMMResult
{
	String factor;               // target factor, e.g. "Voyelle"
	String by_factor;            // conditioning factor, e.g. "Condition"
	Array<String> by_levels;     // levels of the by-factor in order

	// One EMMResult and one ContrastResult per by-level (1-indexed).
	Array<EMMResult> emms;
	Array<ContrastResult> contrasts;
};


//! Compute estimated marginal means for a single categorical factor.
//!
//! The EMMs are population-averaged predictions at each level of the target factor,
//! with other categorical factors balanced (equal weights across levels) and numeric
//! covariates held at their means in the data.
//!
//! For models with smooth terms (GAMs), only the parametric component is used;
//! smooth terms are not evaluated at a reference point.
//!
//! Preconditions:
//!   - model.has_vcov() must be true.
//!   - model.has_variable_info() must be true.
//!   - factor must name a categorical variable present in variable_info.
//!
//! \param model       a fitted model (from stats::fit()).
//! \param factor      name of the categorical variable for which to compute EMMs.
//! \param conf_level  confidence level for intervals (default 0.95).
//! \return            an EMMResult with estimates, standard errors, and CIs.
//!
//! Throws on error (e.g., factor not found, no vcov available, numeric variable).
EMMResult emmeans(const Model &model, const String &factor, double conf_level = 0.95);


//! Compute all pairwise contrasts from a set of estimated marginal means.
//!
//! Contrasts are computed on the link scale: δ_ij = EMM_i − EMM_j for all i < j.
//! Standard errors account for the covariance between EMMs.
//! P-values are adjusted for multiple comparisons using the specified method.
//!
//! Supported adjustment methods:
//!   - "none":       no adjustment (raw p-values).
//!   - "bonferroni": multiply each p-value by the number of tests.
//!   - "holm":       Holm's step-down procedure (default; uniformly more powerful than Bonferroni).
//!
//! \param emm         the EMMs from which to compute contrasts (from emmeans()).
//! \param model       the fitted model (used for df only).
//! \param adjustment  name of the p-value adjustment method (default "holm").
//! \return            a ContrastResult with all pairwise differences, SEs, and adjusted p-values.
ContrastResult pairwise_contrasts(const EMMResult &emm, const Model &model,
                                  const String &adjustment = "holm");


//! Compute EMMs and pairwise contrasts for a target factor at each level of a
//! conditioning ("by") factor.
//!
//! This is equivalent to R's emmeans(model, pairwise ~ factor | by_factor).
//! For each level of by_factor, EMMs are computed with by_factor fixed to that level
//! (indicator) and all other factors balanced. Pairwise contrasts are computed within
//! each by-level.
//!
//! \param model       a fitted model.
//! \param factor      target categorical factor for pairwise comparisons.
//! \param by_factor   conditioning factor (each level produces a separate set of EMMs).
//! \param adjustment  p-value adjustment method (default "holm").
//! \param conf_level  confidence level for intervals (default 0.95).
//! \return            a ByEMMResult with per-by-level EMMs and contrasts.
ByEMMResult emmeans_by(const Model &model, const String &factor, const String &by_factor,
                       const String &adjustment = "holm", double conf_level = 0.95);


//! Estimate the slope (trend) of a continuous variable at each level of a categorical factor.
//!
//! For a model with an interaction between a numeric covariate and a factor
//! (e.g. F2 ~ frequency * group), emtrends estimates the effect of the covariate
//! (dη/dx) at each level of the factor, marginalizing over other variables as in emmeans.
//!
//! Results are always reported on the link scale (e.g. for logistic regression,
//! the trends are changes in log-odds per unit of the covariate). For Gaussian models
//! with identity link, link-scale and response-scale are identical.
//!
//! The returned EMMResult can be passed to pairwise_contrasts() to test whether
//! the slopes differ across factor levels.
//!
//! Mathematical reference:
//!   The L matrix is constructed by differentiating the linear predictor with respect
//!   to the trend variable. Columns not involving the trend variable get 0; the trend
//!   variable's own column gets 1; interaction columns involving the trend variable
//!   get the product of the other components' contributions (indicators, 1/K, or means).
//!
//! \param model       a fitted model (from stats::fit()).
//! \param factor      name of the categorical factor (e.g. "group").
//! \param var         name of the numeric trend variable (e.g. "frequency").
//! \param conf_level  confidence level for intervals (default 0.95).
//! \return            an EMMResult where emmean/se contain the per-level slopes.
//!
//! Throws if factor or var are not found, if factor is numeric, or if var is categorical.
EMMResult emtrends(const Model &model, const String &factor, const String &var,
                   double conf_level = 0.95);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_EMMEANS_HPP
