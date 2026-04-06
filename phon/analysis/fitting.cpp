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
 ***********************************************************************************************************************/

#include <algorithm>
#include <cmath>
#include <vector>
#include <map>
#include <phon/analysis/fitting.hpp>
#include <phon/analysis/regression.hpp>
#include <phon/analysis/mixed_model.hpp>
#include <phon/analysis/smooth.hpp>

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
		dc.name = "(Intercept)";
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
		gi.term_names.append("(Intercept)");
		z_columns.push_back(std::vector<double>(n, 1.0));
	}

	for (intptr_t s = 1; s <= rt.slopes.size(); s++)
	{
		intptr_t scol = find_column(data, rt.slopes[s]);
		if (scol == 0) {
			throw error("Slope variable '%' not found in data", rt.slopes[s]);
		}

		auto ev = expand_variable(data, scol, rows, reference_levels, !rt.intercept);
		for (auto &dc : ev.columns)
		{
			gi.term_names.append(dc.name);
			z_columns.push_back(std::move(dc.values));
		}
	}

	gi.nterms = (intptr_t)z_columns.size();

	if (gi.nterms == 0) {
		throw error("Random-effects term for '%' has no terms (no intercept and no slopes)", rt.group);
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
// Public fit() entry point
// =====================================================================

Model fit(const DataTable &data, const Formula &formula, const String &family,
          const std::map<String, String> &reference_levels,
          FittingCallback progress)
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
			intptr_t scol = find_column(data, rt.slopes[j]);
			if (scol == 0) {
				throw error("Variable '%' not found in data", rt.slopes[j]);
			}
			found = false;
			for (intptr_t c : all_col_indices)
			{
				if (c == scol) { found = true; break; }
			}
			if (!found) {
				all_col_indices.push_back(scol);
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

			// Find column and extract numeric values for complete rows.
			intptr_t scol = find_column(data, st.variable);
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
		throw error("Models with both smooth terms and random effects (GAMMs) are not yet supported. "
		            "Fit the smooth terms without random effects, or use random effects without smooth terms.");
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
			model = penalized_lm(dm.y, dm.X, S_aug, n_parametric, ranges, progress);
		}
		else
		{
			auto fam = Family::from_name(family);
			model = penalized_glm(dm.y, dm.X, S_aug, fam, n_parametric, ranges, progress);
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

		model = mixed_model(dm.y, dm.X, groups, fam, progress);
	}
	else if (family == "gaussian")
	{
		model = lm(dm.y, dm.X);
	}
	else if (family == "negbin")
	{
		model = negbin(dm.y, dm.X);
	}
	else
	{
		auto fam = Family::from_name(family);
		model = glm(dm.y, dm.X, fam);
	}

	// ── Attach metadata ──────────────────────────────────────────────

	model.formula = formula.to_string();
	model.coef_names = std::move(dm.coef_names);
	model.response_levels = std::move(dm.response_levels);
	model.variable_info = std::move(dm.variable_info);

	// For GAMs, set nfixed to the number of parametric terms only.
	// Smooth basis coefficients are reported separately via smooth_terms.
	if (formula.has_smooth_terms()) {
		model.nfixed = n_parametric;
	}

	return model;
}

} // namespace phonometrica::stats
