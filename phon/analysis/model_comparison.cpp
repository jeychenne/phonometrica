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
 * Purpose: see header.                                                                                                *
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

#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>
#include <string>
#include <boost/math/distributions/chi_squared.hpp>
#include <phon/analysis/model_comparison.hpp>

namespace phonometrica::stats {

// =====================================================================
// Helpers for nestedness checking
// =====================================================================

// Check whether every FixedTerm in `small` exists in `large`.
static bool fixed_terms_subset(const Array<FixedTerm> &small, const Array<FixedTerm> &large)
{
	for (intptr_t i = 1; i <= small.size(); i++)
	{
		bool found = false;
		for (intptr_t j = 1; j <= large.size(); j++)
		{
			if (small[i] == large[j]) { found = true; break; }
		}
		if (!found) return false;
	}
	return true;
}

// Check whether every SmoothTerm in `small` has a matching term in `large`.
// We match on (variable, by, basis) but allow k to differ, since a higher-k
// smooth in the larger model nests a lower-k smooth of the same covariate.
static bool smooth_terms_subset(const Array<SmoothTerm> &small, const Array<SmoothTerm> &large)
{
	for (intptr_t i = 1; i <= small.size(); i++)
	{
		bool found = false;
		for (intptr_t j = 1; j <= large.size(); j++)
		{
			if (small[i].variable == large[j].variable &&
			    small[i].by == large[j].by &&
			    small[i].basis == large[j].basis)
			{
				found = true;
				break;
			}
		}
		if (!found) return false;
	}
	return true;
}

// Check whether every RandomTerm in `small` has a matching (or more complex)
// counterpart in `large`.
// A random term (1 + a | g) in `small` is nested in (1 + a + b | g) in `large`
// if they share the same group, the intercept is compatible, and the small
// term's slopes are a subset of the large term's slopes.
static bool random_terms_subset(const Array<RandomTerm> &small, const Array<RandomTerm> &large)
{
	for (intptr_t si = 1; si <= small.size(); si++)
	{
		const auto &st = small[si];
		bool found = false;

		for (intptr_t li = 1; li <= large.size(); li++)
		{
			const auto &lt = large[li];
			if (st.group != lt.group) continue;

			// If small has an intercept, large must also have one.
			if (st.intercept && !lt.intercept) continue;

			// Every slope in small must appear in large.
			bool slopes_ok = true;
			for (intptr_t k = 1; k <= st.slopes.size(); k++)
			{
				bool slope_found = false;
				for (intptr_t m = 1; m <= lt.slopes.size(); m++)
				{
					if (st.slopes[k] == lt.slopes[m]) { slope_found = true; break; }
				}
				if (!slope_found) { slopes_ok = false; break; }
			}
			if (slopes_ok) { found = true; break; }
		}

		if (!found) return false;
	}
	return true;
}


bool formulas_nested(const Formula &small, const Formula &large)
{
	// The response must be the same.
	if (small.response != large.response) return false;

	// If the small model has an intercept, the large model must too.
	// (The opposite is fine: small has no intercept, large has one.)
	if (small.intercept && !large.intercept) return false;

	if (!fixed_terms_subset(small.fixed, large.fixed)) return false;
	if (!smooth_terms_subset(small.smooth, large.smooth)) return false;
	if (!random_terms_subset(small.random, large.random)) return false;

	return true;
}


// =====================================================================
// ANOVA comparison
// =====================================================================

AnovaResult anova_compare(const std::vector<const Model *> &models,
                          const std::vector<int> &labels)
{
	AnovaResult result;

	if (models.size() < 2) return result;

	// All models must be frequentist (the scripting layer's generic compare()
	// dispatches Bayesian models to bayesian_compare instead).
	for (size_t i = 0; i < models.size(); i++)
	{
		if (models[i]->is_bayesian())
			throw error("anova_compare() is for frequentist models. "
			            "Use bayesian_compare() for Bayesian models");
	}

	// REML guards: ML and REML log-likelihoods are not on the same scale,
	// and REML log-likelihoods between models with different fixed-effects
	// designs are not comparable either (the "restriction" depends on X,
	// so each model's REML log-likelihood is the density of a different
	// transformed dataset). We enforce both with hard errors — issuing a
	// warning is too easy to overlook, and the corrective action is a
	// simple refit with method="ML".
	{
		int n_ml = 0, n_reml = 0;
		for (size_t i = 0; i < models.size(); i++) {
			if (models[i]->method == Method::REML) n_reml++;
			else n_ml++;
		}
		if (n_ml > 0 && n_reml > 0) {
			throw error("Cannot compare models fitted with different methods (ML and REML). "
			            "ML and REML log-likelihoods are not on the same scale. "
			            "Refit all models with the same method (typically ML for fixed-effects comparison).");
		}
		if (n_reml > 0)
		{
			// All-REML set: ensure fixed effects are identical across models.
			// We compare every model to the first; if any fixed-effects design
			// differs, REML LRT/AIC/BIC are not valid.
			auto first_formula = Formula::parse(models[0]->formula);
			for (size_t i = 1; i < models.size(); i++) {
				auto fi = Formula::parse(models[i]->formula);
				bool same_fixed = (first_formula.intercept == fi.intercept)
				                  && (first_formula.fixed.size() == fi.fixed.size())
				                  && fixed_terms_subset(first_formula.fixed, fi.fixed)
				                  && fixed_terms_subset(fi.fixed, first_formula.fixed);
				if (!same_fixed) {
					throw error("Cannot compare REML-fitted models with different fixed-effects designs. "
					            "REML log-likelihoods depend on the fixed-effects design and are not "
					            "comparable across models that differ in their fixed effects. "
					            "Refit all models with method=\"ML\" to compare fixed effects, or restrict "
					            "the comparison to models with identical fixed effects (differing only in "
					            "random-effects structure).");
				}
			}
		}
	}

	// Build 1-based display labels for each model.
	std::vector<int> lbl(models.size());
	if (labels.size() == models.size())
	{
		for (size_t i = 0; i < models.size(); i++)
			lbl[i] = labels[i] + 1; // caller passes 0-based indices, we display 1-based
	}
	else
	{
		for (size_t i = 0; i < models.size(); i++)
			lbl[i] = (int)i + 1;
	}

	// Build index array sorted by nparams (ascending), breaking ties by loglik (ascending).
	std::vector<int> order(models.size());
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](int a, int b)
	{
		if (models[a]->nparams() != models[b]->nparams())
			return models[a]->nparams() < models[b]->nparams();
		return models[a]->loglik < models[b]->loglik;
	});

	// ── Pre-flight checks ────────────────────────────────────────────

	const auto *first = models[order[0]];

	// Same number of observations?
	for (size_t i = 1; i < order.size(); i++)
	{
		const auto *m = models[order[i]];
		if (m->nobs != first->nobs)
		{
			result.warnings.push_back(
				"Models have different numbers of observations. "
				"The likelihood ratio test requires all models to be fitted on the same data.");
			break;
		}
	}

	// Same family?
	for (size_t i = 1; i < order.size(); i++)
	{
		const auto *m = models[order[i]];
		if (m->family != first->family)
		{
			result.warnings.push_back(
				"Models use different distributional families. "
				"The likelihood ratio test is not appropriate for comparing models with different families. "
				"Consider using AIC or BIC instead.");
			break;
		}
	}

	// ── Build per-model rows (sorted by nparams) ─────────────────────

	result.rows.resize(order.size());

	for (size_t i = 0; i < order.size(); i++)
	{
		const auto *m = models[order[i]];
		auto &row = result.rows[i];

		row.original_index = order[i];
		row.npar = m->nparams();
		row.loglik = m->loglik;
		row.aic = m->aic;
		row.bic = m->bic;
		row.deviance = -2.0 * m->loglik;
	}

	// ── Pairwise comparisons ─────────────────────────────────────────

	for (size_t i = 0; i < order.size(); i++)
	{
		for (size_t j = i + 1; j < order.size(); j++)
		{
			const auto *ma = models[order[i]]; // simpler (or same complexity)
			const auto *mb = models[order[j]]; // more complex (or same complexity)

			AnovaPair pair;
			pair.index_a = (int)i;
			pair.index_b = (int)j;
			pair.df_diff = mb->nparams() - ma->nparams();

			// ── Nestedness check ────────────────────────────────
			try
			{
				auto fa = Formula::parse(ma->formula);
				auto fb = Formula::parse(mb->formula);

				if (pair.df_diff == 0)
				{
					// Same complexity: check whether the formulas are identical.
					if (!(formulas_nested(fa, fb) && formulas_nested(fb, fa)))
					{
						std::string msg = "Models " + std::to_string(lbl[order[i]])
							+ " and " + std::to_string(lbl[order[j]])
							+ " have the same number of parameters but different terms. "
							  "They are not nested, so the likelihood ratio test cannot be used. "
							  "Use AIC or BIC to compare these models.";
						result.warnings.push_back(String(msg));
					}
				}
				else
				{
					if (!formulas_nested(fa, fb))
					{
						std::string msg = "Models " + std::to_string(lbl[order[i]])
							+ " and " + std::to_string(lbl[order[j]])
							+ " do not appear to be nested "
							  "(the simpler model's terms are not a subset of the more complex model's terms). "
							  "The likelihood ratio test is only valid for nested models. "
							  "Consider using AIC or BIC for comparing non-nested models.";
						result.warnings.push_back(String(msg));
					}
				}
			}
			catch (...)
			{
				std::string msg = "Could not parse formulas for Models "
					+ std::to_string(lbl[order[i]])
					+ " and " + std::to_string(lbl[order[j]])
					+ ". Nestedness could not be verified.";
				result.warnings.push_back(String(msg));
			}

			// ── LRT computation ─────────────────────────────────
			if (pair.df_diff > 0)
			{
				double dev_a = -2.0 * ma->loglik;
				double dev_b = -2.0 * mb->loglik;
				pair.chisq = dev_a - dev_b;
				if (pair.chisq < 0) pair.chisq = 0; // clamp numerical noise

				boost::math::chi_squared_distribution<double> dist(static_cast<double>(pair.df_diff));
				pair.p_value = 1.0 - boost::math::cdf(dist, pair.chisq);
			}
			else
			{
				pair.chisq = std::numeric_limits<double>::quiet_NaN();
				pair.p_value = std::numeric_limits<double>::quiet_NaN();
			}

			result.pairs.push_back(pair);
		}
	}

	return result;
}


