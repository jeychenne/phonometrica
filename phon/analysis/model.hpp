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
 * Note: The core architecture and integration logic were designed and authored by Julien Eychenne. Portions of the    *
 * statistical estimation logic in this file were developed with the assistance of Claude Opus 4.6 (Anthropic), based  *
 * on published statistical literature and reference R implementations.                                                *
 * All AI-assisted logic has been manually audited, refactored, and validated against a diverse suite of datasets and  *
 * reference R packages to ensure mathematical accuracy and implementation integrity.                                  *
 * While every effort has been made to ensure reliability, this software is provided without a guarantee of being      *
 * bug-free. In the event that discrepancies or errors are discovered, the author will do his best to address them.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MODEL_HPP
#define PHONOMETRICA_MODEL_HPP

#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include <boost/math/special_functions/trigamma.hpp>
#include <phon/string.hpp>
#include <phon/array.hpp>
#include <phon/utils/matrix.hpp>
#include <phon/analysis/prior.hpp>

namespace phonometrica::stats {

// A single grouping factor in a mixed-effects model.
// For example, (1 + vowel | speaker) produces one RandomEffectGroup with
// group_name = "speaker", term_names = {"(Intercept)", "vowel[i]", ...},
// and variances/covariances for those terms.
struct RandomEffectGroup
{
	String group_name;             // e.g. "speaker"
	Array<String> term_names;      // names of random terms within this group
	Array<String> level_names;     // names of group levels, e.g. {"spk01", "spk02", ...}
	intptr_t nlevels = 0;          // number of group levels (e.g. number of speakers)
	Array<double> variance;        // variance for each term (diagonal of covariance matrix)
	Array<double> cov_chol;        // lower triangle of Cholesky factor of covariance matrix (packed)
	// Conditional modes (BLUPs): nlevels × nterms, stored in row-major order
	// (level index varies slowest). Element (j, t) = conditional_modes[j * nterms + t].
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


// Summary of grid integration results for posterior draws (PPC, WAIC).
// Populated at fit time by inla_grid_integrate_gaussian / inla_grid_integrate_pirls.
// Not serialised — used only during the fitting session.
struct GridSummary
{
	int n_points = 0;                 // number of CCD grid points
	int n_beta = 0;                   // number of fixed-effect coefficients (p)
	int n_theta = 0;                  // dimension of outer θ vector
	std::vector<double> weights;      // n_points (normalised)
	std::vector<double> beta;         // n_points × n_beta (row-major): β̂_k
	std::vector<double> vcov_diag;    // n_points × n_beta: diag(Σ_k) [for PPC independent draws]
	std::vector<double> theta;        // n_points × n_theta: θ_k values
};


// Unified model: covers linear models, GLMs, and (eventually) GLMMs.
// For fixed-effects-only models, the random_effects array is empty.
struct Model
{
	// ---- Metadata ----
	String label;       // user-defined display label (empty → default "Model N")
	String formula;     // the formula string, e.g. "f1 ~ vowel + (1|speaker)"
	String family;      // "gaussian", "binomial", "poisson", "negbin", "beta"
	String link;        // "identity", "logit", "log"
	intptr_t nobs = 0;  // number of observations
	intptr_t nfixed = 0; // number of fixed-effects parameters (including intercept)
	Array<String> response_levels; // for binary text response: [reference(0), success(1)] (empty if numeric)
	Estimation estimation = Estimation::Frequentist;
	PriorSpec priors;   // prior specification (only meaningful when estimation == Bayesian)

	// ---- Fixed effects ----
	Array<String> coef_names;  // coefficient names: "(Intercept)", "vowel[i]", etc.
	Array<double> beta;        // estimated coefficients
	Array<double> se;          // standard errors
	Array<double> stat;        // test statistics (t-values for Gaussian, z-values for GLM)
	Array<double> p;           // p-values

