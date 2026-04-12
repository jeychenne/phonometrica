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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 * The outer gradient of the Laplace-approximated marginal log-likelihood is computed exactly                          *
 * by algorithmic differentiation (CppAD). The inner Newton solve (which finds the random-effects                      *
 * mode u_hat for given outer parameters) runs with plain doubles. At the converged u_hat, the                         *
 * stationarity condition df/du = 0 means that du_hat/dphi does not contribute to the gradient                         *
 * (implicit function theorem), so we can treat u_hat as a constant when taping the nll w.r.t.                         *
 * the outer parameters phi = (beta, log_sigma_u_1, ..., log_sigma_u_G, [log_sigma]).                                *
 *                                                                                                                     *
 * Mathematical references:                                                                                            *
 *   Breslow & Clayton (1993). JASA 88(421), 9–25.                                                                    *
 *   Kristensen et al. (2016). JSS 70(5), 1–21.                                                                       *
 *   Skaug & Fournier (2006). Automatic approximation of the marginal likelihood in non-Gaussian                      *
 *       hierarchical models. Computational Statistics & Data Analysis, 51, 699–709.                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <cppad/cppad.hpp>
#include <boost/math/distributions/normal.hpp>
#include <phon/analysis/mixed_model.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica::stats {

namespace {

using ADdouble = CppAD::AD<double>;

// =====================================================================
// Multi-group random effects layout (unchanged)
// =====================================================================

struct GroupLayout
{
	intptr_t G;
	intptr_t J_total;       // total random-effect coefficients = Σ J[g] × q[g]
	std::vector<intptr_t> J;       // nlevels per group
	std::vector<intptr_t> q;       // nterms per group (1 = intercept only)
	std::vector<intptr_t> offset;  // u-vector offset: offset[g] = Σ_{g'<g} J[g'] × q[g']
	std::vector<const std::vector<intptr_t> *> group_indices;
	std::vector<const double *> Z_data;  // pointer to each group's Z_design data
	intptr_t n_obs = 0;

	static GroupLayout build(const std::vector<GroupingInfo> &groups)
	{
		GroupLayout lay;
		lay.G = (intptr_t)groups.size();
		lay.J_total = 0;
		lay.J.resize(lay.G);
		lay.q.resize(lay.G);
		lay.offset.resize(lay.G);
		lay.group_indices.resize(lay.G);
		lay.Z_data.resize(lay.G);
		lay.n_obs = groups.empty() ? 0 : (intptr_t)groups[0].indices.size();

		for (intptr_t g = 0; g < lay.G; g++)
		{
			lay.offset[g] = lay.J_total;
			lay.J[g] = groups[g].nlevels;
			lay.q[g] = groups[g].nterms;
			lay.J_total += lay.J[g] * lay.q[g];
			lay.group_indices[g] = &groups[g].indices;
			lay.Z_data[g] = groups[g].Z_design.data();
		}
		return lay;
	}

	// Z design value for observation i, group g, term t.
	double Z(intptr_t g, intptr_t i, intptr_t t) const
	{
		return Z_data[g][i * q[g] + t];
	}

	// u-vector index for group g, level j, term t.
	intptr_t u_idx(intptr_t g, intptr_t j, intptr_t t) const
	{
		return offset[g] + j * q[g] + t;
	}
};


// =====================================================================
// Cholesky parameterization helpers
// =====================================================================
//
// The prior covariance D_g for group g (q × q SPD) is parameterized via
// its lower Cholesky factor: D_g = L_g L_g'.
//
// Packed parameter vector θ_g of length q(q+1)/2:
//   θ[r(r+1)/2 + c] = L_{r,c}  for r > c  (off-diagonal: unconstrained)
//   θ[r(r+1)/2 + r] = log L_{r,r}         (diagonal: log-scale for positivity)
//
// For q=1 this is θ = (log σ_u), consistent with the scalar parameterization.

// Number of Cholesky parameters for a q × q covariance.
static intptr_t n_chol_params(intptr_t q)
{
	return q * (q + 1) / 2;
}

// Total Cholesky parameters across all groups.
static intptr_t total_chol_params(const GroupLayout &lay)
{
	intptr_t total = 0;
	for (intptr_t g = 0; g < lay.G; g++) {
		total += n_chol_params(lay.q[g]);
	}
	return total;
}

// Offset into the packed outer θ vector for group g's Cholesky parameters.
static intptr_t chol_offset(const GroupLayout &lay, intptr_t g)
{
	intptr_t off = 0;
	for (intptr_t g2 = 0; g2 < g; g2++) {
		off += n_chol_params(lay.q[g2]);
	}
	return off;
}

// Unpack a Cholesky parameter slice into a lower-triangular Eigen matrix.
// theta_g: packed vector of length q(q+1)/2.
// Returns the q × q lower Cholesky factor L (with exp applied to diagonal).
static Eigen::MatrixXd unpack_cholesky(const double *theta_g, intptr_t q)
{
	Eigen::MatrixXd L = Eigen::MatrixXd::Zero(q, q);
	for (intptr_t r = 0; r < q; r++)
	{
		for (intptr_t c = 0; c <= r; c++)
		{
			intptr_t idx = r * (r + 1) / 2 + c;
			if (r == c) {
				L(r, c) = std::exp(theta_g[idx]); // diagonal: exponentiate
			} else {
				L(r, c) = theta_g[idx];            // off-diagonal: as-is
			}
		}
	}
	return L;
}

// Compute D = L L' from the Cholesky factor.
static Eigen::MatrixXd cholesky_to_cov(const Eigen::MatrixXd &L)
{
	return L * L.transpose();
}

// Compute D⁻¹ from the Cholesky factor: D⁻¹ = L⁻ᵀ L⁻¹.
static Eigen::MatrixXd cholesky_to_precision(const Eigen::MatrixXd &L)
{
	Eigen::MatrixXd Linv = L.triangularView<Eigen::Lower>().solve(
		Eigen::MatrixXd::Identity(L.rows(), L.cols()));
	return Linv.transpose() * Linv;
}

// log|D| = 2 Σ log L_kk = 2 Σ θ_diag (since diag elements are stored as log).
static double log_det_D(const double *theta_g, intptr_t q)
{
	double ld = 0;
	for (intptr_t r = 0; r < q; r++) {
		ld += theta_g[r * (r + 1) / 2 + r]; // the log L_rr value
	}
	return 2.0 * ld;
}


// =====================================================================
// Method-of-moments variance component estimation (ANOVA decomposition)
// =====================================================================
//
// Given a vector of residuals (or working residuals on the link scale) and
// a grouping factor, estimates the between-group variance σ²_u via the
// one-way ANOVA identity:
//
//     E[MSB] = σ²_within + n₀ σ²_between
//     E[MSW] = σ²_within
//
// where n₀ is the "effective group size" for unbalanced designs:
//     n₀ = (n − Σ n_j² / n) / (J − 1)
//
// Returns max((MSB − MSW) / n₀, 0).  The caller applies a floor.

static double anova_variance_component(const Eigen::VectorXd &resid,
                                        const std::vector<intptr_t> &indices,
                                        intptr_t nlevels, intptr_t n)
{
	std::vector<double> gsum(nlevels, 0.0);
	std::vector<intptr_t> gcnt(nlevels, 0);

	for (intptr_t i = 0; i < n; i++)
	{
		gsum[indices[i]] += resid[i];
		gcnt[indices[i]]++;
	}

	// SSB = Σ_j n_j * mean_j²  (grand mean of OLS residuals ≈ 0)
	double ssb = 0;
	for (intptr_t j = 0; j < nlevels; j++)
	{
		if (gcnt[j] > 0) {
			double mean_j = gsum[j] / gcnt[j];
			ssb += gcnt[j] * mean_j * mean_j;
		}
	}

	// SSW = Σ_i (r_i − mean_{j(i)})²
	double ssw = 0;
	for (intptr_t i = 0; i < n; i++)
	{
		double mean_j = (gcnt[indices[i]] > 0) ? gsum[indices[i]] / gcnt[indices[i]] : 0.0;
		double d = resid[i] - mean_j;
		ssw += d * d;
	}

	double msb = ssb / std::max(nlevels - 1, (intptr_t)1);
	double msw = ssw / std::max(n - nlevels, (intptr_t)1);

	// Effective group size for unbalanced designs
	double sum_nj2 = 0;
	for (intptr_t j = 0; j < nlevels; j++) {
		sum_nj2 += (double)gcnt[j] * (double)gcnt[j];
	}
	double n0 = (nlevels > 1)
	            ? ((double)n - sum_nj2 / (double)n) / (double)(nlevels - 1)
	            : 1.0;

	return std::max((msb - msw) / std::max(n0, 1.0), 0.0);
}


// =====================================================================
// Full log-determinant of the random-effects Hessian
// =====================================================================
//
// H_uu = Z'WZ + block-diag(D_1⁻¹, ..., D_G⁻¹)
//
// For a single grouping factor (G=1), H is block-diagonal with nlevels
// blocks of size q×q. We exploit this for an O(n q² + J q³) algorithm.
//
// For multiple grouping factors (G>1, crossed effects), the full
// J_total × J_total matrix is built and factorized.

static double full_log_det_H(const Eigen::VectorXd &w,
                               const std::vector<Eigen::MatrixXd> &D_inv,
                               const GroupLayout &lay,
                               intptr_t n)
{
	intptr_t J = lay.J_total;

	// Single grouping factor: block-diagonal fast path.
	// Each level j has a q×q block: D_inv + Σ_{i: idx[i]=j} w[i] z_i z_i'.
	if (lay.G == 1)
	{
		intptr_t qg = lay.q[0];
		auto &idx = *lay.group_indices[0];

		// Pre-accumulate Z'_j W Z_j for each level, initialized with D_inv.
		std::vector<Eigen::MatrixXd> blocks(lay.J[0], D_inv[0]);

		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t j = idx[i];
			for (intptr_t t1 = 0; t1 < qg; t1++)
			{
				double wz1 = w[i] * lay.Z(0, i, t1);
				for (intptr_t t2 = t1; t2 < qg; t2++)
				{
					double val = wz1 * lay.Z(0, i, t2);
					blocks[j](t1, t2) += val;
					if (t1 != t2) blocks[j](t2, t1) += val;
				}
			}
		}

		double ld = 0;
		for (intptr_t j = 0; j < lay.J[0]; j++)
		{
			if (qg == 1) {
				ld += std::log(blocks[j](0, 0));
			} else {
				Eigen::LDLT<Eigen::MatrixXd> ldlt(blocks[j]);
				ld += ldlt.vectorD().array().log().sum();
			}
		}
		return ld;
	}

	// General case (G > 1): build the full J × J matrix and factorize.
	Eigen::MatrixXd H = Eigen::MatrixXd::Zero(J, J);

	// Prior precision: D_g⁻¹ block at each level
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					H(base + t1, base + t2) = D_inv[g](t1, t2);
				}
			}
		}
	}

	// Data contributions
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < lay.G; g1++)
		{
			intptr_t j1 = (*lay.group_indices[g1])[i];
			intptr_t base1 = lay.offset[g1] + j1 * lay.q[g1];

			// Within-group: w[i] * z_g1 ⊗ z_g1
			for (intptr_t t1 = 0; t1 < lay.q[g1]; t1++)
			{
				double wz1 = w[i] * lay.Z(g1, i, t1);
				for (intptr_t t2 = t1; t2 < lay.q[g1]; t2++)
				{
					double val = wz1 * lay.Z(g1, i, t2);
					H(base1 + t1, base1 + t2) += val;
					if (t1 != t2) H(base1 + t2, base1 + t1) += val;
				}
			}

			// Cross-group: w[i] * z_g1 ⊗ z_g2
			for (intptr_t g2 = g1 + 1; g2 < lay.G; g2++)
			{
				intptr_t j2 = (*lay.group_indices[g2])[i];
				intptr_t base2 = lay.offset[g2] + j2 * lay.q[g2];

				for (intptr_t t1 = 0; t1 < lay.q[g1]; t1++)
				{
					double wz1 = w[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = 0; t2 < lay.q[g2]; t2++)
					{
						double val = wz1 * lay.Z(g2, i, t2);
						H(base1 + t1, base2 + t2) += val;
						H(base2 + t2, base1 + t1) += val;
					}
				}
			}
		}
	}

	Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
	return ldlt.vectorD().array().log().sum();
}


