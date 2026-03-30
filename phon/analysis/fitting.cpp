/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
static Array<String> extract_levels(const DataTable &data, intptr_t col,
                                      const std::vector<intptr_t> &rows)
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
	for (auto &kv : seen) {
		levels.append(String(kv.first));
	}
	return levels; // already sorted by std::map ordering
}


// =====================================================================
// Variable expansion
// =====================================================================

// Expand a single variable into design columns.
static ExpandedVariable expand_variable(const DataTable &data, intptr_t col,
                                         const std::vector<intptr_t> &rows)
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
		// Categorical: treatment contrasts. First level is reference.
		ev.numeric = false;
		ev.levels = extract_levels(data, col, rows);

		if (ev.levels.size() < 2) {
			throw error("Categorical variable '%' has fewer than 2 levels", name);
		}

		// Create k-1 dummy columns (skip the first/reference level).
		for (intptr_t k = 2; k <= ev.levels.size(); k++)
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
	intptr_t nobs = 0;
	intptr_t ncol = 0;
};


static DesignMatrix build_design_matrix(const DataTable &data, const Formula &formula,
                                         const std::vector<intptr_t> &rows)
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
		var_exp_map[key] = expand_variable(data, col, rows);
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
	if (!is_numeric_column(data, resp_col, rows)) {
		throw error("Response variable '%' must be numeric", formula.response);
	}

	// ── Assemble into Array<double> ──────────────────────────────────

	intptr_t p = (intptr_t)all_columns.size();

	DesignMatrix dm;
	dm.nobs = (intptr_t)n;
	dm.ncol = p;

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
	auto resp_vals = extract_numeric(data, resp_col, rows);
	for (intptr_t i = 0; i < (intptr_t)n; i++) {
		dm.y[i + 1] = resp_vals[i];
	}

	// Coefficient names
	for (intptr_t j = 0; j < p; j++) {
		dm.coef_names.append(all_columns[j].name);
	}

	return dm;
}


} // anonymous namespace


// =====================================================================
// Public fit() entry point
// =====================================================================

Model fit(const DataTable &data, const Formula &formula, const String &family)
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

	// Random effects variables (for complete-case filtering, even though we don't fit them yet)
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

	// ── Complete cases ───────────────────────────────────────────────

	auto rows = find_complete_rows(data, all_col_indices);

	if (rows.empty()) {
		throw error("No complete observations (all rows have missing values)");
	}

	// ── Build design matrix ──────────────────────────────────────────

	auto dm = build_design_matrix(data, formula, rows);

	if (dm.nobs <= dm.ncol) {
		throw error("Not enough complete observations (% rows, % parameters)", dm.nobs, dm.ncol);
	}

	// ── Fit the model ────────────────────────────────────────────────

	Model model;

	if (family == "gaussian")
	{
		model = lm(dm.y, dm.X);
	}
	else
	{
		auto fam = Family::from_name(family);
		model = glm(dm.y, dm.X, fam);
	}

	// ── Attach metadata ──────────────────────────────────────────────

	model.formula = formula.to_string();
	model.coef_names = std::move(dm.coef_names);

	return model;
}

} // namespace phonometrica::stats
