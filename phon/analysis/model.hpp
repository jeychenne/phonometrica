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
 * Purpose: unified statistical model class for fixed-effects and (eventually) mixed-effects models.                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MODEL_HPP
#define PHONOMETRICA_MODEL_HPP

#include <cmath>
#include <limits>
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

	// ---- Nakagawa pseudo-R² (mixed models only) ----
	// Marginal R²: proportion of variance explained by fixed effects.
	// Conditional R²: proportion explained by fixed + random effects.
	// NaN means not computed (e.g. no random effects, or design matrix unavailable).
	double r2_marginal = std::numeric_limits<double>::quiet_NaN();
	double r2_conditional = std::numeric_limits<double>::quiet_NaN();

	// ---- Negative binomial specific ----
	double theta = 0;          // NB overdispersion parameter (θ > 0); 0 for other families

	// ---- Convergence (iterative methods) ----
	int niter = 0;             // number of iterations (0 for OLS)
	bool converged = true;     // whether the optimizer converged

	// ---- Random effects (empty for fixed-effects models) ----
	Array<RandomEffectGroup> random_effects;

	// ---- Smooth terms (empty for non-GAM models) ----
	// One entry per s() term in the formula, populated by the penalized
	// regression solver with EDF, F-test, and plotting metadata.
	struct SmoothResult
	{
		String variable;       // covariate name, e.g. "duration"
		String basis;          // basis type, e.g. "cr"
		intptr_t k = 0;        // original basis dimension
		double edf = 0;        // effective degrees of freedom
		double ref_df = 0;     // reference df for F-test
		double F_stat = 0;     // F-statistic
		double p_value = 1;    // approximate p-value
		intptr_t col_start = 0; // 0-based starting column in X for this smooth's basis
		intptr_t col_count = 0; // number of basis columns
	};
	Array<SmoothResult> smooth_terms;

	// ---- Design matrices (stored for predict/diagnostics) ----
	// X: fixed-effects design matrix (nobs × nfixed), stored as 2D Array
	Array<double> X;
	// Z: random-effects design matrix (empty for fixed-effects models)
	Array<double> Z;
	// y: response vector
	Array<double> y;

	// ---- Convenience accessors ----

	bool has_random_effects() const { return !random_effects.empty(); }

	bool has_smooth_terms() const { return !smooth_terms.empty(); }

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
	// Compute Nakagawa & Schielzeth (2013) pseudo-R² for mixed models.
	// Stores results in r2_marginal and r2_conditional.
	// No-op (leaves NaN) for fixed-effects-only models or if X/beta are empty.
	void compute_pseudo_r2()
	{
		if (!has_random_effects()) return;
		if (X.empty() || beta.empty()) return;
		if (nobs <= 0 || nfixed <= 0) return;

		// σ²_f: variance of the fixed-effects linear predictor.
		Eigen::Map<Matrix<double>> Xm(const_cast<double*>(X.data()), nobs, nfixed);
		Eigen::Map<Vector<double>> bm(const_cast<double*>(beta.data()), nfixed);
		Vector<double> eta_fixed = Xm * bm;
		double mean_eta = eta_fixed.mean();
		double var_f = (eta_fixed.array() - mean_eta).square().mean();

		// σ²_r: random-effects variance contribution.
		double var_r = 0;
		for (intptr_t gi = 1; gi <= random_effects.size(); gi++)
		{
			auto &re = random_effects[gi];
			intptr_t q = re.term_names.size();

			if (!re.Z_design.empty() && !re.cov_chol.empty() && q > 0)
			{
				// Reconstruct Σ = L L' from the packed Cholesky factor.
				// Note: cov_chol stores the raw Cholesky factor from Eigen::LLT
				// (NOT log-diagonal like the optimization parameters).
				Eigen::MatrixXd L = Eigen::MatrixXd::Zero(q, q);
				for (intptr_t r = 0; r < q; r++)
					for (intptr_t c = 0; c <= r; c++)
					{
						intptr_t idx = r * (r + 1) / 2 + c;
						L(r, c) = (idx < re.cov_chol.size()) ? re.cov_chol[idx + 1] : 0.0;
					}
				Eigen::MatrixXd Sigma = L * L.transpose();

				// Z_g: n × q, row-major.
				Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
					Zg(re.Z_design.data(), nobs, q);

				// σ²_r,g = tr(Z_g' Z_g Σ_g) / n
				Eigen::MatrixXd ZtZ = (Zg.transpose() * Zg) / static_cast<double>(nobs);
				var_r += (ZtZ.array() * Sigma.array()).sum();
			}
			else
			{
				// Fallback: sum of diagonal variances.
				// Exact for random intercepts, approximate for random slopes.
				for (intptr_t t = 1; t <= re.variance.size(); t++)
					var_r += re.variance[t];
			}
		}

		// σ²_d: distribution-specific variance.
		double var_d = 0;
		if (is_gaussian())
		{
			var_d = rse * rse;
		}
		else if (family == "binomial")
		{
			var_d = M_PI * M_PI / 3.0; // logit link
		}
		else if (family == "poisson")
		{
			double lambda = std::exp(mean_eta + (var_f + var_r) / 2.0);
			var_d = std::log(1.0 + 1.0 / std::max(lambda, 1e-10));
		}
		else if (is_negbin())
		{
			double lambda = std::exp(mean_eta + (var_f + var_r) / 2.0);
			var_d = std::log(1.0 + 1.0 / std::max(lambda, 1e-10) + 1.0 / std::max(theta, 1e-10));
		}
		else
		{
			return; // unsupported family
		}

		double denom = var_f + var_r + var_d;
		if (denom <= 0) return;

		r2_marginal = var_f / denom;
		r2_conditional = (var_f + var_r) / denom;
	}
};

} // namespace phonometrica::stats

namespace phonometrica::traits {
template<> struct maybe_cyclic<stats::Model> : std::false_type { };
}

#endif // PHONOMETRICA_MODEL_HPP
