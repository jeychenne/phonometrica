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
 * Created: 05/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/students_t.hpp>
#include <phon/analysis/emmeans.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica::stats {

// =====================================================================
// Internal helpers
// =====================================================================

namespace {

// A parsed component of a coefficient name.
// E.g. "vowel[i]" → {variable="vowel", level="i"}
//      "duration" → {variable="duration", level=""}
struct CoefComponent
{
	std::string variable;
	std::string level;  // empty for numeric
	bool is_categorical() const { return !level.empty(); }
};


// Parse a coefficient name into its interaction components.
// "vowel[i]:context[CC]" → [{vowel, i}, {context, CC}]
// "(Intercept)" → returns empty vector (special-cased by caller)
static std::vector<CoefComponent> parse_coef_name(const String &name)
{
	std::string s(name.data(), name.size());
	std::vector<CoefComponent> result;

	// Split on ':'
	size_t pos = 0;
	while (pos < s.size())
	{
		size_t colon = s.find(':', pos);
		std::string part;
		if (colon == std::string::npos) {
			part = s.substr(pos);
			pos = s.size();
		}
		else {
			part = s.substr(pos, colon - pos);
			pos = colon + 1;
		}

		CoefComponent c;
		auto bracket = part.find('[');
		if (bracket != std::string::npos)
		{
			c.variable = part.substr(0, bracket);
			// Extract level: strip '[' and ']'
			c.level = part.substr(bracket + 1, part.size() - bracket - 2);
		}
		else
		{
			c.variable = part;
		}
		result.push_back(std::move(c));
	}

	return result;
}


// Find a VariableInfo by name. Returns nullptr if not found.
static const Model::VariableInfo *find_var_info(const Model &model, const std::string &name)
{
	for (intptr_t i = 1; i <= model.variable_info.size(); i++)
	{
		auto &vi = model.variable_info[i];
		if (std::string(vi.name.data(), vi.name.size()) == name) {
			return &vi;
		}
	}
	return nullptr;
}


// Compute column means for the first p columns of model.X.
static std::vector<double> compute_column_means(const Model &model, intptr_t p)
{
	intptr_t n = model.nobs;
	std::vector<double> means(p, 0.0);

	for (intptr_t j = 0; j < p; j++)
	{
		double sum = 0.0;
		for (intptr_t i = 1; i <= n; i++) {
			sum += model.X(i, j + 1);
		}
		means[j] = sum / n;
	}

	return means;
}


// Metadata for a parsed coefficient column, precomputed once per model.
struct ParsedCoef
{
	std::vector<CoefComponent> components;
	bool is_intercept = false;
};


// Preparse the first p coefficient names.
static std::vector<ParsedCoef> preparse_coefs(const Model &model, intptr_t p)
{
	std::vector<ParsedCoef> parsed(p);

	for (intptr_t j = 0; j < p; j++)
	{
		auto &name = model.coef_names[j + 1];
		std::string s(name.data(), name.size());

		if (s == "(Intercept)") {
			parsed[j].is_intercept = true;
		}
		else {
			parsed[j].components = parse_coef_name(name);
		}
	}

	return parsed;
}


// Find the column index (0-based) of a numeric variable's main-effect column.
// Returns -1 if not found.
static intptr_t find_numeric_main_column(const std::vector<ParsedCoef> &parsed,
                                         const std::string &variable, intptr_t p)
{
	for (intptr_t k = 0; k < p; k++)
	{
		auto &pc = parsed[k];
		if (!pc.is_intercept && pc.components.size() == 1 &&
		    !pc.components[0].is_categorical() &&
		    pc.components[0].variable == variable)
		{
			return k;
		}
	}
	return -1;
}

} // anonymous namespace


// =====================================================================
// emmeans()
// =====================================================================

