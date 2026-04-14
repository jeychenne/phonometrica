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
 * Purpose: GLM family abstraction (link function, inverse link, log-likelihood, variance function).                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FAMILY_HPP
#define PHONOMETRICA_FAMILY_HPP

#include <cmath>
#include <functional>
#include <stdexcept>
#include <boost/math/special_functions/digamma.hpp>
#include <phon/string.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica::stats {

// GLM family: encapsulates the distributional assumption and link function.
// Each family provides:
//   - linkinv:  inverse link function, maps linear predictor η to mean μ
//   - loglik:   log-likelihood contribution given y and μ (summed over observations)
//   - variance: variance function V(μ), needed for IWLS and covariance estimation
//   - link:     link function, maps μ to η (used less often, but needed for initialization)
//   - mu_eta:   derivative of inverse link dμ/dη, needed for generalized IWLS weights
//
// std::function is used rather than plain function pointers so that families with
// extra parameters (e.g. negative binomial θ, beta φ) can capture them via lambdas.

struct Family
{
	String name;      // "gaussian", "binomial", "poisson", "negbin", "beta", "student"
	String link_name; // "identity", "logit", "log"

	// Overdispersion parameter for negative binomial (θ > 0).
	// Unused (0) for other families.
	double theta = 0;

	// Precision parameter for beta regression (φ > 0).
	// Unused (0) for other families.
	double phi = 0;

	// Scale parameter for Student t regression (σ > 0).
	// Unused (0) for other families.
	double sigma = 0;

	// Degrees-of-freedom parameter for Student t regression (ν > 0).
	// Controls tail heaviness; ν → ∞ reduces to Gaussian.
	// Unused (0) for other families.
	double nu = 0;

	// Inverse link: μ = g⁻¹(η)
	// Maps the linear predictor to the mean of the response.
	std::function<Vector<double>(const Vector<double> &eta)> linkinv;

	// Link: η = g(μ)
	// Maps the mean of the response to the linear predictor.
	std::function<Vector<double>(const Vector<double> &mu)> link;

	// Log-likelihood: sum over observations of log f(y | μ)
	// For Gaussian, includes the constant terms.
	std::function<double(const Vector<double> &y, const Vector<double> &mu)> loglik;

	// Variance function: V(μ)
	// Returns a vector of per-observation variances as a function of the mean.
	// For Gaussian: V(μ) = 1; for Binomial: V(μ) = μ(1-μ); for Poisson: V(μ) = μ;
	// for NB: V(μ) = μ + μ²/θ; for Beta: V(μ) = μ(1-μ)/(1+φ);
	// for Student: V(μ) = 1 (identity link, weights handled via custom_weights).
	std::function<Vector<double>(const Vector<double> &mu)> variance;

	// Derivative of inverse link: dμ/dη.
	// For identity: 1; for logit: μ(1-μ); for log: μ.
	// Used for generalized IWLS weights: w_i = (dμ/dη)² / V(μ).
	std::function<Vector<double>(const Vector<double> &mu)> mu_eta;

	// Deviance residuals: d(y, μ)
	// Returns the vector of signed deviance residuals.
	std::function<Vector<double>(const Vector<double> &y, const Vector<double> &mu)> deviance_residuals;

	// Optional custom IWLS weights for non-standard families (e.g. Student t).
	// Takes (y, mu) and returns per-observation weights.  When set, replaces
	// the standard (dμ/dη)² / V(μ) weight computation in PIRLS.
	// Student t:  w_i = (ν + 1) / (ν σ² + (y_i − μ_i)²).
	std::function<Vector<double>(const Vector<double> &y, const Vector<double> &mu)> custom_weights;

	// Third derivative of per-observation log-likelihood w.r.t. linear predictor η.
	// Returns ℓ'''(η_i) for each observation.  Used by the simplified Laplace
	// correction (Tierney-Kadane skewness adjustment) in INLA grid integration.
	//
	// For Gaussian: identically zero (no correction needed).
	// For other families: captures the skewness of the log-likelihood surface.
	//
	// Reference: Rue, Martino & Chopin (2009), Section 3.2.2.
	std::function<Vector<double>(const Vector<double> &y, const Vector<double> &mu,
	                             const Vector<double> &eta)> loglik_d3;

