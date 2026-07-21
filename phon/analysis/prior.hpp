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
 * Purpose: Prior specification for INLA-style approximate Bayesian inference.                                         *
 *                                                                                                                     *
 * The individual prior types provide log_density() as a template so that the same code works for both double          *
 * (general use) and CppAD::AD<double> (automatic differentiation in the Laplace engine). The template bodies use      *
 * only standard arithmetic operators and, where needed, unqualified calls to log() — ADL finds CppAD::log for         *
 * AD<double>, and the using-declaration provides std::log for plain double.                                           *
 *                                                                                                                     *
 * References:                                                                                                         *
 *   Simpson, Rue, Riebler, Martino & Sørbye (2017). Penalising model component complexity: a principled,              *
 *     practical approach to constructing priors. Statistical Science 32(1).                                           *
 *   Rue, Martino & Chopin (2009). Approximate Bayesian inference for latent Gaussian models by using integrated       *
 *     nested Laplace approximations. JRSS-B 71(2).                                                                    *
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

#ifndef PHONOMETRICA_PRIOR_HPP
#define PHONOMETRICA_PRIOR_HPP

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include <phon/string.hpp>
#include <phon/analysis/student_bounds.hpp>

namespace phonometrica::stats {

// =====================================================================
// Estimation method
// =====================================================================

enum class Estimation { Frequentist, Bayesian };


// =====================================================================
// Frequentist estimation method (Gaussian LMM only)
// =====================================================================
//
// ML is the default and applies to all model types. REML is an opt-in
// alternative for Gaussian linear mixed models only — it modifies the
// profiled Laplace objective by an additional + ½ log|XᵀV⁻¹X| term,
// giving unbiased variance-component estimates and matching lme4's
// default. For all non-Gaussian families and for fixed-effects-only
// models, REML is silently coerced to ML (with a fit_warning).
//
// REML log-likelihoods are NOT comparable across models with different
// fixed-effects designs — the comparison machinery enforces this with
// a hard error rather than a warning.

enum class Method { ML, REML };


// =====================================================================
// Individual prior distributions
// =====================================================================

// Normal prior for fixed-effect coefficients.
// Default: N(0, 10) — weakly informative on the linear predictor scale.
struct NormalPrior
{
	double mean = 0.0;
	double sd = 10.0;

	// log p(x) = -0.5 log(2π) - log(σ) - (x - μ)² / (2σ²)
	template <typename T>
	T log_density(T x) const
	{
		static const double log_2pi = std::log(2.0 * M_PI);
		double log_sd = std::log(sd);
		double inv_sd = 1.0 / sd;
		T z = (x - T(mean)) * T(inv_sd);
		return T(-0.5 * log_2pi - log_sd) - T(0.5) * z * z;
	}
};


// Penalised complexity (PC) prior for a standard deviation σ > 0.
// Parameterised by a tail probability statement: P(σ > u) = alpha.
// This induces an Exponential(λ) prior on σ, with λ = -log(α) / u.
// Default: P(σ > 1) = 0.05, i.e. 95% of prior mass below σ = 1.
//
// Reference: Simpson et al. (2017).
struct PCPrior
{
	double u = 1.0;
	double alpha = 0.05;

	double rate() const { return -std::log(alpha) / u; }

	// log p(σ) = log(λ) - λσ     (σ > 0)
	template <typename T>
	T log_density(T sigma) const
	{
		double lambda = rate();
		return T(std::log(lambda)) - T(lambda) * sigma;
	}
};


// Half-Cauchy prior for a standard deviation σ > 0.
// p(σ) = 2 / (π s (1 + (σ/s)²))
// A popular weakly informative choice in the Stan community (Gelman 2006).
struct HalfCauchyPrior
{
	double scale = 2.5;

