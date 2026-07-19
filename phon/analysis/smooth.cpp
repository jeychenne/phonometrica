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
 * Purpose: see header.                                                                                                *
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

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <phon/analysis/smooth.hpp>

namespace phonometrica::stats {

namespace {

// =====================================================================
// Knot placement
// =====================================================================

// Place k knots at quantiles of the unique data values.
// This follows mgcv's default strategy: if there are n_unique unique values and we need k knots,
// place them at evenly-spaced quantiles of the sorted unique values.
static std::vector<double> place_knots_at_quantiles(const std::vector<double> &x, intptr_t k)
{
	// Collect sorted unique values.
	std::set<double> unique_set(x.begin(), x.end());
	std::vector<double> unique_sorted(unique_set.begin(), unique_set.end());
	intptr_t n_unique = (intptr_t)unique_sorted.size();

	if (n_unique < k)
	{
		throw error("Smooth term requires at least % unique covariate values for k=% knots (found %)",
		            k, k, n_unique);
	}

	std::vector<double> knots(k);

	if (n_unique == k)
	{
		// Exactly enough unique values: use them directly.
		for (intptr_t i = 0; i < k; i++) {
			knots[i] = unique_sorted[i];
		}
	}
	else
	{
		// Place at quantiles: knot j is at quantile j/(k-1) of the unique values.
		for (intptr_t j = 0; j < k; j++)
		{
			double frac = (double)j / (double)(k - 1);
			double pos = frac * (double)(n_unique - 1);
			intptr_t lo = (intptr_t)std::floor(pos);
			intptr_t hi = (intptr_t)std::ceil(pos);
			double p = pos - lo;
			knots[j] = (1.0 - p) * unique_sorted[lo] + p * unique_sorted[hi];
		}
	}

	return knots;
}


// =====================================================================
// Penalty matrix construction (Green & Silverman 1994, §2.3)
// =====================================================================
//
// For k knots t[0] < t[1] < ... < t[k-1] with intervals h[i] = t[i+1] - t[i]:
//
// Q is k × (k-2): maps knot values f to the RHS of the tridiagonal system for second derivatives.
//     Q[j, j]   = 1/h[j]
//     Q[j+1, j] = -1/h[j] - 1/h[j+1]
//     Q[j+2, j] = 1/h[j+1]
//   for j = 0, ..., k-3 (column index)
//
// R is (k-2) × (k-2) symmetric tridiagonal:
//     R[j, j]   = (h[j] + h[j+1]) / 3
//     R[j, j+1] = R[j+1, j] = h[j+1] / 6
//   for j = 0, ..., k-3
//
// Penalty: S = Q * R^{-1} * Q'   (k × k, rank k-2)
//
// The second-derivative-to-knot-value mapping: F = [0; R^{-1} * Q'; 0]  (k × k)
//   F[0, :] = F[k-1, :] = 0 (natural boundary conditions)

struct CrMatrices
{
	Matrix<double> Q;       // k × (k-2)
	Matrix<double> R;       // (k-2) × (k-2) tridiagonal
	Matrix<double> S;       // k × k penalty
	Matrix<double> F;       // k × k second-derivative mapping
};


static CrMatrices build_cr_matrices(const std::vector<double> &knots)
{
	using namespace Eigen;

	intptr_t k = (intptr_t)knots.size();
	intptr_t km2 = k - 2;

	// Interval widths.
	std::vector<double> h(k - 1);
	for (intptr_t i = 0; i < k - 1; i++) {
		h[i] = knots[i + 1] - knots[i];
	}

	CrMatrices cm;

	// Build Q (k × (k-2)).
	cm.Q = Matrix<double>::Zero(k, km2);
	for (intptr_t j = 0; j < km2; j++)
	{
		cm.Q(j,     j) = 1.0 / h[j];
		cm.Q(j + 1, j) = -1.0 / h[j] - 1.0 / h[j + 1];
		cm.Q(j + 2, j) = 1.0 / h[j + 1];
	}

	// Build R ((k-2) × (k-2)) symmetric tridiagonal.
	cm.R = Matrix<double>::Zero(km2, km2);
	for (intptr_t j = 0; j < km2; j++)
	{
		cm.R(j, j) = (h[j] + h[j + 1]) / 3.0;
		if (j + 1 < km2)
		{
			cm.R(j, j + 1) = h[j + 1] / 6.0;
			cm.R(j + 1, j) = h[j + 1] / 6.0;
		}
	}

	// Solve R^{-1} * Q' via Cholesky (R is SPD).
	// Result: F_inner = R^{-1} * Q'  ((k-2) × k)
	LLT<Matrix<double>> llt(cm.R);
	if (llt.info() != Eigen::Success) {
		throw error("Penalty matrix R is not positive definite (degenerate knot spacing?)");
	}
	Matrix<double> F_inner = llt.solve(cm.Q.transpose()); // (k-2) × k

	// Penalty: S = Q * F_inner = Q * R^{-1} * Q'  (k × k)
	cm.S = cm.Q * F_inner;

	// Full second-derivative mapping: F (k × k)
	// F[0, :] = 0, F[k-1, :] = 0 (natural BC), F[1..k-2, :] = F_inner
	cm.F = Matrix<double>::Zero(k, k);
	cm.F.block(1, 0, km2, k) = F_inner;

	return cm;
}


// =====================================================================
// Basis evaluation at arbitrary points
// =====================================================================
//
// For a natural cubic spline with knot values f and second derivatives δ = F * f,
// evaluation at point x in [t[l], t[l+1]] with p = (x - t[l]) / h[l]:
//
//   s(x) = (1−p) f[l] + p f[l+1]
//          + h[l]² [ (1−p)³ − (1−p) ] δ[l] / 6
//          + h[l]² [ p³ − p ] δ[l+1] / 6
//
// The basis matrix B (n × k) has B[i, j] = value of j-th basis function at x_i.
// The j-th basis function is the natural cubic spline with f = e_j (unit vector).
//
// Efficient computation: instead of solving k tridiagonal systems, we use
// the precomputed F matrix. For each evaluation point x_i in [t[l], t[l+1]]:
//
//   B[i, :] = (1−p) e_l' + p e_{l+1}'
//             + (h² ((1−p)³ − (1−p)) / 6) · F[l, :]
//             + (h² (p³ − p) / 6) · F[l+1, :]
//
// Cost: O(n × k) after F is precomputed.

static Matrix<double> evaluate_cr_basis(const std::vector<double> &x,
                                         const std::vector<double> &knots,
                                         const Matrix<double> &F)
{
	intptr_t n = (intptr_t)x.size();
	intptr_t k = (intptr_t)knots.size();
	Matrix<double> B = Matrix<double>::Zero(n, k);

	// Precompute interval widths.
	std::vector<double> h(k - 1);
	for (intptr_t i = 0; i < k - 1; i++) {
		h[i] = knots[i + 1] - knots[i];
	}

	for (intptr_t i = 0; i < n; i++)
	{
		double xi = x[i];

		// Find the interval: t[l] <= xi <= t[l+1].
		// Clamp to [knots[0], knots[k-1]].
		intptr_t l;
		if (xi <= knots[0])
		{
			l = 0;
			xi = knots[0];
		}
		else if (xi >= knots[k - 1])
		{
			l = k - 2;
			xi = knots[k - 1];
		}
		else
		{
			// Binary search.
			auto it = std::upper_bound(knots.begin(), knots.end(), xi);
			l = (intptr_t)(it - knots.begin()) - 1;
			if (l < 0) l = 0;
			if (l > k - 2) l = k - 2;
		}

		double p = (xi - knots[l]) / h[l];  // 0 ≤ p ≤ 1
		double h2 = h[l] * h[l];
		double w1 = 1.0 - p;

		// Cubic correction weights.
		double c_l   = h2 * (w1 * w1 * w1 - w1) / 6.0;   // weight for δ[l]
		double c_l1  = h2 * (p * p * p - p) / 6.0;         // weight for δ[l+1]

		// Linear interpolation contribution.
		B(i, l)     += w1;
		B(i, l + 1) += p;

		// Cubic correction via the second-derivative mapping F.
		for (intptr_t j = 0; j < k; j++)
		{
			B(i, j) += c_l * F(l, j) + c_l1 * F(l + 1, j);
		}
	}

	return B;
}


// =====================================================================
// Identifiability constraint absorption
// =====================================================================
//
// A smooth term must be centered when used alongside an intercept.
// The constraint is: Σ_i B[i, j] = 0 for each j, i.e. each column of B sums to zero.
// Equivalently: 1'B = 0.
//
// We absorb this via QR decomposition:
//   c = B' 1     (k × 1 vector of column sums)
//   QR(c) = Q_c R_c    where Q_c is k × k orthogonal
//   Z = Q_c[:, 1:]     (k × (k-1), spans null space of c')
//   B_constrained = B Z    (n × (k-1))
//   S_constrained = Z' S Z ((k-1) × (k-1))

struct ConstraintResult
{
	Matrix<double> B;    // n × (k-1)
	Matrix<double> S;    // (k-1) × (k-1)
	Matrix<double> Z;    // k × (k-1) absorption matrix
};


static ConstraintResult absorb_constraint(const Matrix<double> &B_raw,
                                           const Matrix<double> &S_raw)
{
	using namespace Eigen;

	intptr_t n = B_raw.rows();
	intptr_t k = B_raw.cols();

	// Column sums vector.
	ColVector<double> c = B_raw.colwise().sum().transpose();  // k × 1

	// QR decomposition of c.
	HouseholderQR<ColVector<double>> qr(c);
	Matrix<double> Q_full = qr.householderQ();  // k × k orthogonal

	// Z = columns 1..k-1 of Q_full (skipping column 0 which spans c).
	Matrix<double> Z = Q_full.rightCols(k - 1);  // k × (k-1)

	ConstraintResult cr;
	cr.B = B_raw * Z;      // n × (k-1)
	cr.S = Z.transpose() * S_raw * Z;  // (k-1) × (k-1)
	cr.Z = Z;

	return cr;
}


} // anonymous namespace


// =====================================================================
// Public: build_cr_basis
// =====================================================================

SmoothBasis build_cr_basis(const std::vector<double> &x, intptr_t k)
{
	using namespace Eigen;

	if (k < 3) {
		throw error("Cubic regression spline requires k >= 3 (got %)", k);
	}
	if (x.empty()) {
		throw error("Cannot build smooth basis from empty data");
	}

	intptr_t n = (intptr_t)x.size();

	// ── Place knots at quantiles ─────────────────────────────────────

	auto knots_vec = place_knots_at_quantiles(x, k);

	// ── Build penalty matrix and second-derivative mapping ───────────

	auto cm = build_cr_matrices(knots_vec);

	// ── Evaluate raw basis at data points ────────────────────────────

	Matrix<double> B_raw = evaluate_cr_basis(x, knots_vec, cm.F);

	// ── Absorb identifiability constraint ────────────────────────────

	auto absorbed = absorb_constraint(B_raw, cm.S);

	// Note: scale.penalty (mgcv's default) is applied per-slice in
	// fitting.cpp, not here.  For by-factor smooths each slice has a
	// zero-masked B with effective row count = n_level, so the scale
	// must be computed against THAT B and not the full-data B.

	// ── Pack into SmoothBasis ────────────────────────────────────────

	SmoothBasis sb;
	sb.type = "cr";
	sb.k = k;
	sb.k_eff = k - 1;
	sb.penalty_rank = k - 2;
	sb.null_dim = 2;  // penalty null space: constant + linear

	// Copy knots to Array.
	sb.knots = Array<double>(k, 0.0);
	for (intptr_t i = 0; i < k; i++) {
		sb.knots[i] = knots_vec[i];
	}

	// Copy B (n × k_eff) into 2D Array.
	sb.B = Array<double>(n, sb.k_eff, 0.0);
	for (intptr_t j = 0; j < sb.k_eff; j++) {
		for (intptr_t i = 0; i < n; i++) {
			sb.B(i, j) = absorbed.B(i, j);
		}
	}

	// Copy S (k_eff × k_eff) into 2D Array.
	sb.S = Array<double>(sb.k_eff, sb.k_eff, 0.0);
	for (intptr_t j = 0; j < sb.k_eff; j++) {
		for (intptr_t i = 0; i < sb.k_eff; i++) {
			sb.S(i, j) = absorbed.S(i, j);
		}
	}

	// Store F_deriv2 (k × k) for prediction.
	sb.F_deriv2 = Array<double>(k, k, 0.0);
	for (intptr_t j = 0; j < k; j++) {
		for (intptr_t i = 0; i < k; i++) {
			sb.F_deriv2(i, j) = cm.F(i, j);
		}
	}

	// Store Z_absorb (k × (k-1)) for prediction.
	sb.Z_absorb = Array<double>(k, sb.k_eff, 0.0);
	for (intptr_t j = 0; j < sb.k_eff; j++) {
		for (intptr_t i = 0; i < k; i++) {
			sb.Z_absorb(i, j) = absorbed.Z(i, j);
		}
	}

	return sb;
}


// =====================================================================
// Public: SmoothBasis::predict
// =====================================================================

Array<double> SmoothBasis::predict(const std::vector<double> &x_new) const
{
	using namespace Eigen;

	// Random-effect basis: prediction at new data returns zeros
	// (new levels get the population mean, i.e. random effect = 0).
	if (type == "re")
	{
		intptr_t n_new = (intptr_t)x_new.size();
		return Array<double>(n_new, k_eff, 0.0);
	}

	intptr_t n_new = (intptr_t)x_new.size();

	// Recover knots as std::vector.
	std::vector<double> knots_vec(k);
	for (intptr_t i = 0; i < k; i++) {
		knots_vec[i] = knots[i];
	}

	// Recover F as Eigen matrix.
	Map<Matrix<double>> F_map(const_cast<double *>(F_deriv2.data()), k, k);

	// Evaluate raw basis at new points (n_new × k).
	Matrix<double> B_raw = evaluate_cr_basis(x_new, knots_vec, F_map);

	// Apply the same constraint absorption: B_new = B_raw * Z.
	Map<Matrix<double>> Z_map(const_cast<double *>(Z_absorb.data()), k, k_eff);
	Matrix<double> B_new = B_raw * Z_map;

	// Pack into 2D Array.
	Array<double> result(n_new, k_eff, 0.0);
	for (intptr_t j = 0; j < k_eff; j++) {
		for (intptr_t i = 0; i < n_new; i++) {
			result(i, j) = B_new(i, j);
		}
	}

	return result;
}


// =====================================================================
// Random-effect basis (bs="re")
// =====================================================================

SmoothBasis build_re_basis(const Array<String> &levels,
                           const std::vector<intptr_t> &indices,
                           intptr_t nobs,
                           const std::vector<double> &slope_values)
{
	using namespace Eigen;

	intptr_t J = levels.size();
	bool has_slope = !slope_values.empty();

	// Build raw B: n × J matrix.
	// For random intercepts: B[i,j] = 1 if obs i belongs to level j.
	// For random slopes:     B[i,j] = x_i if obs i belongs to level j.
	Matrix<double> B_raw = Matrix<double>::Zero(nobs, J);
	for (intptr_t i = 0; i < nobs; i++)
	{
		double val = has_slope ? slope_values[i] : 1.0;
		B_raw(i, indices[i]) = val;
	}

	// S_raw: J × J identity penalty.
	Matrix<double> S_raw = Matrix<double>::Identity(J, J);

	SmoothBasis sb;
	sb.type = "re";
	sb.variable = "";    // set by caller after construction
	sb.levels = levels;  // preserved for predict-time level lookup

	if (has_slope)
	{
		// For random slopes, apply a sum-to-zero constraint (matching mgcv):
		// the average random slope is forced to zero, absorbing one column.
		auto absorbed = absorb_constraint(B_raw, S_raw);

		sb.k = J;
		sb.k_eff = J - 1;
		sb.penalty_rank = J - 1;
		sb.null_dim = 0;

		sb.B = Array<double>(nobs, sb.k_eff, 0.0);
		for (intptr_t j = 0; j < sb.k_eff; j++) {
			for (intptr_t i = 0; i < nobs; i++) {
				sb.B(i, j) = absorbed.B(i, j);
			}
		}

		sb.S = Array<double>(sb.k_eff, sb.k_eff, 0.0);
		for (intptr_t j = 0; j < sb.k_eff; j++) {
			for (intptr_t i = 0; i < sb.k_eff; i++) {
				sb.S(i, j) = absorbed.S(i, j);
			}
		}
	}
	else
	{
		// Random intercepts: no constraint. The penalty shrinks intercepts
		// toward zero; the fixed intercept captures the population mean.
		sb.k = J;
		sb.k_eff = J;
		sb.penalty_rank = J;
		sb.null_dim = 0;

		sb.B = Array<double>(nobs, J, 0.0);
		for (intptr_t i = 0; i < nobs; i++) {
			sb.B(i, indices[i]) = 1.0;
		}

		sb.S = Array<double>(J, J, 0.0);
		for (intptr_t j = 0; j < J; j++) {
			sb.S(j, j) = 1.0;
		}
	}

	// Knots, F_deriv2, Z_absorb: not applicable for re basis (left empty).

	return sb;
}


} // namespace phonometrica::stats