// Backward-compatible overload: scalar per-group precisions → diagonal D_inv matrices.
// Used by the Gaussian path (solve_inner, solve_profiled_gaussian) which still operates
// with scalar variances for random-intercept-only models.
static double full_log_det_H(const Eigen::VectorXd &w,
                               const Eigen::VectorXd &inv_sigma2_u,
                               const GroupLayout &lay,
                               intptr_t n)
{
	std::vector<Eigen::MatrixXd> D_inv(lay.G);
	for (intptr_t g = 0; g < lay.G; g++)
	{
		D_inv[g] = Eigen::MatrixXd::Identity(lay.q[g], lay.q[g]) * inv_sigma2_u[g];
	}
	return full_log_det_H(w, D_inv, lay, n);
}


// AD version: manual LDLT for CppAD types.
// Only called when G > 1 (crossed effects); for G = 1 the diagonal
// formula is used directly in laplace_nll_ad.

static ADdouble full_log_det_H_ad(const std::vector<ADdouble> &w,
                                    const std::vector<ADdouble> &inv_sigma2_u,
                                    const GroupLayout &lay,
                                    intptr_t n)
{
	intptr_t J = lay.J_total;

	// Build H with AD entries
	std::vector<std::vector<ADdouble>> H(J, std::vector<ADdouble>(J, ADdouble(0)));

	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			H[off + j][off + j] = inv_sigma2_u[g];
		}
	}
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < lay.G; g1++)
		{
			intptr_t k1 = lay.offset[g1] + (*lay.group_indices[g1])[i];
			H[k1][k1] += w[i];

			for (intptr_t g2 = g1 + 1; g2 < lay.G; g2++)
			{
				intptr_t k2 = lay.offset[g2] + (*lay.group_indices[g2])[i];
				H[k1][k2] += w[i];
				H[k2][k1] += w[i];
			}
		}
	}

	// LDLT decomposition: H = L D L' (no sqrt needed)
	std::vector<ADdouble> D(J);
	// L stored in-place in the lower triangle of H
	for (intptr_t j = 0; j < J; j++)
	{
		ADdouble s = H[j][j];
		for (intptr_t k = 0; k < j; k++) {
			s -= H[j][k] * H[j][k] * D[k];
		}
		D[j] = s;

		for (intptr_t i = j + 1; i < J; i++)
		{
			ADdouble s2 = H[i][j];
			for (intptr_t k = 0; k < j; k++) {
				s2 -= H[i][k] * D[k] * H[j][k];
			}
			H[i][j] = s2 / D[j];   // store L[i][j] in H[i][j]
		}
	}

	ADdouble log_det = ADdouble(0);
	for (intptr_t j = 0; j < J; j++) {
		log_det += CppAD::log(D[j]);
	}
	return log_det;
}


// =====================================================================
// Plain-double helpers for the inner Newton
// =====================================================================

static Eigen::VectorXd working_weights(const Eigen::VectorXd &mu, const Family &fam,
                                        double inv_sigma2)
{
	Eigen::VectorXd V = fam.variance(mu);
	if (fam.name == "gaussian") {
		V *= inv_sigma2;
	}
	return V;
}

static Eigen::VectorXd nll_gradient_eta(const Eigen::VectorXd &y, const Eigen::VectorXd &mu,
                                          const Family &fam, double inv_sigma2)
{
	Eigen::VectorXd g = -(y - mu);
	if (fam.name == "gaussian") {
		g *= inv_sigma2;
	}
	return g;
}


// =====================================================================
// Inner Newton solve (plain double — unchanged)
// =====================================================================

struct InnerResult
{
	Eigen::VectorXd u;
	Eigen::VectorXd mu;
	double laplace_nll;
};

static InnerResult solve_inner(const Eigen::VectorXd &beta,
                                const Eigen::VectorXd &sigma2_u,
                                double sigma2,
                                const Family &fam,
                                const Eigen::Map<Matrix<double>> &Xm,
                                const Eigen::Map<Vector<double>> &ym,
                                const GroupLayout &lay,
                                intptr_t n, intptr_t p)
{
	InnerResult res;
	res.u = Eigen::VectorXd::Zero(lay.J_total);

	bool is_gaussian = (fam.name == "gaussian");
	double inv_sigma2 = is_gaussian ? (1.0 / sigma2) : 0.0;

	Eigen::VectorXd inv_sigma2_u(lay.G);
	for (intptr_t g = 0; g < lay.G; g++) {
		inv_sigma2_u[g] = 1.0 / sigma2_u[g];
	}

	int max_inner = 200;  // convergence check exits early for Gaussian (1–5 steps);
	                      // non-Gaussian with step damping may need more
	double inner_tol = 1e-8;

	Eigen::VectorXd Xbeta = Xm * beta;
	Eigen::VectorXd eta(n), mu(n);

	for (int iter = 0; iter < max_inner; iter++)
	{
		eta = Xbeta;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++) {
				eta[i] += res.u[off + idx[i]];
			}
		}
		mu = fam.linkinv(eta);

		Eigen::VectorXd g_eta = nll_gradient_eta(ym, mu, fam, inv_sigma2);
		Eigen::VectorXd w = working_weights(mu, fam, inv_sigma2);

		Eigen::VectorXd g_u = Eigen::VectorXd::Zero(lay.J_total);
		Eigen::VectorXd h_u = Eigen::VectorXd::Zero(lay.J_total);

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				h_u[off + j] = inv_sigma2_u[g];
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t k = off + idx[i];
				g_u[k] += g_eta[i];
				h_u[k] += w[i];
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				g_u[off + j] += res.u[off + j] * inv_sigma2_u[g];
			}
		}

		double max_change = 0;

		if (!is_gaussian && lay.G > 1)
		{
			// Full Newton step for crossed non-Gaussian effects.
			// The diagonal approximation ignores cross-group Hessian entries
			// and fails to converge for models with multiple grouping factors.
			// Build the full J × J Hessian and solve exactly.
			Eigen::MatrixXd H_full = Eigen::MatrixXd::Zero(lay.J_total, lay.J_total);

			for (intptr_t g = 0; g < lay.G; g++)
			{
				intptr_t off = lay.offset[g];
				for (intptr_t j = 0; j < lay.J[g]; j++) {
					H_full(off + j, off + j) = inv_sigma2_u[g];
				}
			}
			for (intptr_t i = 0; i < n; i++)
			{
				for (intptr_t g1 = 0; g1 < lay.G; g1++)
				{
					intptr_t k1 = lay.offset[g1] + (*lay.group_indices[g1])[i];
					H_full(k1, k1) += w[i];

					for (intptr_t g2 = g1 + 1; g2 < lay.G; g2++)
					{
						intptr_t k2 = lay.offset[g2] + (*lay.group_indices[g2])[i];
						H_full(k1, k2) += w[i];
						H_full(k2, k1) += w[i];
					}
				}
			}

			Eigen::LDLT<Eigen::MatrixXd> ldlt(H_full);
			Eigen::VectorXd step_vec = ldlt.solve(g_u);

			for (intptr_t k = 0; k < lay.J_total; k++)
			{
				double s = std::clamp(step_vec[k], -5.0, 5.0);
				res.u[k] -= s;
				max_change = std::max(max_change, std::abs(s));
			}
		}
		else
		{
			// Diagonal Newton: exact for single grouping factor or Gaussian.
			for (intptr_t k = 0; k < lay.J_total; k++)
			{
				double step = g_u[k] / h_u[k];
				step = std::clamp(step, -5.0, 5.0);
				res.u[k] -= step;
				max_change = std::max(max_change, std::abs(step));
			}
		}

		if (max_change < inner_tol) break;
	}

	// Recompute mu at converged u
	eta = Xbeta;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t off = lay.offset[g];
		for (intptr_t i = 0; i < n; i++) {
			eta[i] += res.u[off + idx[i]];
		}
	}
	res.mu = fam.linkinv(eta);

	// Also compute the plain-double nll (used as the return value for L-BFGS)
	double cond_nll;
	if (is_gaussian)
	{
		double rss = (ym - res.mu).squaredNorm();
		cond_nll = rss / (2.0 * sigma2) + 0.5 * n * std::log(2.0 * M_PI * sigma2);
	}
	else
	{
		cond_nll = -fam.loglik(ym, res.mu);
	}

	double prior_nll = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		double sq = 0;
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			sq += res.u[off + j] * res.u[off + j];
		}
		prior_nll += sq / (2.0 * sigma2_u[g])
		             + 0.5 * lay.J[g] * std::log(2.0 * M_PI * sigma2_u[g]);
	}

	Eigen::VectorXd w_final = working_weights(res.mu, fam, inv_sigma2);
	double log_det_H = full_log_det_H(w_final, inv_sigma2_u, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_H
	                  - 0.5 * lay.J_total * std::log(2.0 * M_PI);

	return res;
}


// =====================================================================
// AD evaluation of Laplace nll (for exact outer gradients)
// =====================================================================
//
// u_hat is treated as a constant (plain double). Only the outer
// parameters phi are AD-active. This is valid because at the inner
// optimum, df/du = 0, so the implicit-function-theorem correction
// du_hat/dphi vanishes from the total derivative.