	// Factory methods for supported families.
	static Family gaussian();
	static Family binomial();
	static Family poisson();
	static Family negbin(double theta);
	static Family beta(double phi);
	static Family student(double sigma, double nu);

	// Look up a family by name. Throws if not found.
	// For "negbin", creates with theta=1 (caller should set theta afterwards).
	// For "beta", creates with phi=1 (caller should set phi afterwards).
	// For "student", creates with sigma=1, nu=5 (caller should update).
	static Family from_name(const String &name);

	// Returns true if this family has extra parameters that must be estimated
	// beyond the standard fixed-effects coefficients and variance components.
	bool has_dispersion_param() const { return name == "negbin" || name == "beta" || name == "student"; }

	// Number of extra dispersion/scale parameters to append to the outer
	// optimization vector:  0 for standard families, 1 for NB/beta, 2 for Student t.
	int n_dispersion_params() const
	{
		if (name == "negbin" || name == "beta") return 1;
		if (name == "student") return 2;
		return 0;
	}
};

// ---------------------------------------------------------------------------
// Inline implementations of family-specific functions
// ---------------------------------------------------------------------------

namespace detail {

// --- Gaussian (identity link) ---

inline Vector<double> gaussian_linkinv(const Vector<double> &eta)
{
	return eta; // identity
}

inline Vector<double> gaussian_link(const Vector<double> &mu)
{
	return mu; // identity
}

inline double gaussian_loglik(const Vector<double> &y, const Vector<double> &mu)
{
	// -n/2 * log(2π) - n/2 * log(σ²) - RSS/(2σ²)
	// For model comparison purposes, we compute the profile log-likelihood
	// where σ² = RSS/n (MLE estimate).
	intptr_t n = y.size();
	double rss = (y - mu).squaredNorm();
	double sigma2 = rss / n;
	if (sigma2 <= 0) sigma2 = 1e-300;
	return -0.5 * n * (std::log(2 * M_PI) + std::log(sigma2) + 1.0);
}

inline Vector<double> gaussian_variance(const Vector<double> &mu)
{
	return Vector<double>::Ones(mu.size()); // V(μ) = 1
}

inline Vector<double> gaussian_mu_eta(const Vector<double> &mu)
{
	return Vector<double>::Ones(mu.size()); // dμ/dη = 1 for identity link
}

inline Vector<double> gaussian_deviance_residuals(const Vector<double> &y, const Vector<double> &mu)
{
	return y - mu; // for Gaussian, deviance residuals = raw residuals
}

// --- Binomial (logit link) ---

inline Vector<double> binomial_linkinv(const Vector<double> &eta)
{
	// μ = 1 / (1 + exp(-η))
	return 1.0 / (1.0 + (-eta.array()).exp());
}

inline Vector<double> binomial_link(const Vector<double> &mu)
{
	// η = log(μ / (1 - μ))
	// Clamp to avoid log(0)
	auto clamped = mu.array().max(1e-10).min(1.0 - 1e-10);
	return (clamped / (1.0 - clamped)).log();
}

inline double binomial_loglik(const Vector<double> &y, const Vector<double> &mu)
{
	// sum( y*log(μ) + (1-y)*log(1-μ) )
	auto clamped = mu.array().max(1e-10).min(1.0 - 1e-10);
	return (y.array() * clamped.log() + (1.0 - y.array()) * (1.0 - clamped).log()).sum();
}

inline Vector<double> binomial_variance(const Vector<double> &mu)
{
	// V(μ) = μ(1-μ)
	return (mu.array() * (1.0 - mu.array())).matrix();
}

inline Vector<double> binomial_mu_eta(const Vector<double> &mu)
{
	// dμ/dη = μ(1-μ) for logit link
	return (mu.array() * (1.0 - mu.array())).matrix();
}

inline Vector<double> binomial_deviance_residuals(const Vector<double> &y, const Vector<double> &mu)
{
	intptr_t n = y.size();
	Vector<double> dr(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double yi = y[i];
		double mi = std::clamp(mu[i], 1e-10, 1.0 - 1e-10);
		double d = 0;
		if (yi > 0) d += yi * std::log(yi / mi);
		if (yi < 1) d += (1 - yi) * std::log((1 - yi) / (1 - mi));
		dr[i] = (yi >= mi ? 1.0 : -1.0) * std::sqrt(2 * d);
	}
	return dr;
}

// --- Poisson (log link) ---

inline Vector<double> poisson_linkinv(const Vector<double> &eta)
{
	return eta.array().exp();
}

inline Vector<double> poisson_link(const Vector<double> &mu)
{
	return mu.array().max(1e-10).log();
}

inline double poisson_loglik(const Vector<double> &y, const Vector<double> &mu)
{
	// sum( y*log(μ) - μ - lgamma(y+1) )
	intptr_t n = y.size();
	double ll = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double mi = std::max(mu[i], 1e-10);
		ll += y[i] * std::log(mi) - mi - std::lgamma(y[i] + 1.0);
	}
	return ll;
}