	// ---- Bayesian posterior (populated only when estimation == Bayesian) ----
	// For fixed effects: posterior summaries from the (mixture-of-)Gaussian(s) approximation.
	Array<double> posterior_mean;   // posterior mean for each fixed-effect coefficient
	Array<double> posterior_mode;   // posterior mode (MAP estimate at θ*)
	Array<double> posterior_median; // posterior median (0.5 quantile of mixture)
	Array<double> posterior_sd;     // posterior standard deviation
	Array<double> ci_lower;        // lower bound of 95% credible interval
	Array<double> ci_upper;        // upper bound of 95% credible interval
	Array<double> pd;              // probability of direction: max(P(β>0), P(β<0))

	// For hyperparameters (variance component SDs, dispersion, etc.)
	Array<String> hyper_names;
	Array<double> hyper_posterior_mean;
	Array<double> hyper_posterior_sd;
	Array<double> hyper_ci_lower;
	Array<double> hyper_ci_upper;

	// ---- Variance-covariance matrix of fixed-effect coefficients ----
	// nfixed × nfixed matrix (2D Array). For OLS: σ²(X'X)⁻¹; for GLMs: (X'WX)⁻¹;
	// for mixed models: conditional Var(β̂ | θ̂) from the Henderson inverse.
	// Populated by the fitting routines; empty if the model was loaded from file
	// without vcov or constructed directly.
	Array<double> vcov;

	// ---- Column means of the fixed-effects design matrix ----
	// Stored at fit time for use by EMMs when the design matrix X is not available
	// (e.g. after loading a saved analysis). Length = nfixed (1D Array, 1-indexed).
	Array<double> col_means;

	// ---- Variable metadata (for post-hoc analysis, e.g. estimated marginal means) ----
	// One entry per unique predictor variable in the formula's fixed effects.
	// Records whether the variable is numeric or categorical, and for categoricals,
	// the complete set of levels (reference level first). This information is needed
	// to construct reference grids for estimated marginal means and contrasts.
	// Populated by fit(); empty if the model was constructed directly.
	struct VariableInfo
	{
		String name;               // column name in the data, e.g. "vowel"
		bool numeric = true;       // true for continuous, false for categorical
		Array<String> levels;      // categorical only: all levels, reference first; empty for numeric
	};
	Array<VariableInfo> variable_info;

	// ---- Predictions and residuals ----
	Array<double> fitted;      // fitted values (conditional on random effects if present)
	Array<double> residuals;   // response residuals (y - fitted)
	Array<double> offset;      // offset vector (added to η before linkinv); empty if no offset

	// ---- Source-table row indices ----
	// 1-based indices into the original DataTable for the complete cases used
	// in fitting. Length == nobs. Together with fitted / residuals / y this
	// lets downstream code align per-observation quantities back to specific
	// rows in the concordance or dataset the model was fitted on. Empty for
	// models loaded from .phon-analysis files saved before this field was
	// introduced.
	//
	// Note: stored as std::vector (0-based container indexing) rather than
	// Array (1-based) because Array<intptr_t>(size, value) is ambiguous
	// against the two-size_type Array(nrow, ncol) matrix constructor — both
	// arguments being intptr_t makes overload resolution fail. The *values*
	// in the vector are still 1-based (they are DataTable row numbers), only
	// the container indexing changes.
	std::vector<intptr_t> source_rows;

	// ---- Overall fit ----
	double loglik = 0;         // log-likelihood at convergence
	double aic = 0;            // Akaike information criterion
	double bic = 0;            // Bayesian information criterion
	double deviance = 0;       // residual deviance (-2 * loglik for GLMs)

	// ---- Bayesian model comparison ----
	// Laplace-approximated log marginal likelihood: log p(y | M).
	// NaN for frequentist models. Used for Bayes factor computation.
	double log_marginal = std::numeric_limits<double>::quiet_NaN();

