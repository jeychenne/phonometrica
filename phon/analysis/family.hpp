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
	String name;      // "gaussian", "binomial", "poisson", "negbin", "beta"
	String link_name; // "identity", "logit", "log"

	// Overdispersion parameter for negative binomial (θ > 0).
	// Unused (0) for other families.
	double theta = 0;

	// Precision parameter for beta regression (φ > 0).
	// Unused (0) for other families.
	double phi = 0;

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
	// for NB: V(μ) = μ + μ²/θ; for Beta: V(μ) = μ(1-μ)/(1+φ).
	std::function<Vector<double>(const Vector<double> &mu)> variance;

	// Derivative of inverse link: dμ/dη.
	// For identity: 1; for logit: μ(1-μ); for log: μ.
	// Used for generalized IWLS weights: w_i = (dμ/dη)² / V(μ).
	std::function<Vector<double>(const Vector<double> &mu)> mu_eta;

	// Deviance residuals: d(y, μ)
	// Returns the vector of signed deviance residuals.
	std::function<Vector<double>(const Vector<double> &y, const Vector<double> &mu)> deviance_residuals;

	// Factory methods for supported families.
	static Family gaussian();
	static Family binomial();
	static Family poisson();
	static Family negbin(double theta);
	static Family beta(double phi);

	// Look up a family by name. Throws if not found.
	// For "negbin", creates with theta=1 (caller should set theta afterwards).
	// For "beta", creates with phi=1 (caller should set phi afterwards).
	static Family from_name(const String &name);

	// Returns true if this family has an extra dispersion/precision parameter
	// that must be estimated (i.e. negbin θ or beta φ). Used by the mixed-model
	// engine to decide whether to append an extra outer parameter.
	bool has_dispersion_param() const { return name == "negbin" || name == "beta"; }
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

} // namespace detail

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
	return f;
}

inline Family Family::from_name(const String &name)
{
	if (name == "gaussian") return gaussian();
	if (name == "binomial") return binomial();
	if (name == "poisson")  return poisson();
	if (name == "negbin")   return negbin(1.0); // default θ; caller should update
	if (name == "beta")     return beta(1.0);   // default φ; caller should update
	throw error("Unknown family: \"%\"", name);
}

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FAMILY_HPP
