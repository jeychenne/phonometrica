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
 * Created: 08/11/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: regression models (linear and generalized linear).                                                         *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_REGRESSION_HPP
#define PHONOMETRICA_REGRESSION_HPP

#include <vector>
#include <functional>
#include <phon/analysis/model.hpp>
#include <phon/analysis/family.hpp>

namespace phonometrica::stats {

// Progress callback: receives (current_step, max_steps).
using FittingCallback = std::function<void(int, int)>;

//! Fits a linear regression model using ordinary least squares.
//! \param y a vector of N observations
//! \param X an N by M design matrix (first column is the intercept).
//! \return a Model with Gaussian family diagnostics.
Model lm(const Array<double> &y, const Array<double> &X);


//! Fits a logistic regression model via L-BFGS.
//! \param y a binary response vector (0/1).
//! \param X an N by M design matrix (first column is the intercept).
//! \param max_iter maximum number of L-BFGS iterations.
//! \return a Model with binomial family diagnostics.
Model logit(const Array<double> &y, const Array<double> &X, int max_iter = 200);


//! Fits a Poisson regression model via L-BFGS.
//! \param y a count response vector.
//! \param X an N by M design matrix (first column is the intercept).
//! \param robust if true, uses sandwich (robust) standard errors.
//! \param max_iter maximum number of L-BFGS iterations.
//! \return a Model with Poisson family diagnostics.
Model poisson(const Array<double> &y, const Array<double> &X, bool robust, int max_iter = 200);


//! Fits a negative binomial regression model via IWLS with alternating
//! profile likelihood for the overdispersion parameter θ.
//! \param y a count response vector.
//! \param X an N by M design matrix (first column is the intercept).
//! \param max_iter maximum number of outer iterations.
//! \return a Model with negbin family diagnostics (theta stored in Model::theta).
Model negbin(const Array<double> &y, const Array<double> &X, int max_iter = 50);


//! Generic GLM fitting via L-BFGS with a specified family.
//! This is the unified entry point that logit() and poisson() delegate to.
//! Not suitable for negative binomial (use negbin() instead).
//! \param y response vector.
//! \param X design matrix.
//! \param fam the GLM family (binomial, poisson, etc.).
//! \param robust if true, uses sandwich standard errors (Poisson only).
//! \param max_iter maximum number of iterations.
//! \return a Model.
Model glm(const Array<double> &y, const Array<double> &X, const Family &fam, bool robust = false, int max_iter = 200);


//! A range of columns belonging to one smooth term in the augmented design matrix.
struct SmoothColumnRange
{
	intptr_t col_start;   // 0-based starting column in X
	intptr_t col_count;   // number of columns (k_eff)
	String variable;      // covariate name
	String basis;         // basis type ("cr")
	intptr_t k;           // original basis dimension
};


//! Fits a penalized linear regression model (Gaussian GAM) via penalized OLS
//! with smoothing parameter(s) selected by GCV.
//!
//! \param y response vector (n observations).
//! \param X augmented design matrix (n × p, parametric + smooth basis columns).
//! \param S total penalty matrix (p × p, sum of all smooth penalties, zero-padded).
//! \param n_parametric number of leading parametric (unpenalized) columns in X.
//! \param smooth_ranges column ranges for each smooth term (for EDF/F-test computation).
//! \return a fitted Model with per-smooth EDF and test statistics in smooth_terms.
Model penalized_lm(const Array<double> &y, const Array<double> &X,
                   const Array<double> &S, intptr_t n_parametric,
                   const std::vector<SmoothColumnRange> &smooth_ranges = {},
                   FittingCallback progress = nullptr);


//! Fits a penalized GLM (non-Gaussian GAM) via penalized IWLS
//! with smoothing parameter selected by GCV at each outer iteration.
//!
//! \param y response vector.
//! \param X augmented design matrix (parametric + smooth basis columns).
//! \param S total penalty matrix (p × p).
//! \param fam GLM family.
//! \param n_parametric number of leading parametric columns.
//! \param smooth_ranges column ranges for each smooth term.
//! \param max_iter maximum number of PIRLS iterations.
//! \return a fitted Model.
Model penalized_glm(const Array<double> &y, const Array<double> &X,
                    const Array<double> &S, const Family &fam,
                    intptr_t n_parametric,
                    const std::vector<SmoothColumnRange> &smooth_ranges = {},
                    FittingCallback progress = nullptr,
                    int max_iter = 50);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_REGRESSION_HPP