	// log p(σ) = log(2/π) - log(s) - log(1 + (σ/s)²)
	template <typename T>
	T log_density(T sigma) const
	{
		using std::log; // ADL finds CppAD::log for AD<double>
		static const double log_2_over_pi = std::log(2.0 / M_PI);
		double log_s = std::log(scale);
		double inv_s = 1.0 / scale;
		T z = sigma * T(inv_s);
		return T(log_2_over_pi - log_s) - log(T(1.0) + z * z);
	}
};


// Half-Normal prior for a standard deviation σ > 0.
// p(σ) = (2 / (s √(2π))) exp(-σ² / (2s²))
struct HalfNormalPrior
{
	double scale = 2.5;

	// log p(σ) = 0.5 log(2/π) - log(s) - σ² / (2s²)
	template <typename T>
	T log_density(T sigma) const
	{
		static const double log_2_over_pi = std::log(2.0 / M_PI);
		double log_s = std::log(scale);
		double inv_2s2 = 0.5 / (scale * scale);
		return T(0.5 * log_2_over_pi - log_s) - T(inv_2s2) * sigma * sigma;
	}
};


// Gamma prior for a positive parameter (e.g. NB θ, Gaussian precision, Beta φ).
// Parameterised by shape a and rate b: p(x) ∝ x^(a-1) exp(-bx).
// Default: Gamma(1, 0.01) — weakly informative for overdispersion.
struct GammaPrior
{
	double shape = 1.0;
	double rate = 0.01;

	// log p(x) = a log(b) - lgamma(a) + (a-1) log(x) - b x
	template <typename T>
	T log_density(T x) const
	{
		using std::log; // ADL finds CppAD::log for AD<double>
		double const_part = shape * std::log(rate) - std::lgamma(shape);
		return T(const_part) + T(shape - 1.0) * log(x) - T(rate) * x;
	}
};


// Uniform prior for a bounded parameter (e.g. Student-t ν ∈ [NU_MIN, NU_MAX]).
// Density is constant inside [lower, upper], zero outside.
//
// One of two alternatives for PriorSpec::student_nu (the other is
// GammaPrior). Consumed by the Bayesian Student-t optimization path
// (PirlsObjective::eval and the two coordinated sites — see the
// coordination invariant in mixed_model.cpp). That path operates on
// plain double, so the branch on x is safe. If used in an AD-traced
// path later, replace the if with CppAD::CondExp.
struct UniformPrior
{
	double lower = 0.0;
	double upper = 1.0;

	// log p(x) = -log(upper - lower) inside [lower, upper], else hard barrier.
	// Returns a large finite negative outside support rather than -∞ so the
	// L-BFGS gradient finite-differences stay well-defined (a true -∞ would
	// give NaN-via-cancellation in the central-difference stencil).
	template <typename T>
	T log_density(T x) const
	{
		double log_width = std::log(upper - lower);
		if (x < T(lower) || x > T(upper))
			return T(-1e30);
		return T(-log_width);
	}
};


// =====================================================================
// Student-t ν prior: choice between Uniform and Gamma
// =====================================================================
//
// Two reasonable shapes exist in the literature for the degrees-of-freedom
// parameter of a Student-t likelihood:
//
//   UniformPrior{NU_MIN, NU_MAX}
//     Weakly-informative: any ν in support is equally plausible.  Useful
//     when prior beliefs are genuinely flat.  Drawback on log-scale
//     optimization: combined with the dν/dlog ν = ν Jacobian, the
//     log-space prior gradient is identically +1 throughout support, i.e.
//     monotonically increases in log ν.  On ν-insensitive (near-Gaussian)
//     data, the posterior pegs at NU_MAX with no resistance — there is no
//     asymptotic shape from the prior to pull it back.
//
//   GammaPrior{2.0, 0.1}
//     Mode at (a-1)/b = 10, mean at a/b = 20, with a finite tail that
//     decays at large ν.  Pulls the log-space posterior back from large
//     ν, producing finite posterior moments even on ν-insensitive data.
//     This is brms's default, and the reference values in Phon's
//     Bayesian Student-t test suite are generated against this prior.
//
// Phon defaults to GammaPrior{2.0, 0.1} for parity with brms.  Users who
// genuinely want flat-on-ν can assign a UniformPrior to the same field.
//
// The variant dispatch is via std::visit; both members provide a
// template log_density that works for plain double and CppAD::AD<double>.
using StudentNuPrior = std::variant<UniformPrior, GammaPrior>;

// Visitor-dispatched log-density for StudentNuPrior.  Provides a uniform
// interface so call sites do not need to switch on the variant alternative.
template <typename T>
T log_density(const StudentNuPrior &prior, T x)
{
	return std::visit([&](const auto &p) -> T { return p.log_density(x); }, prior);
}

// True when the variant holds the default Gamma(2.0, 0.1) — used by
// PriorSpec::is_default() and any diagnostic that needs to suppress
// summary lines for an unchanged default.  Returns false for any
// UniformPrior or any Gamma with non-default parameters.
inline bool is_default_student_nu_prior(const StudentNuPrior &prior)
{
	if (!std::holds_alternative<GammaPrior>(prior)) return false;
	const auto &g = std::get<GammaPrior>(prior);
	return g.shape == 2.0 && g.rate == 0.1;
}


// =====================================================================
// Variance prior: a tagged choice among PC / HalfCauchy / HalfNormal
// =====================================================================

enum class VariancePriorType { PC, HalfCauchy, HalfNormal };

struct VariancePrior
{
	VariancePriorType type = VariancePriorType::PC;

