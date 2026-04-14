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
 * Created: 03/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: ANOVA-style likelihood ratio test for comparing nested models.                                             *
 *          When models are not nested, issues a warning and suggests using AIC/BIC instead.                            *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MODEL_COMPARISON_HPP
#define PHONOMETRICA_MODEL_COMPARISON_HPP

#include <vector>
#include <phon/string.hpp>
#include <phon/analysis/model.hpp>
#include <phon/analysis/formula.hpp>

namespace phonometrica::stats {

// Per-model summary row (for the information criteria table).
struct AnovaRow
{
	int original_index;    // 0-based index into the caller's model vector
	intptr_t npar;         // number of estimated parameters
	double loglik;         // log-likelihood
	double aic;
	double bic;
	double deviance;       // -2 * loglik
};


// One pairwise likelihood ratio test.
struct AnovaPair
{
	int index_a;           // index into AnovaResult::rows (the simpler model)
	int index_b;           // index into AnovaResult::rows (the more complex model)
	intptr_t df_diff;      // npar_b - npar_a  (0 when same complexity)
	double chisq;          // LRT chi-squared statistic (NaN when df_diff == 0)
	double p_value;        // p-value from chi-squared distribution (NaN when undefined)
};


// Result of an ANOVA model comparison.
struct AnovaResult
{
	std::vector<AnovaRow> rows;     // one per model, sorted by nparams ascending
	std::vector<AnovaPair> pairs;   // all pairwise LRT tests

	// Warnings accumulated during the comparison (empty if everything is fine).
	// Each entry is a human-readable sentence.
	std::vector<String> warnings;

	bool has_warnings() const { return !warnings.empty(); }
};


// Compare two or more fitted models using pairwise likelihood ratio tests.
//
// Models are sorted by number of parameters (smallest first). Every pair
// (i, j) with i < j is tested. A heuristic nestedness check is performed:
// if models appear non-nested, the result includes a warning suggesting
// AIC/BIC instead, but the LRT is still computed where possible (the user
// may know better than the heuristic).
//
// Preconditions checked at runtime (violations produce warnings, not errors):
//   - All models must have the same number of observations.
//   - All models should use the same distributional family.
//   - Models should be nested for the LRT to be valid.
//
// The models vector must contain at least 2 entries.
// The optional `labels` vector provides 0-based model indices for display in
// warnings and table labels (e.g. {0, 2, 4} if only those models are being
// compared). If empty, models are labelled 1, 2, 3, ... in input order.
AnovaResult anova_compare(const std::vector<const Model *> &models,
                          const std::vector<int> &labels = {});


// Heuristic nestedness check between two parsed formulas.
// Returns true if `small` appears to be nested within `large`, i.e. every term
// in `small` also appears in `large`. This is a formula-level check; it does
// not inspect the actual design matrices, so it may have false positives/negatives
// in rare edge cases (e.g. aliased interactions, different contrast coding).
bool formulas_nested(const Formula &small, const Formula &large);


// =====================================================================
// Bayesian model comparison (WAIC + Bayes factors)
// =====================================================================

// Per-model summary row for Bayesian comparison.
struct WaicRow
{
	int original_index;    // 0-based index into the caller's model vector
	intptr_t npar;         // number of estimated parameters
	double loglik;         // log-likelihood at mode
	double log_marginal;   // log marginal likelihood (NaN if unavailable)
	double waic;
	double p_waic;
	double lppd;
	double loo_ic;         // PSIS-LOO IC (NaN if unavailable)
	double p_loo;
};


// One pairwise comparison (WAIC and LOO).
struct WaicPair
{
	int index_a;           // index into BayesianCompareResult::rows
	int index_b;
	double delta_waic;     // WAIC_a - WAIC_b; negative favours model a
	double se_diff;        // SE from pointwise elpd differences (Vehtari et al. 2017)
	double delta_loo;      // LOO-IC_a - LOO-IC_b (NaN if LOO unavailable)
	double se_loo_diff;    // SE of ΔLOO-IC from pointwise elpd_loo differences
	double log_bf;         // log Bayes factor (NaN if marginals unavailable)
	bool se_is_approximate; // true if elpd_i was unavailable and SE is conservative
	bool se_loo_is_approximate;
};


// Result of a Bayesian model comparison.
struct BayesianCompareResult
{
	std::vector<WaicRow> rows;   // sorted by WAIC ascending (best first)
	std::vector<WaicPair> pairs; // all pairwise comparisons
	std::vector<String> warnings;
	bool has_bayes_factors = false; // true if all models have log_marginal
	bool has_loo = false;           // true if all models have LOO-IC

	bool has_warnings() const { return !warnings.empty(); }
};


// Compare two or more Bayesian models using WAIC and (optionally) Bayes factors.
//
// Models are sorted by WAIC (smallest = best first). Every pair (i, j) with
// i < j is compared. The SE of ΔWAIC is computed from per-observation elpd
// differences (Vehtari, Gelman & Gabry 2017); falls back to conservative
// sqrt(se_a² + se_b²) if elpd_i is empty (e.g. model loaded from older file).
//
// Bayes factors are reported when all models have a log marginal likelihood.
//
// Preconditions checked at runtime (violations produce warnings, not errors):
//   - All models must be Bayesian.
//   - All models must have the same number of observations.
//   - All models must have WAIC available.
//
// The models vector must contain at least 2 entries.
BayesianCompareResult bayesian_compare(const std::vector<const Model *> &models,
                                       const std::vector<int> &labels = {});


} // namespace phonometrica::stats

#endif // PHONOMETRICA_MODEL_COMPARISON_HPP