inline Vector<double> poisson_variance(const Vector<double> &mu)
{
	return mu; // V(μ) = μ
}

inline Vector<double> poisson_mu_eta(const Vector<double> &mu)
{
	return mu; // dμ/dη = μ for log link
}

inline Vector<double> poisson_deviance_residuals(const Vector<double> &y, const Vector<double> &mu)
{
	intptr_t n = y.size();
	Vector<double> dr(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double yi = y[i];
		double mi = std::max(mu[i], 1e-10);
		double d = 0;
		if (yi > 0) d += yi * std::log(yi / mi);
		d -= (yi - mi);
		dr[i] = (yi >= mi ? 1.0 : -1.0) * std::sqrt(std::max(2.0 * d, 0.0));
	}
	return dr;
}

// --- Negative binomial (log link, NB2 parameterisation) ---
//
// y ~ NB(μ, θ)  where V(μ) = μ + μ²/θ
//
// log f(y|μ,θ) = lgamma(y+θ) - lgamma(θ) - lgamma(y+1)
//              + θ log(θ/(θ+μ)) + y log(μ/(θ+μ))
//
// The log link (η = log μ) is NOT the canonical link for NB, so
// the IWLS weights differ from V(μ): w = μ²/V(μ) = μθ/(θ+μ).

inline Vector<double> negbin_linkinv(const Vector<double> &eta)
{
	return eta.array().exp(); // same as Poisson
}

inline Vector<double> negbin_link(const Vector<double> &mu)
{
	return mu.array().max(1e-10).log(); // same as Poisson
}

inline double negbin_loglik(const Vector<double> &y, const Vector<double> &mu, double theta)
{
	intptr_t n = y.size();
	double ll = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double yi = y[i];
		double mi = std::max(mu[i], 1e-10);
		ll += std::lgamma(yi + theta) - std::lgamma(theta) - std::lgamma(yi + 1.0)
		      + theta * std::log(theta / (theta + mi))
		      + yi * std::log(mi / (theta + mi));
	}
	return ll;
}

inline Vector<double> negbin_variance(const Vector<double> &mu, double theta)
{
	// V(μ) = μ + μ²/θ
	return (mu.array() + mu.array().square() / theta).matrix();
}

inline Vector<double> negbin_mu_eta(const Vector<double> &mu)
{
	return mu; // dμ/dη = μ for log link (same as Poisson)
}

inline Vector<double> negbin_deviance_residuals(const Vector<double> &y, const Vector<double> &mu, double theta)
{
	intptr_t n = y.size();
	Vector<double> dr(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double yi = y[i];
		double mi = std::max(mu[i], 1e-10);
		double d = 0;
		if (yi > 0) {
			d += yi * std::log(yi / mi);
		}
		d -= (yi + theta) * std::log((yi + theta) / (mi + theta));
		dr[i] = (yi >= mi ? 1.0 : -1.0) * std::sqrt(std::max(2.0 * d, 0.0));
	}
	return dr;
}