static ADdouble laplace_nll_ad(const std::vector<ADdouble> &a_phi,
                                const Eigen::VectorXd &u_hat,
                                const std::string &family_name,
                                const Eigen::Map<Matrix<double>> &Xm,
                                const Eigen::Map<Vector<double>> &ym,
                                const GroupLayout &lay,
                                intptr_t n, intptr_t p,
                                bool is_gaussian)
{
	intptr_t G = lay.G;

	// ── Extract variance parameters ─────────────────────────────────

	std::vector<ADdouble> sigma2_u(G), inv_sigma2_u(G);
	for (intptr_t g = 0; g < G; g++)
	{
		sigma2_u[g] = CppAD::exp(2.0 * a_phi[p + g]);
		inv_sigma2_u[g] = 1.0 / sigma2_u[g];
	}

	ADdouble sigma2(0), inv_sigma2(0);
	if (is_gaussian)
	{
		sigma2 = CppAD::exp(2.0 * a_phi[p + G]);
		inv_sigma2 = 1.0 / sigma2;
	}

	// ── Linear predictor: eta = X*beta + Z*u_hat ─────────────────────

	std::vector<ADdouble> eta(n);
	for (intptr_t i = 0; i < n; i++)
	{
		ADdouble sum = 0;
		for (intptr_t j = 0; j < p; j++) {
			sum += Xm(i, j) * a_phi[j];   // double * AD = AD
		}
		for (intptr_t g = 0; g < G; g++) {
			sum += u_hat[lay.offset[g] + (*lay.group_indices[g])[i]];  // double + AD = AD
		}
		eta[i] = sum;
	}

	// ── mu = linkinv(eta) ────────────────────────────────────────────

	std::vector<ADdouble> mu(n);
	if (family_name == "gaussian")
	{
		for (intptr_t i = 0; i < n; i++) {
			mu[i] = eta[i];
		}
	}
	else if (family_name == "binomial")
	{
		for (intptr_t i = 0; i < n; i++) {
			mu[i] = 1.0 / (1.0 + CppAD::exp(-eta[i]));
		}
	}
	else // poisson
	{
		for (intptr_t i = 0; i < n; i++) {
			mu[i] = CppAD::exp(eta[i]);
		}
	}

	// ── Conditional nll: -log p(y | eta) ─────────────────────────────

	ADdouble cond_nll = 0;

	if (family_name == "gaussian")
	{
		ADdouble rss = 0;
		for (intptr_t i = 0; i < n; i++)
		{
			ADdouble d = ym[i] - mu[i];
			rss += d * d;
		}
		cond_nll = rss / (2.0 * sigma2) + 0.5 * n * CppAD::log(2.0 * M_PI * sigma2);
	}
	else if (family_name == "binomial")
	{
		// Numerically stable: use eta directly
		// -loglik = sum -[ y*log(mu) + (1-y)*log(1-mu) ]
		//         = sum -[ y*eta - log(1+exp(eta)) ]
		for (intptr_t i = 0; i < n; i++)
		{
			// log(1 + exp(eta)): use log1p(exp(eta)) for stability
			// For large eta, log(1+exp(eta)) ≈ eta
			// CppAD handles exp and log; we rely on the AD library for correctness
			ADdouble log1pexp = CppAD::log(1.0 + CppAD::exp(eta[i]));
			cond_nll += -ym[i] * eta[i] + log1pexp;
		}
	}
	else // poisson
	{
		for (intptr_t i = 0; i < n; i++)
		{
			// -loglik = sum [ mu - y*log(mu) + lgamma(y+1) ]
			// mu = exp(eta), so log(mu) = eta
			cond_nll += mu[i] - ym[i] * eta[i] + std::lgamma(ym[i] + 1.0);
		}
	}

	// ── Prior nll: sum_g [ ||u_g||^2 / (2 sigma2_u_g) + J_g/2 log(2pi sigma2_u_g) ]

	ADdouble prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t off = lay.offset[g];
		double sq = 0;
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			sq += u_hat[off + j] * u_hat[off + j];
		}
		// sq is plain double, sigma2_u[g] is AD
		prior_nll += sq / (2.0 * sigma2_u[g])
		             + 0.5 * lay.J[g] * CppAD::log(2.0 * M_PI * sigma2_u[g]);
	}

	// ── Working weights and log det H ────────────────────────────────

	std::vector<ADdouble> w(n);
	if (family_name == "gaussian")
	{
		for (intptr_t i = 0; i < n; i++) {
			w[i] = inv_sigma2;  // V(mu) = 1 for Gaussian, scaled by 1/sigma2
		}
	}
	else if (family_name == "binomial")
	{
		for (intptr_t i = 0; i < n; i++) {
			w[i] = mu[i] * (1.0 - mu[i]);
		}
	}
	else // poisson
	{
		for (intptr_t i = 0; i < n; i++) {
			w[i] = mu[i];
		}
	}

	// h_k = inv_sigma2_u[g] + sum_{i in group} w[i]
	// For G > 1 (crossed effects) the full Hessian has off-diagonal blocks;
	// the diagonal formula underestimates log det(H).
	ADdouble log_det_H;
	if (G > 1)
	{
		log_det_H = full_log_det_H_ad(w, inv_sigma2_u, lay, n);
	}
	else
	{
		// Single grouping factor: H is truly diagonal — fast path.
		std::vector<ADdouble> h_u(lay.J_total);
		intptr_t off = lay.offset[0];
		for (intptr_t j = 0; j < lay.J[0]; j++) {
			h_u[j] = inv_sigma2_u[0];
		}
		auto &idx = *lay.group_indices[0];
		for (intptr_t i = 0; i < n; i++) {
			h_u[idx[i]] += w[i];
		}
		log_det_H = ADdouble(0);
		for (intptr_t k = 0; k < lay.J_total; k++) {
			log_det_H += CppAD::log(h_u[k]);
		}
	}

	// ── Laplace nll ──────────────────────────────────────────────────

	return cond_nll + prior_nll + 0.5 * log_det_H
	       - 0.5 * lay.J_total * std::log(2.0 * M_PI);
}


// =====================================================================
// Outer objective with CppAD gradients
// =====================================================================
//
// Outer parameter layout:
//   Non-Gaussian: phi = (beta_1..p, log_sigma_u_1..G)            dim = p + G
//   Gaussian:     phi = (beta_1..p, log_sigma_u_1..G, log_sigma)  dim = p + G + 1

struct OuterObjective
{
	const Family &fam;
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	bool is_gaussian;
	std::string family_name;  // cached for AD (avoids String comparison in tape)

	// Plain-double evaluation (for starting value, final nll, etc.)
	double eval(const Eigen::VectorXd &phi) const
	{
		Eigen::VectorXd beta = phi.head(p);
		Eigen::VectorXd sigma2_u(lay.G);
		for (intptr_t g = 0; g < lay.G; g++) {
			sigma2_u[g] = std::exp(2.0 * phi[p + g]);
		}
		double sigma2 = is_gaussian ? std::exp(2.0 * phi[p + lay.G]) : 0.0;

		auto res = solve_inner(beta, sigma2_u, sigma2, fam, Xm, ym, lay, n, p);
		return res.laplace_nll;
	}

	// L-BFGS callback: value and gradient via finite differences.
	//
	// Finite differences are used for the outer optimization because the
	// implicit re-solve of the inner problem at each perturbed point captures
	// curvature information that improves the L-BFGS search direction, compared
	// to the frozen-u_hat AD gradient. CppAD exact gradients are used for the
	// SE computation (see exact_gradient below), where accuracy matters more
	// than search-direction quality.
	double operator()(const Eigen::VectorXd &phi, Eigen::VectorXd &grad) const
	{
		double f0 = eval(phi);

		intptr_t dim = phi.size();
		for (intptr_t k = 0; k < dim; k++)
		{
			double hk = 1e-5 * std::max(std::abs(phi[k]), 1.0);

			Eigen::VectorXd phi_plus = phi, phi_minus = phi;
			phi_plus[k] += hk;
			phi_minus[k] -= hk;

			grad[k] = (eval(phi_plus) - eval(phi_minus)) / (2.0 * hk);
		}

		return f0;
	}
};


// =====================================================================
// Exact gradient for Hessian estimation (SE computation)
// =====================================================================
//
// Given phi, compute the exact gradient of the Laplace nll.
// This is used to build the numerical Hessian: H_{jk} ≈ (grad_j(phi+e_k) - grad_j(phi-e_k)) / (2h).
// Since the gradient itself is exact (not a finite difference), this gives a first-order-accurate
// Hessian rather than the second-order-noisy Hessian we had before.

static Eigen::VectorXd exact_gradient(const Eigen::VectorXd &phi,
                                       const OuterObjective &obj)
{
	intptr_t dim = phi.size();
	Eigen::VectorXd grad(dim);

	Eigen::VectorXd beta = phi.head(obj.p);
	Eigen::VectorXd sigma2_u(obj.lay.G);
	for (intptr_t g = 0; g < obj.lay.G; g++) {
		sigma2_u[g] = std::exp(2.0 * phi[obj.p + g]);
	}
	double sigma2 = obj.is_gaussian ? std::exp(2.0 * phi[obj.p + obj.lay.G]) : 0.0;

	auto inner = solve_inner(beta, sigma2_u, sigma2, obj.fam, obj.Xm, obj.ym,
	                          obj.lay, obj.n, obj.p);

	std::vector<ADdouble> a_phi(dim);
	for (intptr_t i = 0; i < dim; i++) {
		a_phi[i] = phi[i];
	}
	CppAD::Independent(a_phi);

	std::vector<ADdouble> a_nll(1);
	a_nll[0] = laplace_nll_ad(a_phi, inner.u, obj.family_name,
	                           obj.Xm, obj.ym, obj.lay, obj.n, obj.p, obj.is_gaussian);

	CppAD::ADFun<double> tape;
	tape.Dependent(a_phi, a_nll);

	std::vector<double> phi_vec(phi.data(), phi.data() + dim);
	tape.Forward(0, phi_vec);

	std::vector<double> w(1, 1.0);
	std::vector<double> dw = tape.Reverse(1, w);

	for (intptr_t i = 0; i < dim; i++) {
		grad[i] = dw[i];
	}
	return grad;
}


// =====================================================================
// Profiled inner solve for Gaussian (β concentrated out)
// =====================================================================
//
// For Gaussian LMMs, given variance parameters θ = (log σ_u_1..G, log σ),
// the optimal β is the GLS estimator β̂(θ) = (X'X)⁻¹ X'(y − Zû).
// This function alternates between updating u (diagonal Newton) and
// updating β (OLS on adjusted response) until both converge.
// The result is the joint mode (β̂, û) plus the Laplace nll.

struct ProfiledResult
{
	Eigen::VectorXd beta;
	Eigen::VectorXd u;
	Eigen::VectorXd mu;
	double laplace_nll;
};

static ProfiledResult solve_profiled_gaussian(
	const Eigen::VectorXd &sigma2_u, double sigma2,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p,
	const Eigen::LDLT<Eigen::MatrixXd> &XtX_ldlt,
	const Eigen::MatrixXd &Xt)          // precomputed X'
{
	ProfiledResult res;
	res.u = Eigen::VectorXd::Zero(lay.J_total);
	res.beta = XtX_ldlt.solve(Xt * ym);   // OLS starting β

	double inv_sigma2 = 1.0 / sigma2;
	Eigen::VectorXd inv_sigma2_u(lay.G);
	for (intptr_t g = 0; g < lay.G; g++) {
		inv_sigma2_u[g] = 1.0 / sigma2_u[g];
	}

	// Alternating (u | β) and (β | u) updates, both linear for Gaussian.
	// Typically converges in 2–4 passes for crossed designs.
	for (int iter = 0; iter < 50; iter++)
	{
		// ── Update u given β ────────────────────────────────────────
		Eigen::VectorXd eta = Xm * res.beta;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++) {
				eta[i] += res.u[off + idx[i]];
			}
		}

		Eigen::VectorXd g_u = Eigen::VectorXd::Zero(lay.J_total);
		Eigen::VectorXd h_u = Eigen::VectorXd::Zero(lay.J_total);

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				h_u[off + j] = inv_sigma2_u[g];
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t k = off + idx[i];
				g_u[k] += -(ym[i] - eta[i]) * inv_sigma2;
				h_u[k] += inv_sigma2;
			}
		}
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t off = lay.offset[g];
			for (intptr_t j = 0; j < lay.J[g]; j++) {
				g_u[off + j] += res.u[off + j] * inv_sigma2_u[g];
			}
		}

		double max_u_change = 0;
		for (intptr_t k = 0; k < lay.J_total; k++)
		{
			double step = g_u[k] / h_u[k];
			res.u[k] -= step;
			max_u_change = std::max(max_u_change, std::abs(step));
		}

		// ── Update β given u:  β = (X'X)⁻¹ X'(y − Zu) ─────────────
		Eigen::VectorXd y_adj = ym;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t off = lay.offset[g];
			for (intptr_t i = 0; i < n; i++) {
				y_adj[i] -= res.u[off + idx[i]];
			}
		}
		Eigen::VectorXd beta_new = XtX_ldlt.solve(Xt * y_adj);
		double max_beta_change = (beta_new - res.beta).cwiseAbs().maxCoeff();
		res.beta = beta_new;

		if (std::max(max_u_change, max_beta_change) < 1e-10) break;
	}

	// ── Final eta / mu ──────────────────────────────────────────────
	Eigen::VectorXd eta = Xm * res.beta;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t off = lay.offset[g];
		for (intptr_t i = 0; i < n; i++) {
			eta[i] += res.u[off + idx[i]];
		}
	}
	res.mu = eta;   // identity link

	// ── Laplace nll ─────────────────────────────────────────────────
	double rss = (ym - res.mu).squaredNorm();
	double cond_nll = rss / (2.0 * sigma2) + 0.5 * n * std::log(2.0 * M_PI * sigma2);

	double prior_nll = 0;
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t off = lay.offset[g];
		double sq = 0;
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			sq += res.u[off + j] * res.u[off + j];
		}
		prior_nll += sq / (2.0 * sigma2_u[g])
		             + 0.5 * lay.J[g] * std::log(2.0 * M_PI * sigma2_u[g]);
	}

	// Working weights for Gaussian: w_i = 1/σ² for all i
	Eigen::VectorXd w_prof = Eigen::VectorXd::Constant(n, inv_sigma2);
	double log_det_H = full_log_det_H(w_prof, inv_sigma2_u, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_H
	                  - 0.5 * lay.J_total * std::log(2.0 * M_PI);
	return res;
}


// =====================================================================
// Profiled outer objective (Gaussian only)
// =====================================================================
//
// θ = (log σ_u_1, ..., log σ_u_G, log σ)    dim = G + 1
//
// β is concentrated out: for each θ, the optimal (β̂, û) is computed
// by solve_profiled_gaussian.

