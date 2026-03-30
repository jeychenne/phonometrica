/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: GLM family abstraction (link function, inverse link, log-likelihood, variance function).                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FAMILY_HPP
#define PHONOMETRICA_FAMILY_HPP

#include <cmath>
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

struct Family
{
	String name;      // "gaussian", "binomial", "poisson"
	String link_name; // "identity", "logit", "log"

	// Inverse link: μ = g⁻¹(η)
	// Maps the linear predictor to the mean of the response.
	Vector<double> (*linkinv)(const Vector<double> &eta);

	// Link: η = g(μ)
	// Maps the mean of the response to the linear predictor.
	Vector<double> (*link)(const Vector<double> &mu);

	// Log-likelihood: sum over observations of log f(y | μ)
	// For Gaussian, includes the constant terms.
	double (*loglik)(const Vector<double> &y, const Vector<double> &mu);

	// Variance function: V(μ)
	// Returns a vector of per-observation variances as a function of the mean.
	// For Gaussian: V(μ) = 1; for Binomial: V(μ) = μ(1-μ); for Poisson: V(μ) = μ.
	Vector<double> (*variance)(const Vector<double> &mu);

	// Deviance residuals: d(y, μ)
	// Returns the vector of signed deviance residuals.
	Vector<double> (*deviance_residuals)(const Vector<double> &y, const Vector<double> &mu);

	// Factory methods for supported families.
	static Family gaussian();
	static Family binomial();
	static Family poisson();

	// Look up a family by name. Throws if not found.
	static Family from_name(const String &name);
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

} // namespace detail

// ---------------------------------------------------------------------------
// Factory method implementations (inline for header-only convenience)
// ---------------------------------------------------------------------------

inline Family Family::gaussian()
{
	return {
		"gaussian", "identity",
		detail::gaussian_linkinv,
		detail::gaussian_link,
		detail::gaussian_loglik,
		detail::gaussian_variance,
		detail::gaussian_deviance_residuals
	};
}

inline Family Family::binomial()
{
	return {
		"binomial", "logit",
		detail::binomial_linkinv,
		detail::binomial_link,
		detail::binomial_loglik,
		detail::binomial_variance,
		detail::binomial_deviance_residuals
	};
}

inline Family Family::poisson()
{
	return {
		"poisson", "log",
		detail::poisson_linkinv,
		detail::poisson_link,
		detail::poisson_loglik,
		detail::poisson_variance,
		detail::poisson_deviance_residuals
	};
}

inline Family Family::from_name(const String &name)
{
	if (name == "gaussian") return gaussian();
	if (name == "binomial") return binomial();
	if (name == "poisson")  return poisson();
	throw error("Unknown family: \"%\"", name);
}

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FAMILY_HPP
