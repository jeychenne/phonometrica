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
 * Created: 01/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Penalized regression spline basis construction for generalized additive models (GAMs).                     *
 *                                                                                                                     *
 * Currently implements the cubic regression spline ("cr") basis following Wood (2017) §5.3.1–5.3.2.                   *
 * The "cr" basis parameterizes a natural cubic spline in terms of its values at k knots.                              *
 * The penalty is the integrated squared second derivative (rank k−2, null space = linear functions).                  *
 *                                                                                                                     *
 * An identifiability constraint (sum-to-zero across the data) is absorbed by default, reducing the                    *
 * effective basis dimension from k to k−1. This ensures smooth terms are identifiable in the                          *
 * presence of an intercept.                                                                                           *
 *                                                                                                                     *
 * Mathematical references:                                                                                            *
 *   Green, P.J. & Silverman, B.W. (1994). Nonparametric Regression and Generalized Linear Models.                    *
 *   Wood, S.N. (2017). Generalized Additive Models: An Introduction with R (2nd ed.). CRC Press.                     *
 *   Eilers, P.H.C. & Marx, B.D. (1996). Flexible Smoothing with B-splines and Penalties.                            *
 *       Statistical Science, 11(2), 89–121.                                                                          *
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

#ifndef PHONOMETRICA_SMOOTH_HPP
#define PHONOMETRICA_SMOOTH_HPP

#include <vector>
#include <phon/string.hpp>
#include <phon/array.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica::stats {

// A constructed spline basis with its penalty matrix.
//
// After construction, the basis matrix B (n × k_eff) is ready to be appended to a fixed-effects
// design matrix X. The penalty matrix S (k_eff × k_eff) is the wiggliness penalty to be used
// in the penalized objective: -ℓ(β) + λ · β_s' S β_s.
//
// The identifiability constraint (sum-to-zero over the fitting data) has already been absorbed,
// so k_eff = k − 1 for "cr" (one degree of freedom lost to the constraint).
//
// For prediction at new x-values, call predict(). This evaluates the unconstrained basis at the
// new points and applies the same constraint transformation, yielding an n_new × k_eff matrix.
struct SmoothBasis
{
	String type;           // basis type: "cr" (cubic regression spline)
	String variable;       // covariate name, e.g. "duration"
	intptr_t k;            // original basis dimension (number of knots)
	intptr_t k_eff;        // effective dimension after identifiability constraint (k − 1 for "cr")
	intptr_t penalty_rank; // rank of the penalty matrix (k − 2 for "cr")
	intptr_t null_dim;     // dimension of penalty null space (2 for "cr": intercept + slope)

	Array<double> B;       // n × k_eff basis matrix (identifiability constraint absorbed)
	Array<double> S;       // k_eff × k_eff penalty matrix (absorbed)
	Array<double> knots;   // k knot positions (1-based Array)

	// Internal: k × k matrix F mapping knot values to second derivatives at knots.
	// F[0,:] = F[k-1,:] = 0 (natural boundary conditions).
	// Needed for evaluation at new points.
	Array<double> F_deriv2;

	// Internal: k × (k−1) constraint absorption matrix Z.
	// B_absorbed = B_raw * Z, S_absorbed = Z' * S_raw * Z.
	// Needed for prediction at new points.
	Array<double> Z_absorb;

	// Evaluate the constrained basis at new x-values (for prediction and plotting).
	// Returns an n_new × k_eff matrix.
	Array<double> predict(const std::vector<double> &x_new) const;
};


// Construct a cubic regression spline ("cr") basis.
//
// Given n data values in x, places k knots at quantiles of the unique values and constructs:
//   - The basis matrix B (n × k_eff)
//   - The penalty matrix S (k_eff × k_eff)
//   - All internal data needed for predict() at new x-values.
//
// Throws if k < 3, or if there are fewer unique values than k.
//
// \param x       covariate values (n observations, 0-indexed std::vector)
// \param k       basis dimension = number of knots (default 10, minimum 3)
// \return a fully constructed SmoothBasis
SmoothBasis build_cr_basis(const std::vector<double> &x, intptr_t k = 10);


// Construct a P-spline basis (Eilers & Marx 1996).
//
// Uses a B-spline basis of order m (default 3 = cubic) with a d-th order difference penalty
// (default d=2) on the coefficients. k equally-spaced knots span the range of x.
//
// \param x       covariate values
// \param k       number of basis functions (default 10, minimum m+1)
// \param m       spline order: 1=linear, 2=quadratic, 3=cubic (default 3)
// \param d       difference penalty order (default 2)
// \return a fully constructed SmoothBasis
// SmoothBasis build_ps_basis(const std::vector<double> &x, intptr_t k = 10, int m = 3, int d = 2);
// TODO: implement after cr is validated.


// Construct a random-effect ("re") basis for a categorical grouping factor.
//
// This implements the equivalent of mgcv's bs="re": a penalized indicator matrix
// with identity penalty, suitable for modelling random intercepts (and random slopes)
// within the penalized regression (GAM) framework.
//
// Random intercepts (slope_values empty):
//   B = n × J indicator matrix (B[i,j] = 1 if observation i belongs to level j)
//
// Random slopes (slope_values provided):
//   B[i,j] = slope_values[i] if observation i belongs to level j, else 0.
//   This is equivalent to mgcv's s(group, by=x, bs="re").
//
// In both cases:  S = I  (J × J identity penalty)
//
// The smoothing parameter λ selected by GCV maps to a variance component:
//   σ²_u = σ²_ε / λ
//
// No identifiability constraint is applied: the penalty shrinks random effects
// toward zero, and the fixed effects capture the population-level structure.
//
// \param levels        sorted unique levels (1-based Array of Strings)
// \param indices       per-observation level index (0-based, length n)
// \param nobs          number of observations
// \param slope_values  numeric covariate values for random slopes (empty for intercepts)
// \return a SmoothBasis with type="re", k_eff=J, penalty_rank=J, null_dim=0.
SmoothBasis build_re_basis(const Array<String> &levels,
                           const std::vector<intptr_t> &indices,
                           intptr_t nobs,
                           const std::vector<double> &slope_values = {});

} // namespace phonometrica::stats

#endif // PHONOMETRICA_SMOOTH_HPP
