/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
