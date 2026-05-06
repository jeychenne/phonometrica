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
 * Created: 05/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include <Eigen/Core>
#include <boost/math/distributions/normal.hpp>

#include <phon/analysis/predict.hpp>
#include <phon/analysis/formula.hpp>
#include <phon/analysis/family.hpp>
#include <phon/analysis/smooth.hpp>
#include <phon/application/data_table.hpp>
#include <phon/error.hpp>

namespace phonometrica::stats {

namespace {

// ─── Refusal helpers ─────────────────────────────────────────────────

// Validate the model and options against the Phase 1 MVP scope.
// Returns empty string if OK, otherwise a human-readable refusal message.
static String validate_scope(const Model &model, const PredictOptions &opts)
{
	if (model.is_bayesian()) {
		return String("predict() does not yet support Bayesian models. "
		              "This will be added in a future release.");
	}
	if (model.has_random_effects()) {
		return String("predict() does not yet support mixed-effects models "
		              "(random-effects terms). This will be added in a "
		              "future release.");
	}
	for (intptr_t i = 1; i <= model.smooth_terms.size(); i++)
	{
		auto &sm = model.smooth_terms[i];
		if (!sm.by.empty()) {
			return String("predict() does not yet support by-factor smooths "
			              "(s(x, by=...)). This will be added in a future "
			              "release.");
		}
		if (sm.basis == "re") {
			return String("predict() does not yet support random-effect "
			              "smooths (s(g, bs=\"re\")). This will be added in "
			              "a future release.");
		}
	}
	if (opts.type != "ci") {
		return String("predict() currently supports type=\"ci\" only. "
		              "Prediction intervals (\"pi\", \"both\") will be added "
		              "in a future release.");
	}
	if (opts.scale != "response" && opts.scale != "link") {
		return String("predict(): scale must be either \"response\" or \"link\".");
	}
	if (opts.ci_level <= 0.0 || opts.ci_level >= 1.0) {
		return String("predict(): ci_level must be strictly between 0 and 1.");
	}
	return String();
}


// ─── DataTable column lookup ─────────────────────────────────────────

// Find the 1-based column index for a variable name in a DataTable.
// Returns 0 if not found. Mirrors find_column() in fitting.cpp.
static intptr_t find_column(const DataTable &data, const String &name)
{
	intptr_t nc = data.column_count();
	for (intptr_t j = 1; j <= nc; j++)
	{
		if (data.get_header(j) == name) return j;
	}
	return 0;
}


// ─── Per-row design replay ───────────────────────────────────────────

// Look up the saved VariableInfo for a variable. Returns nullptr if not
// found (which is a logic error: every variable in the formula was
// expanded at fit time and therefore must have an entry).
static const Model::VariableInfo *find_variable_info(const Model &model, const String &name)
{
	for (intptr_t i = 1; i <= model.variable_info.size(); i++)
	{
		if (model.variable_info[i].name == name) {
			return &model.variable_info[i];
		}
	}
	return nullptr;
}


// Build a coefficient-name → coefficient-index (0-based) lookup over the
// parametric block of the design matrix. Smooth columns are excluded —
// their placement is found via SmoothResult::col_start instead.
static std::map<std::string, intptr_t>
build_coef_index(const Model &model, intptr_t parametric_end)
{
	std::map<std::string, intptr_t> idx;
	for (intptr_t j = 0; j < parametric_end; j++)
	{
		auto &n = model.coef_names[j + 1];     // Array is 1-based
		idx[std::string(n.data(), n.size())] = j;
	}
	return idx;
}


// Compute parametric_end as the smallest col_start across all smooth terms,
// or model.beta.size() if there are no smooths. The parametric block
// always precedes any smooth blocks (build_design_matrix invariant).
static intptr_t parametric_end_of(const Model &model)
{
	intptr_t end = model.beta.size();
	for (intptr_t i = 1; i <= model.smooth_terms.size(); i++)
	{
		auto &sm = model.smooth_terms[i];
		if (sm.col_start < end) end = sm.col_start;
	}
	return end;
}


// Outcome of expanding a single new-data row's contribution to one variable.
// 'numeric' rows produce a single (name="<var>", value=x) entry.
// 'categorical' rows produce one entry per non-reference level: every dummy
// is 0 except the dummy matching the cell, which is 1.0. If the cell is
// the reference level, all dummies are 0. If the cell is unseen, ok=false.
struct ExpandedRowVar
{
	bool ok = true;
	String error;     // populated when ok=false
	bool numeric = true;
	double numeric_value = 0.0;        // numeric only
	Array<String> dummy_names;          // categorical only: name of each dummy column
	std::vector<double> dummy_values;   // categorical only: 0/1 entries, parallel to dummy_names
};


// Expand a variable for one new-data row, using saved VariableInfo.
// Errors: cell is empty, cell fails numeric parse for a numeric variable,
// cell value is unseen for a categorical variable.
static ExpandedRowVar
expand_row_variable(const DataTable &data, intptr_t row, intptr_t col,
                    const Model::VariableInfo &vi)
{
	ExpandedRowVar out;
	String cell = data.get_cell(row, col);

	if (vi.numeric)
	{
		out.numeric = true;
		double x = 0;
		// try_parse_double-like behaviour: if parse fails or cell is empty,
		// flag the row as unpredictable. Caller emits NaN.
		if (cell.empty()) {
			out.ok = false;
			out.error = String("missing value");
			return out;
		}
		try
		{
			std::string s(cell.data(), cell.size());
			size_t pos = 0;
			x = std::stod(s, &pos);
			// Reject "1.5abc" — partial parses are not OK for numeric predictors.
			while (pos < s.size() && std::isspace((unsigned char) s[pos])) pos++;
			if (pos != s.size()) {
				out.ok = false;
				out.error = String("non-numeric value");
				return out;
			}
		}
		catch (...)
		{
			out.ok = false;
			out.error = String("non-numeric value");
			return out;
		}
		out.numeric_value = x;
		return out;
	}

	// Categorical.
	out.numeric = false;
	if (vi.levels.size() < 2) {
		// Should not happen — fit-time check would have rejected.
		out.ok = false;
		out.error = String("variable has fewer than 2 levels");
		return out;
	}

	// Reference level is at position 1 in vi.levels (extract_levels guarantees).
	// Dummies are columns named "<var>[<lev>]" for lev in vi.levels[2..].
	intptr_t found_index = 0;       // 1-based; 0 means "not in levels"
	for (intptr_t k = 1; k <= vi.levels.size(); k++)
	{
		if (cell == vi.levels[k]) {
			found_index = k;
			break;
		}
	}
	if (found_index == 0)
	{
		out.ok = false;
		out.error = String::format(
			"unseen level '%s' for variable '%s' (allow_new_levels is not yet supported)",
			std::string(cell.data(), cell.size()).c_str(),
			std::string(vi.name.data(), vi.name.size()).c_str());
		return out;
	}

	for (intptr_t k = 2; k <= vi.levels.size(); k++)
	{
		String name = vi.name;
		name.append("[");
		name.append(vi.levels[k]);
		name.append("]");
		out.dummy_names.append(std::move(name));
		out.dummy_values.push_back(found_index == k ? 1.0 : 0.0);
	}
	return out;
}


// Build the parametric portion of x_row for one row of newdata, in the
// coefficient ordering implied by model.coef_names. Returns false if
// any predictor in the formula could not be evaluated for this row;
// the caller emits NaN for the row.
//
// On success, x_row[0..parametric_end) is populated (intercept + main
// effects + interactions, every coefficient name in the parametric block
// is touched exactly once).
static bool build_param_row(const DataTable &newdata, intptr_t row,
                            const Formula &formula,
                            const Model &model,
                            const std::map<std::string, intptr_t> &coef_idx,
                            const std::map<String, intptr_t> &col_idx,
                            std::vector<double> &x_row,
                            String &row_error)
{
	auto put = [&](const String &name, double value) -> bool
	{
		auto it = coef_idx.find(std::string(name.data(), name.size()));
		if (it == coef_idx.end())
		{
			// A name we built doesn't appear in model.coef_names. This
			// would mean the variable_info / formula and the saved beta
			// are out of sync — a hard logic error, surface clearly.
			row_error = String::format(
				"internal: predicted column '%s' not present in model "
				"coefficient names", std::string(name.data(), name.size()).c_str());
			return false;
		}
		x_row[(size_t) it->second] = value;
		return true;
	};

	// Intercept
	if (formula.intercept) {
		if (!put(String("Intercept"), 1.0)) return false;
	}

	// Cache expansions per variable (an interaction may reference the
	// same variable twice; we still only expand it once). Keyed by
	// variable name, value is the expansion.
	std::map<std::string, ExpandedRowVar> cache;
	auto get_expansion = [&](const String &var_name) -> ExpandedRowVar *
	{
		std::string key(var_name.data(), var_name.size());
		auto cit = cache.find(key);
		if (cit != cache.end()) return &cit->second;

		auto colit = col_idx.find(var_name);
		if (colit == col_idx.end()) {
			ExpandedRowVar bad;
			bad.ok = false;
			bad.error = String::format("variable '%s' not present in newdata",
				std::string(var_name.data(), var_name.size()).c_str());
			cache[key] = std::move(bad);
			return &cache[key];
		}
		auto vi = find_variable_info(model, var_name);
		if (!vi) {
			ExpandedRowVar bad;
			bad.ok = false;
			bad.error = String::format(
				"internal: variable '%s' has no saved VariableInfo",
				std::string(var_name.data(), var_name.size()).c_str());
			cache[key] = std::move(bad);
			return &cache[key];
		}
		cache[key] = expand_row_variable(newdata, row, colit->second, *vi);
		return &cache[key];
	};

	for (intptr_t i = 1; i <= formula.fixed.size(); i++)
	{
		auto &ft = formula.fixed[i];

		if (ft.variables.size() == 1)
		{
			auto *exp = get_expansion(ft.variables[1]);
			if (!exp->ok) { row_error = exp->error; return false; }

			if (exp->numeric)
			{
				if (!put(ft.variables[1], exp->numeric_value)) return false;
			}
			else
			{
				for (intptr_t k = 1; k <= exp->dummy_names.size(); k++)
				{
					if (!put(exp->dummy_names[k], exp->dummy_values[(size_t)(k - 1)]))
						return false;
				}
			}
		}
		else
		{
			// Interaction: compute cross products.
			// Collect the per-variable column-name + value pairs, then take
			// the cartesian product to build the interaction columns. This
			// must match build_interaction()'s naming and ordering exactly.
			struct Component { Array<String> names; std::vector<double> vals; };
			std::vector<Component> comps;
			for (intptr_t j = 1; j <= ft.variables.size(); j++)
			{
				auto *exp = get_expansion(ft.variables[j]);
				if (!exp->ok) { row_error = exp->error; return false; }
				Component c;
				if (exp->numeric)
				{
					c.names.append(ft.variables[j]);
					c.vals.push_back(exp->numeric_value);
				}
				else
				{
					for (intptr_t k = 1; k <= exp->dummy_names.size(); k++)
					{
						c.names.append(exp->dummy_names[k]);
						c.vals.push_back(exp->dummy_values[(size_t)(k - 1)]);
					}
				}
				comps.push_back(std::move(c));
			}
			// Cartesian product, mirroring build_interaction in fitting.cpp.
			Array<String> result_names = comps[0].names;
			std::vector<double> result_vals = comps[0].vals;
			for (size_t c = 1; c < comps.size(); c++)
			{
				Array<String> new_names;
				std::vector<double> new_vals;
				for (intptr_t li = 1; li <= result_names.size(); li++)
				{
					for (intptr_t ri = 1; ri <= comps[c].names.size(); ri++)
					{
						String name = result_names[li];
						name.append(":");
						name.append(comps[c].names[ri]);
						new_names.append(std::move(name));
						new_vals.push_back(result_vals[(size_t)(li - 1)]
						                   * comps[c].vals[(size_t)(ri - 1)]);
					}
				}
				result_names = std::move(new_names);
				result_vals = std::move(new_vals);
			}
			for (intptr_t k = 1; k <= result_names.size(); k++)
			{
				if (!put(result_names[k], result_vals[(size_t)(k - 1)])) return false;
			}
		}
	}

	return true;
}


// Build the smooth-block portion of x_row for one row, in-place at
// the correct column offsets (sm.col_start .. sm.col_start + col_count).
// Returns false if a smooth's basis_data is missing (model loaded from
// an older save) or if the new-data x value can't be parsed.
static bool build_smooth_row(const DataTable &newdata, intptr_t row,
                             const Model &model,
                             const std::map<String, intptr_t> &col_idx,
                             std::vector<double> &x_row,
                             String &row_error)
{
	for (intptr_t i = 1; i <= model.smooth_terms.size(); i++)
	{
		auto &sm = model.smooth_terms[i];
		auto &bd = sm.basis_data;
		if (bd.type.empty())
		{
			row_error = String::format(
				"smooth term s(%s) has no persisted basis data — refit the "
				"model in this session and call predict() before saving",
				std::string(sm.variable.data(), sm.variable.size()).c_str());
			return false;
		}

		auto it = col_idx.find(sm.variable);
		if (it == col_idx.end())
		{
			row_error = String::format(
				"variable '%s' (smooth term) not present in newdata",
				std::string(sm.variable.data(), sm.variable.size()).c_str());
			return false;
		}

		String cell = newdata.get_cell(row, it->second);
		if (cell.empty()) {
			row_error = String("missing value (smooth predictor)");
			return false;
		}
		double xv = 0;
		try
		{
			std::string s(cell.data(), cell.size());
			size_t pos = 0;
			xv = std::stod(s, &pos);
			while (pos < s.size() && std::isspace((unsigned char) s[pos])) pos++;
			if (pos != s.size()) {
				row_error = String("non-numeric value (smooth predictor)");
				return false;
			}
		}
		catch (...)
		{
			row_error = String("non-numeric value (smooth predictor)");
			return false;
		}

		// Evaluate the basis at this single x.
		std::vector<double> x_new = { xv };
		Array<double> B_row = bd.predict(x_new);   // shape 1 × k_eff
		// k_eff should equal sm.col_count.
		intptr_t k_eff = sm.col_count;
		for (intptr_t j = 0; j < k_eff; j++)
		{
			x_row[(size_t)(sm.col_start + j)] = B_row(1, j + 1);
		}
	}
	return true;
}


// ─── Inverse link (response-scale transform) ─────────────────────────

// Apply the inverse link to a single value, then clamp to the family's
// natural support if relevant. Avoids constructing a Family object for a
// 1-element vector — direct math is cheaper and equally correct.
static double linkinv_scalar(const String &family, double eta)
{
	if (family == "binomial" || family == "beta")
	{
		// μ = 1 / (1 + exp(−η))
		return 1.0 / (1.0 + std::exp(-eta));
	}
	if (family == "poisson" || family == "negbin")
	{
		return std::exp(eta);
	}
	// gaussian, student: identity
	return eta;
}


// ─── CI assembly on link scale ───────────────────────────────────────

// Given fit on link scale and SE on link scale, fill ci_lower/ci_upper.
// Endpoints transform via the (monotone) inverse link if scale="response".
static void assemble_ci(const Model &model, const PredictOptions &opts,
                        const std::vector<double> &eta,
                        const std::vector<double> &se,
                        PredictResult &out)
{
	intptr_t n = (intptr_t) eta.size();
	out.fit = Array<double>(n, 0.0);
	out.se_fit = Array<double>(n, 0.0);
	out.ci_lower = Array<double>(n, 0.0);
	out.ci_upper = Array<double>(n, 0.0);

	// Two-sided z-quantile for the requested coverage.
	boost::math::normal N(0.0, 1.0);
	double z = boost::math::quantile(N, 0.5 + 0.5 * opts.ci_level);

	bool resp = (opts.scale == "response");

	for (intptr_t i = 0; i < n; i++)
	{
		double eta_i = eta[(size_t) i];
		double se_i = se[(size_t) i];
		out.se_fit.data()[i] = se_i;

		if (std::isnan(eta_i) || std::isnan(se_i))
		{
			out.fit.data()[i] = std::nan("");
			out.ci_lower.data()[i] = std::nan("");
			out.ci_upper.data()[i] = std::nan("");
			continue;
		}

		double lo_link = eta_i - z * se_i;
		double hi_link = eta_i + z * se_i;

		if (resp)
		{
			out.fit.data()[i] = linkinv_scalar(model.family, eta_i);
			out.ci_lower.data()[i] = linkinv_scalar(model.family, lo_link);
			out.ci_upper.data()[i] = linkinv_scalar(model.family, hi_link);
		}
		else
		{
			out.fit.data()[i] = eta_i;
			out.ci_lower.data()[i] = lo_link;
			out.ci_upper.data()[i] = hi_link;
		}
	}
}


// ─── Eigen views over Model storage ──────────────────────────────────

// Convert a stored beta Array into an Eigen vector view.
static Eigen::Map<const Eigen::VectorXd> beta_view(const Model &model)
{
	return Eigen::Map<const Eigen::VectorXd>(model.beta.data(), model.beta.size());
}


// Convert a stored vcov Array (p×p) into an Eigen matrix view.
// Phonometrica's Array<double> 2D storage is column-major (verified via
// d2_to_base0 in array.hpp: index = (col-1)*nrow + (row-1)). Eigen's default
// is also column-major, so a plain Map suffices — no strides, no copies.
// vcov is required to be square; caller has verified !vcov.empty().
static Eigen::Map<const Eigen::MatrixXd>
vcov_view(const Model &model, intptr_t p)
{
	return Eigen::Map<const Eigen::MatrixXd>(model.vcov.data(), p, p);
}


} // anonymous namespace


// =====================================================================
// Public: predict_at_training
// =====================================================================

PredictResult predict_at_training(const Model &model, const PredictOptions &opts)
{
	PredictResult out;

	String err = validate_scope(model, opts);
	if (!err.empty()) { out.error = err; return out; }

	if (model.beta.empty()) {
		out.error = String("predict(): model has no fitted coefficients.");
		return out;
	}
	if (!model.has_vcov()) {
		out.error = String("predict(): model has no vcov; cannot compute SE.");
		return out;
	}
	if (model.X.empty()) {
		out.error = String(
			"predict(model) is unavailable because the design matrix is not "
			"persisted across save/load. Call predict(model, newdata) instead, "
			"passing the dataset the model was fit on (or any dataset with the "
			"same predictor columns).");
		return out;
	}

	intptr_t p = model.beta.size();
	intptr_t n = model.X.nrow();

	if (model.X.ncol() != p) {
		out.error = String::format(
			"predict(): design matrix has %d columns but model has %d coefficients",
			(int) model.X.ncol(), (int) p);
		return out;
	}
	if (model.fitted.size() != n) {
		out.error = String::format(
			"predict(): model.X has %d rows but model.fitted has %d entries",
			(int) n, (int) model.fitted.size());
		return out;
	}

	// η̂_i = x_i · β, SE_η̂_i = sqrt(x_i' V x_i).
	// Phonometrica Array<double> 2D storage is column-major (see vcov_view
	// for the reasoning), so a plain default Eigen Map of model.X gives an
	// n×p matrix whose row(i) is the i-th observation's design row.
	auto V = vcov_view(model, p);
	auto beta = beta_view(model);

	std::vector<double> eta((size_t) n, 0.0);
	std::vector<double> se((size_t) n, 0.0);

	Eigen::Map<const Eigen::MatrixXd> Xmap(model.X.data(), n, p);

	for (intptr_t i = 0; i < n; i++)
	{
		// .row(i) returns a row expression; .transpose() makes it a column
		// vector for dot/matrix-vector ops.
		Eigen::VectorXd xi = Xmap.row(i).transpose();
		eta[(size_t) i] = xi.dot(beta);
		double q = xi.dot(V * xi);
		se[(size_t) i] = (q > 0) ? std::sqrt(q) : 0.0;
	}

	assemble_ci(model, opts, eta, se, out);
	out.ok = true;
	return out;
}


// =====================================================================
// Public: predict_at(model, newdata, opts)
// =====================================================================

PredictResult predict_at(const Model &model, const DataTable &newdata,
                         const PredictOptions &opts)
{
	PredictResult out;

	String err = validate_scope(model, opts);
	if (!err.empty()) { out.error = err; return out; }

	if (model.beta.empty()) {
		out.error = String("predict(): model has no fitted coefficients.");
		return out;
	}
	if (!model.has_vcov()) {
		out.error = String("predict(): model has no vcov; cannot compute SE.");
		return out;
	}
	if (model.formula.empty()) {
		out.error = String("predict(): model has no formula stored.");
		return out;
	}

	Formula formula;
	try
	{
		formula = Formula::parse(model.formula);
	}
	catch (std::exception &e)
	{
		out.error = String::format("predict(): could not parse model formula: %s", e.what());
		return out;
	}

	// Map predictor names → newdata column indices (1-based; 0 = missing).
	// We allow newdata to omit columns that are not in the formula; we
	// only require columns for variables that appear as predictors.
	std::map<String, intptr_t> col_idx;
	intptr_t nc = newdata.column_count();
	for (intptr_t j = 1; j <= nc; j++)
	{
		col_idx[newdata.get_header(j)] = j;
	}

	// Verify every formula variable maps to a column in newdata. This is a
	// hard precondition error (not per-row NaN), since a missing column is
	// a structural mismatch rather than a data gap.
	auto require_col = [&](const String &name) -> intptr_t {
		auto it = col_idx.find(name);
		return (it == col_idx.end()) ? 0 : it->second;
	};
	for (intptr_t i = 1; i <= formula.fixed.size(); i++)
	{
		auto &ft = formula.fixed[i];
		for (intptr_t j = 1; j <= ft.variables.size(); j++)
		{
			if (require_col(ft.variables[j]) == 0) {
				out.error = String::format(
					"predict(): newdata is missing required column '%s'",
					std::string(ft.variables[j].data(), ft.variables[j].size()).c_str());
				return out;
			}
		}
	}
	for (intptr_t i = 1; i <= formula.smooth.size(); i++)
	{
		if (require_col(formula.smooth[i].variable) == 0) {
			out.error = String::format(
				"predict(): newdata is missing required column '%s' (smooth)",
				std::string(formula.smooth[i].variable.data(),
				            formula.smooth[i].variable.size()).c_str());
			return out;
		}
	}

	intptr_t p = model.beta.size();
	intptr_t parametric_end = parametric_end_of(model);
	auto coef_idx = build_coef_index(model, parametric_end);

	auto V = vcov_view(model, p);
	auto beta = beta_view(model);

	intptr_t n_new = newdata.row_count();
	std::vector<double> eta((size_t) n_new, std::nan(""));
	std::vector<double> se((size_t) n_new, std::nan(""));

	std::vector<double> x_row;

	for (intptr_t row = 1; row <= n_new; row++)
	{
		x_row.assign((size_t) p, 0.0);
		String row_error;

		bool ok = build_param_row(newdata, row, formula, model, coef_idx, col_idx,
		                          x_row, row_error);
		if (ok && !model.smooth_terms.empty()) {
			ok = build_smooth_row(newdata, row, model, col_idx, x_row, row_error);
		}
		if (!ok)
		{
			// Per-row failures (missing values, unseen levels for the
			// allow_new_levels=false default, non-numeric cells in numeric
			// predictors) emit NaN. Hard structural errors (missing columns,
			// unparseable formula) were caught above and returned eagerly.
			//
			// Note: an unseen level here is a per-row event, not a structural
			// one — the column exists, but this row has a value not seen at
			// fit time. The current behaviour is to NaN it out per the
			// "allow_new_levels=false default" policy. A future release will
			// expose allow_new_levels=true to set u=0 for the unseen level.
			eta[(size_t)(row - 1)] = std::nan("");
			se[(size_t)(row - 1)] = std::nan("");
			continue;
		}

		Eigen::Map<const Eigen::VectorXd> xv(x_row.data(), p);
		eta[(size_t)(row - 1)] = xv.dot(beta);
		double q = xv.dot(V * xv);
		se[(size_t)(row - 1)] = (q > 0) ? std::sqrt(q) : 0.0;
	}

	assemble_ci(model, opts, eta, se, out);
	out.ok = true;
	return out;
}

} // namespace phonometrica::stats
