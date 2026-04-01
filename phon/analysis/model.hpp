/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: unified statistical model class for fixed-effects and (eventually) mixed-effects models.                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MODEL_HPP
#define PHONOMETRICA_MODEL_HPP

#include <cmath>
#include <vector>
#include <phon/string.hpp>
#include <phon/array.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica::stats {

// A single grouping factor in a mixed-effects model.
// For example, (1 + vowel | speaker) produces one RandomEffectGroup with
// group_name = "speaker", term_names = {"(Intercept)", "vowel[i]", ...},
// and variances/covariances for those terms.
struct RandomEffectGroup
{
	String group_name;             // e.g. "speaker"
	Array<String> term_names;      // names of random terms within this group
	intptr_t nlevels = 0;          // number of group levels (e.g. number of speakers)
	Array<double> variance;        // variance for each term (diagonal of covariance matrix)
	Array<double> cov_chol;        // lower triangle of Cholesky factor of covariance matrix (packed)
	// Conditional modes (BLUPs): nlevels × nterms, stored in column-major order.
	// Empty until a mixed model is fitted.
	Array<double> conditional_modes;

	// ---- Z design info (for simulation-based diagnostics) ----
	// These are populated at fitting time from GroupingInfo and are NOT serialised.
	// If the model is loaded from file, they will be empty, and the diagnostic
	// code falls back to the analytical (conditional) path.
	intptr_t nterms = 1;                    // q_g: number of random terms per level
	std::vector<intptr_t> indices;           // n_obs: per-observation level index [0, nlevels)
	std::vector<double> Z_design;            // n_obs × nterms, row-major
};


// Unified model: covers linear models, GLMs, and (eventually) GLMMs.
// For fixed-effects-only models, the random_effects array is empty.
struct Model
{
	// ---- Metadata ----
	String formula;     // the formula string, e.g. "f1 ~ vowel + (1|speaker)"
	String family;      // "gaussian", "binomial", "poisson", "negbin"
	String link;        // "identity", "logit", "log"
	intptr_t nobs = 0;  // number of observations
	intptr_t nfixed = 0; // number of fixed-effects parameters (including intercept)
	Array<String> response_levels; // for binary text response: [reference(0), success(1)] (empty if numeric)

	// ---- Fixed effects ----
	Array<String> coef_names;  // coefficient names: "(Intercept)", "vowel[i]", etc.
	Array<double> beta;        // estimated coefficients
	Array<double> se;          // standard errors
	Array<double> stat;        // test statistics (t-values for Gaussian, z-values for GLM)
	Array<double> p;           // p-values

	// ---- Predictions and residuals ----
	Array<double> fitted;      // fitted values (conditional on random effects if present)
	Array<double> residuals;   // response residuals (y - fitted)

	// ---- Overall fit ----
	double loglik = 0;         // log-likelihood at convergence
	double aic = 0;            // Akaike information criterion
	double bic = 0;            // Bayesian information criterion
	double deviance = 0;       // residual deviance (-2 * loglik for GLMs)

	// ---- Linear model specific ----
	double rse = 0;            // residual standard error (Gaussian only)
	intptr_t df_residual = 0;  // residual degrees of freedom
	double r2 = 0;             // R² (Gaussian only)
	double adj_r2 = 0;         // adjusted R² (Gaussian only)

	// ---- Negative binomial specific ----
	double theta = 0;          // NB overdispersion parameter (θ > 0); 0 for other families

	// ---- Convergence (iterative methods) ----
	int niter = 0;             // number of iterations (0 for OLS)
	bool converged = true;     // whether the optimizer converged

	// ---- Random effects (empty for fixed-effects models) ----
	Array<RandomEffectGroup> random_effects;

	// ---- Design matrices (stored for predict/diagnostics) ----
	// X: fixed-effects design matrix (nobs × nfixed), stored as 2D Array
	Array<double> X;
	// Z: random-effects design matrix (empty for fixed-effects models)
	Array<double> Z;
	// y: response vector
	Array<double> y;

	// ---- Convenience accessors ----

	bool has_random_effects() const { return !random_effects.empty(); }

	bool is_gaussian() const { return family == "gaussian"; }

	bool is_negbin() const { return family == "negbin"; }

	// Number of estimated parameters (for AIC/BIC).
	// Fixed-effects parameters + dispersion (if Gaussian or NB) + variance components.
	intptr_t nparams() const
	{
		intptr_t k = nfixed;
		if (is_gaussian()) k += 1; // residual variance
		if (is_negbin()) k += 1;   // overdispersion θ
		for (intptr_t i = 1; i <= random_effects.size(); i++)
		{
			auto &g = random_effects[i];
			// Number of unique variance/covariance parameters = q*(q+1)/2
			intptr_t q = g.term_names.size();
			k += q * (q + 1) / 2;
		}
		return k;
	}

	// Compute AIC and BIC from log-likelihood and nparams.
	void compute_information_criteria()
	{
		intptr_t k = nparams();
		aic = -2 * loglik + 2 * k;
		bic = -2 * loglik + std::log(static_cast<double>(nobs)) * k;
		deviance = -2 * loglik;
	}

	// Compute fitted values from stored design matrix and coefficients.
	// For Gaussian: fitted = X * beta.
	// For GLMs: fitted = linkinv(X * beta).
	// Caller must provide the inverse link function.
	void compute_fitted(const std::function<Vector<double>(const Vector<double> &)> &linkinv)
	{
		Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), nobs, nfixed);
		Eigen::Map<Vector<double>> bm(beta.data(), nfixed);
		Vector<double> eta = Xm * bm;
		Vector<double> mu = linkinv(eta);

		fitted.resize(nobs);
		residuals.resize(nobs);
		Eigen::Map<Vector<double>> ym(y.data(), nobs);
		for (intptr_t i = 0; i < nobs; i++)
		{
			fitted[i + 1] = mu[i];
			residuals[i + 1] = ym[i] - mu[i];
		}
	}
};

} // namespace phonometrica::stats

namespace phonometrica::traits {
template<> struct maybe_cyclic<stats::Model> : std::false_type { };
}

#endif // PHONOMETRICA_MODEL_HPP