	// Parameters are stored uniformly; interpretation depends on type.
	// PC:         param1 = u,     param2 = alpha
	// HalfCauchy: param1 = scale, param2 unused
	// HalfNormal: param1 = scale, param2 unused
	double param1 = 1.0;
	double param2 = 0.05;

	template <typename T>
	T log_density(T sigma) const
	{
		switch (type)
		{
		case VariancePriorType::PC:
		{
			PCPrior p;
			p.u = param1;
			p.alpha = param2;
			return p.log_density(sigma);
		}
		case VariancePriorType::HalfCauchy:
		{
			HalfCauchyPrior p;
			p.scale = param1;
			return p.log_density(sigma);
		}
		case VariancePriorType::HalfNormal:
		{
			HalfNormalPrior p;
			p.scale = param1;
			return p.log_density(sigma);
		}
		}
		return T(0); // unreachable
	}
};


// =====================================================================
// PriorSpec: complete prior specification for a model
// =====================================================================

struct PriorSpec
{
	// ---- Fixed effects ----
	// Default prior applied to all fixed-effect coefficients.
	NormalPrior fixed_effects;

	// Per-coefficient overrides, keyed by coefficient name (e.g. "age").
	// If a coefficient name is found here, its prior is used instead of
	// the default fixed_effects prior.
	std::map<String, NormalPrior> coefficient_priors;

	// ---- Variance components (random-effect SDs) ----
	// Applied to each random-effect group's standard deviation(s).
	VariancePrior variance_components;

	// LKJ-style prior on the correlation structure of the random-effect
	// covariance, applied when a group has q_g ≥ 2 random terms
	// (intercept + slope, or multiple slopes).  The density is
	//
	//     p(R | η) ∝ |R|^(η − 1)
	//
	// where R is the correlation matrix derived from D = σ R σ via
	// R_ij = D_ij / (σ_i σ_j).  η = 1 is uniform over correlation matrices
	// (the default — equivalent to no correlation prior); η > 1 concentrates
	// mass toward the identity (independent random terms); η < 1 pushes
	// toward highly correlated random terms.  In typical mixed-model
	// applications, η = 2 gives a mildly regularising prior.
	// Reference: Lewandowski, Kurowicka & Joe (2009), J. Multivariate Anal.
	double lkj_eta = 1.0;

	// ---- Residual SD (Gaussian family only) ----
	VariancePrior residual;

	// ---- Family-specific dispersion priors ----
	// Negative binomial θ (overdispersion): Gamma prior.
	GammaPrior negbin_theta;