// =====================================================================
// Bayesian model comparison
// =====================================================================

BayesianCompareResult bayesian_compare(const std::vector<const Model *> &models,
                                       const std::vector<int> &labels)
{
	BayesianCompareResult result;

	if (models.size() < 2) return result;

	// All models must be Bayesian.
	for (size_t i = 0; i < models.size(); i++)
	{
		if (!models[i]->is_bayesian())
			throw error("bayesian_compare() requires all models to be Bayesian. "
			            "Use anova_compare() for frequentist models");
	}

	// Build 1-based display labels.
	std::vector<int> lbl(models.size());
	if (labels.size() == models.size())
	{
		for (size_t i = 0; i < models.size(); i++)
			lbl[i] = labels[i] + 1;
	}
	else
	{
		for (size_t i = 0; i < models.size(); i++)
			lbl[i] = (int)i + 1;
	}

	// ── Pre-flight checks ────────────────────────────────────────────

	const auto *first = models[0];

	// Same number of observations?
	for (size_t i = 1; i < models.size(); i++)
	{
		if (models[i]->nobs != first->nobs)
		{
			result.warnings.push_back(
				"Models have different numbers of observations. "
				"WAIC comparison requires all models to be fitted on the same data.");
			break;
		}
	}

	// All models need WAIC.
	for (size_t i = 0; i < models.size(); i++)
	{
		if (std::isnan(models[i]->waic))
		{
			std::string msg = "Model " + std::to_string(lbl[i])
				+ " does not have a WAIC value. "
				  "WAIC is only available for Bayesian models fitted with grid integration.";
			result.warnings.push_back(String(msg));
		}
	}

	// Check Bayes factor availability.
	result.has_bayes_factors = true;
	for (size_t i = 0; i < models.size(); i++)
	{
		if (std::isnan(models[i]->log_marginal))
		{
			result.has_bayes_factors = false;
			break;
		}
	}

	// Check LOO-IC availability.
	result.has_loo = true;
	for (size_t i = 0; i < models.size(); i++)
	{
		if (std::isnan(models[i]->loo_ic))
		{
			result.has_loo = false;
			break;
		}
	}

	// ── Build index array sorted by WAIC ascending (best first) ──────

	std::vector<int> order(models.size());
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](int a, int b)
	{
		double wa = std::isnan(models[a]->waic) ? 1e300 : models[a]->waic;
		double wb = std::isnan(models[b]->waic) ? 1e300 : models[b]->waic;
		return wa < wb;
	});

	// ── Build per-model rows ─────────────────────────────────────────

	result.rows.resize(order.size());

	for (size_t i = 0; i < order.size(); i++)
	{
		const auto *m = models[order[i]];
		auto &row = result.rows[i];

		row.original_index = order[i];
		row.npar = m->nparams();
		row.loglik = m->loglik;
		row.log_marginal = m->log_marginal;
		row.waic = m->waic;
		row.p_waic = m->p_waic;
		row.lppd = m->lppd;
		row.loo_ic = m->loo_ic;
		row.p_loo = m->p_loo;
	}

	// ── Pairwise comparisons ─────────────────────────────────────────

	intptr_t n = first->nobs;

	for (size_t i = 0; i < order.size(); i++)
	{
		for (size_t j = i + 1; j < order.size(); j++)
		{
			const auto *ma = models[order[i]];
			const auto *mb = models[order[j]];

			WaicPair pair;
			pair.index_a = (int)i;
			pair.index_b = (int)j;

			// ── ΔWAIC ──────────────────────────────────────────
			if (!std::isnan(ma->waic) && !std::isnan(mb->waic))
			{
				pair.delta_waic = ma->waic - mb->waic;

				// Proper SE from pointwise elpd differences.
				if (ma->elpd_i.size() == n && mb->elpd_i.size() == n)
				{
					// SE(ΔWAIC) = 2 * sqrt(n * Var_i(elpd_a_i - elpd_b_i))
					double mean_diff = 0;
					for (intptr_t k = 1; k <= n; k++)
						mean_diff += (ma->elpd_i[k] - mb->elpd_i[k]);
					mean_diff /= n;

					double var_diff = 0;
					for (intptr_t k = 1; k <= n; k++)
					{
						double d = (ma->elpd_i[k] - mb->elpd_i[k]) - mean_diff;
						var_diff += d * d;
					}
					var_diff /= (n - 1); // sample variance

					pair.se_diff = 2.0 * std::sqrt(static_cast<double>(n) * var_diff);
					pair.se_is_approximate = false;
				}
				else
				{
					// Fallback: conservative SE ignoring correlation.
					double se_a = std::isnan(ma->se_waic) ? 0 : ma->se_waic;
					double se_b = std::isnan(mb->se_waic) ? 0 : mb->se_waic;
					pair.se_diff = std::sqrt(se_a * se_a + se_b * se_b);
					pair.se_is_approximate = true;
				}
			}
			else
			{
				pair.delta_waic = std::numeric_limits<double>::quiet_NaN();
				pair.se_diff = std::numeric_limits<double>::quiet_NaN();
				pair.se_is_approximate = false;
			}

			// ── Bayes factor ───────────────────────────────────
			if (!std::isnan(ma->log_marginal) && !std::isnan(mb->log_marginal))
				pair.log_bf = ma->log_marginal - mb->log_marginal;
			else
				pair.log_bf = std::numeric_limits<double>::quiet_NaN();

			// ── ΔLOO-IC ────────────────────────────────────────
			if (!std::isnan(ma->loo_ic) && !std::isnan(mb->loo_ic))
			{
				pair.delta_loo = ma->loo_ic - mb->loo_ic;

				// Proper SE from pointwise elpd_loo differences.
				if (ma->elpd_loo_i.size() == n && mb->elpd_loo_i.size() == n)
				{
					double mean_diff = 0;
					for (intptr_t k = 1; k <= n; k++)
						mean_diff += (ma->elpd_loo_i[k] - mb->elpd_loo_i[k]);
					mean_diff /= n;

					double var_diff = 0;
					for (intptr_t k = 1; k <= n; k++)
					{
						double d = (ma->elpd_loo_i[k] - mb->elpd_loo_i[k]) - mean_diff;
						var_diff += d * d;
					}
					var_diff /= (n - 1);

					pair.se_loo_diff = 2.0 * std::sqrt(static_cast<double>(n) * var_diff);
					pair.se_loo_is_approximate = false;
				}
				else
				{
					double se_a = std::isnan(ma->se_loo) ? 0 : ma->se_loo;
					double se_b = std::isnan(mb->se_loo) ? 0 : mb->se_loo;
					pair.se_loo_diff = std::sqrt(se_a * se_a + se_b * se_b);
					pair.se_loo_is_approximate = true;
				}
			}
			else
			{
				pair.delta_loo = std::numeric_limits<double>::quiet_NaN();
				pair.se_loo_diff = std::numeric_limits<double>::quiet_NaN();
				pair.se_loo_is_approximate = false;
			}

			result.pairs.push_back(pair);
		}
	}

	// Warn if any pair used approximate SEs.
	bool warned_waic = false, warned_loo = false;
	for (auto &pair : result.pairs)
	{
		if (pair.se_is_approximate && !warned_waic)
		{
			result.warnings.push_back(
				"Some models lack per-observation elpd values (e.g. loaded from an older file). "
				"The SE of \u0394WAIC is approximate (conservative, ignoring correlation). "
				"Refit these models to obtain the proper SE.");
			warned_waic = true;
		}
		if (pair.se_loo_is_approximate && !warned_loo)
		{
			result.warnings.push_back(
				"Some models lack per-observation LOO elpd values. "
				"The SE of \u0394LOO-IC is approximate.");
			warned_loo = true;
		}
	}

	return result;
}

} // namespace phonometrica::stats
