/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: mixed-effects model fitting via Laplace approximation.                                                     *
 *                                                                                                                     *
 * This is a unified engine: the same code path handles Gaussian, binomial, and Poisson families with random            *
 * intercepts. The only family-dependent piece is the conditional log-likelihood and its derivatives, which are         *
 * supplied by the Family struct. Estimation is by maximum likelihood (not REML).                                      *
 *                                                                                                                     *
 * Algorithm references (all published mathematical specifications, no GPL code):                                      *
 *   - Breslow & Clayton (1993). Approximate inference in generalized linear mixed models. JASA 88(421).              *
 *   - Kristensen et al. (2016). TMB: Automatic Differentiation and Laplace Approximation. JSS 70(5).                *
 *   - Bates et al. (2015). Fitting Linear Mixed-Effects Models Using lme4. JSS 67(1).                               *
 *   - Henderson (1984). Applications of Linear Models in Animal Breeding. University of Guelph Press.                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MIXED_MODEL_HPP
#define PHONOMETRICA_MIXED_MODEL_HPP

#include <vector>
#include <phon/analysis/model.hpp>
#include <phon/analysis/family.hpp>

namespace phonometrica::stats {

// Information about a single grouping factor, built by the fitting layer from a DataTable column.
struct GroupingInfo
{
	String name;                       // column name, e.g. "speaker"
	Array<String> levels;              // sorted unique levels
	std::vector<intptr_t> indices;     // for each observation (0-based), the group index in [0, nlevels)
	intptr_t nlevels = 0;
};


//! Fit a mixed-effects model with a single random intercept via Laplace approximation.
//!
//! Model:   y_i | u ~ Family(mu_i), with  eta_i = x_i' beta + u_{g(i)}
//!          u_j ~ N(0, sigma^2_u)
//!
//! For Gaussian responses, this estimates (beta, sigma, sigma_u) jointly by ML.
//! For binomial/Poisson, this estimates (beta, sigma_u) by ML.
//! In both cases the random effects u are integrated out via the Laplace approximation.
//!
//! For Gaussian, the Laplace approximation is exact (the random effects integral is Gaussian),
//! so this gives the same ML estimates as a direct likelihood approach.
//!
//! \param y         response vector (n observations)
//! \param X         fixed-effects design matrix (n × p, first column is intercept)
//! \param group     grouping factor information (indices, levels, name)
//! \param fam       GLM family (gaussian, binomial, or poisson)
//! \return a fitted Model with random_effects populated
Model mixed_model(const Array<double> &y, const Array<double> &X,
                  const GroupingInfo &group, const Family &fam);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_MIXED_MODEL_HPP