EMMResult emmeans(const Model &model, const String &factor, double conf_level)
{
	if (!model.has_vcov()) {
		throw error("Model has no variance-covariance matrix (required for EMMs)");
	}
	if (!model.has_variable_info()) {
		throw error("Model has no variable metadata (required for EMMs)");
	}

	// Find the target factor in variable_info.
	std::string factor_key(factor.data(), factor.size());
	const Model::VariableInfo *target_info = find_var_info(model, factor_key);

	if (!target_info) {
		throw error("Variable '%' not found in model", factor);
	}
	if (target_info->numeric) {
		throw error("Variable '%' is numeric; EMMs require a categorical factor", factor);
	}
	if (target_info->levels.size() < 2) {
		throw error("Variable '%' has fewer than 2 levels", factor);
	}

	intptr_t K = target_info->levels.size();  // total levels including reference
	intptr_t p = model.nfixed;                // parametric coefficients only

	// Precompute column means (for numeric covariates).
	auto col_means = compute_column_means(model, p);

	// Preparse coefficient names.
	auto parsed = preparse_coefs(model, p);

	// ── Build the L matrix: K rows × p columns ──────────────────────
	//
	// For each level of the target factor, each coefficient column contributes:
	//   - Intercept: 1
	//   - Main effect of target factor: indicator (1 if level matches, 0 otherwise)
	//   - Main effect of other categorical factor: 1/K_other (balanced average)
	//   - Main effect of numeric covariate: its column mean
	//   - Interaction: product of the component contributions above

	Eigen::MatrixXd L = Eigen::MatrixXd::Zero(K, p);

	for (intptr_t lv = 0; lv < K; lv++)
	{
		std::string target_level(target_info->levels[lv + 1].data(),
		                         target_info->levels[lv + 1].size());

		for (intptr_t j = 0; j < p; j++)
		{
			auto &pc = parsed[j];

			if (pc.is_intercept) {
				L(lv, j) = 1.0;
				continue;
			}
			if (pc.components.empty()) {
				continue;
			}

			// Compute contribution as product of component contributions.
			double value = 1.0;

			for (auto &comp : pc.components)
			{
				if (comp.variable == factor_key)
				{
					// Target factor: indicator for this level.
					if (comp.is_categorical())
					{
						value *= (comp.level == target_level) ? 1.0 : 0.0;
					}
					else
					{
						// Numeric variable with the same name as the factor (shouldn't happen).
						value *= col_means[j];
					}
				}
				else if (comp.is_categorical())
				{
					// Other categorical factor: balanced average over its levels.
					// Each dummy column gets weight 1/K_other.
					auto *other_info = find_var_info(model, comp.variable);
					if (other_info && !other_info->numeric) {
						value *= 1.0 / other_info->levels.size();
					}
					else {
						// Fallback: use column mean.
						value *= col_means[j];
					}
				}
				else
				{
					// Numeric covariate: use its main-effect column mean.
					intptr_t main_col = find_numeric_main_column(parsed, comp.variable, p);
					if (main_col >= 0) {
						value *= col_means[main_col];
					}
					else {
						// Variable not found as a main effect; use this column's mean.
						value *= col_means[j];
					}
				}
			}

			L(lv, j) = value;
		}
	}

	// ── Compute EMMs on link scale ──────────────────────────────────

	Eigen::Map<const Vector<double>> beta(model.beta.data(), p);

	Eigen::VectorXd emm_link = L * beta;

	// V_eta = L V L'  (covariance of link-scale EMMs)
	// Use the top-left p×p block of model.vcov (column-major Array).
	Eigen::Map<const Matrix<double>> V_full(model.vcov.data(), model.vcov.nrow(), model.vcov.ncol());
	auto V = V_full.topLeftCorner(p, p);

	Eigen::MatrixXd V_eta = L * V * L.transpose();

	Eigen::VectorXd se_link(K);
	for (intptr_t i = 0; i < K; i++) {
		se_link[i] = std::sqrt(std::max(V_eta(i, i), 0.0));
	}

	// ── Degrees of freedom and critical value ───────────────────────

	double df = std::numeric_limits<double>::infinity();
	if (model.is_gaussian() && !model.has_random_effects()) {
		df = model.df_residual;
	}

	double alpha = 1.0 - conf_level;
	double crit;
	if (std::isfinite(df) && df > 0) {
		boost::math::students_t_distribution<double> tdist(df);
		crit = boost::math::quantile(tdist, 1.0 - alpha / 2.0);
	}
	else {
		boost::math::normal_distribution<double> ndist;
		crit = boost::math::quantile(ndist, 1.0 - alpha / 2.0);
	}

	// ── Populate result ─────────────────────────────────────────────

	EMMResult result;
	result.factor = factor;
	result.df = df;
	result.levels = target_info->levels;

	result.emmean_link = Array<double>(K, 0.0);
	result.se_link     = Array<double>(K, 0.0);
	result.lower_link  = Array<double>(K, 0.0);
	result.upper_link  = Array<double>(K, 0.0);
	result.emmean      = Array<double>(K, 0.0);
	result.se          = Array<double>(K, 0.0);
	result.lower_ci    = Array<double>(K, 0.0);
	result.upper_ci    = Array<double>(K, 0.0);

	// Store link-scale covariance for pairwise_contrasts().
	result.cov_link = Array<double>(K, K, 0.0);
	for (intptr_t i = 0; i < K; i++) {
		for (intptr_t j = 0; j < K; j++) {
			result.cov_link(i + 1, j + 1) = V_eta(i, j);
		}
	}

	bool is_identity = (model.link == "identity");

	// Get the family for inverse-link transformation.
	Family fam = Family::from_name(model.family);
	if (model.is_negbin()) {
		fam = Family::negbin(model.theta);
	}

	for (intptr_t i = 0; i < K; i++)
	{
		result.emmean_link[i + 1] = emm_link[i];
		result.se_link[i + 1]     = se_link[i];
		result.lower_link[i + 1]  = emm_link[i] - crit * se_link[i];
		result.upper_link[i + 1]  = emm_link[i] + crit * se_link[i];

		if (is_identity)
		{
			result.emmean[i + 1]    = emm_link[i];
			result.se[i + 1]        = se_link[i];
			result.lower_ci[i + 1]  = result.lower_link[i + 1];
			result.upper_ci[i + 1]  = result.upper_link[i + 1];
		}
		else
		{
			// Response scale via the delta method for SE,
			// and endpoint back-transformation for CI.
			//
			//   μ̂ = g⁻¹(η̂)
			//   SE_μ ≈ |dμ/dη| × SE_η
			//   CI_μ = [g⁻¹(η̂ − z·SE_η), g⁻¹(η̂ + z·SE_η)]
			//
			// Endpoint transformation is more accurate than the delta method
			// for CIs, especially for binomial/Poisson models far from the mean.

			Eigen::VectorXd eta_vec(1);
			eta_vec[0] = emm_link[i];
			Eigen::VectorXd mu_vec = fam.linkinv(eta_vec);
			double mu = mu_vec[0];
			double dmu = fam.mu_eta(mu_vec)[0]; // dμ/dη evaluated at μ

			result.emmean[i + 1] = mu;
			result.se[i + 1]     = std::abs(dmu) * se_link[i];

			// Back-transform CI endpoints.
			eta_vec[0] = result.lower_link[i + 1];
			result.lower_ci[i + 1] = fam.linkinv(eta_vec)[0];
			eta_vec[0] = result.upper_link[i + 1];
			result.upper_ci[i + 1] = fam.linkinv(eta_vec)[0];
		}
	}

	return result;
}