// --- Beta (logit link, Ferrari & Cribari-Neto 2004 parameterisation) ---
//
// y ~ Beta(μφ, (1-μ)φ)  where φ > 0 is the precision parameter.
//
// E[Y] = μ,  Var(Y) = μ(1-μ) / (1+φ)
//
// log f(y|μ,φ) = lgamma(φ) - lgamma(μφ) - lgamma((1-μ)φ)
//              + (μφ - 1) log(y) + ((1-μ)φ - 1) log(1 - y)
//
// The logit link (η = log(μ/(1-μ))) is the standard link for beta regression.
// dμ/dη = μ(1-μ), same as binomial logit.
//
// IWLS working weights: w_i = (dμ/dη)² / V(μ) = μ(1-μ)(1+φ) = φ · μ(1-μ).
// (Since V(μ) = μ(1-μ)/(1+φ) and (dμ/dη)² = [μ(1-μ)]².)
//
// Mathematical reference:
//   Ferrari, S. L. P. & Cribari-Neto, F. (2004). Beta regression for modelling
//   rates and proportions. Journal of Applied Statistics, 31(7), 799–815.

// Link and inverse link are the same as binomial logit.

inline double beta_loglik(const Vector<double> &y, const Vector<double> &mu, double phi)
{
	intptr_t n = y.size();
	double ll = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double mi = std::clamp(mu[i], 1e-10, 1.0 - 1e-10);
		double yi = std::clamp(y[i], 1e-10, 1.0 - 1e-10);
		double a = mi * phi;           // shape1
		double b = (1.0 - mi) * phi;   // shape2
		ll += std::lgamma(phi) - std::lgamma(a) - std::lgamma(b)
		      + (a - 1.0) * std::log(yi) + (b - 1.0) * std::log(1.0 - yi);
	}
	return ll;
}

inline Vector<double> beta_variance(const Vector<double> &mu, double phi)
{
	// V(μ) = μ(1-μ) / (1+φ)
	return (mu.array() * (1.0 - mu.array()) / (1.0 + phi)).matrix();
}

inline Vector<double> beta_deviance_residuals(const Vector<double> &y, const Vector<double> &mu, double phi)
{
	// Signed deviance residuals:
	//   d_i = sign(y_i - μ_i) * sqrt(2 * [loglik_sat_i - loglik_i])
	// where the saturated model has μ = y.
	intptr_t n = y.size();
	Vector<double> dr(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double mi = std::clamp(mu[i], 1e-10, 1.0 - 1e-10);
		double yi = std::clamp(y[i], 1e-10, 1.0 - 1e-10);

		// Saturated: μ = y
		double a_sat = yi * phi, b_sat = (1.0 - yi) * phi;
		double ll_sat = std::lgamma(phi) - std::lgamma(a_sat) - std::lgamma(b_sat)
		                + (a_sat - 1.0) * std::log(yi) + (b_sat - 1.0) * std::log(1.0 - yi);

		// Fitted
		double a_fit = mi * phi, b_fit = (1.0 - mi) * phi;
		double ll_fit = std::lgamma(phi) - std::lgamma(a_fit) - std::lgamma(b_fit)
		                + (a_fit - 1.0) * std::log(yi) + (b_fit - 1.0) * std::log(1.0 - yi);

		double d = 2.0 * (ll_sat - ll_fit);
		dr[i] = (yi >= mi ? 1.0 : -1.0) * std::sqrt(std::max(d, 0.0));
	}
	return dr;
}

// --- Student t (identity link, location-scale regression) ---
//
// y ~ t(μ, σ, ν)  where μ is the location (mean for ν > 1),
// σ > 0 is the scale, and ν > 0 is the degrees of freedom.
//
// E[Y] = μ (for ν > 1),  Var(Y) = σ² ν/(ν−2) (for ν > 2)
//
// log f(y|μ,σ,ν) = lgamma((ν+1)/2) - lgamma(ν/2) - ½ log(νπσ²)
//                - (ν+1)/2 · log(1 + (y−μ)²/(νσ²))
//
// Identity link (η = μ): same as Gaussian.
//
// IWLS weights (Fisher scoring for the location parameter):
//   w_i = (ν + 1) / (ν σ² + (y_i − μ_i)²)
//
// These are observation-dependent and downweight outliers: when ν → ∞,
// w_i → 1/σ² (Gaussian); when ν is small, large residuals get much
// lower weight.  This is what makes t-regression robust.
//
// Mathematical reference:
//   Lange, K. L., Little, R. J. A. & Taylor, J. M. G. (1989). Robust
//   statistical modeling using the t distribution. JASA, 84(408), 881–896.