struct ProfiledObjective
{
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	const Eigen::LDLT<Eigen::MatrixXd> &XtX_ldlt;
	const Eigen::MatrixXd &Xt;

	double eval(const Eigen::VectorXd &theta) const
	{
		Eigen::VectorXd sigma2_u(lay.G);
		for (intptr_t g = 0; g < lay.G; g++) {
			sigma2_u[g] = std::exp(2.0 * theta[g]);
		}
		double sigma2 = std::exp(2.0 * theta[lay.G]);

		auto res = solve_profiled_gaussian(sigma2_u, sigma2, Xm, ym, lay,
		                                    n, p, XtX_ldlt, Xt);
		return res.laplace_nll;
	}
};


// =====================================================================
// Gaussian Henderson solve with Cholesky-parameterized covariance
// =====================================================================
//
// One-shot Henderson system for Gaussian LMM with known σ² and
// generalized Z design matrix.  Supports random slopes via the
// Cholesky-parameterized D_g for each grouping factor.
//
// The Henderson system for Gaussian is linear, so no PIRLS iteration
// is needed — a single solve gives the exact (β̂, û) for given θ.

static ProfiledResult solve_gaussian_henderson(
	const std::vector<Eigen::MatrixXd> &D_inv,
	const std::vector<double> &log_det_Dg,
	double sigma2,
	const Eigen::Map<Matrix<double>> &Xm,
	const Eigen::Map<Vector<double>> &ym,
	const GroupLayout &lay,
	intptr_t n, intptr_t p)
{
	ProfiledResult res;
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;
	intptr_t sdim = p + J;

	double inv_sigma2 = 1.0 / sigma2;

	// Build Henderson system:
	// [(1/σ²)X'X     (1/σ²)X'Z           ] [β]   [(1/σ²)X'y ]
	// [(1/σ²)Z'X   (1/σ²)Z'Z + D⁻¹       ] [u ] = [(1/σ²)Z'y ]

	Eigen::MatrixXd H = Eigen::MatrixXd::Zero(sdim, sdim);
	Eigen::VectorXd rhs = Eigen::VectorXd::Zero(sdim);

	// (1/σ²)X'X and (1/σ²)X'y
	for (intptr_t i = 0; i < n; i++)
	{
		double wy = inv_sigma2 * ym[i];
		for (intptr_t j1 = 0; j1 < p; j1++)
		{
			rhs[j1] += Xm(i, j1) * wy;
			double wx = Xm(i, j1) * inv_sigma2;
			for (intptr_t j2 = j1; j2 < p; j2++) {
				H(j1, j2) += wx * Xm(i, j2);
			}
		}
	}
	for (intptr_t j1 = 0; j1 < p; j1++) {
		for (intptr_t j2 = j1 + 1; j2 < p; j2++) {
			H(j2, j1) = H(j1, j2);
		}
	}

	// D⁻¹ blocks
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = p + lay.offset[g] + j * qg;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					H(base + t1, base + t2) += D_inv[g](t1, t2);
				}
			}
		}
	}

	// (1/σ²)X'Z, Z'X, Z'Z, Z'y
	for (intptr_t i = 0; i < n; i++)
	{
		for (intptr_t g1 = 0; g1 < G; g1++)
		{
			intptr_t j1 = (*lay.group_indices[g1])[i];
			intptr_t q1 = lay.q[g1];
			intptr_t base1 = p + lay.offset[g1] + j1 * q1;

			for (intptr_t t = 0; t < q1; t++)
			{
				double z_val = lay.Z(g1, i, t);
				double wz = inv_sigma2 * z_val;

				for (intptr_t j = 0; j < p; j++)
				{
					double val = Xm(i, j) * wz;
					H(j, base1 + t) += val;
					H(base1 + t, j) += val;
				}

				rhs[base1 + t] += inv_sigma2 * ym[i] * z_val;
			}

			// Z'Z within-group
			for (intptr_t t1 = 0; t1 < q1; t1++)
			{
				double wz1 = inv_sigma2 * lay.Z(g1, i, t1);
				for (intptr_t t2 = t1; t2 < q1; t2++)
				{
					double val = wz1 * lay.Z(g1, i, t2);
					H(base1 + t1, base1 + t2) += val;
					if (t1 != t2) H(base1 + t2, base1 + t1) += val;
				}
			}

			// Z'Z cross-group
			for (intptr_t g2 = g1 + 1; g2 < G; g2++)
			{
				intptr_t j2 = (*lay.group_indices[g2])[i];
				intptr_t q2 = lay.q[g2];
				intptr_t base2 = p + lay.offset[g2] + j2 * q2;

				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = inv_sigma2 * lay.Z(g1, i, t1);
					for (intptr_t t2 = 0; t2 < q2; t2++)
					{
						double val = wz1 * lay.Z(g2, i, t2);
						H(base1 + t1, base2 + t2) += val;
						H(base2 + t2, base1 + t1) += val;
					}
				}
			}
		}
	}

	// Solve
	Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
	Eigen::VectorXd sol = ldlt.solve(rhs);

	res.beta = sol.head(p);
	res.u = sol.tail(J);

	// Final η = Xβ + Zu
	Eigen::VectorXd eta = Xm * res.beta;
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++) {
				eta[i] += lay.Z(g, i, t) * res.u[base + t];
			}
		}
	}
	res.mu = eta; // identity link

	// Laplace nll
	double rss = (ym - res.mu).squaredNorm();
	double cond_nll = rss / (2.0 * sigma2) + 0.5 * n * std::log(2.0 * M_PI * sigma2);

	double prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					quad += res.u[base + t1] * D_inv[g](t1, t2) * res.u[base + t2];
				}
			}
			prior_nll += quad / 2.0;
		}
		prior_nll += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI) + 0.5 * log_det_Dg[g]);
	}

	Eigen::VectorXd w_final = Eigen::VectorXd::Constant(n, inv_sigma2);
	double log_det_Huu = full_log_det_H(w_final, D_inv, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_Huu
	                  - 0.5 * J * std::log(2.0 * M_PI);
	return res;
}


// =====================================================================
// Gaussian outer objective with Cholesky parameterization
// =====================================================================
//
// θ = (chol_1, ..., chol_G, log σ)
// dim = Σ q_g(q_g+1)/2 + 1

struct GaussianCholObjective
{
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	intptr_t n_chol;

	double eval(const Eigen::VectorXd &theta) const
	{
		std::vector<Eigen::MatrixXd> D_inv(lay.G);
		std::vector<double> log_det_Dg(lay.G);
		intptr_t chol_pos = 0;

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
			D_inv[g] = cholesky_to_precision(L);
			log_det_Dg[g] = log_det_D(theta.data() + chol_pos, qg);
			chol_pos += n_chol_params(qg);
		}
		double sigma2 = std::exp(2.0 * theta[n_chol]);

		return solve_gaussian_henderson(D_inv, log_det_Dg, sigma2,
		                                 Xm, ym, lay, n, p).laplace_nll;
	}
};


// =====================================================================
// PIRLS inner solve for non-Gaussian (β and u concentrated out)
// =====================================================================
//
// Penalized Iteratively Reweighted Least Squares (Breslow & Clayton 1993,
// Bates et al. 2015 §3).  For given variance parameters θ, finds the
// joint mode (β̂, û) by iterating:
//   1. Compute working response z and weights w from current (β, u)
//   2. Solve the weighted Henderson equations for (β, u)
//   3. Repeat until convergence
//
// The Henderson system is (p + J_total) × (p + J_total) and is solved
// directly by Eigen's LDLT.  For random intercepts in typical phonetics
// models this is at most a few hundred × a few hundred — negligible cost.

static ProfiledResult solve_pirls(const std::vector<Eigen::MatrixXd> &D_inv,
                                   const std::vector<double> &log_det_Dg,
                                   const Family &fam,
                                   const Eigen::Map<Matrix<double>> &Xm,
                                   const Eigen::Map<Vector<double>> &ym,
                                   const GroupLayout &lay,
                                   intptr_t n, intptr_t p,
                                   const Eigen::VectorXd &beta_init,
                                   const Eigen::VectorXd &u_init = Eigen::VectorXd())
{
	ProfiledResult res;
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;
	intptr_t sdim = p + J;   // Henderson system dimension

	res.beta = beta_init;
	res.u = (u_init.size() == J) ? u_init : Eigen::VectorXd::Zero(J);

	for (int pirls_iter = 0; pirls_iter < 100; pirls_iter++)
	{
		// ── η = Xβ + Zu ──────────────────────────────────────
		Eigen::VectorXd eta = Xm * res.beta;
		for (intptr_t g = 0; g < G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t qg = lay.q[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t base = lay.offset[g] + idx[i] * qg;
				for (intptr_t t = 0; t < qg; t++) {
					eta[i] += lay.Z(g, i, t) * res.u[base + t];
				}
			}
		}
		Eigen::VectorXd mu = fam.linkinv(eta);

		// ── Working weights and response ────────────────────────
		// General GLM: w_i = (dμ/dη)² / V(μ),  z_i = η_i + (y_i − μ_i) / (dμ/dη)
		Eigen::VectorXd V = fam.variance(mu);
		Eigen::VectorXd me = fam.mu_eta(mu);
		Eigen::VectorXd w(n), z(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double v = std::max(V[i], 1e-10);
			double d = std::max(me[i], 1e-10);
			w[i] = d * d / v;  // generalized IWLS weight
			z[i] = eta[i] + (ym[i] - mu[i]) / d;
		}

		// ── Henderson system ────────────────────────────────────
		//  [X'WX       X'WZ           ] [β]   [X'Wz ]
		//  [Z'WX   Z'WZ + D⁻¹        ] [u ] = [Z'Wz ]

		Eigen::MatrixXd H = Eigen::MatrixXd::Zero(sdim, sdim);
		Eigen::VectorXd rhs = Eigen::VectorXd::Zero(sdim);

		// X'WX (p × p) and X'Wz (p)
		for (intptr_t i = 0; i < n; i++)
		{
			double wz = w[i] * z[i];
			for (intptr_t j1 = 0; j1 < p; j1++)
			{
				rhs[j1] += Xm(i, j1) * wz;
				double wx = Xm(i, j1) * w[i];
				for (intptr_t j2 = j1; j2 < p; j2++) {
					H(j1, j2) += wx * Xm(i, j2);
				}
			}
		}
		for (intptr_t j1 = 0; j1 < p; j1++) {
			for (intptr_t j2 = j1 + 1; j2 < p; j2++) {
				H(j2, j1) = H(j1, j2);
			}
		}

		// D⁻¹: q_g × q_g block at each level
		for (intptr_t g = 0; g < G; g++)
		{
			intptr_t qg = lay.q[g];
			for (intptr_t j = 0; j < lay.J[g]; j++)
			{
				intptr_t base = p + lay.offset[g] + j * qg;
				for (intptr_t t1 = 0; t1 < qg; t1++) {
					for (intptr_t t2 = 0; t2 < qg; t2++) {
						H(base + t1, base + t2) += D_inv[g](t1, t2);
					}
				}
			}
		}

		// Data contributions to X'WZ, Z'WX, Z'WZ, Z'Wz
		for (intptr_t i = 0; i < n; i++)
		{
			double wz = w[i] * z[i];

			for (intptr_t g1 = 0; g1 < G; g1++)
			{
				intptr_t j1 = (*lay.group_indices[g1])[i];
				intptr_t q1 = lay.q[g1];
				intptr_t base1 = p + lay.offset[g1] + j1 * q1;

				// X'WZ and Z'WX: for each random term t
				for (intptr_t t = 0; t < q1; t++)
				{
					double z_val = lay.Z(g1, i, t);
					double wz_val = w[i] * z_val;

					for (intptr_t j = 0; j < p; j++)
					{
						double val = Xm(i, j) * wz_val;
						H(j, base1 + t) += val;
						H(base1 + t, j) += val;
					}

					// Z'Wz
					rhs[base1 + t] += wz * z_val;
				}

				// Z'WZ within-group: w[i] * z_g1 ⊗ z_g1
				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = w[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = t1; t2 < q1; t2++)
					{
						double val = wz1 * lay.Z(g1, i, t2);
						H(base1 + t1, base1 + t2) += val;
						if (t1 != t2) H(base1 + t2, base1 + t1) += val;
					}
				}

				// Z'WZ cross-group: w[i] * z_g1 ⊗ z_g2
				for (intptr_t g2 = g1 + 1; g2 < G; g2++)
				{
					intptr_t j2 = (*lay.group_indices[g2])[i];
					intptr_t q2 = lay.q[g2];
					intptr_t base2 = p + lay.offset[g2] + j2 * q2;

					for (intptr_t t1 = 0; t1 < q1; t1++)
					{
						double wz1 = w[i] * lay.Z(g1, i, t1);
						for (intptr_t t2 = 0; t2 < q2; t2++)
						{
							double val = wz1 * lay.Z(g2, i, t2);
							H(base1 + t1, base2 + t2) += val;
							H(base2 + t2, base1 + t1) += val;
						}
					}
				}
			}
		}

		// ── Solve ───────────────────────────────────────────────
		Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
		Eigen::VectorXd sol = ldlt.solve(rhs);

		Eigen::VectorXd beta_new = sol.head(p);
		Eigen::VectorXd u_new = sol.tail(J);

		// ── Convergence ─────────────────────────────────────────
		double max_change = (beta_new - res.beta).cwiseAbs().maxCoeff();
		max_change = std::max(max_change, (u_new - res.u).cwiseAbs().maxCoeff());

		res.beta = beta_new;
		res.u = u_new;

		if (max_change < 1e-8) break;
		if (pirls_iter == 99) break;
	}

	// ── Final η, μ ──────────────────────────────────────────────────
	Eigen::VectorXd eta = Xm * res.beta;
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++) {
				eta[i] += lay.Z(g, i, t) * res.u[base + t];
			}
		}
	}
	res.mu = fam.linkinv(eta);

	// ── Laplace nll ─────────────────────────────────────────────────
	double cond_nll = -fam.loglik(ym, res.mu);

	// Prior: Σ_g Σ_j [ u_{gj}' D_g⁻¹ u_{gj} / 2 + q_g/2 log(2π) + 1/2 log|D_g| ]
	double prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++) {
				for (intptr_t t2 = 0; t2 < qg; t2++) {
					quad += res.u[base + t1] * D_inv[g](t1, t2) * res.u[base + t2];
				}
			}
			prior_nll += quad / 2.0;
		}
		prior_nll += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI) + 0.5 * log_det_Dg[g]);
	}

	// Laplace correction: 1/2 log|H_uu| - J_total/2 log(2π)
	Eigen::VectorXd V_final = fam.variance(res.mu);
	Eigen::VectorXd me_final = fam.mu_eta(res.mu);
	Eigen::VectorXd w_final(n);
	for (intptr_t i = 0; i < n; i++) {
		double v = std::max(V_final[i], 1e-10);
		double d = std::max(me_final[i], 1e-10);
		w_final[i] = d * d / v;
	}
	double log_det_Huu = full_log_det_H(w_final, D_inv, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_Huu
	                  - 0.5 * J * std::log(2.0 * M_PI);

	return res;
}

