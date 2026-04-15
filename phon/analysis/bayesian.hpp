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
 * Created: 12/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: INLA-style approximate Bayesian inference.                                                                 *
 *                                                                                                                     *
 * Phase 1 (this file): post-hoc Gaussian adjustment of the frequentist MLE.                                           *
 *   - For fixed effects β with Normal prior N(μ₀, Σ₀):                                                                *
 *       Posterior precision = H_lik + Σ₀⁻¹  (Fisher info + prior precision)                                           *
 *       Posterior covariance Σ_post = (H_lik + Σ₀⁻¹)⁻¹                                                                *
 *       Posterior mean β̂_post = Σ_post (H_lik β̂_MLE + Σ₀⁻¹ μ₀)                                                      *
 *   - This is exact for Gaussian LMs and a Gaussian approximation for GLMs/GLMMs.                                     *
 *   - Hyperparameter posteriors (variance components) are reported at the MLE with                                    *
 *     prior-informed uncertainty from the outer Hessian.                                                              *
 *                                                                                                                     *
 * Phase 2 (mixed_model.cpp): full grid-based integration over hyperparameters for                                     *
 *   mixed-effects models.  Provides mixture posteriors for β and marginal posteriors                                  *
 *   for variance components, dispersion parameters, and residual SD.                                                  *
 *   Includes the simplified Laplace correction (Tierney-Kadane skewness adjustment                                    *
 *   via third derivatives) for non-Gaussian GLMMs.                                                                    *
 *                                                                                                                     *
 * References:                                                                                                         *
 *   Rue, Martino & Chopin (2009). Approximate Bayesian inference for latent Gaussian                                  *
 *     models by using integrated nested Laplace approximations. JRSS-B 71(2).                                         *
 *   Tierney, L. & Kadane, J. B. (1986). Accurate approximations for posterior                                         *
 *     moments and marginal densities. JASA 81(393).                                                                   *
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

#ifndef PHONOMETRICA_BAYESIAN_HPP
#define PHONOMETRICA_BAYESIAN_HPP

#include <phon/analysis/model.hpp>
#include <phon/analysis/prior.hpp>

namespace phonometrica::stats {

//! Convert a frequentist Model to a Bayesian Model by applying a Gaussian
//! approximation to the posterior using the supplied priors.
//!
//! Preconditions:
//!   - model.vcov must be populated (the frequentist covariance matrix)
//!   - model.beta and model.coef_names must be populated
//!
//! The function:
//!   1. Computes the posterior covariance by adding prior precision to the
//!      Fisher information (inverse of vcov).
//!   2. Computes the posterior mean as a precision-weighted average of the
//!      MLE and the prior mean.
//!   3. Fills in posterior_mean, posterior_sd, ci_lower, ci_upper, pd.
//!   4. Sets estimation = Bayesian and stores the PriorSpec.
//!   5. Populates hyper_* fields for variance components (if present).
//!
//! \param model  the fitted frequentist Model (modified in place)
//! \param priors the prior specification
void bayesian_adjust(Model &model, const PriorSpec &priors);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_BAYESIAN_HPP
