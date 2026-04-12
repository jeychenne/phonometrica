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

	// Reject mixing Bayesian and frequentist models.
	for (size_t i = 1; i < models.size(); i++)
	{
		if (models[i]->estimation != models[0]->estimation)
			throw error("Cannot compare models with different estimation methods (frequentist vs Bayesian)");
	}

	// Bayesian models cannot be compared by LRT.
	if (models[0]->is_bayesian())
		throw error("Bayesian model comparison (WAIC/LOO-IC) is not yet implemented. "
		            "Use compare() on frequentist models for likelihood ratio tests");

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

} // namespace phonometrica::stats