// Backward-compatible overload: scalar variances (for random-intercept-only models).
static ProfiledResult solve_pirls(const Eigen::VectorXd &sigma2_u,
                                   const Family &fam,
                                   const Eigen::Map<Matrix<double>> &Xm,
                                   const Eigen::Map<Vector<double>> &ym,
                                   const GroupLayout &lay,
                                   intptr_t n, intptr_t p,
                                   const Eigen::VectorXd &beta_init)
{
	std::vector<Eigen::MatrixXd> D_inv(lay.G);
	std::vector<double> log_det_Dg(lay.G);
	for (intptr_t g = 0; g < lay.G; g++)
	{
		intptr_t qg = lay.q[g];
		D_inv[g] = Eigen::MatrixXd::Identity(qg, qg) / sigma2_u[g];
		log_det_Dg[g] = qg * std::log(sigma2_u[g]);
	}
	return solve_pirls(D_inv, log_det_Dg, fam, Xm, ym, lay, n, p, beta_init);
}


// =====================================================================
// u-only IRLS solver (β fixed) — used by Phase 2 joint optimization
// =====================================================================
//
// Much cheaper than full PIRLS: no β solve, and for random intercepts
// the u-update is diagonal (no matrix factorization).

static ProfiledResult solve_u_given_beta(
    const std::vector<Eigen::MatrixXd> &D_inv,
    const std::vector<double> &log_det_Dg,
    const Family &fam,
    const Eigen::Map<Matrix<double>> &Xm,
    const Eigen::Map<Vector<double>> &ym,
    const GroupLayout &lay,
    intptr_t n, intptr_t p,
    const Eigen::VectorXd &beta,
    const Eigen::VectorXd &u_init = Eigen::VectorXd())
{
	ProfiledResult res;
	intptr_t G = lay.G;
	intptr_t J = lay.J_total;

	res.beta = beta;  // fixed — not updated
	res.u = (u_init.size() == J) ? u_init : Eigen::VectorXd::Zero(J);

	Eigen::VectorXd Xbeta = Xm * beta;

	for (int iter = 0; iter < 100; iter++)
	{
		// ── η = Xβ + Zu ──
		Eigen::VectorXd eta = Xbeta;
		for (intptr_t g = 0; g < G; g++)
		{
			auto &idx = *lay.group_indices[g];
			intptr_t qg = lay.q[g];
			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t base = lay.offset[g] + idx[i] * qg;
				for (intptr_t t = 0; t < qg; t++)
					eta[i] += lay.Z(g, i, t) * res.u[base + t];
			}
		}

		Eigen::VectorXd mu = fam.linkinv(eta.cwiseMax(-30.0).cwiseMin(30.0));
		Eigen::VectorXd V = fam.variance(mu);
		Eigen::VectorXd me = fam.mu_eta(mu);

		// Working weights and residual r = z - Xβ
		Eigen::VectorXd w(n), r(n);
		bool bad = false;
		for (intptr_t i = 0; i < n; i++)
		{
			double v = std::max(V[i], 1e-10);
			double d = std::max(me[i], 1e-10);
			w[i] = d * d / v;
			r[i] = eta[i] - Xbeta[i] + (ym[i] - mu[i]) / d;
			if (!std::isfinite(r[i]) || !std::isfinite(w[i])) bad = true;
		}
		if (bad) break;

		Eigen::VectorXd u_new(J);

		if (G == 1)
		{
			// ── Single group: block-diagonal solve (exact) ──
			intptr_t qg = lay.q[0];
			intptr_t Jg = lay.J[0];
			auto &idx = *lay.group_indices[0];

			std::vector<Eigen::MatrixXd> blocks(Jg, D_inv[0]);
			std::vector<Eigen::VectorXd> rhs_v(Jg, Eigen::VectorXd::Zero(qg));

			for (intptr_t i = 0; i < n; i++)
			{
				intptr_t j = idx[i];
				for (intptr_t t1 = 0; t1 < qg; t1++)
				{
					double wz1 = w[i] * lay.Z(0, i, t1);
					rhs_v[j][t1] += wz1 * r[i];
					for (intptr_t t2 = t1; t2 < qg; t2++)
					{
						double val = wz1 * lay.Z(0, i, t2);
						blocks[j](t1, t2) += val;
						if (t1 != t2) blocks[j](t2, t1) += val;
					}
				}
			}

			for (intptr_t j = 0; j < Jg; j++)
			{
				intptr_t base = lay.offset[0] + j * qg;
				if (qg == 1) {
					u_new[base] = rhs_v[j][0] / blocks[j](0, 0);
				} else {
					u_new.segment(base, qg) = blocks[j].ldlt().solve(rhs_v[j]);
				}
			}
		}
		else
		{
			// ── Crossed groups: full J × J system ──
			// (Z'WZ + D⁻¹) u = Z'W r
			Eigen::MatrixXd H = Eigen::MatrixXd::Zero(J, J);
			Eigen::VectorXd rhs = Eigen::VectorXd::Zero(J);

			// D⁻¹ blocks on diagonal
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				for (intptr_t j = 0; j < lay.J[g]; j++)
				{
					intptr_t base = lay.offset[g] + j * qg;
					for (intptr_t t1 = 0; t1 < qg; t1++)
						for (intptr_t t2 = 0; t2 < qg; t2++)
							H(base + t1, base + t2) = D_inv[g](t1, t2);
				}
			}

			// Data contributions: Z'WZ and Z'Wr
			for (intptr_t i = 0; i < n; i++)
			{
				for (intptr_t g1 = 0; g1 < G; g1++)
				{
					intptr_t j1 = (*lay.group_indices[g1])[i];
					intptr_t q1 = lay.q[g1];
					intptr_t base1 = lay.offset[g1] + j1 * q1;

					for (intptr_t t = 0; t < q1; t++)
						rhs[base1 + t] += w[i] * lay.Z(g1, i, t) * r[i];

					// Within-group Z'WZ
					for (intptr_t t1 = 0; t1 < q1; t1++)
					{
						double wz1 = w[i] * lay.Z(g1, i, t1);
						for (intptr_t t2 = t1; t2 < q1; t2++)
						{
							double val = wz1 * lay.Z(g1, i, t2);
							H(base1 + t1, base1 + t2) += val;
							if (t1 != t2) H(base1 + t2, base1 + t1) += val;
						}
					}

					// Cross-group Z'WZ
					for (intptr_t g2 = g1 + 1; g2 < G; g2++)
					{
						intptr_t j2 = (*lay.group_indices[g2])[i];
						intptr_t q2 = lay.q[g2];
						intptr_t base2 = lay.offset[g2] + j2 * q2;

						for (intptr_t t1 = 0; t1 < q1; t1++)
						{
							double wz1 = w[i] * lay.Z(g1, i, t1);
							for (intptr_t t2 = 0; t2 < q2; t2++)
							{
								double val = wz1 * lay.Z(g2, i, t2);
								H(base1 + t1, base2 + t2) += val;
								H(base2 + t2, base1 + t1) += val;
							}
						}
					}
				}
			}

			// Solve
			u_new = Eigen::LDLT<Eigen::MatrixXd>(H).solve(rhs);
		}

		double max_change = (u_new - res.u).cwiseAbs().maxCoeff();
		res.u = u_new;
		if (!std::isfinite(max_change)) break;
		if (max_change < 1e-10) break;
	}

	// ── Final η, μ ──
	Eigen::VectorXd eta_f = Xbeta;
	for (intptr_t g = 0; g < G; g++)
	{
		auto &idx = *lay.group_indices[g];
		intptr_t qg = lay.q[g];
		for (intptr_t i = 0; i < n; i++)
		{
			intptr_t base = lay.offset[g] + idx[i] * qg;
			for (intptr_t t = 0; t < qg; t++)
				eta_f[i] += lay.Z(g, i, t) * res.u[base + t];
		}
	}
	res.mu = fam.linkinv(eta_f.cwiseMax(-30.0).cwiseMin(30.0));

	// ── Laplace NLL ──
	double cond_nll = -fam.loglik(ym, res.mu);
	if (!std::isfinite(cond_nll)) {
		res.laplace_nll = 1e30;
		return res;
	}

	double prior_nll = 0;
	for (intptr_t g = 0; g < G; g++)
	{
		intptr_t qg = lay.q[g];
		for (intptr_t j = 0; j < lay.J[g]; j++)
		{
			intptr_t base = lay.offset[g] + j * qg;
			double quad = 0;
			for (intptr_t t1 = 0; t1 < qg; t1++)
				for (intptr_t t2 = 0; t2 < qg; t2++)
					quad += res.u[base + t1] * D_inv[g](t1, t2) * res.u[base + t2];
			prior_nll += quad / 2.0;
		}
		prior_nll += lay.J[g] * (0.5 * qg * std::log(2.0 * M_PI) + 0.5 * log_det_Dg[g]);
	}

	Eigen::VectorXd V_f = fam.variance(res.mu);
	Eigen::VectorXd me_f = fam.mu_eta(res.mu);
	Eigen::VectorXd w_f(n);
	for (intptr_t i = 0; i < n; i++)
	{
		double v = std::max(V_f[i], 1e-10);
		double d = std::max(me_f[i], 1e-10);
		w_f[i] = d * d / v;
	}
	double log_det_Huu = full_log_det_H(w_f, D_inv, lay, n);

	res.laplace_nll = cond_nll + prior_nll + 0.5 * log_det_Huu
	                  - 0.5 * J * std::log(2.0 * M_PI);
	return res;
}