	// WAIC (Watanabe-Akaike / Widely Applicable Information Criterion).
	// Computed at fit time for Bayesian models. NaN for frequentist models.
	// Reference: Gelman, Hwang & Vehtari (2014), Statistics and Computing.
	double waic    = std::numeric_limits<double>::quiet_NaN();
	double p_waic  = std::numeric_limits<double>::quiet_NaN();  // effective number of parameters
	double lppd    = std::numeric_limits<double>::quiet_NaN();  // log pointwise predictive density
	double se_waic = std::numeric_limits<double>::quiet_NaN();  // standard error of WAIC

	// Per-observation expected log pointwise predictive density:
	//   elpd_i[j] = lppd_j - pwaic_j  (1-indexed, length = nobs).
	// Populated at fit time for Bayesian models. Used to compute the proper
	// SE of ΔWAIC when comparing models (Vehtari, Gelman & Gabry 2017).
	Array<double> elpd_i;

	// PSIS-LOO (Pareto Smoothed Importance Sampling Leave-One-Out cross-validation).
	// Computed at fit time for Bayesian models. NaN for frequentist models.
	// Reference: Vehtari, Gelman & Gabry (2017), Statistics and Computing.
	double loo_ic  = std::numeric_limits<double>::quiet_NaN();
	double p_loo   = std::numeric_limits<double>::quiet_NaN();  // effective number of parameters
	double se_loo  = std::numeric_limits<double>::quiet_NaN();  // standard error of LOO-IC
	Array<double> elpd_loo_i;  // per-observation LOO elpd (for comparison SE)
	Array<double> pareto_k;    // per-observation Pareto k diagnostic

	// ---- Grid integration summary (not serialised) ----
	// Populated by inla_grid_integrate_* for later use by PPC.
	std::optional<GridSummary> grid_summary;

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

	// ---- Beta regression specific ----
	double phi = 0;            // Beta precision parameter (φ > 0); 0 for other families

	// ---- Student t regression specific ----
	double sigma = 0;          // Student t scale parameter (σ > 0); 0 for other families
	double nu = 0;             // Student t degrees of freedom (ν > 0); 0 for other families

	// ---- Convergence (iterative methods) ----
	int niter = 0;             // number of iterations (0 for OLS)
	bool converged = true;     // whether the optimizer converged
	String optimizer;          // name of the optimizer that produced the final estimates:
	                           // "newton" (Newton's method with LM-damped FD Hessian) or
	                           // "lbfgs" (limited-memory BFGS). Empty for fixed-effects
	                           // GLMs/OLS where the optimizer is implicit.

	// ---- Identifiability diagnostic ----
	// Set to false when the outer joint (β, θ) Hessian at the reported
	// optimum is not positive-definite, ill-conditioned (condition number
	// above ~1e12), or produces non-finite / non-positive SE diagonals
	// under inversion.  These conditions indicate the model is weakly or
	// non-identified by the data — typical causes are a random-slope
	// predictor that is constant within groups, a collinear fixed effect,
	// or a covariance parameter pinned at a boundary (σ²≈0 or |ρ|≈1).
	// The optimizer can still report "converged" in these cases because
	// the prior-regularized objective has a well-defined mode; the flag
	// is the principled warning that the mode is not information-rich.
	// When false, standard errors fall back to the Henderson conditional
	// vcov (still a useful lower bound) and should be interpreted with
	// caution.  Empty `fit_warning` is invariant when `well_identified`
	// is true.
	bool well_identified = true;
	String fit_warning;        // human-readable explanation when well_identified is false

	// ---- Random effects (empty for fixed-effects models) ----
	Array<RandomEffectGroup> random_effects;

	// ---- Smooth terms (empty for non-GAM models) ----
	// One entry per s() term in the formula, populated by the penalized
	// regression solver with EDF, F-test, and plotting metadata.
	struct SmoothResult
	{
		String variable;       // covariate name, e.g. "duration"
		String by;             // by-variable name (empty for plain smooth/intercept)
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