	// Beta φ (precision): Gamma prior.
	GammaPrior beta_phi;

	// Student-t ν (degrees of freedom): variant prior over Uniform | Gamma.
	// Default: Gamma(2.0, 0.1) — matches brms's default and the reference
	// values in the Bayesian Student-t validation suite.  Mode at 10, mean
	// at 20, with a finite tail; on ν-insensitive (near-Gaussian) data this
	// produces a finite posterior rather than pegging at the NU_MAX clamp
	// boundary the way the older UniformPrior default did.
	//
	// To use a flat prior instead, assign `UniformPrior{NU_MIN, NU_MAX}`
	// (or any other bounded window) to this field after constructing the
	// PriorSpec.
	//
	// Student-t σ uses the existing `residual` field — σ plays the residual-
	// scale role for Student-t the same way σ_residual does for Gaussian,
	// and the auto-scaling at fitting.cpp:1549 already produces the right
	// data-scaled PC prior. No separate field is needed.
	StudentNuPrior student_nu = GammaPrior{2.0, 0.1};

	// ---- Auto-scaling flags ----
	// When true, the corresponding prior is replaced at fit time by a
	// data-scaled weakly informative default (à la brms). User calls to
	// set_fixed() / set_variance() / set_residual() clear the flag.
	bool fixed_auto = true;
	bool variance_auto = true;
	bool residual_auto = true;

	// ---- Convenience ----

	// Return the prior for a given fixed-effect coefficient.
	// Uses the per-coefficient override if one exists, otherwise the default.
	const NormalPrior &prior_for(const String &coef_name) const
	{
		auto it = coefficient_priors.find(coef_name);
		if (it != coefficient_priors.end())
			return it->second;
		return fixed_effects;
	}

	// True if every field is at its default value (no customisation).
	bool is_default() const
	{
		return coefficient_priors.empty()
		    && fixed_effects.mean == 0.0 && fixed_effects.sd == 10.0
		    && variance_components.type == VariancePriorType::PC
		    && variance_components.param1 == 1.0 && variance_components.param2 == 0.05
		    && residual.type == VariancePriorType::PC
		    && residual.param1 == 1.0 && residual.param2 == 0.05
		    && negbin_theta.shape == 1.0 && negbin_theta.rate == 0.01
		    && beta_phi.shape == 1.0 && beta_phi.rate == 0.01
		    && is_default_student_nu_prior(student_nu)
		    && lkj_eta == 1.0;
	}

	// Compute the total log-prior for fixed-effect coefficients.
	// coef_names and beta must have the same size.
	template <typename T>
	T log_prior_fixed(const Array<String> &coef_names, const T *beta, intptr_t nfixed) const
	{
		T lp = T(0);
		for (intptr_t j = 0; j < nfixed; j++)
		{
			const auto &pr = prior_for(coef_names[j]);
			lp += pr.log_density(beta[j]);
		}
		return lp;
	}

	// Compute the total log-prior for variance components.
	// sigma is a pointer to ngroups standard deviations (0-indexed).
	template <typename T>
	T log_prior_variance(const T *sigma, intptr_t ngroups) const
	{
		T lp = T(0);
		for (intptr_t g = 0; g < ngroups; g++)
			lp += variance_components.log_density(sigma[g]);
		return lp;
	}