// =====================================================================
// PIRLS outer objective (non-Gaussian)
// =====================================================================
//
// Outer parameters:
//   θ = (chol_1, ..., chol_G, [log θ_nb])
// where chol_g is the packed lower Cholesky factor for group g
// (q_g(q_g+1)/2 elements, diagonal on log scale).
//
// Total dim = Σ q_g(q_g+1)/2 + (1 if NB).
//
// β and u are concentrated out via PIRLS at each θ evaluation.

struct PirlsObjective
{
	const Family &fam;
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p;
	Eigen::VectorXd beta_init;
	intptr_t n_chol;  // total Cholesky params = Σ q_g(q_g+1)/2

	// Warm-start: cache the random effects from the last PIRLS solve
	// so consecutive evaluations at nearby θ start close to the mode.
	mutable Eigen::VectorXd last_u;

	double eval(const Eigen::VectorXd &theta) const
	{

		// Unpack Cholesky parameters → D_inv and log|D| for each group
		std::vector<Eigen::MatrixXd> D_inv(lay.G);
		std::vector<double> log_det_Dg(lay.G);
		intptr_t chol_pos = 0;

		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			intptr_t np = n_chol_params(qg);
			Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
			D_inv[g] = cholesky_to_precision(L);
			log_det_Dg[g] = log_det_D(theta.data() + chol_pos, qg);
			chol_pos += np;
		}

		ProfiledResult res;

		// For NB, the element after the Cholesky params is log(θ_nb).
		if (fam.name == "negbin")
		{
			double theta_nb = std::exp(theta[n_chol]);
			auto fam_nb = Family::negbin(theta_nb);
			res = solve_pirls(D_inv, log_det_Dg, fam_nb, Xm, ym, lay, n, p, beta_init, last_u);
		}
		else if (fam.name == "beta")
		{
			double phi_beta = std::exp(theta[n_chol]);
			auto fam_beta = Family::beta(phi_beta);
			res = solve_pirls(D_inv, log_det_Dg, fam_beta, Xm, ym, lay, n, p, beta_init, last_u);
		}
		else
		{
			res = solve_pirls(D_inv, log_det_Dg, fam, Xm, ym, lay, n, p, beta_init, last_u);
		}

		last_u = std::move(res.u);
		return res.laplace_nll;
	}
};


// =====================================================================
// Phase 2 joint (β, θ) objective for non-Gaussian GLMMs
// =====================================================================
//
// After Phase 1 converges (PIRLS profiles β out, Newton over θ only),
// Phase 2 re-optimizes (β, θ) jointly with u profiled out via u-only
// IRLS. This matches the joint optimization done by lme4/glmmTMB.
// Phase 2 is only needed for non-Gaussian families (for Gaussian,
// the working weights are constant and ∂log|H|/∂β = 0).
//
// Outer parameter vector: phi = [β (p), θ_chol (n_chol), log(θ_nb)?]

struct LaplaceJointObjective
{
	const Family &fam;
	const Eigen::Map<Matrix<double>> &Xm;
	const Eigen::Map<Vector<double>> &ym;
	const GroupLayout &lay;
	intptr_t n, p, n_chol;

	mutable Eigen::VectorXd last_u;

	double eval(const Eigen::VectorXd &phi) const
	{
		Eigen::VectorXd beta = phi.head(p);

		// Unpack Cholesky → D_inv, log_det_Dg
		std::vector<Eigen::MatrixXd> D_inv(lay.G);
		std::vector<double> log_det_Dg(lay.G);
		intptr_t chol_pos = 0;
		for (intptr_t g = 0; g < lay.G; g++)
		{
			intptr_t qg = lay.q[g];
			intptr_t np = n_chol_params(qg);
			Eigen::MatrixXd L = unpack_cholesky(phi.data() + p + chol_pos, qg);
			D_inv[g] = cholesky_to_precision(L);
			log_det_Dg[g] = log_det_D(phi.data() + p + chol_pos, qg);
			chol_pos += np;
		}

		Family fam_used = fam;
		if (fam.name == "negbin")
		{
			double theta_nb = std::exp(phi[p + n_chol]);
			fam_used = Family::negbin(theta_nb);
		}
		else if (fam.name == "beta")
		{
			double phi_beta = std::exp(phi[p + n_chol]);
			fam_used = Family::beta(phi_beta);
		}

		auto res = solve_u_given_beta(D_inv, log_det_Dg, fam_used,
		                               Xm, ym, lay, n, p, beta, last_u);
		if (std::isfinite(res.laplace_nll))
			last_u = std::move(res.u);
		else
			res.laplace_nll = 1e30;  // reject this step
		return res.laplace_nll;
	}
};


// =====================================================================
// Newton optimizer
// =====================================================================
//
// For small outer dimensions (typically 2–10 parameters in phonetics
// models) full Newton (exact Hessian via finite differences of the
// objective, Armijo backtracking) converges in 5–15 iterations to
// machine precision.  This is what nlminb (the default optimizer in
// glmmTMB/lme4) does — L-BFGS is designed for thousands of parameters
// and is unreliable for these low-dimensional surfaces.
//
// The Objective type must provide: double eval(const Eigen::VectorXd &) const

struct NewtonResult
{
	Eigen::VectorXd theta;
	double fx;
	int niter;
	bool converged;
};

template<typename Objective>
static NewtonResult newton_optimize(const Objective &obj,
                                     Eigen::VectorXd theta,
                                     int max_iter = 200,
                                     double grad_tol = 1e-8,
                                     FittingCallback progress = nullptr,
                                     double h_scale_hint = 0)
{
	intptr_t dim = theta.size();
	NewtonResult res;
	res.converged = false;
	res.fx = obj.eval(theta);
	int stall_count = 0;

	// Finite-difference step scale.  When h_scale_hint > 0, use it directly
	// (the caller knows the noise floor of the objective).  Otherwise, use
	// dimension-dependent defaults for objectives with negligible noise
	// (e.g. Gaussian Henderson, which is solved exactly).
	double h_scale = (h_scale_hint > 0) ? h_scale_hint
	               : (dim <= 3) ? 1e-4 : 1e-3;

	for (int iter = 0; iter < max_iter; iter++)
	{
		// ── Gradient and Hessian from function values ─────────────
		//
		// For dim=3 this needs 1 + 2*3 + 4*3 = 19 function evals,
		// each of which is a fast profiled inner solve.

		double f0 = res.fx;

		// Per-dimension step sizes, forward/backward function values
		std::vector<double> h(dim), fp(dim), fm(dim);
		for (intptr_t j = 0; j < dim; j++)
		{
			h[j] = h_scale * std::max(std::abs(theta[j]), 1.0);
			Eigen::VectorXd tp = theta, tm = theta;
			tp[j] += h[j];
			tm[j] -= h[j];
			fp[j] = obj.eval(tp);
			fm[j] = obj.eval(tm);
		}

		Eigen::VectorXd grad(dim);
		Eigen::MatrixXd H(dim, dim);

		for (intptr_t j = 0; j < dim; j++)
		{
			grad[j] = (fp[j] - fm[j]) / (2.0 * h[j]);
			H(j, j) = (fp[j] - 2.0 * f0 + fm[j]) / (h[j] * h[j]);
		}

		// Off-diagonal: H_{jk} = (f++ − f+− − f−+ + f−−) / (4 h_j h_k)
		for (intptr_t j = 0; j < dim; j++)
		{
			for (intptr_t k = j + 1; k < dim; k++)
			{
				Eigen::VectorXd tpp = theta, tpm = theta, tmp = theta, tmm = theta;
				tpp[j] += h[j]; tpp[k] += h[k];
				tpm[j] += h[j]; tpm[k] -= h[k];
				tmp[j] -= h[j]; tmp[k] += h[k];
				tmm[j] -= h[j]; tmm[k] -= h[k];

				double val = (obj.eval(tpp) - obj.eval(tpm)
				              - obj.eval(tmp) + obj.eval(tmm))
				             / (4.0 * h[j] * h[k]);
				H(j, k) = val;
				H(k, j) = val;
			}
		}

		// ── Convergence check ────────────────────────────────────
		// Parameters below boundary_min are at the edge of the parameter
		// space (log σ → −∞, i.e. σ → 0); the gradient there doesn't
		// converge to 0 because the optimum is at −∞ on the log scale.
		// Convergence is checked only on the free (interior) parameters.
		{
			double grad_norm_free = 0;
			for (intptr_t j = 0; j < dim; j++)
			{
				bool at_boundary = (theta[j] < -10.0 && grad[j] > 0);
				if (!at_boundary) {
					grad_norm_free += grad[j] * grad[j];
				}
			}
			grad_norm_free = std::sqrt(grad_norm_free);

			if (grad_norm_free < grad_tol)
			{
				res.converged = true;
				res.theta = theta;
				res.niter = iter;
				return res;
			}
		}

		// ── Newton direction: −H⁻¹g ─────────────────────────────
		Eigen::VectorXd step;
		{
			Eigen::LDLT<Eigen::MatrixXd> ldlt(H);
			if (ldlt.info() == Eigen::Success && ldlt.isPositive()) {
				step = -ldlt.solve(grad);
			} else {
				// Hessian not positive-definite: steepest-descent fallback
				step = -grad * (1.0 / grad.norm());
			}
		}

		// ── Backtracking line search (Armijo) ────────────────────
		double alpha = 1.0;
		double slope = grad.dot(step);

		// If slope ≥ 0 the step is not a descent direction
		if (slope >= 0) {
			step = -grad * (1.0 / grad.norm());
			slope = grad.dot(step);
		}

		for (int ls = 0; ls < 30; ls++)
		{
			Eigen::VectorXd theta_new = theta + alpha * step;
			double fx_new = obj.eval(theta_new);
			if (fx_new <= f0 + 1e-4 * alpha * slope)
			{
				theta = theta_new;
				res.fx = fx_new;
				break;
			}
			alpha *= 0.5;
			if (ls == 29)
			{
				// Line search stalled — accept current best
				theta = theta + alpha * step;
				res.fx = obj.eval(theta);
			}
		}

		// ── Function-value stall detection ───────────────────────
		// If the nll hasn't changed meaningfully in 5 consecutive
		// iterations, declare convergence.  The 1e-8 relative tolerance
		// matches the gradient tolerance and ensures the optimizer
		// continues refining as long as meaningful progress is possible.
		if (std::abs(res.fx - f0) < 1e-8 * (1.0 + std::abs(f0)))
		{
			stall_count++;
			if (stall_count >= 5)
			{
				res.converged = true;
				res.theta = theta;
				res.niter = iter;
				return res;
			}
		}
		else
		{
			stall_count = 0;
		}

		// Report progress.
		if (progress && iter % 5 == 0)
			progress(iter, max_iter);
	}

	if (progress) progress(max_iter, max_iter);
	res.theta = theta;
	res.niter = max_iter;
	return res;
}


} // anonymous namespace


// =====================================================================
// Public entry point
// =====================================================================

