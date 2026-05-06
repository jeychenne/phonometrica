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

// Validate the model and options against the supported scope.
// Returns empty string if OK, otherwise a human-readable refusal message.
// The script-side binding wraps the message with "predict(): %", so messages
// here should not include their own "predict()" prefix.
//
// Bayesian models are supported via the same arithmetic as Frequentist:
// model.beta and model.vcov hold the posterior mean and posterior
// covariance (set by bayesian_adjust / mixture summaries / etc.), so
// `η = X·β` and `SE² = x'·V·x` already produce the posterior mean of η
// and its posterior SD under the Gaussian/Laplace approximation. The
// resulting "CI" interval is a 95% credible interval (or whatever
// ci_level requests). This matches what INLA / brms posterior_interval
// return under the same Gaussian marginal-posterior assumption.
//
// Mixed-effects models are supported with re_form set to:
//   - "none"        : population-level prediction (u = 0). Default.
//   - "all"         : sum BLUPs across all random-effects groups present.
//   - "<group>"     : use BLUPs for the named group only; others stay at u=0.
// The set of valid group-name values depends on the model and is therefore
// validated inside predict_at_training / predict_at, not here.
static String validate_scope(const Model &model, const PredictOptions &opts)
{
	for (intptr_t i = 1; i <= model.smooth_terms.size(); i++)
	{
		auto &sm = model.smooth_terms[i];
		if (!sm.by.empty()) {
			return String("does not yet support by-factor smooths "
			              "(s(x, by=...)). This will be added in a future "
			              "release.");
		}
		if (sm.basis == "re") {
			return String("does not yet support random-effect "
			              "smooths (s(g, bs=\"re\")). This will be added in "
			              "a future release.");
		}
	}
	if (opts.type != "ci") {
		return String("currently supports type=\"ci\" only. "
		              "Prediction intervals (\"pi\", \"both\") will be added "
		              "in a future release.");
	}
	if (opts.scale != "response" && opts.scale != "link") {
		return String("scale must be either \"response\" or \"link\".");
	}
	if (opts.ci_level <= 0.0 || opts.ci_level >= 1.0) {
		return String("ci_level must be strictly between 0 and 1.");
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


// ─── Random-effects (conditional prediction) ─────────────────────────
//
// One spec per resolved random-effects group. The Z·u contribution for a
// row in `newdata` is: sum_t  x_row[term_x_col[t]] * conditional_modes[L*nterms + t]
// where L is the row's level index in this group's level_names. For the
// "Intercept" random term, the matching x_row entry is always 1.0 (column
// 0), so the formula above handles intercepts uniformly with slopes.
struct REGroupSpec
{
	intptr_t group_idx = 0;          // 1-based index into model.random_effects
	intptr_t newdata_col = 0;        // 1-based column index of the grouping factor in newdata (0 if N/A)
	std::map<String, intptr_t> level_idx;   // level name → 0-based row in conditional_modes
	std::vector<intptr_t> term_x_col;       // 0-based X column for each random term
	intptr_t nterms = 0;
};


// Parse opts.re_form against model.random_effects. On success, fills `specs`
// (empty for "none" / no random effects) and returns true. On failure sets
// `error_message` and returns false. `col_idx` may be empty when called from
// predict_at_training (where the lookup is by saved per-row index instead
// of a newdata column).
//
// We accept three values for opts.re_form:
//   "none"  → empty specs (population-level)
//   "all"   → one spec per random-effect group
//   "<name>"→ one spec for the group with that group_name
static bool resolve_re_groups(const Model &model, const PredictOptions &opts,
                              const std::map<String, intptr_t> *col_idx,
                              std::vector<REGroupSpec> &specs,
                              String &error_message)
{
	specs.clear();

	if (opts.re_form == "none" || !model.has_random_effects()) {
		return true;
	}

	std::vector<intptr_t> wanted;  // 1-based group indices
	if (opts.re_form == "all") {
		for (intptr_t g = 1; g <= model.random_effects.size(); g++) {
			wanted.push_back(g);
		}
	} else {
		intptr_t found = 0;
		for (intptr_t g = 1; g <= model.random_effects.size(); g++) {
			if (model.random_effects[g].group_name == opts.re_form) {
				found = g; break;
			}
		}
		if (found == 0) {
			std::string valid = "\"none\", \"all\"";
			for (intptr_t g = 1; g <= model.random_effects.size(); g++) {
				valid += ", \"";
				valid += std::string(model.random_effects[g].group_name.data(),
				                     (size_t) model.random_effects[g].group_name.size());
				valid += "\"";
			}
			error_message = String::format(
				"re_form must be one of %s. Got \"%s\".",
				valid.c_str(),
				std::string(opts.re_form.data(), (size_t) opts.re_form.size()).c_str());
			return false;
		}
		wanted.push_back(found);
	}

	// Build the parametric coef-name → X column index map.
	intptr_t parametric_end = parametric_end_of(model);
	auto coef_idx = build_coef_index(model, parametric_end);

	for (intptr_t g : wanted)
	{
		auto &re = model.random_effects[g];
		REGroupSpec spec;
		spec.group_idx = g;
		spec.nterms = re.nterms;

		// Locate grouping-column in newdata when we have a column map.
		if (col_idx) {
			auto it = col_idx->find(re.group_name);
			if (it == col_idx->end()) {
				error_message = String::format(
					"newdata is missing the random-effects grouping column '%s'.",
					std::string(re.group_name.data(), (size_t) re.group_name.size()).c_str());
				return false;
			}
			spec.newdata_col = it->second;
		}

		// level name → 0-based row
		for (intptr_t l = 1; l <= re.level_names.size(); l++) {
			spec.level_idx[re.level_names[l]] = l - 1;
		}

		// Each random term must match a fixed-effect coefficient name (the
		// random side reuses the fixed-side expand_variable / build_interaction
		// machinery, so the name strings agree exactly when the random term
		// has a fixed-effect counterpart).
		spec.term_x_col.reserve((size_t) re.term_names.size());
		for (intptr_t t = 1; t <= re.term_names.size(); t++) {
			std::string tname(re.term_names[t].data(), (size_t) re.term_names[t].size());
			auto it_c = coef_idx.find(tname);
			if (it_c == coef_idx.end()) {
				error_message = String::format(
					"random-effects term '%s' (in group '%s') has no matching "
					"fixed-effect coefficient. Conditional prediction currently "
					"requires every random term to correspond to a fixed-effect "
					"column. This will be relaxed in a future release.",
					tname.c_str(),
					std::string(re.group_name.data(), (size_t) re.group_name.size()).c_str());
				return false;
			}
			spec.term_x_col.push_back(it_c->second);
		}

		// Refuse cleanly if BLUPs are not populated on this group, or if
		// nlevels / nterms are inconsistent with the BLUP block size. The
		// load path derives nterms / nlevels from term_names / level_names
		// when the file lacks those integer fields, so this check should
		// only fire on a genuinely broken model — not on an older save
		// file that's just missing the redundant <Nterms>/<Nlevels> tags.
		if (re.nterms <= 0 || re.nlevels <= 0
		    || re.conditional_modes.size() != re.nlevels * re.nterms)
		{
			error_message = String::format(
				"random-effects group '%s' has no usable BLUPs "
				"(conditional_modes.size = %d, nlevels = %d, nterms = %d; "
				"expected %d). The model may not have been fully fitted, "
				"or the saved file is corrupt. Refit the model.",
				std::string(re.group_name.data(), (size_t) re.group_name.size()).c_str(),
				(int) re.conditional_modes.size(),
				(int) re.nlevels,
				(int) re.nterms,
				(int)(re.nlevels * re.nterms));
			return false;
		}

		specs.push_back(std::move(spec));
	}

	return true;
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
		out.error = String("model has no fitted coefficients.");
		return out;
	}
	if (!model.has_vcov()) {
		out.error = String("model has no vcov; cannot compute SE.");
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

	// Resolve random-effects groups for conditional prediction. For
	// training-row prediction the per-row level lookup uses the saved
	// `indices` vector populated at fit time (NOT the newdata cell, since
	// we have no DataTable here). `indices` is not serialised: a model
	// loaded from file has empty `indices` and conditional prediction
	// needs to be done via predict(model, source_data) instead.
	std::vector<REGroupSpec> re_specs;
	{
		String re_err;
		if (!resolve_re_groups(model, opts, /*col_idx=*/nullptr, re_specs, re_err)) {
			out.error = re_err;
			return out;
		}
	}
	if (!re_specs.empty())
	{
		for (auto &spec : re_specs) {
			auto &re = model.random_effects[spec.group_idx];
			if ((intptr_t) re.indices.size() != n) {
				out.error = String::format(
					"predict(): conditional prediction at training rows is "
					"unavailable for group '%s' because per-row level indices "
					"are not persisted across save/load. Call predict(model, "
					"source_data, opts) instead, passing the original dataset.",
					std::string(re.group_name.data(), (size_t) re.group_name.size()).c_str());
				return out;
			}
		}
	}

	std::vector<double> eta((size_t) n, 0.0);
	std::vector<double> se((size_t) n, 0.0);

	Eigen::Map<const Eigen::MatrixXd> Xmap(model.X.data(), n, p);

	for (intptr_t i = 0; i < n; i++)
	{
		// .row(i) returns a row expression; .transpose() makes it a column
		// vector for dot/matrix-vector ops.
		Eigen::VectorXd xi = Xmap.row(i).transpose();
		double eta_row = xi.dot(beta);
		double q = xi.dot(V * xi);

		// Conditional prediction: add Σ_g  z_i(g)' · u_blup(g, level_i(g)).
		// Same formula as in predict_at — only the level lookup differs
		// (saved indices vs newdata cell).
		for (auto &spec : re_specs) {
			auto &re = model.random_effects[spec.group_idx];
			intptr_t L = re.indices[(size_t) i];  // 0-based already
			double zu = 0.0;
			for (intptr_t t = 0; t < spec.nterms; t++) {
				intptr_t xc = spec.term_x_col[(size_t) t];
				zu += xi[xc] * re.conditional_modes[L * spec.nterms + t + 1]; // 1-based
			}
			eta_row += zu;
		}

		eta[(size_t) i] = eta_row;
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
		out.error = String("model has no fitted coefficients.");
		return out;
	}
	if (!model.has_vcov()) {
		out.error = String("model has no vcov; cannot compute SE.");
		return out;
	}
	if (model.formula.empty()) {
		out.error = String("model has no formula stored.");
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

	// Resolve random-effects groups for conditional prediction. Empty `re_specs`
	// means population-level (η = X·β with u=0); non-empty means we add Z·u
	// per row using saved BLUPs. The `col_idx` map is needed here so the row
	// loop can look up each row's grouping-factor cell.
	std::vector<REGroupSpec> re_specs;
	{
		String re_err;
		if (!resolve_re_groups(model, opts, &col_idx, re_specs, re_err)) {
			out.error = re_err;
			return out;
		}
	}

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
		double eta_row = xv.dot(beta);
		double q = xv.dot(V * xv);

		// Add Σ_g  z_row(g)' · u_blup(g, level_for_this_row)  to η.
		// SE is unchanged: under conditional prediction we treat u as fixed
		// at the BLUP, matching lme4's predict.merMod / glmmTMB's
		// predict(re.form=NULL) defaults — both ignore u uncertainty in the
		// SE calculation. A future release may add a "type=both" mode that
		// propagates u uncertainty via the joint vcov V_{β,u}.
		bool re_ok = true;
		for (auto &spec : re_specs)
		{
			auto &re = model.random_effects[spec.group_idx];

			// Look up the row's grouping cell. Empty / unseen → NaN this row.
			String cell = newdata.get_cell(row, spec.newdata_col);
			if (cell.empty()) { re_ok = false; break; }
			auto it_l = spec.level_idx.find(cell);
			if (it_l == spec.level_idx.end()) { re_ok = false; break; }
			intptr_t L = it_l->second;

			// Σ_t  x_row[term_x_col[t]] · conditional_modes[L*nterms + t]
			double zu = 0.0;
			for (intptr_t t = 0; t < spec.nterms; t++) {
				intptr_t xc = spec.term_x_col[(size_t) t];
				zu += x_row[(size_t) xc]
				      * re.conditional_modes[L * spec.nterms + t + 1]; // 1-based
			}
			eta_row += zu;
		}
		if (!re_ok) {
			eta[(size_t)(row - 1)] = std::nan("");
			se[(size_t)(row - 1)] = std::nan("");
			continue;
		}

		eta[(size_t)(row - 1)] = eta_row;
		se[(size_t)(row - 1)] = (q > 0) ? std::sqrt(q) : 0.0;
	}

	assemble_ci(model, opts, eta, se, out);
	out.ok = true;
	return out;
}

} // namespace phonometrica::stats
