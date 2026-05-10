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
#include <random>
#include <vector>
#include <map>
#include <boost/math/distributions/normal.hpp>
#include <phon/analysis/fitting.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/analysis/mixed_model.hpp>
#include <phon/analysis/smooth.hpp>
#include <phon/analysis/bayesian.hpp>
#include <phon/analysis/waic.hpp>
#include <phon/analysis/psis.hpp>

namespace phonometrica::stats {

// =====================================================================
// Internal types
// =====================================================================

namespace {

// A single column of the design matrix, with its coefficient name.
struct DesignColumn
{
	std::vector<double> values;
	String name;
};

// A variable expanded into its design columns.
// Continuous: one column.  Categorical: k-1 dummy columns (treatment contrasts).
struct ExpandedVariable
{
	std::vector<DesignColumn> columns;
	bool numeric = true;
	Array<String> levels; // sorted levels for categorical variables (empty for numeric)
};


// =====================================================================
// Column lookup
// =====================================================================

// Find the 1-based column index for a variable name in the DataTable.
// Returns 0 if not found.
static intptr_t find_column(const DataTable &data, const String &name)
{
	intptr_t nc = data.column_count();
	for (intptr_t j = 1; j <= nc; j++)
	{
		if (data.get_header(j) == name) {
			return j;
		}
	}
	return 0;
}


// =====================================================================
// Type detection
// =====================================================================

// Try to parse a cell as a double. Returns true if successful, with value in *out.
// Empty strings and "nan" are treated as missing (returns false).
static bool try_parse_double(const String &cell, double *out)
{
	if (cell.empty()) return false;
	if (cell == "nan" || cell == "NaN" || cell == "NA") return false;

	bool ok = false;
	double val = cell.to_float(&ok);
	if (ok && std::isfinite(val))
	{
		*out = val;
		return true;
	}
	return false;
}

// Determine whether a column is numeric.
// A column is numeric if every non-missing cell parses as a finite double.
// Returns false (categorical) if any non-missing cell fails to parse.
static bool is_numeric_column(const DataTable &data, intptr_t col, const std::vector<intptr_t> &rows)
{
	for (intptr_t row : rows)
	{
		String cell = data.get_cell(row, col);
		if (cell.empty() || cell == "nan" || cell == "NaN" || cell == "NA") continue;

		bool ok = false;
		double val = cell.to_float(&ok);
		if (!ok || !std::isfinite(val)) return false;
	}
	return true;
}


// =====================================================================
// Row selection (complete cases)
// =====================================================================

// Find all rows where every variable in the formula has a non-missing value.
static std::vector<intptr_t> find_complete_rows(const DataTable &data,
                                                  const std::vector<intptr_t> &col_indices)
{
	intptr_t nr = data.row_count();
	std::vector<intptr_t> rows;
	rows.reserve(nr);

	for (intptr_t i = 1; i <= nr; i++)
	{
		bool complete = true;
		for (intptr_t col : col_indices)
		{
			String cell = data.get_cell(i, col);
			if (cell.empty() || cell == "nan" || cell == "NaN" || cell == "NA")
			{
				complete = false;
				break;
			}
		}
		if (complete) {
			rows.push_back(i);
		}
	}

	return rows;
}


// =====================================================================
// Data extraction
// =====================================================================

// Extract a numeric column for the given rows.
static std::vector<double> extract_numeric(const DataTable &data, intptr_t col,
                                            const std::vector<intptr_t> &rows)
{
	std::vector<double> values;
	values.reserve(rows.size());
	for (intptr_t row : rows)
	{
		double val = 0;
		try_parse_double(data.get_cell(row, col), &val);
		values.push_back(val);
	}
	return values;
}


// Collect sorted unique levels for a categorical column.
// If reference is non-empty and appears among the levels, it is placed first;
// all remaining levels follow in alphabetical order.
static Array<String> extract_levels(const DataTable &data, intptr_t col,
                                      const std::vector<intptr_t> &rows,
                                      const String &reference = String())
{
	// Use a map to collect unique levels in sorted order.
	std::map<std::string, bool> seen;
	for (intptr_t row : rows)
	{
		String cell = data.get_cell(row, col);
		std::string key(cell.data(), cell.size());
		seen[key] = true;
	}

	Array<String> levels;

	// Place the reference level first if specified and present.
	std::string ref_key;
	if (!reference.empty())
	{
		ref_key.assign(reference.data(), reference.size());
		if (seen.count(ref_key)) {
			levels.append(reference);
		}
	}

	for (auto &kv : seen)
	{
		if (!ref_key.empty() && kv.first == ref_key)
			continue; // already placed first
		levels.append(String(kv.first));
	}

	return levels;
}


// =====================================================================
// Variable expansion
// =====================================================================

// Look up the reference level for a variable, if any.
static String lookup_reference(const String &name, const std::map<String, String> &reference_levels)
{
	auto it = reference_levels.find(name);
	if (it != reference_levels.end())
		return it->second;
	return String();
}


// Expand a single variable into design columns.
static ExpandedVariable expand_variable(const DataTable &data, intptr_t col,
                                         const std::vector<intptr_t> &rows,
                                         const std::map<String, String> &reference_levels,
                                         bool full_rank = false)
{
	ExpandedVariable ev;
	String name = data.get_header(col);

	if (is_numeric_column(data, col, rows))
	{
		// Continuous: one column, raw values.
		ev.numeric = true;
		DesignColumn dc;
		dc.name = name;
		dc.values = extract_numeric(data, col, rows);
		ev.columns.push_back(std::move(dc));
	}
	else
	{
		// Categorical.
		// full_rank=false (default): treatment contrasts, k-1 dummies (reference level omitted).
		// full_rank=true: cell-means coding, k dummies (all levels included).
		// The full_rank form is used for (0 + factor | group) random effects,
		// where there is no intercept to absorb the reference level.
		ev.numeric = false;
		ev.levels = extract_levels(data, col, rows, lookup_reference(name, reference_levels));

		if (ev.levels.size() < 2) {
			throw error("Categorical variable '%' has fewer than 2 levels", name);
		}

		intptr_t start_level = full_rank ? 1 : 2;
		for (intptr_t k = start_level; k <= ev.levels.size(); k++)
		{
			DesignColumn dc;
			dc.name = name;
			dc.name.append("[");
			dc.name.append(ev.levels[k]);
			dc.name.append("]");

			dc.values.resize(rows.size(), 0.0);
			for (size_t r = 0; r < rows.size(); r++)
			{
				String cell = data.get_cell(rows[r], col);
				if (cell == ev.levels[k]) {
					dc.values[r] = 1.0;
				}
			}
			ev.columns.push_back(std::move(dc));
		}
	}

	return ev;
}


// =====================================================================
// Interaction expansion
// =====================================================================

// Build design columns for an interaction term (a:b or a:b:c).
// This takes the cross product of columns from each component variable.
static std::vector<DesignColumn> build_interaction(const std::vector<ExpandedVariable> &components,
                                                    size_t n_rows)
{
	// Start with the columns from the first component.
	std::vector<DesignColumn> result = components[0].columns;

	// Successively cross with each additional component.
	for (size_t c = 1; c < components.size(); c++)
	{
		auto &next = components[c].columns;
		std::vector<DesignColumn> crossed;

		for (auto &left : result)
		{
			for (auto &right : next)
			{
				DesignColumn dc;
				dc.name = left.name;
				dc.name.append(":");
				dc.name.append(right.name);

				dc.values.resize(n_rows);
				for (size_t r = 0; r < n_rows; r++) {
					dc.values[r] = left.values[r] * right.values[r];
				}
				crossed.push_back(std::move(dc));
			}
		}

		result = std::move(crossed);
	}

	return result;
}


// =====================================================================
// Design matrix assembly
// =====================================================================

struct DesignMatrix
{
	Array<double> X;          // 2D design matrix (n × p)
	Array<double> y;          // response vector (n)
	Array<String> coef_names; // coefficient names (p)
	Array<String> response_levels; // for binary text response: [reference, success] (empty for numeric)
	Array<Model::VariableInfo> variable_info; // per-variable metadata for EMMs
	intptr_t nobs = 0;
	intptr_t ncol = 0;
};


static DesignMatrix build_design_matrix(const DataTable &data, const Formula &formula,
                                         const std::vector<intptr_t> &rows,
                                         const String &family,
                                         const std::map<String, String> &reference_levels)
{
	size_t n = rows.size();

	// ── Expand all unique variables ──────────────────────────────────

	// Collect unique variable names from fixed terms (excluding response).
	std::map<std::string, intptr_t> var_col_map;  // variable name → column index in DataTable
	std::map<std::string, ExpandedVariable> var_exp_map; // variable name → expanded columns

	auto ensure_expanded = [&](const String &name)
	{
		std::string key(name.data(), name.size());
		if (var_col_map.find(key) != var_col_map.end()) return;

		intptr_t col = find_column(data, name);
		if (col == 0) {
			throw error("Variable '%' not found in data", name);
		}
		var_col_map[key] = col;
		var_exp_map[key] = expand_variable(data, col, rows, reference_levels);
	};

	for (intptr_t i = 1; i <= formula.fixed.size(); i++)
	{
		auto &ft = formula.fixed[i];
		for (intptr_t j = 1; j <= ft.variables.size(); j++) {
			ensure_expanded(ft.variables[j]);
		}
	}

	// ── Build columns for each fixed term ────────────────────────────

	std::vector<DesignColumn> all_columns;

	// Intercept
	if (formula.intercept)
	{
		DesignColumn dc;
		dc.name = "Intercept";
		dc.values.assign(n, 1.0);
		all_columns.push_back(std::move(dc));
	}

	// Fixed-effects terms
	for (intptr_t i = 1; i <= formula.fixed.size(); i++)
	{
		auto &ft = formula.fixed[i];

		if (ft.variables.size() == 1)
		{
			// Main effect: directly use the expanded columns.
			std::string key(ft.variables[1].data(), ft.variables[1].size());
			auto &ev = var_exp_map[key];
			for (auto &dc : ev.columns) {
				all_columns.push_back(dc);
			}
		}
		else
		{
			// Interaction: cross-product of component columns.
			std::vector<ExpandedVariable> components;
			for (intptr_t j = 1; j <= ft.variables.size(); j++)
			{
				std::string key(ft.variables[j].data(), ft.variables[j].size());
				components.push_back(var_exp_map[key]);
			}
			auto interaction_cols = build_interaction(components, n);
			for (auto &dc : interaction_cols) {
				all_columns.push_back(std::move(dc));
			}
		}
	}

	// ── Response variable ────────────────────────────────────────────

	intptr_t resp_col = find_column(data, formula.response);
	if (resp_col == 0) {
		throw error("Response variable '%' not found in data", formula.response);
	}

	bool resp_is_numeric = is_numeric_column(data, resp_col, rows);
	Array<String> resp_levels;   // populated only for binary text response

	if (!resp_is_numeric)
	{
		// Non-numeric response: allowed only for binomial, and must have exactly 2 levels.
		if (family != "binomial") {
			throw error("Response variable '%' must be numeric for family '%'",
			            formula.response, family);
		}
		// For the response, also honour user-specified reference level.
		resp_levels = extract_levels(data, resp_col, rows,
		                              lookup_reference(formula.response, reference_levels));
		if (resp_levels.size() != 2)
		{
			throw error("Binomial response '%' must have exactly 2 levels (found %)",
			            formula.response, resp_levels.size());
		}
		// First level = 0 (reference), second = 1 (success).
	}

	// ── Assemble into Array<double> ──────────────────────────────────

	intptr_t p = (intptr_t)all_columns.size();

	DesignMatrix dm;
	dm.nobs = (intptr_t)n;
	dm.ncol = p;
	dm.response_levels = std::move(resp_levels);

	// X: n × p matrix
	dm.X = Array<double>((intptr_t)n, p, 0.0);
	for (intptr_t j = 0; j < p; j++)
	{
		for (intptr_t i = 0; i < (intptr_t)n; i++) {
			dm.X((intptr_t)(i + 1), (intptr_t)(j + 1)) = all_columns[j].values[i];
		}
	}

	// y: response vector
	dm.y = Array<double>((intptr_t)n, 0.0);

	if (!resp_is_numeric)
	{
		// Binary text response: code as 0/1 using the sorted levels.
		// First level = 0, second = 1.
		for (size_t i = 0; i < n; i++)
		{
			String cell = data.get_cell(rows[i], resp_col);
			dm.y[(intptr_t)(i + 1)] = (cell == dm.response_levels[2]) ? 1.0 : 0.0;
		}
	}
	else
	{
		auto resp_vals = extract_numeric(data, resp_col, rows);
		for (intptr_t i = 0; i < (intptr_t)n; i++) {
			dm.y[i + 1] = resp_vals[i];
		}
	}

	// Coefficient names
	for (intptr_t j = 0; j < p; j++) {
		dm.coef_names.append(all_columns[j].name);
	}

	// Variable metadata for post-hoc analysis (estimated marginal means).
	// One entry per unique predictor variable, recording whether it is
	// numeric or categorical and (for categoricals) the full level set.
	for (auto &kv : var_exp_map)
	{
		Model::VariableInfo vi;
		vi.name = String(kv.first);
		vi.numeric = kv.second.numeric;
		vi.levels = kv.second.levels;
		dm.variable_info.append(std::move(vi));
	}

	return dm;
}


// =====================================================================
// Grouping factor construction
// =====================================================================

// Build a GroupingInfo from a RandomTerm and the DataTable.
// Expands slope variables into treatment-coded design columns (same logic as fixed effects).
// The Z_design matrix is n_obs × nterms, row-major.
static GroupingInfo build_grouping(const DataTable &data, const RandomTerm &rt,
                                    const std::vector<intptr_t> &rows,
                                    const std::map<String, String> &reference_levels)
{
	GroupingInfo gi;

	// ── Grouping factor ──────────────────────────────────────────────

	intptr_t gcol = find_column(data, rt.group);
	if (gcol == 0) {
		throw error("Grouping variable '%' not found in data", rt.group);
	}

	gi.name = data.get_header(gcol);
	gi.levels = extract_levels(data, gcol, rows);
	gi.nlevels = gi.levels.size();

	if (gi.nlevels < 2) {
		throw error("Grouping factor '%' must have at least 2 levels", gi.name);
	}

	// Build a fast lookup: level string → 0-based index.
	std::map<std::string, intptr_t> level_map;
	for (intptr_t k = 1; k <= gi.levels.size(); k++) {
		std::string key(gi.levels[k].data(), gi.levels[k].size());
		level_map[key] = k - 1; // 0-based
	}

	gi.indices.reserve(rows.size());
	for (intptr_t row : rows)
	{
		String cell = data.get_cell(row, gcol);
		std::string key(cell.data(), cell.size());
		gi.indices.push_back(level_map[key]);
	}

	// ── Z design matrix ──────────────────────────────────────────────
	//
	// Collect design columns: intercept first (if present), then expanded
	// slope variables. Categorical slopes are expanded into treatment
	// contrasts (k−1 dummy columns), exactly as for fixed effects.

	size_t n = rows.size();
	std::vector<std::vector<double>> z_columns;

	if (rt.intercept)
	{
		gi.term_names.append("Intercept");
		z_columns.push_back(std::vector<double>(n, 1.0));
	}

	for (intptr_t s = 1; s <= rt.slopes.size(); s++)
	{
		const auto &st = rt.slopes[s];

		if (st.variables.size() == 1)
		{
			// Main-effect slope.
			intptr_t scol = find_column(data, st.variables[1]);
			if (scol == 0) {
				throw error("Slope variable '%' not found in data", st.variables[1]);
			}

			auto ev = expand_variable(data, scol, rows, reference_levels, !rt.intercept);
			for (auto &dc : ev.columns)
			{
				gi.term_names.append(dc.name);
				z_columns.push_back(std::move(dc.values));
			}
		}
		else
		{
			// Interaction slope: cross-product of treatment-coded components,
			// mirroring the fixed-side build_design_matrix logic. Components
			// are always treatment-coded for interactions, regardless of
			// whether a random intercept is included — the (0 + a:b | g)
			// "cell-means" form would need different handling and is not
			// currently supported.
			std::vector<ExpandedVariable> components;
			components.reserve(st.variables.size());

			for (intptr_t v = 1; v <= st.variables.size(); v++)
			{
				intptr_t vcol = find_column(data, st.variables[v]);
				if (vcol == 0) {
					throw error("Slope variable '%' not found in data", st.variables[v]);
				}
				components.push_back(expand_variable(data, vcol, rows, reference_levels, /*full_rank=*/false));
			}

			auto interaction_cols = build_interaction(components, n);
			for (auto &dc : interaction_cols)
			{
				gi.term_names.append(dc.name);
				z_columns.push_back(std::move(dc.values));
			}
		}
	}

	gi.nterms = (intptr_t)z_columns.size();

	if (gi.nterms == 0) {
		throw error("Random-effects term for '%' has no terms (no intercept and no slopes)", rt.group);
	}

	// Soft warning for high-dimensional random-effects blocks. The covariance
	// matrix Σ has q(q+1)/2 free hyperparameters, and the CCD integration grid
	// grows rapidly with q. q > 4 is rarely well-identified in practice.
	if (gi.nterms > 4)
	{
		intptr_t nhyper = gi.nterms * (gi.nterms + 1) / 2;
		std::fprintf(stderr,
			"Warning: random-effects block for '%.*s' has q=%ld terms (uncharted territory).\n"
			"         The %ld-dimensional covariance matrix has %ld free hyperparameters,\n"
			"         which may not be well identified, and the integration grid grows\n"
			"         rapidly with q. Consider simplifying the random-effects structure\n"
			"         (e.g., dropping correlations, removing interaction slopes, or\n"
			"         splitting into separate random-effects blocks).\n",
			(int) rt.group.size(), rt.group.data(), (long) gi.nterms,
			(long) gi.nterms, (long) nhyper);
	}

	// Pack into row-major Z_design: n × nterms
	gi.Z_design.resize(n * gi.nterms);
	for (size_t i = 0; i < n; i++)
	{
		for (intptr_t t = 0; t < gi.nterms; t++) {
			gi.Z_design[i * gi.nterms + t] = z_columns[t][i];
		}
	}

	return gi;
}


} // anonymous namespace


// =====================================================================
// Public reconstruction helper
// =====================================================================

GroupingInfo build_re_design_info(const DataTable &data, const RandomTerm &rt,
                                   const std::vector<intptr_t> &rows,
                                   const std::map<String, String> &reference_levels)
{
	return build_grouping(data, rt, rows, reference_levels);
}


// =====================================================================
// Public fit() entry point
// =====================================================================

static Model fit_impl(const DataTable &data, const Formula &formula, const String &family,
                       const std::map<String, String> &reference_levels,
                       FittingCallback progress,
                       const PriorSpec *priors,
                       int max_iter)
{
	if (formula.response.empty()) {
		throw error("Formula has no response variable");
	}
	if (formula.fixed.empty() && !formula.intercept) {
		throw error("Formula has no terms (no fixed effects and no intercept)");
	}
	if (data.row_count() == 0) {
		throw error("Data table is empty");
	}

	// ── Validate random effects (current limitations) ────────────────

	if (formula.has_random_effects())
	{
		for (intptr_t i = 1; i <= formula.random.size(); i++)
		{
			auto &rt = formula.random[i];
			if (!rt.intercept && rt.slopes.empty()) {
				throw error("Random-effects term must include an intercept or at least one slope");
			}
		}
	}

	// ── Collect all column indices used by the formula ────────────────

	std::vector<intptr_t> all_col_indices;

	// Response
	intptr_t resp_col = find_column(data, formula.response);
	if (resp_col == 0) {
		throw error("Response variable '%' not found in data", formula.response);
	}
	all_col_indices.push_back(resp_col);

	// Fixed effects variables
	for (intptr_t i = 1; i <= formula.fixed.size(); i++)
	{
		auto &ft = formula.fixed[i];
		for (intptr_t j = 1; j <= ft.variables.size(); j++)
		{
			intptr_t col = find_column(data, ft.variables[j]);
			if (col == 0) {
				throw error("Variable '%' not found in data", ft.variables[j]);
			}
			// Avoid duplicates
			bool found = false;
			for (intptr_t c : all_col_indices)
			{
				if (c == col) { found = true; break; }
			}
			if (!found) {
				all_col_indices.push_back(col);
			}
		}
	}

	// Random effects variables
	for (intptr_t i = 1; i <= formula.random.size(); i++)
	{
		auto &rt = formula.random[i];

		intptr_t gcol = find_column(data, rt.group);
		if (gcol == 0) {
			throw error("Grouping variable '%' not found in data", rt.group);
		}
		bool found = false;
		for (intptr_t c : all_col_indices)
		{
			if (c == gcol) { found = true; break; }
		}
		if (!found) {
			all_col_indices.push_back(gcol);
		}

		for (intptr_t j = 1; j <= rt.slopes.size(); j++)
		{
			auto &st = rt.slopes[j];
			for (intptr_t v = 1; v <= st.variables.size(); v++)
			{
				intptr_t scol = find_column(data, st.variables[v]);
				if (scol == 0) {
					throw error("Variable '%' not found in data", st.variables[v]);
				}
				bool found2 = false;
				for (intptr_t c : all_col_indices)
				{
					if (c == scol) { found2 = true; break; }
				}
				if (!found2) {
					all_col_indices.push_back(scol);
				}
			}
		}
	}

	// Smooth term variables
	for (intptr_t i = 1; i <= formula.smooth.size(); i++)
	{
		intptr_t scol = find_column(data, formula.smooth[i].variable);
		if (scol == 0) {
			throw error("Smooth variable '%' not found in data", formula.smooth[i].variable);
		}
		bool found = false;
		for (intptr_t c : all_col_indices)
		{
			if (c == scol) { found = true; break; }
		}
		if (!found) {
			all_col_indices.push_back(scol);
		}

		// Also collect the by-variable if present.
		if (!formula.smooth[i].by.empty())
		{
			intptr_t bcol = find_column(data, formula.smooth[i].by);
			if (bcol == 0) {
				throw error("By-variable '%' not found in data", formula.smooth[i].by);
			}
			found = false;
			for (intptr_t c : all_col_indices)
			{
				if (c == bcol) { found = true; break; }
			}
			if (!found) {
				all_col_indices.push_back(bcol);
			}
		}
	}

	// Offset column
	if (formula.has_offset())
	{
		intptr_t off_col = find_column(data, formula.offset);
		if (off_col == 0) {
			throw error("Offset variable '%' not found in data", formula.offset);
		}
		bool found = false;
		for (intptr_t c : all_col_indices)
		{
			if (c == off_col) { found = true; break; }
		}
		if (!found) {
			all_col_indices.push_back(off_col);
		}
	}

	// ── Complete cases ───────────────────────────────────────────────

	auto rows = find_complete_rows(data, all_col_indices);

	if (rows.empty()) {
		throw error("No complete observations (all rows have missing values)");
	}

	// ── Build design matrix ──────────────────────────────────────────

	auto dm = build_design_matrix(data, formula, rows, family, reference_levels);

	if (dm.nobs <= dm.ncol) {
		throw error("Not enough complete observations (% rows, % parameters)", dm.nobs, dm.ncol);
	}

	// Validate response range for beta regression.
	if (family == "beta")
	{
		for (intptr_t i = 1; i <= dm.nobs; i++)
		{
			double yi = dm.y[i];
			if (yi <= 0.0 || yi >= 1.0) {
				throw error("Beta regression requires response values strictly in (0, 1); "
				            "found y = % at observation %", yi, i);
			}
		}
	}

	// ── Extract offset vector (if present) ──────────────────────────

	Array<double> off;
	if (formula.has_offset())
	{
		intptr_t off_col = find_column(data, formula.offset);
		if (off_col == 0) {
			throw error("Offset variable '%' not found in data", formula.offset);
		}
		off = Array<double>(dm.nobs, 0.0);
		for (intptr_t i = 0; i < dm.nobs; i++)
			off[i + 1] = data.get_cell(rows[i], off_col).to_float();
	}

	// ── Build smooth bases and augment design matrix ──────────────

	intptr_t n_parametric = dm.ncol; // number of purely parametric columns

	// A SmoothSlice represents one block of smooth basis columns in the augmented X.
	// For a plain smooth s(x), there is one slice.
	// For a by-factor smooth s(x, by=f), there is one slice per level of f.
	struct SmoothSlice {
		SmoothBasis basis;       // the shared basis (same knots for all levels)
		intptr_t col_start;      // 0-based starting column in augmented X
		intptr_t col_count;      // number of columns (k_eff)
		intptr_t smooth_index;   // 1-based index into formula.smooth
		String level;            // empty for plain smooth, level name for by-factor
	};
	std::vector<SmoothSlice> smooth_slices;

	if (formula.has_smooth_terms())
	{
		for (intptr_t si = 1; si <= formula.smooth.size(); si++)
		{
			auto &st = formula.smooth[si];
			intptr_t scol = find_column(data, st.variable);

			if (st.basis == "re")
			{
				// ── Random-effect basis ──────────────────────────────
				// Variable must be categorical (grouping factor).
				if (is_numeric_column(data, scol, rows)) {
					throw error("Random-effect smooth s(%, bs=re) requires a categorical variable "
					            "(got numeric)", st.variable);
				}

				auto levels = extract_levels(data, scol, rows,
				                              lookup_reference(st.variable, reference_levels));

				// Build per-observation level index (0-based).
				std::vector<intptr_t> indices;
				indices.reserve(rows.size());
				for (intptr_t row : rows)
				{
					auto val = data.get_cell(row, scol);
					for (intptr_t lv = 1; lv <= levels.size(); lv++) {
						if (val == levels[lv]) { indices.push_back(lv - 1); break; }
					}
				}

				// For random slopes: extract numeric by-variable values.
				std::vector<double> slope_values;
				if (st.has_by())
				{
					intptr_t by_col = find_column(data, st.by);
					if (!is_numeric_column(data, by_col, rows)) {
						throw error("Random-slope by-variable '%' must be numeric", st.by);
					}
					slope_values.reserve(rows.size());
					for (intptr_t row : rows) {
						slope_values.push_back(data.get_cell(row, by_col).to_float());
					}
				}

				auto basis = build_re_basis(levels, indices, (intptr_t)rows.size(), slope_values);
				basis.variable = st.variable;

				intptr_t aug_col = n_parametric;
				for (auto &prev : smooth_slices) {
					aug_col += prev.col_count;
				}

				SmoothSlice slice;
				slice.col_start = aug_col;
				slice.col_count = basis.k_eff;
				slice.smooth_index = si;
				slice.basis = std::move(basis);
				smooth_slices.push_back(std::move(slice));
			}
			else
			{
				// ── Spline basis (cr) ────────────────────────────────
				// Find column and extract numeric values for complete rows.
				if (!is_numeric_column(data, scol, rows)) {
					throw error("Smooth variable '%' must be numeric", st.variable);
				}
				auto x_vals = extract_numeric(data, scol, rows);

				// Build the spline basis (shared knots for all levels).
				auto basis = build_cr_basis(x_vals, st.k);

				if (st.has_by())
				{
					// Factor by-variable: create one masked slice per level.
					intptr_t bcol = find_column(data, st.by);
					if (is_numeric_column(data, bcol, rows)) {
						throw error("By-variable '%' in s(%,by=%) must be categorical (got numeric)",
						            st.by, st.variable, st.by);
					}
					auto levels = extract_levels(data, bcol, rows,
					                              lookup_reference(st.by, reference_levels));

					// Extract by-variable values for all complete rows.
					std::vector<String> by_vals;
					by_vals.reserve(rows.size());
					for (intptr_t row : rows) {
						by_vals.push_back(data.get_cell(row, bcol));
					}

					for (intptr_t lv = 1; lv <= levels.size(); lv++)
					{
						SmoothSlice slice;
						// Create a zero-masked copy of the basis for this level.
						slice.basis = basis; // copy (shares knots, F_deriv2, Z_absorb)
						slice.basis.B = Array<double>(dm.nobs, basis.k_eff, 0.0);
						for (intptr_t j = 1; j <= basis.k_eff; j++) {
							for (intptr_t i = 0; i < (intptr_t)rows.size(); i++) {
								if (by_vals[i] == levels[lv]) {
									slice.basis.B(i + 1, j) = basis.B(i + 1, j);
								}
								// else: already 0
							}
						}

						intptr_t aug_col = n_parametric;
						for (auto &prev : smooth_slices) {
							aug_col += prev.col_count;
						}
						slice.col_start = aug_col;
						slice.col_count = basis.k_eff;
						slice.smooth_index = si;
						slice.level = levels[lv];
						smooth_slices.push_back(std::move(slice));
					}
				}
				else
				{
					// Plain smooth: one slice.
					intptr_t aug_col = n_parametric;
					for (auto &prev : smooth_slices) {
						aug_col += prev.col_count;
					}

					SmoothSlice slice;
					slice.col_start = aug_col;
					slice.col_count = basis.k_eff;
					slice.smooth_index = si;
					slice.basis = std::move(basis);
					smooth_slices.push_back(std::move(slice));
				}
			}
		}

		// ── scale.penalty (mgcv default scale.penalty=TRUE) ──────────
		//
		// Rescale each slice's S so mean(|diag(S)|) ≈ mean(|diag(B'B)|),
		// where B is the slice-specific (possibly zero-masked) basis.
		// This makes log10(λ) values directly comparable to mgcv's
		// m$sp and brings the natural-scale optimum inside the GCV
		// grid range [−5, +5] used in regression.cpp.
		//
		// Per-slice (not per-formula-term) is essential: by-factor
		// smooths share one underlying basis but each level's B is
		// zero-masked, so the appropriate scale is set by that level's
		// non-zero rows alone — matching mgcv's per-penalty-block
		// scaling. Random-effect smooths (bs="re") skip rescaling, also
		// matching mgcv (no.rescale=TRUE).
		for (auto &sl : smooth_slices)
		{
			auto &sb = sl.basis;
			if (sb.type == "re") continue;
			intptr_t nr = sb.B.nrow();
			intptr_t nc = sb.B.ncol();
			if (nr == 0 || nc == 0) continue;
			double mean_BtB = 0.0;
			for (intptr_t j = 1; j <= nc; j++) {
				double col_sq = 0.0;
				for (intptr_t i = 1; i <= nr; i++) {
					double v = sb.B(i, j);
					col_sq += v * v;
				}
				mean_BtB += col_sq;
			}
			mean_BtB /= (double) nc;
			double mean_S = 0.0;
			intptr_t Sn = sb.S.nrow();
			for (intptr_t j = 1; j <= Sn; j++) {
				mean_S += std::abs(sb.S(j, j));
			}
			if (Sn > 0) mean_S /= (double) Sn;
			if (mean_BtB > 0.0 && mean_S > 0.0)
			{
				double scale = mean_BtB / mean_S;
				for (intptr_t i = 1; i <= Sn; i++) {
					for (intptr_t j = 1; j <= Sn; j++) {
						sb.S(i, j) *= scale;
					}
				}
			}
		}

		// Total augmented dimension.
		intptr_t p_total = n_parametric;
		for (auto &sl : smooth_slices) {
			p_total += sl.col_count;
		}

		// Build augmented X: [X_parametric | B_1 | B_2 | ...]
		Array<double> X_aug(dm.nobs, p_total, 0.0);

		// Copy parametric columns.
		for (intptr_t j = 1; j <= n_parametric; j++) {
			for (intptr_t i = 1; i <= dm.nobs; i++) {
				X_aug(i, j) = dm.X(i, j);
			}
		}

		// Copy smooth basis columns.
		for (auto &sl : smooth_slices)
		{
			for (intptr_t j = 0; j < sl.col_count; j++) {
				for (intptr_t i = 1; i <= dm.nobs; i++) {
					X_aug(i, sl.col_start + j + 1) = sl.basis.B(i, j + 1);
				}
			}
		}

		// Build augmented penalty: S_total (p_total × p_total).
		Array<double> S_aug(p_total, p_total, 0.0);

		for (auto &sl : smooth_slices)
		{
			for (intptr_t r = 0; r < sl.col_count; r++) {
				for (intptr_t c = 0; c < sl.col_count; c++) {
					S_aug(sl.col_start + r + 1, sl.col_start + c + 1) = sl.basis.S(r + 1, c + 1);
				}
			}
		}

		// Update coefficient names.
		Array<String> aug_names;
		for (intptr_t j = 1; j <= dm.coef_names.size(); j++) {
			aug_names.append(dm.coef_names[j]);
		}
		for (auto &sl : smooth_slices)
		{
			auto &st = formula.smooth[sl.smooth_index];
			for (intptr_t j = 1; j <= sl.col_count; j++)
			{
				String name("s(");
				name.append(st.variable);
				name.append(")");
				if (!sl.level.empty()) {
					name.append(":");
					name.append(sl.level);
				}
				name.append(".");
				name.append(std::to_string(j));
				aug_names.append(std::move(name));
			}
		}

		// Replace dm fields with augmented versions.
		dm.X = std::move(X_aug);
		dm.ncol = p_total;
		dm.coef_names = std::move(aug_names);
	}

	// ── Fit the model ────────────────────────────────────────────────

	Model model;

	if (formula.has_smooth_terms() && formula.has_random_effects())
	{
		throw error("Models with both smooth terms and (1|group) random effects are not yet supported. "
		            "To account for grouping structure in a GAM, use s(group, bs=re) instead of (1|group). "
		            "For example: F1 ~ vowel + s(duration) + s(speaker, bs=re)");
	}

	if (formula.has_smooth_terms())
	{
		// ── GAM path: penalized regression with GCV ──────────────
		// Build the combined penalty from smooth_slices (already in S_aug above).
		intptr_t p_total = dm.ncol;
		Array<double> S_aug(p_total, p_total, 0.0);
		for (auto &sl : smooth_slices)
		{
			for (intptr_t r = 0; r < sl.col_count; r++) {
				for (intptr_t c = 0; c < sl.col_count; c++) {
					S_aug(sl.col_start + r + 1, sl.col_start + c + 1) = sl.basis.S(r + 1, c + 1);
				}
			}
		}

		// Build smooth column range descriptors.
		std::vector<SmoothColumnRange> ranges;
		for (auto &sl : smooth_slices)
		{
			auto &st = formula.smooth[sl.smooth_index];
			SmoothColumnRange scr;
			scr.col_start = sl.col_start;
			scr.col_count = sl.col_count;
			scr.variable = st.variable;
			scr.by = st.by;
			if (!sl.level.empty()) {
				// Label: "F1:speaker_level" for by-factor
				scr.variable.append(":");
				scr.variable.append(sl.level);
			}
			scr.basis = st.basis;
			scr.k = st.k;
			ranges.push_back(std::move(scr));
		}

		if (family == "gaussian")
		{
			model = penalized_lm(dm.y, dm.X, S_aug, n_parametric, ranges, progress, off);
		}
		else
		{
			auto fam = Family::from_name(family);
			model = penalized_glm(dm.y, dm.X, S_aug, fam, n_parametric, ranges, progress, max_iter, off);

			// The penalized GLM path uses IRLS with a fixed Family object and
			// does not estimate a dispersion parameter.  For negative binomial
			// this means θ stays at its default (1.0) rather than being fit to
			// the data, which yields a mis-specified fit and an unreliable
			// AIC.  Surface this as a warning so the user isn't silently given
			// a wrong answer; for GAMs with count data, Poisson is supported
			// correctly, and if overdispersion is a concern θ can be fit
			// separately on the unsmoothed model and inspected.
			if (family == "negbin") {
				model.fit_warning = String(
					"Negative-binomial GAMs are fit with a fixed dispersion "
					"parameter θ = 1 (no outer θ estimation in the penalized "
					"IRLS path).  Coefficients and AIC from this fit should "
					"not be trusted.  For count data with smooth terms, use "
					"family=\"poisson\"; if overdispersion is suspected, fit "
					"the unsmoothed model with family=\"negbin\" to estimate "
					"θ and report it separately.");
			}
		}

		// Persist per-smooth basis state on the returned model so predict()
		// can replay the basis at new x-values after a save/load round trip.
		// smooth_slices and model.smooth_terms are populated in the same
		// formula order (one slice per by-level for by-factor smooths, one
		// per smooth otherwise), so a simple parallel walk is correct.
		// We deliberately drop the n × k_eff training basis B and the
		// k_eff × k_eff penalty S — neither is consulted by SmoothBasis::predict,
		// and keeping them would bloat .phon-analysis files for no gain.
		if (smooth_slices.size() == (size_t) model.smooth_terms.size())
		{
			for (intptr_t i = 0; i < (intptr_t) smooth_slices.size(); i++)
			{
				SmoothBasis &sb = smooth_slices[i].basis;
				sb.B = Array<double>();   // training-only, drop for storage
				sb.S = Array<double>();   // penalty, not needed at predict
				model.smooth_terms[i + 1].basis_data = std::move(sb);
			}
		}
	}
	else if (formula.has_random_effects())
	{
		// ── Mixed model path (unified Laplace engine) ────────────────
		std::vector<GroupingInfo> groups;
		for (intptr_t i = 1; i <= formula.random.size(); i++)
		{
			groups.push_back(build_grouping(data, formula.random[i], rows, reference_levels));
		}
		auto fam = Family::from_name(family);

		// Multi-start activates by default for Student-t (n_starts = 4);
		// other families fall through to a single fit. See the wrapper
		// in mixed_model.cpp for the perturbation strategy.
		model = mixed_model_multistart(dm.y, dm.X, groups, fam, progress,
		                                priors, &dm.coef_names, max_iter, off);
	}
	else if (family == "gaussian")
	{
		model = lm(dm.y, dm.X, off);
	}
	else if (family == "negbin" || family == "beta" || family == "student")
	{
		// Route through the Laplace engine with empty groups for unified
		// optimization, ensuring comparable log-likelihoods with mixed models.
		std::vector<GroupingInfo> groups;
		auto fam = Family::from_name(family);
		model = mixed_model_multistart(dm.y, dm.X, groups, fam, progress,
		                                priors, &dm.coef_names, max_iter, off);
	}
	else
	{
		auto fam = Family::from_name(family);
		model = glm(dm.y, dm.X, fam, false, max_iter, off);
	}

	// ── Attach metadata ──────────────────────────────────────────────

	model.formula = formula.to_string();
	model.coef_names = std::move(dm.coef_names);
	model.response_levels = std::move(dm.response_levels);
	model.variable_info = std::move(dm.variable_info);

	// Precompute column means of the design matrix for EMMs (so they survive save/load).
	{
		intptr_t p = model.nfixed;
		intptr_t n = model.nobs;
		model.col_means = Array<double>(p, 0.0);
		for (intptr_t j = 0; j < p; j++)
		{
			double sum = 0.0;
			for (intptr_t i = 1; i <= n; i++) {
				sum += model.X(i, j + 1);
			}
			model.col_means[j + 1] = sum / n;
		}
	}

	// For GAMs, set nfixed to the number of parametric terms only.
	// Smooth basis coefficients are reported separately via smooth_terms.
	if (formula.has_smooth_terms()) {
		model.nfixed = n_parametric;
	}

	// Record the source-table row indices for the complete cases used in
	// fitting, so downstream code can align per-observation quantities
	// (fitted values, residuals, scaled residuals, posterior predictions)
	// back to specific rows in the source DataTable. `rows` is already a
	// std::vector<intptr_t> with 1-based DataTable row indices, so the field
	// is populated by a single copy.
	model.source_rows = rows;

	return model;
}


// =====================================================================
// Data-dependent default priors (à la brms)
// =====================================================================
//
// When auto-scale flags are set (the user didn't override that prior),
// replace the constructor defaults with priors scaled to the response.
//
// Following brms (Bürkner 2017):
//   Fixed effects:
//     Intercept: N(mean(y), 2.5 × sd(y_link))
//     Slopes:    N(0,       2.5 × sd(y_link))
//   Variance components: PC(2.5 × sd(y_link), 0.05)
//   Residual SD:         PC(2.5 × sd(y_link), 0.05)
//
// The 2.5× multiplier produces priors that are genuinely weakly
// informative at any measurement scale (Hz, bark, semitones, etc.).

static void scale_default_priors(PriorSpec &priors,
                                  const DataTable &data,
                                  const Formula &formula,
                                  const String &family)
{
	// Find response column.
	intptr_t resp_col = find_column(data, formula.response);
	if (resp_col == 0) return;  // response not found; fit() will error later

	intptr_t n = data.row_count();
	if (n < 2) return;

	// Accumulate mean and SD on the link scale.
	double sum = 0, sum2 = 0;
	intptr_t count = 0;

	for (intptr_t i = 1; i <= n; i++)
	{
		double v = 0;
		if (!try_parse_double(data.get_cell(i, resp_col), &v))
			continue;

		double lv = v;
		if (family == "binomial" || family == "beta")
		{
			// Logit link.
			v = std::clamp(v, 0.001, 0.999);
			lv = std::log(v / (1.0 - v));
		}
		else if (family == "poisson" || family == "negbin")
		{
			// Log link.
			lv = std::log(v + 0.5);
		}
		// else: identity link (gaussian, student) — use raw v.

		sum += lv;
		sum2 += lv * lv;
		count++;
	}

	if (count < 2) return;

	double y_mean = sum / count;
	double y_var = (sum2 - sum * sum / count) / (count - 1);
	double y_sd = std::sqrt(std::max(y_var, 1e-10));

	// Scale factor: 2.5 × sd(y_link), with a floor of 2.5.
	double scale = std::max(2.5, 2.5 * y_sd);

	if (priors.fixed_auto)
	{
		// Slopes: N(0, scale).
		priors.fixed_effects.mean = 0.0;
		priors.fixed_effects.sd = scale;

		// Intercept: N(mean(y_link), scale).
		// Added as a per-coefficient override so it doesn't affect slopes.
		// Only inserted if the user has not already set a per-coefficient
		// prior for the intercept via set_fixed(prior, "Intercept", ...);
		// overwriting would silently discard the user's override.
		if (priors.coefficient_priors.find(String("Intercept"))
		    == priors.coefficient_priors.end())
		{
			NormalPrior intercept_prior;
			intercept_prior.mean = y_mean;
			intercept_prior.sd = scale;
			priors.coefficient_priors[String("Intercept")] = intercept_prior;
		}
	}

	if (priors.variance_auto)
	{
		priors.variance_components.type = VariancePriorType::PC;
		priors.variance_components.param1 = scale;   // u
		priors.variance_components.param2 = 0.05;    // alpha
	}

	if (priors.residual_auto)
	{
		priors.residual.type = VariancePriorType::PC;
		priors.residual.param1 = scale;   // u
		priors.residual.param2 = 0.05;    // alpha
	}
}


// =====================================================================
// Public frequentist fit (delegates to fit_impl)
// =====================================================================

Model fit(const DataTable &data, const Formula &formula, const String &family,
          const std::map<String, String> &reference_levels,
          FittingCallback progress,
          int max_iter)
{
	return fit_impl(data, formula, family, reference_levels, progress, nullptr, max_iter);
}


// =====================================================================
// Bayesian fit (INLA-style approximate Bayesian inference)
// =====================================================================
//
// For mixed models: fit_impl passes priors to mixed_model(), which
// optimizes the negative log-posterior directly. The returned model
// already has β at the posterior mode and vcov = posterior covariance
// (Henderson with prior precision). We just extract summaries.
//
// For fixed-effects-only models: fit_impl produces the MLE, then
// bayesian_adjust() applies the post-hoc Gaussian approximation.

static void bayesian_summaries(Model &model, const PriorSpec &priors)
{
	intptr_t p = model.nfixed;
	if (p <= 0) return;

	boost::math::normal_distribution<double> normal;
	double z_975 = boost::math::quantile(normal, 0.975);

	model.posterior_mean = Array<double>(p, 0.0);
	model.posterior_mode = Array<double>(p, 0.0);
	model.posterior_median = Array<double>(p, 0.0);
	model.posterior_sd = Array<double>(p, 0.0);
	model.ci_lower = Array<double>(p, 0.0);
	model.ci_upper = Array<double>(p, 0.0);
	model.pd = Array<double>(p, 0.0);

	for (intptr_t j = 1; j <= p; j++)
	{
		double mean = model.beta[j];
		double var = model.vcov(j, j);
		double sd = (var > 0) ? std::sqrt(var) : 0.0;

		model.posterior_mean[j] = mean;
		model.posterior_mode[j] = mean;    // Gaussian: mode = mean
		model.posterior_median[j] = mean;  // Gaussian: median = mean
		model.posterior_sd[j] = sd;
		model.ci_lower[j] = mean - z_975 * sd;
		model.ci_upper[j] = mean + z_975 * sd;

		if (sd > 0) {
			model.pd[j] = boost::math::cdf(normal, std::abs(mean) / sd);
		} else {
			model.pd[j] = 1.0;
		}
	}

	// Update se/stat for compatibility with existing display code.
	for (intptr_t j = 1; j <= p; j++)
	{
		model.se[j] = model.posterior_sd[j];
		model.stat[j] = (model.se[j] > 0) ? model.beta[j] / model.se[j] : 0.0;
		model.p[j] = std::numeric_limits<double>::quiet_NaN();
	}

	// Hyperparameter posteriors (variance-component SDs).
	if (model.has_random_effects())
	{
		intptr_t n_hyper = 0;
		for (intptr_t g = 1; g <= model.random_effects.size(); g++) {
			n_hyper += model.random_effects[g].term_names.size();
		}
		if (model.is_gaussian()) {
			n_hyper += 1;
		}

		model.hyper_names = Array<String>(n_hyper, String());
		model.hyper_posterior_mean = Array<double>(n_hyper, 0.0);
		model.hyper_posterior_sd = Array<double>(n_hyper, std::numeric_limits<double>::quiet_NaN());
		model.hyper_ci_lower = Array<double>(n_hyper, std::numeric_limits<double>::quiet_NaN());
		model.hyper_ci_upper = Array<double>(n_hyper, std::numeric_limits<double>::quiet_NaN());

		intptr_t idx = 1;
		for (intptr_t g = 1; g <= model.random_effects.size(); g++)
		{
			auto &re = model.random_effects[g];
			for (intptr_t t = 1; t <= re.term_names.size(); t++)
			{
				std::string name = "sd(" + std::string(re.term_names[t].data(), re.term_names[t].size())
				                 + "|" + std::string(re.group_name.data(), re.group_name.size()) + ")";
				model.hyper_names[idx] = String(name);
				model.hyper_posterior_mean[idx] = std::sqrt(std::max(re.variance[t], 0.0));
				idx++;
			}
		}

		if (model.is_gaussian())
		{
			model.hyper_names[idx] = "sd(residual)";
			model.hyper_posterior_mean[idx] = model.rse;
			idx++;
		}
	}

	// This path is only reached for NB/beta/Student without random effects;
	// the grid integration paths and bayesian_adjust cover the common cases.

	// ── WAIC ────────────────────────────────────────────────────────────
	//
	// Draw from the full posterior. model.beta and model.vcov may include
	// smooth basis coefficients for GAMs — use the full dimensions.

	if (!model.X.empty() && !model.y.empty() && model.has_vcov())
	{
		constexpr int S = 1000;
		constexpr unsigned int SEED = 12345;

		intptr_t n = model.nobs;

		// Total coefficients (parametric + smooth basis for GAMs).
		intptr_t p_draw = model.beta.size();
		if (model.X.ndim() == 2 && model.X.ncol() != p_draw)
			p_draw = model.nfixed;  // fallback

		Eigen::Map<Matrix<double>> Xm(const_cast<double *>(model.X.data()), n, p_draw);
		Eigen::Map<Vector<double>> ym(const_cast<double *>(model.y.data()), n);

		Eigen::Map<Vector<double>> beta_full(const_cast<double *>(model.beta.data()), p_draw);
		Eigen::Map<Matrix<double>> vcov_full(const_cast<double *>(model.vcov.data()), p_draw, p_draw);
		Eigen::LLT<Eigen::MatrixXd> chol_post(vcov_full);

		if (chol_post.info() == Eigen::Success)
		{
			// Scalar inverse link.
			std::function<double(double)> linkinv_fn;
			if (model.family == "beta") {
				linkinv_fn = [](double eta) { return 1.0 / (1.0 + std::exp(-eta)); };
			} else if (model.family == "negbin") {
				linkinv_fn = [](double eta) { return std::exp(std::clamp(eta, -30.0, 30.0)); };
			} else {
				// Student: identity link
				linkinv_fn = [](double eta) { return eta; };
			}

			std::vector<double> loglik_matrix(n * S);
			std::mt19937 rng(SEED);
			std::normal_distribution<double> std_normal(0.0, 1.0);

			for (int s = 0; s < S; s++)
			{
				Eigen::VectorXd z(p_draw);
				for (intptr_t j = 0; j < p_draw; j++)
					z[j] = std_normal(rng);

				Eigen::VectorXd beta_s = beta_full + chol_post.matrixL() * z;
				Eigen::VectorXd eta = Xm * beta_s;
				// Offset contribution: η = X β + offset. Must match the
				// convention used during fitting (see mixed_model() and the
				// offset handling in bayesian_adjust / compute_grid_waic).
				if (!model.offset.empty()) {
					Eigen::Map<const Eigen::VectorXd> off(model.offset.data(), n);
					eta += off;
				}

				for (intptr_t i = 0; i < n; i++)
				{
					double mu_i = linkinv_fn(eta[i]);
					loglik_matrix[i * S + s] = pointwise_loglik(ym[i], mu_i, model);
				}
			}

			compute_waic_from_loglik(model, loglik_matrix, n, S);
			compute_loo_from_loglik(model, loglik_matrix, n, S);
		}
	}

	model.estimation = Estimation::Bayesian;
	model.priors = priors;
}


// Post-fit diagnostic: detect prior-scale / data-scale mismatch.
//
// For identity-link Bayesian fits (Gaussian, Student), a fixed-effects prior
// whose scale is much tighter than the response scale produces a degenerate
// joint optimum: β is pulled toward the prior mean (usually 0), and the
// optimizer compensates by inflating σ so the prior-shrunken predictions
// don't look too improbable.  For Student-t specifically, ν then races to
// the upper clamp because tiny standardised residuals are maximised as
// ν → ∞.  The fit "converges" and posterior summaries print without issue,
// but the estimates are driven by the prior rather than the data.
//
// The canonical symptom is σ >> sd(y).  A healthy fit satisfies σ < sd(y):
// the residual spread after subtracting X β̂ is less than the total spread
// of y.  When σ exceeds sd(y) we know β̂ explains less variance than a
// constant-mean fit, which is only plausible when the prior is dominating.
//
// This check is Bayesian-only and advisory: we set model.prior_warning and
// continue.  Frequentist paths never enter this code.
static void check_prior_scale_mismatch(Model &model,
                                         const DataTable &data,
                                         const Formula &formula,
                                         const String &family)
{
	// Only identity-link families are susceptible: others fit coefficients
	// on the logit or log scale where N(0, 10) is a loose prior by default.
	if (family != "gaussian" && family != "student") return;

	// Resolve the residual / scale parameter σ.  Storage differs by family
	// and by whether random effects are present:
	//   - Student (any):        model.sigma is populated during the fit
	//   - Gaussian fixed-effect: model.rse holds residual SE on the link scale
	//   - Gaussian mixed:       hyper_posterior_mean[hyper_names == "sd(residual)"]
	double sigma = 0;
	if (family == "student")
	{
		sigma = model.sigma;
	}
	else  // gaussian
	{
		if (formula.has_random_effects())
		{
			for (intptr_t k = 1; k <= model.hyper_names.size(); k++)
			{
				if (model.hyper_names[k] == String("sd(residual)"))
				{
					sigma = model.hyper_posterior_mean[k];
					break;
				}
			}
		}
		else
		{
			sigma = model.rse;
		}
	}
	if (!(sigma > 0) || !std::isfinite(sigma)) return;

	// Compute sd(y) on the raw scale (identity link, so link scale = raw).
	intptr_t resp_col = find_column(data, formula.response);
	if (resp_col == 0) return;
	intptr_t n = data.row_count();
	if (n < 2) return;

	double sum = 0, sum2 = 0;
	intptr_t count = 0;
	for (intptr_t i = 1; i <= n; i++)
	{
		double v = 0;
		if (!try_parse_double(data.get_cell(i, resp_col), &v)) continue;
		sum += v;
		sum2 += v * v;
		count++;
	}
	if (count < 2) return;

	double y_var = (sum2 - sum * sum / count) / (count - 1);
	double y_sd = std::sqrt(std::max(y_var, 1e-10));
	if (!(y_sd > 0) || !std::isfinite(y_sd)) return;

	// Threshold: σ > 1.5 × sd(y) is a strong signal of prior-driven distortion.
	// A healthy fit typically has σ < sd(y), often considerably less once
	// fixed effects explain some variance.  We allow 1.5× slack for cases
	// where the predictors are weak but the fit is otherwise sound.
	const double sigma_threshold = 1.5 * y_sd;
	bool sigma_inflated = (sigma > sigma_threshold);

	// Student-specific: ν pinned at upper clamp (200) is an independent signal.
	bool nu_pinned = (family == "student") && (model.nu >= 199.0);

	if (!sigma_inflated && !nu_pinned) return;  // Fit looks fine, no warning.

	// Build a clear, actionable message.
	std::string msg;
	char buf[256];

	if (sigma_inflated)
	{
		snprintf(buf, sizeof(buf),
		         "Residual scale (sigma = %.3g) exceeds %.1f x sd(y) = %.3g. ",
		         sigma, 1.5, y_sd);
		msg += buf;
		msg += "This usually indicates that the fixed-effects prior is too tight "
		       "for the response scale. ";
	}

	if (nu_pinned)
	{
		msg += "The degrees-of-freedom parameter nu has converged to its upper "
		       "bound (200), indicating the fit has collapsed to an effectively "
		       "Gaussian regime. ";
	}

	msg += "For identity-link families (Gaussian, Student t), coefficients are "
	       "on the response scale (e.g. Hz for F1 data), so a prior like N(0, 10) "
	       "shrinks coefficients toward zero while the optimizer compensates by "
	       "inflating sigma. ";

	// Recommendation tailored to what the user has set.
	if (!model.priors.fixed_auto)
	{
		msg += "Consider removing the explicit fixed-effects prior so data-scaled "
		       "auto-defaults are used (N(mean(y), 2.5 x sd(y)) for the intercept "
		       "and N(0, 2.5 x sd(y)) for slopes), or set a prior SD comparable to "
		       "2.5 x sd(y). ";
	}
	else
	{
		msg += "The auto-scaled defaults were applied, so this may reflect a "
		       "genuinely ill-conditioned fit — check the frequentist version of "
		       "the same model for comparison. ";
	}

	msg += "Comparing against the frequentist fit is a reliable sanity check: "
	       "if the Bayesian estimates diverge substantially from the frequentist "
	       "estimates, the prior is driving the result.";

	model.prior_warning = String(msg.data(), msg.size());
}


Model fit(const DataTable &data, const Formula &formula, const String &family,
          const PriorSpec &priors,
          const std::map<String, String> &reference_levels,
          FittingCallback progress,
          int max_iter)
{
#if defined(PHON_INLA_BAYES_DIAG)
	// Waypoint [fit-entry]: captures the PriorSpec as seen by the C++ fit()
	// before the local copy, before scale_default_priors, before fit_impl.
	auto diag_dump_priors = [](const char *label, const PriorSpec &p) {
		std::fprintf(stderr,
			"[PHON_INLA_BAYES_DIAG/%s] &priors=%p"
			"  fixed_effects={mean=%.4f, sd=%.4f}"
			"  fixed_auto=%s\n",
			label, (const void *) &p,
			p.fixed_effects.mean, p.fixed_effects.sd,
			p.fixed_auto ? "true" : "false");
		std::fprintf(stderr,
			"  coefficient_priors (%zu entries):\n",
			p.coefficient_priors.size());
		for (const auto &[name, np] : p.coefficient_priors)
		{
			std::fprintf(stderr,
				"    \"%.*s\"  {mean=%.4f, sd=%.4f}  &entry=%p%s\n",
				(int) name.size(), name.data(),
				np.mean, np.sd,
				(const void *) &np,
				(np.sd > 0 && std::isfinite(np.sd)) ? "" : "  [INVALID SD]");
		}
	};
	diag_dump_priors("fit-entry", priors);
#endif

	// Auto-scale any prior fields the user didn't set explicitly.
	PriorSpec scaled_priors = priors;
#if defined(PHON_INLA_BAYES_DIAG)
	diag_dump_priors("post-copy", scaled_priors);
#endif
	scale_default_priors(scaled_priors, data, formula, family);
#if defined(PHON_INLA_BAYES_DIAG)
	diag_dump_priors("post-scale", scaled_priors);
#endif

	// For mixed models and Laplace-routed families (NB, beta, Student t):
	// fit_impl passes priors to mixed_model(), which optimizes the
	// negative log-posterior. The model already has the posterior mode.
	bool uses_laplace = formula.has_random_effects()
	                 || family == "negbin" || family == "beta" || family == "student";

	Model model = fit_impl(data, formula, family, reference_levels, progress,
	                         uses_laplace ? &scaled_priors : nullptr, max_iter);

	if (uses_laplace)
	{
		// If INLA grid integration already populated the posterior fields
		// (Gaussian LMMs and non-Gaussian GLMMs with random effects),
		// skip. Otherwise, extract summaries from the mode.
		if (model.posterior_mean.empty())
			bayesian_summaries(model, scaled_priors);
	}
	else
	{
		// Fixed-effects LM/GLM: post-hoc adjustment (exact for Gaussian, approximate for GLM).
		bayesian_adjust(model, scaled_priors);
	}

	// Post-fit diagnostic: flag suspected prior-scale mismatch for identity-link fits.
	check_prior_scale_mismatch(model, data, formula, family);

	return model;
}

} // namespace phonometrica::stats