Model mixed_model(const Array<double> &y, const Array<double> &X,
                  const std::vector<GroupingInfo> &groups, const Family &fam,
                  FittingCallback progress)
{

	if (y.ndim() != 1) {
		throw error("y must be a one-dimensional array");
	}
	if (X.ndim() != 2) {
		throw error("X must be a two-dimensional array");
	}
	if (X.nrow() != y.size()) {
		throw error("Inconsistent number of observations in y and X");
	}
	if (groups.empty()) {
		throw error("At least one grouping factor is required");
	}

	intptr_t n = y.size();
	intptr_t p = X.ncol();
	bool is_gaussian = (fam.name == "gaussian");

	for (auto &g : groups)
	{
		if (g.nlevels < 2) {
			throw error("Grouping factor '%' must have at least 2 levels", g.name);
		}
		if (static_cast<intptr_t>(g.indices.size()) != n) {
			throw error("Group index vector length for '%' does not match number of observations", g.name);
		}
	}

	if (n <= p) {
		throw error("Not enough observations to fit model (% obs, % parameters)", n, p);
	}

	auto lay = GroupLayout::build(groups);
	intptr_t G = lay.G;

	Eigen::Map<Matrix<double>> Xm(const_cast<double *>(X.data()), n, p);
	Eigen::Map<Vector<double>> ym(const_cast<double *>(y.data()), n);

	// ── Starting values ─────────────────────────────────────────────
	//
	// Fixed effects β are initialized from a preliminary OLS/GLM fit.
	// Variance components are initialized via method-of-moments (ANOVA-based
	// variance decomposition) on the residuals of that fit, giving each
	// grouping factor its own starting variance rather than a single blanket
	// value.  This brings the optimizer much closer to the true optimum and
	// reduces sensitivity to the choice of starting point.
	//
	// For Gaussian models: ANOVA on OLS residuals.
	// For non-Gaussian:    ANOVA on working residuals (Pearson residuals
	//                      scaled by the link derivative, i.e. (y−μ)/V(μ)).

	Model fe;
	if (is_gaussian) {
		fe = lm(y, X);
	} else if (fam.name == "negbin") {
		// For NB, use Poisson as starting fit (glm() uses canonical-link
		// gradient which is incorrect for NB's non-canonical log link).
		fe = glm(y, X, Family::poisson());
	} else if (fam.name == "beta") {
		// For beta, use binomial as starting fit (same logit link).
		fe = glm(y, X, Family::binomial());
	} else {
		fe = glm(y, X, fam);
	}

	intptr_t outer_dim = is_gaussian ? (p + G + 1) : (p + G);
	Eigen::VectorXd phi = Eigen::VectorXd::Zero(outer_dim);

	for (intptr_t i = 0; i < p; i++) {
		phi[i] = fe.beta[i + 1];
	}

	if (is_gaussian)
	{
		// OLS residuals → per-factor ANOVA variance decomposition.
		Eigen::VectorXd resid(n);
		for (intptr_t i = 0; i < n; i++) {
			resid[i] = fe.residuals[i + 1];
		}
		double total_var = resid.squaredNorm() / std::max(n - p, (intptr_t)1);

		double sum_sigma2_u = 0;
		for (intptr_t g = 0; g < G; g++)
		{
			double s2 = anova_variance_component(resid, groups[g].indices,
			                                      groups[g].nlevels, n);
			// Floor at 1% of total variance to keep log(σ) finite and reasonable.
			s2 = std::max(s2, 0.01 * total_var);
			phi[p + g] = 0.5 * std::log(s2);   // log(σ_u_g)
			sum_sigma2_u += s2;
		}

		// Residual σ²: total − Σ σ²_u, floored at 10% of total.
		double sigma2_resid = std::max(total_var - sum_sigma2_u, 0.1 * total_var);
		phi[p + G] = 0.5 * std::log(sigma2_resid);
	}
	else
	{
		// Working residuals on the link scale: r_i = (y_i − μ̂_i) / V(μ̂_i).
		// For canonical links this equals the derivative of the deviance w.r.t. η,
		// giving residuals whose between-group variance approximates σ²_u on the
		// linear predictor scale.
		Eigen::VectorXd mu_fe(n);
		for (intptr_t i = 0; i < n; i++) {
			mu_fe[i] = fe.fitted[i + 1];
		}
		Eigen::VectorXd V = fam.variance(mu_fe);

		Eigen::VectorXd wr(n);
		for (intptr_t i = 0; i < n; i++)
		{
			double v = std::max(V[i], 1e-8);
			wr[i] = (ym[i] - mu_fe[i]) / v;
		}

		for (intptr_t g = 0; g < G; g++)
		{
			double s2 = anova_variance_component(wr, groups[g].indices,
			                                      groups[g].nlevels, n);
			// Floor at a small positive value; no upper clamp — large random
			// effects (complete separation within groups) are common in logistic models.
			s2 = std::max(s2, 0.01);
			phi[p + g] = 0.5 * std::log(s2);
		}
	}

	// ── Outer optimisation ──────────────────────────────────────────
	//
	// Gaussian:     β is profiled out — Newton's method searches only over
	//               θ = (log σ_u_1..G, log σ), dimension G+1.
	//               For each θ, the joint mode (β̂,û) is found by
	//               solve_profiled_gaussian.  This matches the profiling
	//               strategy used by lme4/glmmTMB internally.
	//
	// Non-Gaussian: L-BFGS searches over φ = (β, log σ_u_1..G),
	//               dimension p+G (original approach).

	Eigen::VectorXd beta_hat;
	Eigen::VectorXd sigma2_u(G);
	std::vector<Eigen::MatrixXd> D_inv_final;  // converged prior precision matrices (for SE computation)
	std::vector<double> log_det_Dg_final;       // converged log|D_g| values
	double sigma2 = 0;
	int niter = 0;
	bool converged = true;
	Family fam_used = fam;  // mutable copy; updated with fitted θ_nb for negative binomial

	if (is_gaussian)
	{
		intptr_t n_chol = total_chol_params(lay);
		intptr_t outer_dim_gauss = n_chol + 1; // Cholesky params + log σ

		GaussianCholObjective gauss_obj{Xm, ym, lay, n, p, n_chol};

		// ── Initialize Cholesky from ANOVA variance estimates ──
		Eigen::VectorXd theta(outer_dim_gauss);
		{
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				double s2_init = std::exp(2.0 * phi[p + g]);
				for (intptr_t r = 0; r < qg; r++)
				{
					for (intptr_t c = 0; c <= r; c++)
					{
						intptr_t idx = chol_pos + r * (r + 1) / 2 + c;
						if (r == c) {
							double var_init = (r == 0) ? s2_init : s2_init * 0.1;
							theta[idx] = 0.5 * std::log(std::max(var_init, 1e-4));
						} else {
							theta[idx] = 0.0;
						}
					}
				}
				chol_pos += np;
			}
		}
		theta[n_chol] = phi[p + G]; // log σ

		auto newton_res = newton_optimize(gauss_obj, theta, 200, 1e-8, progress);
		theta = newton_res.theta;
		niter = newton_res.niter;
		converged = newton_res.converged;

		// ── Unpack converged Cholesky → D_inv, sigma2_u, sigma2 ──
		D_inv_final.resize(G);
		log_det_Dg_final.resize(G);
		{
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
				Eigen::MatrixXd D = cholesky_to_cov(L);
				D_inv_final[g] = cholesky_to_precision(L);
				log_det_Dg_final[g] = log_det_D(theta.data() + chol_pos, qg);
				sigma2_u[g] = D(0, 0);
				chol_pos += n_chol_params(qg);
			}
		}
		sigma2 = std::exp(2.0 * theta[n_chol]);

		auto final_gauss = solve_gaussian_henderson(D_inv_final, log_det_Dg_final, sigma2,
		                                             Xm, ym, lay, n, p);
		beta_hat = final_gauss.beta;
	}
	else
	{
		// Non-Gaussian: PIRLS profiling — Newton over θ = (chol_1, ..., chol_G, [log disp]).
		// β is concentrated out via PIRLS at each θ evaluation.
		// Outer dimension: Σ q_g(q_g+1)/2 + (1 if NB or Beta).
		bool is_nb = (fam.name == "negbin");
		bool is_beta = (fam.name == "beta");
		bool has_disp = (is_nb || is_beta);
		intptr_t n_chol = total_chol_params(lay);
		intptr_t outer_dim_pirls = has_disp ? (n_chol + 1) : n_chol;

		Eigen::VectorXd beta_init = phi.head(p);

		// ── Initialize Cholesky parameters ────────────────────────────
		Eigen::VectorXd theta(outer_dim_pirls);

		// For NB with random slopes, the ANOVA-based initialization puts
		// the slope variances far from the NB optimum (the NB overdispersion
		// absorbs variance that Poisson attributes to random effects).
		// A much better starting point: fit a Poisson mixed model with the
		// same random structure and use its converged Cholesky parameters.
		bool has_slopes = false;
		for (intptr_t g = 0; g < G; g++) {
			if (lay.q[g] > 1) { has_slopes = true; break; }
		}

		if (is_nb && has_slopes)
		{
			auto pois_model = mixed_model(y, X, groups, Family::poisson());

			// Use Poisson β as starting fixed effects
			for (intptr_t i = 0; i < p; i++) {
				beta_init[i] = pois_model.beta[i + 1];
			}

			// Use Poisson Cholesky as starting outer theta
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				auto &re = pois_model.random_effects[g + 1]; // 1-based Array

				for (intptr_t r = 0; r < qg; r++)
				{
					for (intptr_t c = 0; c <= r; c++)
					{
						intptr_t pack_idx = r * (r + 1) / 2 + c;
						double val = re.cov_chol[pack_idx + 1]; // 1-based Array
						if (r == c) {
							theta[chol_pos + pack_idx] = std::log(std::max(val, 1e-6));
						} else {
							theta[chol_pos + pack_idx] = val;
						}
					}
				}
				chol_pos += np;
			}
		}
		else
		{
			// Standard initialization: ANOVA variance decomposition.
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				double s2_init = std::exp(2.0 * phi[p + g]);
				for (intptr_t r = 0; r < qg; r++)
				{
					for (intptr_t c = 0; c <= r; c++)
					{
						intptr_t idx = chol_pos + r * (r + 1) / 2 + c;
						if (r == c) {
							double var_init = (r == 0) ? s2_init : s2_init * 0.1;
							theta[idx] = 0.5 * std::log(std::max(var_init, 1e-4));
						} else {
							theta[idx] = 0.0;
						}
					}
				}
				chol_pos += np;
			}
		}

		if (is_nb)
		{
			// Initialize θ_nb from method-of-moments
			double ybar = ym.mean();
			double yvar = (ym.array() - ybar).square().sum() / std::max(n - 1, (intptr_t)1);
			double theta_nb_init = (yvar > ybar) ? ybar * ybar / (yvar - ybar) : 10.0;
			theta_nb_init = std::clamp(theta_nb_init, 0.01, 1e6);
			theta[n_chol] = std::log(theta_nb_init);
		}
		else if (is_beta)
		{
			// Initialize φ from method-of-moments: φ = μ̄(1-μ̄)/Var(y) - 1
			double ybar = ym.mean();
			double yvar = (ym.array() - ybar).square().sum() / std::max(n - 1, (intptr_t)1);
			double phi_init = 10.0;
			if (yvar > 0 && yvar < ybar * (1.0 - ybar)) {
				phi_init = ybar * (1.0 - ybar) / yvar - 1.0;
			}
			phi_init = std::clamp(phi_init, 0.1, 1e6);
			theta[n_chol] = std::log(phi_init);
		}

		// Create PirlsObjective after beta_init is finalized

		PirlsObjective pirls_obj{fam, Xm, ym, lay, n, p, beta_init, n_chol};

		auto newton_res = newton_optimize(pirls_obj, theta, 200, 1e-8, progress, 1e-2);
		theta = newton_res.theta;
		niter = newton_res.niter;
		converged = newton_res.converged;

		// ── Phase 2: joint (β, θ) optimization ─────────────────
		// For non-Gaussian, the Laplace log-det depends on β through
		// the working weights W = μ(1−μ). Phase 1 PIRLS profiles β
		// out ignoring ∂log|H|/∂β; Phase 2 re-optimizes (β, θ)
		// jointly with u profiled out, matching lme4/glmmTMB.
		{
			// Get Phase 1 β̂ via one PIRLS call at converged θ
			std::vector<Eigen::MatrixXd> D_inv_p1(G);
			std::vector<double> log_det_p1(G);
			intptr_t cp = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				Eigen::MatrixXd L = unpack_cholesky(theta.data() + cp, qg);
				D_inv_p1[g] = cholesky_to_precision(L);
				log_det_p1[g] = log_det_D(theta.data() + cp, qg);
				cp += np;
			}

			Family fam_p1 = fam;
			if (is_nb)
				fam_p1 = Family::negbin(std::exp(theta[n_chol]));
			else if (is_beta)
				fam_p1 = Family::beta(std::exp(theta[n_chol]));

			auto p1_pirls = solve_pirls(D_inv_p1, log_det_p1, fam_p1,
			                             Xm, ym, lay, n, p, beta_init);

			// Build Phase 2 parameter vector: [β, θ]
			intptr_t outer_dim2 = p + (intptr_t)theta.size();
			Eigen::VectorXd phi2(outer_dim2);
			phi2.head(p) = p1_pirls.beta;
			phi2.tail(theta.size()) = theta;

			LaplaceJointObjective joint_obj{fam, Xm, ym, lay, n, p, n_chol};
			joint_obj.last_u = std::move(p1_pirls.u);

			auto res2 = newton_optimize(joint_obj, phi2, 200, 1e-8, progress, 1e-2);

			// Update β and θ from Phase 2
			beta_hat = res2.theta.head(p);
			theta = Eigen::VectorXd(res2.theta.tail(theta.size()));
			niter += res2.niter;
			converged = res2.converged;
		}

		// ── Unpack converged Cholesky → D_inv, sigma2_u, log_det_Dg ──
		D_inv_final.resize(G);
		log_det_Dg_final.resize(G);
		{
			intptr_t chol_pos = 0;
			for (intptr_t g = 0; g < G; g++)
			{
				intptr_t qg = lay.q[g];
				intptr_t np = n_chol_params(qg);
				Eigen::MatrixXd L = unpack_cholesky(theta.data() + chol_pos, qg);
				Eigen::MatrixXd D = cholesky_to_cov(L);
				D_inv_final[g] = cholesky_to_precision(L);
				log_det_Dg_final[g] = log_det_D(theta.data() + chol_pos, qg);
				// For backward compat, store the intercept variance in sigma2_u
				sigma2_u[g] = D(0, 0);
				chol_pos += np;
			}
		}

		// For NB, update the Family with the converged θ_nb
		if (is_nb) {
			double theta_nb = std::exp(theta[n_chol]);
			fam_used = Family::negbin(theta_nb);
		}
		// For beta, update the Family with the converged φ
		else if (is_beta) {
			double phi_beta = std::exp(theta[n_chol]);
			fam_used = Family::beta(phi_beta);
		}

		// beta_hat already set by Phase 2

		// Assemble full φ = (β̂, θ̂) for SE computation
		phi.resize(outer_dim);
		phi.head(p) = beta_hat;
		for (intptr_t g = 0; g < G; g++) {
			phi[p + g] = theta[g];
		}
	}

	// ── Final result at converged estimates ──────────────────────────
	//
	// Gaussian: solve_inner (diagonal Newton converges exactly for identity link).
	// Non-Gaussian: re-run PIRLS (its Henderson system solve is more accurate
	//               than solve_inner's diagonal Newton for the u mode).

	InnerResult final_inner;

	if (is_gaussian)
	{
		auto gauss_final = solve_gaussian_henderson(D_inv_final, log_det_Dg_final, sigma2,
		                                            Xm, ym, lay, n, p);
		final_inner.u = std::move(gauss_final.u);
		final_inner.mu = std::move(gauss_final.mu);
		final_inner.laplace_nll = gauss_final.laplace_nll;
	}
	else
	{
		// Use solve_u_given_beta to ensure β̂ from Phase 2 is not modified
		auto pirls_final = solve_u_given_beta(D_inv_final, log_det_Dg_final, fam_used,
		                                       Xm, ym, lay, n, p, beta_hat);
		final_inner.u = std::move(pirls_final.u);
		final_inner.mu = std::move(pirls_final.mu);
		final_inner.laplace_nll = pirls_final.laplace_nll;
	}

	// ── Standard errors ─────────────────────────────────────────────
	//
	// Conditional Var(β̂ | θ̂) from the inverse of the Henderson matrix:
	//   C = [X'WX     X'WZ       ]
	//       [Z'WX   Z'WZ + D⁻¹   ]
	// For Gaussian: W = (1/σ²)I.
	// For non-Gaussian: W = diag(w_i) with w_i = (dμ/dη)²/V(μ).
	// The top-left p×p block of C⁻¹ is the conditional covariance
	// of β̂.  This is what lme4/glmmTMB report.

	Eigen::MatrixXd vcov;

	{
		Eigen::VectorXd w_se(n);
		if (is_gaussian)
		{
			w_se.setConstant(1.0 / sigma2);
		}
		else
		{
			Eigen::VectorXd V_se = fam_used.variance(final_inner.mu);
			Eigen::VectorXd me_se = fam_used.mu_eta(final_inner.mu);
			for (intptr_t i = 0; i < n; i++) {
				double v = std::max(V_se[i], 1e-10);
				double d = std::max(me_se[i], 1e-10);
				w_se[i] = d * d / v;
			}
		}

		intptr_t J = lay.J_total;
		intptr_t sdim = p + J;

		Eigen::MatrixXd C = Eigen::MatrixXd::Zero(sdim, sdim);

		// X'WX (p × p)
		for (intptr_t i = 0; i < n; i++)
		{
			for (intptr_t j1 = 0; j1 < p; j1++)
			{
				double wx = Xm(i, j1) * w_se[i];
				for (intptr_t j2 = j1; j2 < p; j2++) {
					C(j1, j2) += wx * Xm(i, j2);
				}
			}
		}
		for (intptr_t j1 = 0; j1 < p; j1++) {
			for (intptr_t j2 = j1 + 1; j2 < p; j2++) {
				C(j2, j1) = C(j1, j2);
			}
		}

		// D⁻¹ blocks
		for (intptr_t g = 0; g < G; g++)
		{
			intptr_t qg = lay.q[g];
			auto &Dinv_g = D_inv_final[g];
			for (intptr_t j = 0; j < lay.J[g]; j++)
			{
				intptr_t base = p + lay.offset[g] + j * qg;
				for (intptr_t t1 = 0; t1 < qg; t1++) {
					for (intptr_t t2 = 0; t2 < qg; t2++) {
						C(base + t1, base + t2) += Dinv_g(t1, t2);
					}
				}
			}
		}

		// X'WZ, Z'WX, Z'WZ
		for (intptr_t i = 0; i < n; i++)
		{
			for (intptr_t g1 = 0; g1 < G; g1++)
			{
				intptr_t j1 = groups[g1].indices[i];
				intptr_t q1 = lay.q[g1];
				intptr_t base1 = p + lay.offset[g1] + j1 * q1;

				for (intptr_t t = 0; t < q1; t++)
				{
					double z_val = lay.Z(g1, i, t);
					double wz_val = w_se[i] * z_val;

					for (intptr_t j = 0; j < p; j++)
					{
						double val = Xm(i, j) * wz_val;
						C(j, base1 + t) += val;
						C(base1 + t, j) += val;
					}
				}

				// Z'WZ within-group
				for (intptr_t t1 = 0; t1 < q1; t1++)
				{
					double wz1 = w_se[i] * lay.Z(g1, i, t1);
					for (intptr_t t2 = t1; t2 < q1; t2++)
					{
						double val = wz1 * lay.Z(g1, i, t2);
						C(base1 + t1, base1 + t2) += val;
						if (t1 != t2) C(base1 + t2, base1 + t1) += val;
					}
				}

				// Z'WZ cross-group
				for (intptr_t g2 = g1 + 1; g2 < G; g2++)
				{
					intptr_t j2 = groups[g2].indices[i];
					intptr_t q2 = lay.q[g2];
					intptr_t base2 = p + lay.offset[g2] + j2 * q2;

					for (intptr_t t1 = 0; t1 < q1; t1++)
					{
						double wz1 = w_se[i] * lay.Z(g1, i, t1);
						for (intptr_t t2 = 0; t2 < q2; t2++)
						{
							double val = wz1 * lay.Z(g2, i, t2);
							C(base1 + t1, base2 + t2) += val;
							C(base2 + t2, base1 + t1) += val;
						}
					}
				}
			}
		}

		// Var(β̂) = top-left p×p block of C⁻¹
		Eigen::LDLT<Eigen::MatrixXd> ldlt(C);
		Eigen::MatrixXd Cinv = ldlt.solve(Eigen::MatrixXd::Identity(sdim, sdim));
		vcov = Cinv.topLeftCorner(p, p);
	}

	// ── Build the Model ─────────────────────────────────────────────

	Model model;
	model.family = fam.name;
	model.link = fam.link_name;
	model.theta = fam_used.theta;
	model.phi = fam_used.phi;
	model.nobs = n;
	model.nfixed = p;
	model.y = y;
	model.X = X;

	model.beta = Array<double>(p, 0.0);
	model.se = Array<double>(p, 0.0);
	model.stat = Array<double>(p, 0.0);
	model.p = Array<double>(p, 0.0);

	boost::math::normal_distribution<double> normal;

	for (intptr_t i = 1; i <= p; i++)
	{
		model.beta[i] = beta_hat[i - 1];
		double var_i = vcov(i - 1, i - 1);
		model.se[i] = (var_i > 0) ? std::sqrt(var_i) : 0.0;
		model.stat[i] = (model.se[i] > 0) ? model.beta[i] / model.se[i] : 0.0;
		model.p[i] = 2.0 * (1.0 - boost::math::cdf(normal, std::abs(model.stat[i])));
	}

	// Store full variance-covariance matrix of fixed effects
	{
		model.vcov = Array<double>(p, p, 0.0);
		for (intptr_t i = 0; i < p; i++) {
			for (intptr_t j = 0; j < p; j++) {
				model.vcov(i + 1, j + 1) = vcov(i, j);
			}
		}
	}

	model.fitted = Array<double>(n, 0.0);
	model.residuals = Array<double>(n, 0.0);
	for (intptr_t i = 0; i < n; i++)
	{
		model.fitted[i + 1] = final_inner.mu[i];
		model.residuals[i + 1] = ym[i] - final_inner.mu[i];
	}

	if (is_gaussian)
	{
		model.rse = std::sqrt(sigma2);
		model.df_residual = n - p;
	}

	for (intptr_t g = 0; g < G; g++)
	{
		RandomEffectGroup reg;
		reg.group_name = groups[g].name;
		reg.nlevels = groups[g].nlevels;
		reg.level_names = groups[g].levels;

		intptr_t qg = lay.q[g];

		// Term names from the GroupingInfo (e.g. "(Intercept)", "vowel[i]", "vowel[u]")
		reg.term_names = groups[g].term_names;

		// Covariance matrix D_g: from D_inv_final (non-Gaussian/Cholesky path)
		// or from scalar sigma2_u (Gaussian intercept-only path).
		Eigen::MatrixXd D_g;
		if (!D_inv_final.empty())
		{
			// Invert D_inv to get D (small matrix, at most q × q)
			D_g = D_inv_final[g].inverse();
		}
		else
		{
			// Gaussian scalar path: diagonal covariance
			D_g = Eigen::MatrixXd::Zero(qg, qg);
			D_g(0, 0) = sigma2_u[g];
		}

		// Variance for each term (diagonal of D_g)
		for (intptr_t t = 0; t < qg; t++) {
			reg.variance.append(D_g(t, t));
		}

		// Cholesky factor (packed lower triangle) for the covariance display
		Eigen::LLT<Eigen::MatrixXd> llt(D_g);
		Eigen::MatrixXd L_out = llt.matrixL();
		for (intptr_t r = 0; r < qg; r++) {
			for (intptr_t c = 0; c <= r; c++) {
				reg.cov_chol.append(L_out(r, c));
			}
		}

		// Conditional modes: u[j*q + t] for each level j and term t
		for (intptr_t j = 0; j < lay.J[g]; j++) {
			for (intptr_t t = 0; t < qg; t++) {
				reg.conditional_modes.append(final_inner.u[lay.offset[g] + j * qg + t]);
			}
		}

		// Z design info for simulation-based diagnostics (DHARMa-style scaled residuals).
		// This is transient fitting-time data; not serialised.
		reg.nterms = groups[g].nterms;
		reg.indices = groups[g].indices;
		reg.Z_design = groups[g].Z_design;

		model.random_effects.append(std::move(reg));
	}

	model.loglik = -final_inner.laplace_nll;
	model.compute_information_criteria();

	model.niter = niter;
	model.converged = converged;

	return model;
}

} // namespace phonometrica::stats
