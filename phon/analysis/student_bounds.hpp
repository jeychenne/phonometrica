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
 * Created: 12/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Numerical clamp bounds for Student-t degrees of freedom ν.                                                 *
 *                                                                                                                     *
 * Owned here rather than in family.hpp to keep prior.hpp (which consumes these bounds for the UniformPrior            *
 * student_nu default) free of the boost::math::special_functions and Eigen transitive includes that family.hpp        *
 * brings in.  All ν-clamping sites in the engine must reference these constants — refactoring NU_MAX (e.g., to        *
 * widen for very near-Gaussian datasets) is therefore a single-line change.                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_STUDENT_BOUNDS_HPP
#define PHONOMETRICA_STUDENT_BOUNDS_HPP

#include <cmath>

namespace phonometrica::stats {

// =====================================================================
// Numerical clamp on Student-t degrees of freedom ν
// =====================================================================
//
// Lower bound (NU_MIN = 2). Hard mathematical constraint: the Student-t
// variance is σ² · ν / (ν − 2), undefined for ν ≤ 2.  Values below 2 also
// break the IRLS working-weight derivation used by the PIRLS solver and
// the Fisher-information-based Laplace approximation.  Cannot be relaxed
// without redesigning the engine's Student-t treatment.
//
// Upper bound (NU_MAX = 1000). Numerical convenience: as ν → ∞ the
// Student-t converges to Gaussian, so any sufficiently large value is
// "effectively Gaussian" and the distinction stops mattering.  Choice
// rationale:
//   - log(1000) ≈ 6.91 is well within double precision; CCD grid points
//     in log(ν) and the U(NU_MIN, NU_MAX) prior integrate cleanly.
//   - Amply wide for real datasets.  Datasets calling for ν > 1000 are
//     numerically indistinguishable from Gaussian, where switching to a
//     Gaussian fit is more honest than chasing larger ν.
//   - 5× wider than the historical bound of 200, which proved restrictive
//     on near-Gaussian datasets — e.g., the M5 schwa Bayesian test pinned
//     the posterior on ν at exactly 200 (sd ≈ 0) despite the data wanting
//     ν > 400.  The 200 cap was conservative; 1000 closes that mode of
//     failure without numerical cost.
//
// LOG_NU_{MIN,MAX} are runtime-initialised once via std::log (which is not
// yet constexpr in C++20).  Used by CCD grid construction and log-space
// prior integration.
//
// ALL ν-clamping sites in the engine MUST use these bounds:
//
//   - mixed_model.cpp: ~17 std::clamp(std::exp(theta[...]), NU_MIN, NU_MAX)
//     sites across PirlsObjective::eval, LaplaceJointObjective::eval,
//     eval_pirls_grid_point, the Bayesian post-process, and posterior
//     predictive helpers.  The three Student-t-prior sites (see invariant
//     block at student_nu_prior_log_density) are part of this set.
//
//   - posterior_predictive.cpp: 1 clamp at draw time.
//
//   - prior.hpp: PriorSpec::student_nu — when a UniformPrior alternative
//     is selected, its bounds default to {NU_MIN, NU_MAX}. The default
//     GammaPrior alternative does not consult these bounds directly.
//
// Refactoring NU_MAX requires updating ONLY this file; all clamp sites
// dereference the constants directly.
inline constexpr double NU_MIN = 2.0;
inline constexpr double NU_MAX = 1000.0;

inline const double LOG_NU_MIN = std::log(NU_MIN);
inline const double LOG_NU_MAX = std::log(NU_MAX);

} // namespace phonometrica::stats

#endif // PHONOMETRICA_STUDENT_BOUNDS_HPP