	bool has_vcov() const { return !vcov.empty(); }
	bool has_col_means() const { return !col_means.empty(); }

	bool has_variable_info() const { return !variable_info.empty(); }

	bool is_gaussian() const { return family == "gaussian"; }

	bool is_negbin() const { return family == "negbin"; }

	bool is_beta() const { return family == "beta"; }

	bool is_student() const { return family == "student"; }

	bool is_bayesian() const { return estimation == Estimation::Bayesian; }

	bool is_frequentist() const { return estimation == Estimation::Frequentist; }

	bool has_source_rows() const { return !source_rows.empty(); }

	// Scatter a per-observation vector (length nobs) back to source-table
	// coordinates. Returns an Array of length n_source_rows (1-indexed) where
	// entries at positions source_rows[i] hold per_obs[i], and all other
	// entries are NaN. Returns an empty Array if source_rows is not populated
	// (e.g. the model was loaded from a pre-this-version .phon-analysis file)
	// or if the caller passes a mismatched length.
	Array<double> align_to_source(const Array<double> &per_obs,
	                              intptr_t n_source_rows) const
	{
		if (source_rows.empty() || per_obs.size() != nobs || n_source_rows <= 0)
			return Array<double>();

		Array<double> out(n_source_rows, std::nan(""));
		// source_rows is std::vector (0-based) but holds 1-based DataTable row
		// indices; per_obs is Array<double> (1-based).
		for (intptr_t i = 0; i < nobs; i++)
		{
			intptr_t r = source_rows[i];
			if (r >= 1 && r <= n_source_rows)
				out[r] = per_obs[i + 1];
		}
		return out;
	}

	// Convenience: fitted values aligned to source-table rows (NaN for rows
	// excluded from fitting because of missing cells).
	Array<double> fitted_aligned(intptr_t n_source_rows) const
	{
		return align_to_source(fitted, n_source_rows);
	}

	// Convenience: residuals aligned to source-table rows.
	Array<double> residuals_aligned(intptr_t n_source_rows) const
	{
		return align_to_source(residuals, n_source_rows);
	}

	// Number of estimated parameters (for AIC/BIC).
	// Fixed-effects parameters + dispersion (if Gaussian, NB, or Beta) + variance components.
	intptr_t nparams() const
	{
		intptr_t k = nfixed;
		if (is_gaussian()) k += 1; // residual variance
		if (is_negbin()) k += 1;   // overdispersion θ
		if (is_beta()) k += 1;     // precision φ
		if (is_student()) k += 2;  // scale σ and degrees of freedom ν
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
		if (!offset.empty()) {
			Eigen::Map<const Vector<double>> off(offset.data(), nobs);
			eta += off;
		}
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
		if (!offset.empty()) {
			Eigen::Map<const Vector<double>> off(const_cast<double*>(offset.data()), nobs);
			eta_fixed += off;
		}
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
		else if (is_beta())
		{
			// Beta with logit link: distribution-specific variance uses the
			// trigamma-based formula from Nakagawa et al. (2017).
			// σ²_d = trigamma(μ̄φ) + trigamma((1-μ̄)φ)
			// where μ̄ = logistic(mean(η)) is the mean predicted proportion.
			double mu_bar = 1.0 / (1.0 + std::exp(-mean_eta));
			mu_bar = std::clamp(mu_bar, 1e-6, 1.0 - 1e-6);
			double phi_val = std::max(phi, 1e-10);
			var_d = boost::math::trigamma(mu_bar * phi_val) + boost::math::trigamma((1.0 - mu_bar) * phi_val);
		}
		else if (is_student())
		{
			// Student t with identity link: use σ² directly.
			// Defensible since the t distribution is a scale mixture of normals.
			// For ν > 2, Var(Y) = σ² ν/(ν−2), but using σ² is the natural
			// analogue of the Gaussian case (where var_d = σ²_resid).
			var_d = sigma * sigma;
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