// Link and inverse link are the same as Gaussian (identity).

inline double student_loglik(const Vector<double> &y, const Vector<double> &mu,
                              double sigma, double nu)
{
	intptr_t n = y.size();
	double ll = 0;
	double log_const = std::lgamma(0.5 * (nu + 1.0)) - std::lgamma(0.5 * nu)
	                   - 0.5 * std::log(nu * M_PI * sigma * sigma);
	for (intptr_t i = 0; i < n; i++)
	{
		double r = y[i] - mu[i];
		ll += log_const - 0.5 * (nu + 1.0) * std::log(1.0 + r * r / (nu * sigma * sigma));
	}
	return ll;
}

inline Vector<double> student_weights(const Vector<double> &y, const Vector<double> &mu,
                                       double sigma, double nu)
{
	// w_i = (ν + 1) / (ν σ² + (y_i − μ_i)²)
	intptr_t n = y.size();
	double nu_sigma2 = nu * sigma * sigma;
	Vector<double> w(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double r = y[i] - mu[i];
		w[i] = (nu + 1.0) / (nu_sigma2 + r * r);
	}
	return w;
}

inline Vector<double> student_deviance_residuals(const Vector<double> &y, const Vector<double> &mu,
                                                  double sigma, double nu)
{
	// Signed deviance residuals: d_i = sign(y_i − μ_i) · sqrt(2 (ll_sat − ll_fit))
	// The saturated model has μ = y (ll_sat_i = log_const, since log(1 + 0) = 0).
	intptr_t n = y.size();
	Vector<double> dr(n);
	double sigma2 = sigma * sigma;
	for (intptr_t i = 0; i < n; i++)
	{
		double r = y[i] - mu[i];
		// ll_sat_i - ll_fit_i = (ν+1)/2 · log(1 + r²/(νσ²))
		double d = (nu + 1.0) * std::log(1.0 + r * r / (nu * sigma2));
		dr[i] = (r >= 0 ? 1.0 : -1.0) * std::sqrt(std::max(d, 0.0));
	}
	return dr;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Third derivative of per-observation log-likelihood w.r.t. η.
// Placed outside detail:: because some use lambdas capturing family params.
// ---------------------------------------------------------------------------

namespace d3_detail {

// Gaussian (identity link): ℓ(η) = -(y-η)²/(2σ²) → ℓ'''(η) = 0 for all observations.
inline Vector<double> gaussian_d3(const Vector<double> &y, const Vector<double> &mu,
                                   const Vector<double> &eta)
{
	return Vector<double>::Zero(y.size());
}

// Poisson (log link): ℓ(η) = yη - exp(η) → ℓ'''(η) = -exp(η) = -μ.
inline Vector<double> poisson_d3(const Vector<double> &y, const Vector<double> &mu,
                                  const Vector<double> &eta)
{
	return -mu;
}

// Binomial (logit link): ℓ'''(η) = -μ(1-μ)(1-2μ).
inline Vector<double> binomial_d3(const Vector<double> &y, const Vector<double> &mu,
                                   const Vector<double> &eta)
{
	intptr_t n = y.size();
	Vector<double> d3(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double m = std::clamp(mu[i], 1e-10, 1.0 - 1e-10);
		d3[i] = -m * (1.0 - m) * (1.0 - 2.0 * m);
	}
	return d3;
}

// Negative binomial (log link):
//   ℓ''(η)  = -θμ(θ+y) / (μ+θ)²
//   ℓ'''(η) = -θμ(θ+y)(θ-μ) / (μ+θ)³
// where μ = exp(η), θ = NB size parameter.
inline Vector<double> negbin_d3(const Vector<double> &y, const Vector<double> &mu,
                                 const Vector<double> &eta, double theta)
{
	intptr_t n = y.size();
	Vector<double> d3(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double m = std::max(mu[i], 1e-10);
		double mt = m + theta;
		d3[i] = -theta * m * (theta + y[i]) * (theta - m) / (mt * mt * mt);
	}
	return d3;
}

// Beta (logit link): per-observation numerical third derivative via
// central differences on the per-observation log-likelihood.
//
// ℓ_i(η) = lgamma(φ) − lgamma(μφ) − lgamma((1−μ)φ)
//         + (μφ−1)log(y_i) + ((1−μ)φ−1)log(1−y_i)
// where μ = 1/(1+exp(−η)).
inline Vector<double> beta_d3(const Vector<double> &y, const Vector<double> &mu,
                               const Vector<double> &eta, double phi)
{
	intptr_t n = y.size();
	Vector<double> d3(n);
	double h = 1e-4;
	double inv_2h3 = 1.0 / (2.0 * h * h * h);

	auto per_obs_ll = [phi](double yi, double eta_i) -> double
	{
		double mu_i = 1.0 / (1.0 + std::exp(-eta_i));
		mu_i = std::clamp(mu_i, 1e-10, 1.0 - 1e-10);
		yi = std::clamp(yi, 1e-10, 1.0 - 1e-10);
		double a = mu_i * phi;
		double b = (1.0 - mu_i) * phi;
		return std::lgamma(phi) - std::lgamma(a) - std::lgamma(b)
		       + (a - 1.0) * std::log(yi) + (b - 1.0) * std::log(1.0 - yi);
	};

	for (intptr_t i = 0; i < n; i++)
	{
		double e = eta[i];
		double f_p2 = per_obs_ll(y[i], e + 2.0 * h);
		double f_p1 = per_obs_ll(y[i], e + h);
		double f_m1 = per_obs_ll(y[i], e - h);
		double f_m2 = per_obs_ll(y[i], e - 2.0 * h);
		d3[i] = (f_p2 - 2.0 * f_p1 + 2.0 * f_m1 - f_m2) * inv_2h3;
	}
	return d3;
}

// Student t (identity link):
//   ℓ_i(η) = const − (ν+1)/2 · log(1 + r²/(νσ²))   where r = y_i − η
//   ℓ'''(η) = −2(ν+1) r (3νσ² − r²) / (νσ² + r²)³
inline Vector<double> student_d3(const Vector<double> &y, const Vector<double> &mu,
                                  const Vector<double> &eta, double sigma, double nu)
{
	intptr_t n = y.size();
	Vector<double> d3(n);
	double s2 = nu * sigma * sigma;

	for (intptr_t i = 0; i < n; i++)
	{
		double r = y[i] - mu[i]; // identity link: η = μ
		double r2 = r * r;
		double denom = s2 + r2;
		d3[i] = -2.0 * (nu + 1.0) * r * (3.0 * s2 - r2) / (denom * denom * denom);
	}
	return d3;
}

} // namespace d3_detail

// ---------------------------------------------------------------------------
// Factory method implementations (inline for header-only convenience)
// ---------------------------------------------------------------------------

inline Family Family::gaussian()
{
	Family f;
	f.name = "gaussian";
	f.link_name = "identity";
	f.theta = 0;
	f.linkinv = detail::gaussian_linkinv;
	f.link = detail::gaussian_link;
	f.loglik = detail::gaussian_loglik;
	f.variance = detail::gaussian_variance;
	f.mu_eta = detail::gaussian_mu_eta;
	f.deviance_residuals = detail::gaussian_deviance_residuals;
	f.loglik_d3 = d3_detail::gaussian_d3;
	return f;
}

inline Family Family::binomial()
{
	Family f;
	f.name = "binomial";
	f.link_name = "logit";
	f.theta = 0;
	f.linkinv = detail::binomial_linkinv;
	f.link = detail::binomial_link;
	f.loglik = detail::binomial_loglik;
	f.variance = detail::binomial_variance;
	f.mu_eta = detail::binomial_mu_eta;
	f.deviance_residuals = detail::binomial_deviance_residuals;
	f.loglik_d3 = d3_detail::binomial_d3;
	return f;
}

inline Family Family::poisson()
{
	Family f;
	f.name = "poisson";
	f.link_name = "log";
	f.theta = 0;
	f.linkinv = detail::poisson_linkinv;
	f.link = detail::poisson_link;
	f.loglik = detail::poisson_loglik;
	f.variance = detail::poisson_variance;
	f.mu_eta = detail::poisson_mu_eta;
	f.deviance_residuals = detail::poisson_deviance_residuals;
	f.loglik_d3 = d3_detail::poisson_d3;
	return f;
}

inline Family Family::negbin(double theta)
{
	Family f;
	f.name = "negbin";
	f.link_name = "log";
	f.theta = theta;
	f.linkinv = detail::negbin_linkinv;
	f.link = detail::negbin_link;
	f.mu_eta = detail::negbin_mu_eta;

	// Capture θ by value in lambdas.
	f.loglik = [theta](const Vector<double> &y, const Vector<double> &mu) {
		return detail::negbin_loglik(y, mu, theta);
	};
	f.variance = [theta](const Vector<double> &mu) {
		return detail::negbin_variance(mu, theta);
	};
	f.deviance_residuals = [theta](const Vector<double> &y, const Vector<double> &mu) {
		return detail::negbin_deviance_residuals(y, mu, theta);
	};
	f.loglik_d3 = [theta](const Vector<double> &y, const Vector<double> &mu,
	                       const Vector<double> &eta) {
		return d3_detail::negbin_d3(y, mu, eta, theta);
	};
	return f;
}

inline Family Family::beta(double phi)
{
	Family f;
	f.name = "beta";
	f.link_name = "logit";
	f.phi = phi;

	// Link and inverse link are the same as binomial logit.
	f.linkinv = detail::binomial_linkinv;
	f.link = detail::binomial_link;
	f.mu_eta = detail::binomial_mu_eta;

	// Capture φ by value in lambdas.
	f.loglik = [phi](const Vector<double> &y, const Vector<double> &mu) {
		return detail::beta_loglik(y, mu, phi);
	};
	f.variance = [phi](const Vector<double> &mu) {
		return detail::beta_variance(mu, phi);
	};
	f.deviance_residuals = [phi](const Vector<double> &y, const Vector<double> &mu) {
		return detail::beta_deviance_residuals(y, mu, phi);
	};
	f.loglik_d3 = [phi](const Vector<double> &y, const Vector<double> &mu,
	                     const Vector<double> &eta) {
		return d3_detail::beta_d3(y, mu, eta, phi);
	};
	return f;
}

inline Family Family::student(double sigma, double nu)
{
	Family f;
	f.name = "student";
	f.link_name = "identity";
	f.sigma = sigma;
	f.nu = nu;

	// Identity link: same as Gaussian.
	f.linkinv = detail::gaussian_linkinv;
	f.link = detail::gaussian_link;
	f.mu_eta = detail::gaussian_mu_eta;

	// V(μ) = 1 for IWLS fallback (not used when custom_weights is set).
	f.variance = detail::gaussian_variance;

	// Capture σ and ν by value in lambdas.
	f.loglik = [sigma, nu](const Vector<double> &y, const Vector<double> &mu) {
		return detail::student_loglik(y, mu, sigma, nu);
	};
	f.deviance_residuals = [sigma, nu](const Vector<double> &y, const Vector<double> &mu) {
		return detail::student_deviance_residuals(y, mu, sigma, nu);
	};
	f.custom_weights = [sigma, nu](const Vector<double> &y, const Vector<double> &mu) {
		return detail::student_weights(y, mu, sigma, nu);
	};
	f.loglik_d3 = [sigma, nu](const Vector<double> &y, const Vector<double> &mu,
	                           const Vector<double> &eta) {
		return d3_detail::student_d3(y, mu, eta, sigma, nu);
	};
	return f;
}

inline Family Family::from_name(const String &name)
{
	if (name == "gaussian") return gaussian();
	if (name == "binomial") return binomial();
	if (name == "poisson")  return poisson();
	if (name == "negbin")   return negbin(1.0);      // default θ; caller should update
	if (name == "beta")     return beta(1.0);         // default φ; caller should update
	if (name == "student")  return student(1.0, 5.0); // default σ, ν; caller should update
	throw error("Unknown family: \"%\"", name);
}

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FAMILY_HPP
