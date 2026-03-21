/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2025 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
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
 * Purpose: linear regression.                                                                                         *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_REGRESSION_HPP
#define PHONOMETRICA_REGRESSION_HPP

#include <functional>
#include <phon/string.hpp>
#include <phon/array.hpp>

namespace phonometrica::stats {

struct LinearModel
{
	Array<double> beta;      // regression coefficients
	Array<double> se;        // standard errors
	Array<double> t;         // t-values
	Array<double> p;         // p-values
	Array<double> predicted; // predicted values
	Array<double> residuals; // residual errors
	double rse;              // residual standard error
	intptr_t df;             // degrees of freedom
	double r2;               // R squared
	double adj_r2;           // Adjusted R squared
};

// Generalized linear model
struct GLModel
{
	Array<double> beta;      // regression coefficients
	Array<double> se;        // standard errors
	Array<double> z;         // z-values
	Array<double> p;         // p-values for Wald test
	int niter;               // number of iterations
	bool converged;          // whether the model converged
};


//! Performs linear regression using the least-squared method.
//! \param y a vector of N observations
//! \param X an N by M matrix, where N is the number of observations and M the number of regression coefficients. The
// first column contains the intercept (beta_0).
//! \return a vector of N coefficient (the first coefficient is the intercept).

LinearModel lm(const Array<double> &y, const Array<double> &X);


// Logistic regression
GLModel logit(const Array<double> &y, const Array<double> &X, int max_iter = 200);


// Poisson regression
GLModel poisson(const Array<double> &y, const Array<double> &X, bool robust, int max_iter = 200);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_REGRESSION_HPP