// =====================================================================
// emtrends()
// =====================================================================

EMMResult emtrends(const Model &model, const String &factor, const String &var,
                   double conf_level)
{
	if (!model.has_vcov()) {
		throw error("Model has no variance-covariance matrix (required for emtrends)");
	}
	if (!model.has_variable_info()) {
		throw error("Model has no variable metadata (required for emtrends)");
	}

	// Validate the factor (must be categorical).
	std::string factor_key(factor.data(), factor.size());
	const Model::VariableInfo *target_info = find_var_info(model, factor_key);

	if (!target_info) {
		throw error("Variable '%' not found in model", factor);
	}
	if (target_info->numeric) {
		throw error("Variable '%' is numeric; emtrends requires a categorical factor", factor);
	}
	if (target_info->levels.size() < 2) {
		throw error("Variable '%' has fewer than 2 levels", factor);
	}

	// Validate the trend variable (must be numeric).
	std::string var_key(var.data(), var.size());
	const Model::VariableInfo *var_info = find_var_info(model, var_key);

	if (!var_info) {
		throw error("Variable '%' not found in model", var);
	}
	if (!var_info->numeric) {
		throw error("Variable '%' is categorical; emtrends requires a numeric trend variable", var);
	}

	intptr_t K = target_info->levels.size();
	intptr_t p = model.nfixed;

	auto col_means = compute_column_means(model, p);
	auto parsed = preparse_coefs(model, p);

	// ── Build the L matrix for slopes (dη/d(var)) ───────────────────
	//
	// For each level of the target factor, each coefficient column contributes:
	//   - Intercept: 0  (constant w.r.t. the trend variable)
	//   - Column NOT involving the trend variable: 0
	//   - Main effect of the trend variable: 1
	//   - Interaction involving the trend variable:
	//       For the trend variable component: 1 (derivative)
	//       For the target factor component: indicator for current level
	//       For other categorical components: 1/K_other (balanced)
	//       For other numeric components: column mean

	Eigen::MatrixXd L = Eigen::MatrixXd::Zero(K, p);

	for (intptr_t lv = 0; lv < K; lv++)
	{
		std::string target_level(target_info->levels[lv + 1].data(),
		                         target_info->levels[lv + 1].size());

		for (intptr_t j = 0; j < p; j++)
		{
			auto &pc = parsed[j];

			// Intercept: always 0 for trends.
			if (pc.is_intercept) continue;
			if (pc.components.empty()) continue;

			// Check if this column involves the trend variable.
			bool involves_trend = false;
			for (auto &comp : pc.components) {
				if (comp.variable == var_key) {
					involves_trend = true;
					break;
				}
			}

			// Columns that don't involve the trend variable contribute 0.
			if (!involves_trend) continue;

			// This column involves the trend variable.
			// Compute the product of contributions from all NON-trend components.
			// The trend variable's own contribution is 1 (derivative).
			double value = 1.0;

			for (auto &comp : pc.components)
			{
				if (comp.variable == var_key)
				{
					// Trend variable itself: derivative = 1.
					// (value *= 1.0, no-op)
					continue;
				}

				if (comp.variable == factor_key)
				{
					// Target factor: indicator for this level.
					if (comp.is_categorical()) {
						value *= (comp.level == target_level) ? 1.0 : 0.0;
					}
				}
				else if (comp.is_categorical())
				{
					// Other categorical factor: balanced average.
					auto *other_info = find_var_info(model, comp.variable);
					if (other_info && !other_info->numeric) {
						value *= 1.0 / other_info->levels.size();
					} else {
						value *= col_means[j];
					}
				}
				else
				{
					// Other numeric covariate: use its main-effect column mean.
					intptr_t main_col = find_numeric_main_column(parsed, comp.variable, p);
					if (main_col >= 0) {
						value *= col_means[main_col];
					} else {
						value *= col_means[j];
					}
				}
			}

			L(lv, j) = value;
		}
	}

	// ── Compute slopes and SEs on the link scale ────────────────────

	Eigen::Map<const Vector<double>> beta(model.beta.data(), p);
	Eigen::VectorXd trends = L * beta;

	Eigen::Map<const Matrix<double>> V_full(model.vcov.data(), model.vcov.nrow(), model.vcov.ncol());
	auto V = V_full.topLeftCorner(p, p);
	Eigen::MatrixXd V_eta = L * V * L.transpose();

	Eigen::VectorXd se(K);
	for (intptr_t i = 0; i < K; i++) {
		se[i] = std::sqrt(std::max(V_eta(i, i), 0.0));
	}

	// ── Degrees of freedom and critical value ───────────────────────

	double df = std::numeric_limits<double>::infinity();
	if (model.is_gaussian() && !model.has_random_effects()) {
		df = model.df_residual;
	}

	double alpha = 1.0 - conf_level;
	double crit;
	if (std::isfinite(df) && df > 0) {
		boost::math::students_t_distribution<double> tdist(df);
		crit = boost::math::quantile(tdist, 1.0 - alpha / 2.0);
	}
	else {
		boost::math::normal_distribution<double> ndist;
		crit = boost::math::quantile(ndist, 1.0 - alpha / 2.0);
	}

	// ── Populate result ─────────────────────────────────────────────
	//
	// Trends are always on the link scale. For identity link, this IS the
	// response scale. For non-identity links, these are slopes of the linear
	// predictor (e.g. change in log-odds per unit x for logit).
	// We do not back-transform trends, following R's emtrends() convention.

	EMMResult result;
	result.factor = factor;
	result.df = df;
	result.levels = target_info->levels;

	result.emmean_link = Array<double>(K, 0.0);
	result.se_link     = Array<double>(K, 0.0);
	result.lower_link  = Array<double>(K, 0.0);
	result.upper_link  = Array<double>(K, 0.0);
	result.emmean      = Array<double>(K, 0.0);
	result.se          = Array<double>(K, 0.0);
	result.lower_ci    = Array<double>(K, 0.0);
	result.upper_ci    = Array<double>(K, 0.0);

	// Store link-scale covariance for pairwise_contrasts().
	result.cov_link = Array<double>(K, K, 0.0);
	for (intptr_t i = 0; i < K; i++) {
		for (intptr_t j = 0; j < K; j++) {
			result.cov_link(i + 1, j + 1) = V_eta(i, j);
		}
	}

	for (intptr_t i = 0; i < K; i++)
	{
		result.emmean_link[i + 1] = trends[i];
		result.se_link[i + 1]     = se[i];
		result.lower_link[i + 1]  = trends[i] - crit * se[i];
		result.upper_link[i + 1]  = trends[i] + crit * se[i];

		// No back-transformation for trends.
		result.emmean[i + 1]    = trends[i];
		result.se[i + 1]        = se[i];
		result.lower_ci[i + 1]  = result.lower_link[i + 1];
		result.upper_ci[i + 1]  = result.upper_link[i + 1];
	}

	return result;
}


