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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: mixed-effects model fitting via Laplace approximation.                                                     *
 *                                                                                                                     *
 * This is a unified engine: the same code path handles Gaussian, binomial, Poisson, and negative binomial families    *
 * with one or more random-effects terms (intercepts and/or slopes, crossed or nested). The only family-dependent      *
 * piece is the conditional log-likelihood and its derivatives, supplied by the Family struct.                         *
 * Estimation is by maximum likelihood (not REML).                                                                     *
 *                                                                                                                     *
 * Algorithm references (all published mathematical specifications, no GPL code):                                      *
 *   - Breslow & Clayton (1993). Approximate inference in generalized linear mixed models. JASA 88(421).               *
 *   - Kristensen et al. (2016). TMB: Automatic Differentiation and Laplace Approximation. JSS 70(5).                  *
 *   - Bates et al. (2015). Fitting Linear Mixed-Effects Models Using lme4. JSS 67(1).                                 *
 *   - Henderson (1984). Applications of Linear Models in Animal Breeding. University of Guelph Press.                 *
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

#ifndef PHONOMETRICA_MIXED_MODEL_HPP
#define PHONOMETRICA_MIXED_MODEL_HPP

#include <vector>
#include <functional>
#include <phon/analysis/model.hpp>
#include <phon/analysis/family.hpp>

namespace phonometrica::stats {

// Progress callback: receives (current_step, max_steps).
using FittingCallback = std::function<void(int, int)>;

// Information about a single random-effects term, built by the fitting layer.
//
// A random-effects term like (1 + vowel | speaker) where vowel has 3 levels
// produces one GroupingInfo with:
//   name      = "speaker"
//   nlevels   = number of unique speakers
//   nterms    = 3 (intercept + 2 treatment-coded dummies for vowel)
//   term_names = {"Intercept", "vowel[i]", "vowel[u]"}
//   Z_design  = n × 3 row-major matrix
//
// For (1 | speaker) (intercept only), nterms=1 and Z_design is all 1s.
//
// The u vector for this group has nlevels × nterms elements, organised as:
//   u[j * nterms + t]  = random coefficient for level j, term t.
//
struct GroupingInfo
{
	String name;                       // grouping variable name, e.g. "speaker"
	Array<String> levels;              // sorted unique levels
	std::vector<intptr_t> indices;     // n_obs: per-observation level index [0, nlevels)
	intptr_t nlevels = 0;

	// Random-effects design.
	// nterms = q = total number of random terms (intercept + expanded slope columns).
	// Z_design: n_obs × q row-major matrix.
	//   Z_design[i * q + t] = design value for observation i, term t.
	//   First column is 1.0 if the intercept is included.
	intptr_t nterms = 1;
	Array<String> term_names;
	std::vector<double> Z_design;
};


//! Fit a mixed-effects model via Laplace approximation.
//!
//! Supports random intercepts and random slopes with a Cholesky-parameterised
//! covariance structure for each grouping factor.
//!
//! Model:   y_i | u ~ Family(mu_i)
//!          eta_i = x_i' beta + sum_g z_{g,i}' u_{g, k_g(i)}
//!          u_{g,j} ~ N(0, D_g)    for each grouping factor g, level j
//!
//! where D_g = L_g L_g' is the q_g × q_g covariance matrix for random effects
//! within group g, parameterised via its Cholesky factor.
//!
//! \param y         response vector (n observations)
//! \param X         fixed-effects design matrix (n × p, first column is intercept)
//! \param groups    vector of grouping factor information (one per random-effects term)
//! \param fam       GLM family (gaussian, binomial, poisson, negbin)
//! \return a fitted Model with random_effects populated (one RandomEffectGroup per grouping factor)
Model mixed_model(const Array<double> &y, const Array<double> &X,
                  const std::vector<GroupingInfo> &groups, const Family &fam,
                  FittingCallback progress = nullptr,
                  const PriorSpec *priors = nullptr,
                  const Array<String> *coef_names = nullptr,
                  int max_iter = 200,
                  const Array<double> &offset = Array<double>());

} // namespace phonometrica::stats

#endif // PHONOMETRICA_MIXED_MODEL_HPP