	// Construct a default PriorSpec (weakly informative).
	static PriorSpec default_spec()
	{
		return PriorSpec();
	}
};


// =====================================================================
// String conversion helpers (for display and diagnostics)
// =====================================================================

inline const char *variance_prior_type_name(VariancePriorType t)
{
	switch (t)
	{
	case VariancePriorType::PC:         return "PC";
	case VariancePriorType::HalfCauchy: return "Half-Cauchy";
	case VariancePriorType::HalfNormal: return "Half-Normal";
	}
	return "Unknown";
}

inline const char *estimation_name(Estimation e)
{
	switch (e)
	{
	case Estimation::Frequentist: return "Frequentist";
	case Estimation::Bayesian:    return "Bayesian";
	}
	return "Unknown";
}


// Format a human-readable summary of the prior specification.
// family is needed to decide which dispersion prior to show.
inline std::string format_prior_summary(const PriorSpec &p, const String &family)
{
	std::string s;

	// Auto-scaling note.
	bool any_auto = p.fixed_auto || p.variance_auto || p.residual_auto;
	if (any_auto)
		s += "Priors (data-scaled defaults marked with *):\n";
	else
		s += "Priors:\n";

	// Fixed effects.
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "  Fixed effects:  N(%.4g, %.4g)%s\n",
		         p.fixed_effects.mean, p.fixed_effects.sd,
		         p.fixed_auto ? " *" : "");
		s += buf;

		// Per-coefficient overrides.  Width fits the longest "name:" label so
		// long coefficient names like "subsystem[vowels]:man.dist" don't push
		// the prior expression out of alignment.
		int label_w = 14; // minimum
		for (auto &[name, prior] : p.coefficient_priors)
		{
			int len = (int)name.size() + 1; // +1 for trailing colon
			if (len > label_w) label_w = len;
		}

		for (auto &[name, prior] : p.coefficient_priors)
		{
			std::string label(name.data(), name.size());
			label += ":";
			snprintf(buf, sizeof(buf), "    %-*s N(%.4g, %.4g)\n",
			         label_w, label.c_str(), prior.mean, prior.sd);
			s += buf;
		}
	}

	// Variance components.
	{
		char buf[80];
		auto &v = p.variance_components;
		if (v.type == VariancePriorType::PC)
			snprintf(buf, sizeof(buf), "  Variance (SD):  PC(%.4g, %.4g)%s\n",
			         v.param1, v.param2, p.variance_auto ? " *" : "");
		else
			snprintf(buf, sizeof(buf), "  Variance (SD):  %s(%.4g)%s\n",
			         variance_prior_type_name(v.type), v.param1,
			         p.variance_auto ? " *" : "");
		s += buf;
	}

	// Residual (Gaussian/Student only).
	if (family == "gaussian" || family == "student")
	{
		char buf[80];
		auto &r = p.residual;
		if (r.type == VariancePriorType::PC)
			snprintf(buf, sizeof(buf), "  Residual (SD):  PC(%.4g, %.4g)%s\n",
			         r.param1, r.param2, p.residual_auto ? " *" : "");
		else
			snprintf(buf, sizeof(buf), "  Residual (SD):  %s(%.4g)%s\n",
			         variance_prior_type_name(r.type), r.param1,
			         p.residual_auto ? " *" : "");
		s += buf;
	}

	// Dispersion priors.
	if (family == "negbin")
	{
		char buf[80];
		snprintf(buf, sizeof(buf), "  NB theta:       Gamma(%.4g, %.4g)\n",
		         p.negbin_theta.shape, p.negbin_theta.rate);
		s += buf;
	}
	if (family == "beta")
	{
		char buf[80];
		snprintf(buf, sizeof(buf), "  Beta phi:       Gamma(%.4g, %.4g)\n",
		         p.beta_phi.shape, p.beta_phi.rate);
		s += buf;
	}
	if (family == "student")
	{
		char buf[80];
		if (std::holds_alternative<GammaPrior>(p.student_nu))
		{
			const auto &g = std::get<GammaPrior>(p.student_nu);
			snprintf(buf, sizeof(buf), "  Student nu:     Gamma(%.4g, %.4g)\n",
			         g.shape, g.rate);
		}
		else
		{
			const auto &u = std::get<UniformPrior>(p.student_nu);
			snprintf(buf, sizeof(buf), "  Student nu:     U(%.4g, %.4g)\n",
			         u.lower, u.upper);
		}
		s += buf;
	}

	return s;
}

} // namespace phonometrica::stats

#endif // PHONOMETRICA_PRIOR_HPP