// =====================================================================
// pairwise_contrasts()
// =====================================================================

ContrastResult pairwise_contrasts(const EMMResult &emm, const Model &model,
                                  const String &adjustment)
{
	intptr_t K = emm.levels.size();
	intptr_t npairs = K * (K - 1) / 2;

	if (npairs == 0) {
		throw error("Need at least 2 levels for pairwise contrasts");
	}

	// Validate adjustment method.
	std::string adj(adjustment.data(), adjustment.size());
	if (adj != "none" && adj != "bonferroni" && adj != "holm") {
		throw error("Unsupported p-value adjustment method: '%'. Use \"none\", \"bonferroni\", or \"holm\"",
		            adjustment);
	}

	// Recover the link-scale covariance matrix.
	Eigen::Map<const Matrix<double>> V_eta(emm.cov_link.data(), K, K);

	double df = emm.df;

	ContrastResult result;
	result.adjustment = adjustment;
	result.df = df;

	result.label    = Array<String>(npairs, String());
	result.estimate = Array<double>(npairs, 0.0);
	result.se       = Array<double>(npairs, 0.0);
	result.stat     = Array<double>(npairs, 0.0);
	result.p_value  = Array<double>(npairs, 0.0);

	// Compute all pairwise contrasts: δ_ij = EMM_i − EMM_j (link scale).
	intptr_t idx = 0;
	for (intptr_t i = 0; i < K; i++)
	{
		for (intptr_t j = i + 1; j < K; j++)
		{
			idx++;

			// Label: "level_i - level_j"
			String lbl;
			lbl.append(emm.levels[i + 1]);
			lbl.append(" - ");
			lbl.append(emm.levels[j + 1]);
			result.label[idx] = std::move(lbl);

			// Contrast estimate.
			double delta = emm.emmean_link[i + 1] - emm.emmean_link[j + 1];
			result.estimate[idx] = delta;

			// SE: sqrt(V(i,i) + V(j,j) - 2 V(i,j))
			double var_delta = V_eta(i, i) + V_eta(j, j) - 2.0 * V_eta(i, j);
			double se = std::sqrt(std::max(var_delta, 0.0));
			result.se[idx] = se;

			// Test statistic.
			double stat = (se > 0) ? delta / se : 0.0;
			result.stat[idx] = stat;

			// Raw p-value (two-sided).
			double p_raw;
			if (std::isfinite(df) && df > 0) {
				boost::math::students_t_distribution<double> tdist(df);
				p_raw = 2.0 * (1.0 - boost::math::cdf(tdist, std::abs(stat)));
			}
			else {
				boost::math::normal_distribution<double> ndist;
				p_raw = 2.0 * (1.0 - boost::math::cdf(ndist, std::abs(stat)));
			}
			result.p_value[idx] = p_raw;
		}
	}

	// ── P-value adjustment ──────────────────────────────────────────

	if (adj == "bonferroni")
	{
		for (intptr_t i = 1; i <= npairs; i++) {
			result.p_value[i] = std::min(result.p_value[i] * npairs, 1.0);
		}
	}
	else if (adj == "holm")
	{
		// Holm's step-down procedure:
		//   1. Sort p-values ascending.
		//   2. Multiply p[k] by (m - k + 1) where m = npairs.
		//   3. Enforce monotonicity (each adjusted p >= previous adjusted p).
		//   4. Cap at 1.0.

		// Build an index array sorted by raw p-value.
		std::vector<intptr_t> order(npairs);
		std::iota(order.begin(), order.end(), 1); // 1-based indices
		std::sort(order.begin(), order.end(), [&](intptr_t a, intptr_t b) {
			return result.p_value[a] < result.p_value[b];
		});

		// Adjust in sorted order.
		std::vector<double> adjusted(npairs);
		for (intptr_t k = 0; k < npairs; k++)
		{
			adjusted[k] = result.p_value[order[k]] * (npairs - k);
		}

		// Enforce monotonicity: p_adj[k] = max(p_adj[k], p_adj[k-1])
		for (intptr_t k = 1; k < npairs; k++)
		{
			if (adjusted[k] < adjusted[k - 1]) {
				adjusted[k] = adjusted[k - 1];
			}
		}

		// Write back in original order, capping at 1.0.
		for (intptr_t k = 0; k < npairs; k++) {
			result.p_value[order[k]] = std::min(adjusted[k], 1.0);
		}
	}
	// "none": no adjustment, raw p-values already in place.

	return result;
}

} // namespace phonometrica::stats
