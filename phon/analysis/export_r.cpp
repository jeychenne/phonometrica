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
 * Created: 21/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see export_r.hpp.                                                                                          *
 *                                                                                                                     *
 * The generated script must fit *the same model on the same rows*, or the comparison manufactures a discrepancy that  *
 * is not a bug. Each rule below closes one way in which the R fit could silently differ:                              *
 *                                                                                                                     *
 *   rows            data.csv is the model frame (Model::source_rows), not the source table.                           *
 *   column types    read.csv gets an explicit colClasses, so a categorical column with numeric-looking codes ("01",   *
 *                   "1.0") stays a string and groups exactly as Phonometrica grouped it.                              *
 *   column names    check.names = FALSE, and non-syntactic names are written in backticks.                            *
 *   factor levels   every categorical variable gets factor(..., levels = ...) with Phonometrica's order, which puts    *
 *                   a user-set reference level first.                                                                 *
 *   contrasts       options(contrasts = ...) is reset, in case the user's .Rprofile changed it.                       *
 *   coding          R codes a factor with one dummy per level when a formula is not hierarchical (e.g. f:g without   *
 *                   g, or no intercept); Phonometrica always uses treatment contrasts. Such models are refused (see   *
 *                   coding_blocker) rather than exported with a parameterization that cannot match.                   *
 *   method          REML/ML is always written out.                                                                    *
 *   Student t       glmmTMB's t_family diverges on a Hz-scale response, so the script fits a standardized response    *
 *                   and transforms back (exact for a location-scale family).                                          *
 *                                                                                                                     *
 * Coefficients are joined on a canonical R-side key: the R coefficient name without backticks, with the components of *
 * an interaction sorted bytewise (R orders interaction components by first appearance in the formula, which need not  *
 * be Phonometrica's order). Random-effect groups are joined by position, because glmmTMB passes group names through   *
 * make.names() (so "school:class" becomes "school.class" and a repeated "speaker" becomes "speaker.1").               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <phon/analysis/export_r.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/engine/types/file.hpp>

namespace phonometrica::stats {

namespace {

// ─────────────────────────────────────────────────────────────────────
// Small string helpers. Everything is assembled in std::string and only
// converted to String at the API boundary.
// ─────────────────────────────────────────────────────────────────────

std::string str(const String &s)
{
	return std::string(s.data(), static_cast<size_t>(s.size()));
}

[[noreturn]] void fail(const std::string &msg)
{
	throw std::runtime_error(msg);
}

bool is_missing_cell(const String &cell)
{
	return cell.empty() || cell == "nan" || cell == "NaN" || cell == "NA";
}

// Parse a cell exactly as fitting.cpp does (String::to_float, finite only).
bool parse_cell(const String &cell, double *out)
{
	if (is_missing_cell(cell)) return false;
	bool ok = false;
	double v = cell.to_float(&ok);
	if (!ok || !std::isfinite(v)) return false;
	*out = v;
	return true;
}

// Locale-independent, shortest round-trip representation. The GUI runs
// under the user's locale (setlocale(LC_ALL, "") on Unix), so printf-style
// formatting would write "1,5" under a French locale.
std::string fmt_num(double v)
{
	if (std::isnan(v)) return "NA";
	if (std::isinf(v)) return v > 0 ? "Inf" : "-Inf";
	char buf[64];
	auto res = std::to_chars(buf, buf + sizeof buf, v);
	return std::string(buf, res.ptr);
}

std::string fmt_int(intptr_t v)
{
	return std::to_string(static_cast<long long>(v));
}

// An R string literal in double quotes.
std::string r_string(const std::string &s)
{
	std::string out = "\"";
	for (unsigned char c : s)
	{
		switch (c)
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20 || c == 0x7f)
				{
					char buf[8];
					std::snprintf(buf, sizeof buf, "\\x%02x", c);
					out += buf;
				}
				else
				{
					out += static_cast<char>(c);
				}
		}
	}
	out += "\"";
	return out;
}

// A variable name as it must appear in an R formula (backticks if needed).
std::string r_name(const std::string &s)
{
	return str(r_quote_name(String(s.c_str(), static_cast<intptr_t>(s.size()))));
}

// The R-side join key of a coefficient or random-effect term name: drop
// backticks, split on ':', sort the components bytewise, rejoin. The R
// script applies the identical transform to R's own names (.canon()).
std::string canon_key(const std::string &r_coef_name)
{
	std::string s;
	for (char c : r_coef_name)
		if (c != '`') s += c;
	std::vector<std::string> parts;
	size_t start = 0;
	for (size_t i = 0; i <= s.size(); i++)
	{
		if (i == s.size() || s[i] == ':')
		{
			parts.push_back(s.substr(start, i - start));
			start = i + 1;
		}
	}
	std::sort(parts.begin(), parts.end());
	std::string out;
	for (size_t i = 0; i < parts.size(); i++)
	{
		if (i) out += ':';
		out += parts[i];
	}
	return out;
}

std::string csv_text(const std::string &s)
{
	std::string out = "\"";
	for (char c : s)
	{
		if (c == '"') out += '"';
		out += c;
	}
	out += "\"";
	return out;
}

std::vector<std::string> split_group(const String &group, bool synthetic)
{
	std::vector<std::string> parts;
	std::string g = str(group);
	if (!synthetic)
	{
		parts.push_back(g);
		return parts;
	}
	size_t start = 0;
	for (size_t i = 0; i <= g.size(); i++)
	{
		if (i == g.size() || g[i] == ':')
		{
			if (i > start) parts.push_back(g.substr(start, i - start));
			start = i + 1;
		}
	}
	return parts;
}


// ─────────────────────────────────────────────────────────────────────
// Factor coding: does R parameterize the formula the way Phonometrica does?
// ─────────────────────────────────────────────────────────────────────

// One model-formula context: the fixed part, or the left-hand side of one
// random term. `phon_full` gives, per term, whether Phonometrica codes a
// categorical main effect with one dummy per level (true only for main-effect
// slopes of a random term without intercept; see build_grouping).
struct CodingContext
{
	String where;                       // "the fixed effects" or "the random term (0 + a | g)"
	bool intercept = true;
	std::vector<std::vector<std::string>> terms;
	bool phon_full_main_effects = false;
};

// Emulates R's TermCode (src/library/stats/src/model.c): terms are taken in
// order of increasing degree; a factor in a term is coded by contrasts if the
// term with that factor removed is present (the empty term counts as present),
// and by one dummy per level otherwise. Without an intercept, the first factor
// found is switched to dummy coding. Returns an empty string if R and
// Phonometrica agree, otherwise a user-facing explanation.
template<class IsCategorical>
std::string coding_mismatch(const CodingContext &ctx, IsCategorical is_categorical)
{
	auto as_set = [](const std::vector<std::string> &t) { return std::set<std::string>(t.begin(), t.end()); };

	std::vector<size_t> order(ctx.terms.size());
	for (size_t i = 0; i < order.size(); i++) order[i] = i;
	std::stable_sort(order.begin(), order.end(),
	                 [&](size_t a, size_t b) { return ctx.terms[a].size() < ctx.terms[b].size(); });

	std::set<std::set<std::string>> present;
	for (auto &t : ctx.terms) present.insert(as_set(t));

	bool first_factor_pending = !ctx.intercept;

	for (size_t oi : order)
	{
		const auto &term = ctx.terms[oi];
		for (const auto &v : term)
		{
			if (!is_categorical(v)) continue;

			bool r_full;
			std::string why;
			if (first_factor_pending)
			{
				r_full = true;
				first_factor_pending = false;
				why = "there is no intercept";
			}
			else
			{
				auto rest = as_set(term);
				rest.erase(v);
				r_full = !rest.empty() && present.find(rest) == present.end();
				if (r_full)
				{
					std::string missing;
					for (auto &r : rest)
					{
						if (!missing.empty()) missing += ":";
						missing += r;
					}
					why = "the lower-order term '" + missing + "' is absent";
				}
			}

			bool phon_full = ctx.phon_full_main_effects && term.size() == 1;

			if (r_full != phon_full)
			{
				std::string tname;
				for (auto &x : term)
				{
					if (!tname.empty()) tname += ":";
					tname += x;
				}
				if (r_full)
				{
					return "In " + str(ctx.where) + ", R would code the factor '" + v + "' in the term '" + tname +
					       "' with one dummy variable per level, because " + why +
					       ", whereas Phonometrica uses treatment contrasts. The two fits would be "
					       "parameterized differently and could not be compared. Make the formula "
					       "hierarchical (include every lower-order term, and the intercept) to export it.";
				}
				return "In " + str(ctx.where) + ", Phonometrica codes every categorical slope with one "
				       "dummy per level, but R would use treatment contrasts for '" + v +
				       "' because only the first factor of a term without intercept gets full coding. "
				       "Use a separate random term for each categorical slope to export this model.";
			}
		}
	}
	return std::string();
}

std::vector<CodingContext> coding_contexts(const Formula &f)
{
	std::vector<CodingContext> out;

	CodingContext fixed;
	fixed.where = "the fixed effects";
	fixed.intercept = f.intercept;
	for (auto &t : f.fixed)
	{
		std::vector<std::string> vars;
		for (auto &v : t.variables) vars.push_back(str(v));
		fixed.terms.push_back(std::move(vars));
	}
	out.push_back(std::move(fixed));

	for (auto &rt : f.random)
	{
		CodingContext c;
		c.where = String("the random term ");
		c.where.append(rt.to_string());
		c.intercept = rt.intercept;
		c.phon_full_main_effects = !rt.intercept;
		for (auto &t : rt.slopes)
		{
			std::vector<std::string> vars;
			for (auto &v : t.variables) vars.push_back(str(v));
			c.terms.push_back(std::move(vars));
		}
		out.push_back(std::move(c));
	}
	return out;
}

template<class IsCategorical>
std::string formula_coding_blocker(const Formula &f, IsCategorical is_categorical)
{
	for (auto &ctx : coding_contexts(f))
	{
		auto msg = coding_mismatch(ctx, is_categorical);
		if (!msg.empty()) return msg;
	}
	return std::string();
}

// Categorical-ness from the model's own metadata (no data needed), for the
// GUI tooltip. variable_info covers fixed-effects predictors; a variable that
// only appears as a random slope is categorical iff the engine produced
// "var[level]" term names for it.
bool model_says_categorical(const Model &m, const std::string &v)
{
	for (auto &vi : m.variable_info)
		if (str(vi.name) == v) return !vi.numeric;

	std::string prefix = v + "[";
	auto matches = [&](const String &name) {
		std::string n = str(name);
		size_t start = 0;
		for (size_t i = 0; i <= n.size(); i++)
		{
			if (i == n.size() || n[i] == ':')
			{
				if (n.compare(start, prefix.size(), prefix) == 0) return true;
				start = i + 1;
			}
		}
		return false;
	};
	for (auto &g : m.random_effects)
		for (auto &t : g.term_names)
			if (matches(t)) return true;
	return false;
}


// ─────────────────────────────────────────────────────────────────────
// Family dispatch
// ─────────────────────────────────────────────────────────────────────

struct FamilySpec
{
	const char *family;      // Phonometrica family
	const char *link;        // the only link the engine uses for it
	const char *glmmtmb;     // glmmTMB family constructor
	const char *glm;         // stats::glm family, or nullptr if fixed-effects fits go through glmmTMB too
};

const FamilySpec *find_family(const String &family)
{
	static const FamilySpec specs[] = {
		{"gaussian", "identity", "gaussian()", nullptr /* lm() */},
		{"binomial", "logit", "binomial(link = \"logit\")", "binomial(link = \"logit\")"},
		{"poisson", "log", "poisson(link = \"log\")", "poisson(link = \"log\")"},
		{"negbin", "log", "nbinom2(link = \"log\")", nullptr},
		{"beta", "logit", "beta_family(link = \"logit\")", nullptr},
		{"student", "identity", "t_family(link = \"identity\")", nullptr},
	};
	for (auto &s : specs)
		if (family == s.family) return &s;
	return nullptr;
}

enum class Engine { Lm, Glm, GlmmTMB };

Engine choose_engine(const Model &m)
{
	if (m.has_random_effects()) return Engine::GlmmTMB;
	if (m.family == "gaussian") return Engine::Lm;
	if (m.family == "binomial" || m.family == "poisson") return Engine::Glm;
	// negbin, beta, student: the engine always takes the unified Laplace path
	// for families with a dispersion parameter, so glmmTMB is the like-for-like
	// comparison even without random effects (MASS::glm.nb and betareg are not).
	return Engine::GlmmTMB;
}


// ─────────────────────────────────────────────────────────────────────
// Variables of the model frame
// ─────────────────────────────────────────────────────────────────────

struct VarDesc
{
	std::string name;
	std::string rname;                 // how R refers to it in the formula (backticked or aliased)
	std::string alias;                 // syntactic alias column, if any (see make_plan)
	intptr_t col = -1;
	bool numeric = true;
	std::vector<std::string> levels;   // categorical only, reference first
	bool predictor = false;            // used in the fixed part or as a random slope
	bool grouping = false;             // a (component of a) random-effects grouping factor
	bool response = false;
	bool offset = false;
};

intptr_t find_column(const DataTable &data, const std::string &name)
{
	for (intptr_t j = 0; j < data.column_count(); j++)
		if (str(data.get_header(j)) == name) return j;
	return -1;
}

// Same rule and ordering as fitting.cpp's is_numeric_column / extract_levels:
// numeric iff every non-missing cell of the used rows parses; levels sorted
// bytewise with the user's reference level first.
void describe(VarDesc &v, const DataTable &data, const std::vector<intptr_t> &rows,
              const std::map<String, String> &reference_levels)
{
	v.numeric = true;
	for (intptr_t r : rows)
	{
		String cell = data.get_cell(r, v.col);
		if (is_missing_cell(cell)) continue;
		double x;
		if (!parse_cell(cell, &x))
		{
			v.numeric = false;
			break;
		}
	}
	if (v.numeric) return;

	std::set<std::string> seen;
	for (intptr_t r : rows) seen.insert(str(data.get_cell(r, v.col)));
	std::string ref;
	auto it = reference_levels.find(String(v.name.c_str()));
	if (it != reference_levels.end()) ref = str(it->second);
	v.levels.clear();
	if (!ref.empty() && seen.count(ref)) v.levels.push_back(ref);
	for (auto &s : seen)
		if (s != ref || ref.empty()) v.levels.push_back(s);
}


// ─────────────────────────────────────────────────────────────────────
// Design-term enumeration (the source of both naming schemes)
// ─────────────────────────────────────────────────────────────────────

struct NamedColumn
{
	std::string phon;  // "vowel[e]:sex[m]"
	std::string r;     // "vowele:sexm" (with backticks where R puts them)
};

// Columns of one term, in the engine's order (build_interaction: the first
// component varies slowest). `full_rank` mirrors expand_variable's flag.
std::vector<NamedColumn> term_columns(const std::vector<const VarDesc *> &vars, bool full_rank)
{
	std::vector<NamedColumn> result{{"", ""}};
	bool first = true;
	for (auto *v : vars)
	{
		std::vector<NamedColumn> one;
		if (v->numeric)
		{
			one.push_back({v->name, v->rname});
		}
		else
		{
			for (size_t k = full_rank ? 0 : 1; k < v->levels.size(); k++)
				one.push_back({v->name + "[" + v->levels[k] + "]", v->rname + v->levels[k]});
		}
		std::vector<NamedColumn> crossed;
		for (auto &l : result)
			for (auto &o : one)
				crossed.push_back({first ? o.phon : l.phon + ":" + o.phon, first ? o.r : l.r + ":" + o.r});
		result = std::move(crossed);
		first = false;
	}
	return result;
}


// ─────────────────────────────────────────────────────────────────────
// Everything the writers need, resolved and cross-checked once.
// ─────────────────────────────────────────────────────────────────────

struct Plan
{
	const Model *model = nullptr;
	Formula formula;
	Formula r_formula;                              // `formula` with aliased variables renamed
	const FamilySpec *fam = nullptr;
	Engine engine = Engine::Lm;
	std::vector<VarDesc> vars;                      // model-frame columns, in output order
	std::vector<NamedColumn> fixed;                 // fixed-effect coefficients
	std::vector<std::vector<NamedColumn>> re_terms; // per random-effects group
	std::vector<std::string> re_labels;             // "speaker", "school:class"
	bool student_scaled = false;

	const VarDesc *var(const std::string &name) const
	{
		for (auto &v : vars)
			if (v.name == name) return &v;
		return nullptr;
	}
};

VarDesc &add_var(std::vector<VarDesc> &vars, const DataTable &data, const std::string &name)
{
	for (auto &v : vars)
		if (v.name == name) return v;
	VarDesc v;
	v.name = name;
	v.col = find_column(data, name);
	if (v.col < 0)
		fail("The column '" + name + "' used by this model is no longer in the data. "
		     "Refit the model before exporting it.");
	vars.push_back(std::move(v));
	return vars.back();
}

// Phonometrica's and R's names for every fixed-effect coefficient and every
// random-effect term, from the formula and the variables' current R names.
void compute_names(Plan &plan)
{
	const Formula &f = plan.formula;
	plan.fixed.clear();
	plan.re_terms.clear();
	plan.re_labels.clear();
	if (f.intercept) plan.fixed.push_back({"Intercept", "(Intercept)"});
	for (auto &t : f.fixed)
	{
		std::vector<const VarDesc *> comps;
		for (auto &v : t.variables) comps.push_back(plan.var(str(v)));
		for (auto &c : term_columns(comps, false)) plan.fixed.push_back(c);
	}
	for (intptr_t k = 0; k < f.random.size(); k++)
	{
		const auto &rt = f.random[k];
		std::vector<NamedColumn> cols;
		if (rt.intercept) cols.push_back({"Intercept", "(Intercept)"});
		for (auto &t : rt.slopes)
		{
			std::vector<const VarDesc *> comps;
			for (auto &v : t.variables) comps.push_back(plan.var(str(v)));
			bool full = t.variables.size() == 1 && !rt.intercept;
			for (auto &c : term_columns(comps, full)) cols.push_back(c);
		}
		plan.re_terms.push_back(std::move(cols));
		plan.re_labels.push_back(k < plan.model->random_effects.size()
		                         ? str(plan.model->random_effects[k].group_name) : str(rt.group));
	}
}

const char *stale_data_hint = " The data has probably changed since the model was fitted: refit it, then export again.";

Plan make_plan(const Model &model, const DataTable &data, const ExportROptions &opts)
{
	String blocker = r_export_blocker(model);
	if (!blocker.empty()) fail(str(blocker));

	Plan plan;
	plan.model = &model;
	plan.formula = Formula::parse(model.formula);
	plan.fam = find_family(model.family);
	plan.engine = choose_engine(model);
	const Formula &f = plan.formula;

	// ── Rows ──────────────────────────────────────────────────────────
	const auto &rows = model.source_rows;
	if (static_cast<intptr_t>(rows.size()) != model.nobs)
		fail("This model's record of the rows it was fitted on is inconsistent (" + fmt_int(rows.size()) +
		     " rows for n = " + fmt_int(model.nobs) + ")." + stale_data_hint);
	for (intptr_t r : rows)
		if (r < 0 || r >= data.row_count())
			fail(std::string("The model refers to rows that are no longer in the data.") + stale_data_hint);

	// ── Columns, in a stable, readable order ─────────────────────────
	{
		auto &v = add_var(plan.vars, data, str(f.response));
		v.response = true;
	}
	auto add_predictor = [&](const String &name) { add_var(plan.vars, data, str(name)).predictor = true; };
	for (auto &t : f.fixed)
		for (auto &v : t.variables) add_predictor(v);
	for (auto &rt : f.random)
		for (auto &t : rt.slopes)
			for (auto &v : t.variables) add_predictor(v);
	for (auto &rt : f.random)
		for (auto &g : split_group(rt.group, rt.is_synthetic_group))
			add_var(plan.vars, data, g).grouping = true;
	if (f.has_offset()) add_var(plan.vars, data, str(f.offset)).offset = true;

	for (auto &v : plan.vars)
	{
		if (v.grouping && !v.predictor && !v.response && !v.offset)
		{
			// Grouping is by cell string, as in build_grouping: always text.
			v.numeric = false;
			std::set<std::string> seen;
			for (intptr_t r : rows) seen.insert(str(data.get_cell(r, v.col)));
			v.levels.assign(seen.begin(), seen.end());
			continue;
		}
		describe(v, data, rows, opts.reference_levels);
	}

	// R names (aliases for non-syntactic random-slope names are decided
	// across the whole export, see apply_aliases).
	for (auto &v : plan.vars) v.rname = r_name(v.name);
	plan.r_formula = f;

	// Every exported cell must be present (the rows are complete cases).
	for (auto &v : plan.vars)
		for (intptr_t r : rows)
			if (is_missing_cell(data.get_cell(r, v.col)))
				fail("Column '" + v.name + "' has a missing value in a row the model was fitted on." +
				     stale_data_hint);

	// ── Response ─────────────────────────────────────────────────────
	{
		VarDesc &resp = plan.vars.front();
		if (!model.response_levels.empty())
		{
			if (resp.numeric || model.response_levels.size() != 2)
				fail(std::string("The response no longer matches the fitted model.") + stale_data_hint);
			resp.levels = {str(model.response_levels[0]), str(model.response_levels[1])};
			std::set<std::string> seen;
			for (intptr_t r : rows) seen.insert(str(data.get_cell(r, resp.col)));
			for (auto &s : seen)
				if (s != resp.levels[0] && s != resp.levels[1])
					fail("The response '" + resp.name + "' has a value ('" + s +
					     "') that was not present when the model was fitted." + stale_data_hint);
		}
		else if (!resp.numeric)
		{
			fail(std::string("The response is no longer numeric.") + stale_data_hint);
		}

		// Spot-check the response against the stored y vector, when present.
		if (model.y.size() == model.nobs)
		{
			for (intptr_t i = 0; i < model.nobs; i++)
			{
				String cell = data.get_cell(rows[i], resp.col);
				double expected = model.y[i];
				double got;
				if (!model.response_levels.empty())
					got = (str(cell) == resp.levels[1]) ? 1.0 : 0.0;
				else if (!parse_cell(cell, &got))
					got = std::nan("");
				if (!(std::abs(got - expected) <= 1e-9 * std::max(1.0, std::abs(expected))))
					fail("The response values in the data differ from those the model was fitted on." +
					     std::string(stale_data_hint));
			}
		}
	}

	// ── Cross-check the predictors against the model's own metadata ──
	for (auto &vi : model.variable_info)
	{
		const VarDesc *v = plan.var(str(vi.name));
		if (!v) continue;
		bool same = v->numeric == vi.numeric;
		if (same && !v->numeric)
		{
			same = static_cast<intptr_t>(v->levels.size()) == vi.levels.size();
			for (intptr_t k = 0; same && k < vi.levels.size(); k++)
				same = v->levels[k] == str(vi.levels[k]);
		}
		if (!same)
			fail("The variable '" + v->name + "' no longer has the type or levels it had when the model was fitted." +
			     stale_data_hint);
	}

	// ── Coding check with the authoritative, data-derived types ──────
	auto categorical = [&](const std::string &name) {
		auto *v = plan.var(name);
		return v && !v->numeric;
	};
	if (auto msg = formula_coding_blocker(f, categorical); !msg.empty()) fail(msg);

	// ── Coefficient and random-effect term names ─────────────────────
	if (static_cast<intptr_t>(f.random.size()) != model.random_effects.size())
		fail(std::string("The random effects of this model do not match its formula.") + stale_data_hint);
	compute_names(plan);

	bool names_ok = static_cast<intptr_t>(plan.fixed.size()) == model.coef_names.size();
	for (size_t i = 0; names_ok && i < plan.fixed.size(); i++)
		names_ok = plan.fixed[i].phon == str(model.coef_names[i]);
	if (!names_ok)
		fail(std::string("The fixed-effect coefficients of this model do not match its formula and data.") +
		     stale_data_hint);

	for (intptr_t k = 0; k < model.random_effects.size(); k++)
	{
		const auto &grp = model.random_effects[k];
		const auto &cols = plan.re_terms[k];
		bool ok = static_cast<intptr_t>(cols.size()) == grp.term_names.size() &&
		          grp.variance.size() == grp.term_names.size();
		for (size_t i = 0; ok && i < cols.size(); i++)
			ok = cols[i].phon == str(grp.term_names[i]);
		if (!ok)
			fail("The random effects for '" + str(grp.group_name) + "' do not match the formula and data." +
			     stale_data_hint);
	}

	plan.student_scaled = model.family == "student";
	return plan;
}


// ─────────────────────────────────────────────────────────────────────
// Writers
// ─────────────────────────────────────────────────────────────────────

// Create `dir` and any missing parents.
void make_directories(const String &dir)
{
	if (dir.empty() || filesystem::exists(dir)) return;
	std::string d = str(dir);
	while (d.size() > 1 && filesystem::is_separator(d.back())) d.pop_back();
	if (d.size() != static_cast<size_t>(dir.size()))
	{
		make_directories(String(d.c_str()));
		return;
	}
	String parent = filesystem::directory_name(dir);
	if (!parent.empty() && parent != dir) make_directories(parent);
	filesystem::create_directory(dir);
}

void write_file(const String &path, const std::string &content)
{
	std::FILE *fp = os_open_file(path, "wb");
	if (!fp) fail("Cannot write file " + str(path));
	size_t n = std::fwrite(content.data(), 1, content.size(), fp);
	bool ok = n == content.size();
	ok = (std::fclose(fp) == 0) && ok;
	if (!ok) fail("Error while writing file " + str(path));
}

std::string today()
{
	std::time_t t = std::time(nullptr);
	char buf[32];
	std::tm tm_buf{};
#if defined(_WIN32)
	localtime_s(&tm_buf, &t);
#else
	localtime_r(&t, &tm_buf);
#endif
	std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm_buf);
	return buf;
}

std::string phon_version()
{
#if defined(PHON_VERSION_MAJOR) && defined(PHON_VERSION_MINOR) && defined(PHON_VERSION_MICRO)
	return std::to_string(PHON_VERSION_MAJOR) + "." + std::to_string(PHON_VERSION_MINOR) + "." +
	       std::to_string(PHON_VERSION_MICRO);
#else
	return "(development build)";
#endif
}

std::string method_label(const Model &m)
{
	return m.method == Method::REML ? "REML" : "ML";
}

// ── Analysis-wide columns ────────────────────────────────────────────

// A column of data.csv: the union of what the exported models need.
struct Column
{
	std::string name;
	intptr_t col = -1;
	bool any_numeric = false;       // numeric in at least one model
	bool any_categorical = false;   // categorical (or grouping) in at least one model
	std::vector<std::string> levels;
	std::string alias;              // syntactic copy for glmmTMB random slopes

	bool text() const { return any_categorical; }
	bool mixed() const { return any_numeric && any_categorical; }
};

struct Export
{
	std::vector<Plan> plans;
	std::vector<std::string> ids;      // "m3": the model's position in the analysis
	std::vector<std::string> labels;   // display labels
	std::vector<Column> columns;
	std::vector<intptr_t> rows;        // union of the models' rows, sorted
	std::vector<std::pair<std::string, std::string>> skipped;  // (label, reason)

	Column *column(const std::string &name)
	{
		for (auto &c : columns)
			if (c.name == name) return &c;
		return nullptr;
	}
	bool any_glmmtmb() const
	{
		for (auto &p : plans)
			if (p.engine == Engine::GlmmTMB) return true;
		return false;
	}
};

// glmmTMB (checked on 1.1.13) cannot fit a random slope whose variable has a
// non-syntactic name: it stops with "fnames == names(mf) is not TRUE",
// although such names work in the fixed part and as grouping factors. Those
// variables get a syntactic alias, used for the variable everywhere in that
// model's R formula so that the fixed and random parts still refer to the
// same column. Aliases are chosen once for the whole export, so that a
// column has the same alias in every model.
void apply_aliases(Export &ex)
{
	std::set<std::string> taken;
	for (auto &c : ex.columns) taken.insert(c.name);
	for (auto &plan : ex.plans)
	{
		if (plan.engine != Engine::GlmmTMB) continue;
		for (auto &rt : plan.formula.random)
			for (auto &t : rt.slopes)
				for (auto &v : t.variables)
				{
					Column *c = ex.column(str(v));
					if (!c || !c->alias.empty() || r_name(c->name) == c->name) continue;
					std::string a;
					for (unsigned char ch : c->name)
					{
						bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
						          ch == '.' || ch == '_';
						if (ok) a += static_cast<char>(ch);
						else if (a.empty() || a.back() != '_') a += '_';
					}
					if (a.empty() || !((a[0] >= 'a' && a[0] <= 'z') || (a[0] >= 'A' && a[0] <= 'Z'))) a = "x_" + a;
					std::string base = a;
					for (int k = 2; taken.count(a) || r_name(a) != a; k++) a = base + "_" + std::to_string(k);
					taken.insert(a);
					c->alias = a;
				}
	}

	// Rename aliased variables in each glmmTMB model's R formula and names.
	for (auto &plan : ex.plans)
	{
		if (plan.engine != Engine::GlmmTMB) continue;
		bool changed = false;
		for (auto &v : plan.vars)
		{
			Column *c = ex.column(v.name);
			if (c && !c->alias.empty())
			{
				v.alias = c->alias;
				v.rname = c->alias;
				changed = true;
			}
		}
		if (!changed) continue;
		auto rename = [&](Array<FixedTerm> &terms) {
			for (auto &t : terms)
				for (auto &name : t.variables)
					if (auto *v = plan.var(str(name)); v && !v->alias.empty()) name = String(v->alias.c_str());
		};
		plan.r_formula = plan.formula;
		rename(plan.r_formula.fixed);
		for (auto &rt : plan.r_formula.random) rename(rt.slopes);
		compute_names(plan);
	}
}

// Build the analysis-wide view: columns, union of rows, levels. A model whose
// rows could not be reproduced by complete.cases() on the union is skipped.
void build_columns(Export &ex, const DataTable &data, const ExportROptions &opts)
{
	// A model's rows in R are complete.cases() of its columns over the union of
	// rows. Phonometrica took the complete cases over the whole table, so the
	// two agree unless the data changed after the fit; check it, model by model.
	std::set<intptr_t> all;
	for (auto &p : ex.plans) all.insert(p.model->source_rows.begin(), p.model->source_rows.end());
	for (size_t k = 0; k < ex.plans.size();)
	{
		const Plan &p = ex.plans[k];
		intptr_t complete = 0;
		for (intptr_t r : all)
		{
			bool ok = true;
			for (auto &v : p.vars)
				if (is_missing_cell(data.get_cell(r, v.col))) { ok = false; break; }
			complete += ok;
		}
		if (complete != p.model->nobs)
		{
			ex.skipped.push_back({ex.labels[k], "The rows this model was fitted on no longer match the data " +
			                                    std::string("(") + fmt_int(complete) + " complete rows for n = " +
			                                    fmt_int(p.model->nobs) + ")." + stale_data_hint});
			ex.plans.erase(ex.plans.begin() + k);
			ex.ids.erase(ex.ids.begin() + k);
			ex.labels.erase(ex.labels.begin() + k);
			if (ex.plans.empty()) return;
			// The union may have shrunk: start over.
			all.clear();
			for (auto &q : ex.plans) all.insert(q.model->source_rows.begin(), q.model->source_rows.end());
			k = 0;
			continue;
		}
		k++;
	}
	ex.rows.assign(all.begin(), all.end());

	for (auto &p : ex.plans)
	{
		for (auto &v : p.vars)
		{
			Column *c = ex.column(v.name);
			if (!c)
			{
				ex.columns.push_back(Column{});
				c = &ex.columns.back();
				c->name = v.name;
				c->col = v.col;
			}
			(v.numeric ? c->any_numeric : c->any_categorical) = true;
		}
	}

	// Levels over the union of rows, with the user's reference level first and
	// the rest sorted bytewise -- the order fitting.cpp uses. After
	// droplevels() on a model's rows this is exactly that model's level order.
	for (auto &c : ex.columns)
	{
		if (!c.text()) continue;
		std::set<std::string> seen;
		for (intptr_t r : ex.rows)
		{
			String cell = data.get_cell(r, c.col);
			if (!is_missing_cell(cell)) seen.insert(str(cell));
		}
		std::string ref;
		auto it = opts.reference_levels.find(String(c.name.c_str()));
		if (it != opts.reference_levels.end()) ref = str(it->second);
		if (!ref.empty() && seen.count(ref)) c.levels.push_back(ref);
		for (auto &l : seen)
			if (l != ref || ref.empty()) c.levels.push_back(l);
	}
	apply_aliases(ex);
}


// ── data.csv ─────────────────────────────────────────────────────────

std::string make_data_csv(const Export &ex, const DataTable &data)
{
	std::string out;
	for (size_t j = 0; j < ex.columns.size(); j++)
	{
		if (j) out += ',';
		out += csv_text(ex.columns[j].name);
	}
	out += '\n';
	for (intptr_t r : ex.rows)
	{
		for (size_t j = 0; j < ex.columns.size(); j++)
		{
			if (j) out += ',';
			const auto &c = ex.columns[j];
			String cell = data.get_cell(r, c.col);
			if (is_missing_cell(cell))
				out += "NA";   // unquoted, so read.csv always reads it as missing
			else if (c.text())
				out += csv_text(str(cell));
			else
			{
				double x;
				out += parse_cell(cell, &x) ? fmt_num(x) : "NA";
			}
		}
		out += '\n';
	}
	return out;
}


// ── phonometrica_estimates.csv ───────────────────────────────────────

struct EstRow
{
	std::string block, term, key, quantity;
	double value;
};

std::vector<EstRow> make_estimates(const Plan &plan)
{
	const Model &m = *plan.model;
	std::vector<EstRow> rows;
	for (size_t i = 0; i < plan.fixed.size(); i++)
	{
		auto key = canon_key(plan.fixed[i].r);
		rows.push_back({"fixed", plan.fixed[i].phon, key, "estimate", m.beta[i]});
		rows.push_back({"fixed", plan.fixed[i].phon, key, "se", m.se[i]});
	}
	for (size_t k = 0; k < plan.re_terms.size(); k++)
	{
		const auto &grp = m.random_effects[k];
		for (size_t t = 0; t < plan.re_terms[k].size(); t++)
		{
			std::string term = "sd(" + plan.re_terms[k][t].phon + "|" + plan.re_labels[k] + ")";
			std::string key = "re" + std::to_string(k + 1) + "|" + canon_key(plan.re_terms[k][t].r);
			rows.push_back({"varcomp", term, key, "sd", std::sqrt(grp.variance[t])});
		}
	}
	if (m.family == "gaussian")
		rows.push_back({"dispersion", "sd(residual)", "dispersion", "sigma", m.rse});
	else if (m.family == "negbin")
		rows.push_back({"dispersion", "theta", "dispersion", "theta", m.theta});
	else if (m.family == "beta")
		rows.push_back({"dispersion", "phi", "dispersion", "phi", m.phi});
	else if (m.family == "student")
	{
		rows.push_back({"dispersion", "sigma (scale)", "dispersion", "sigma", m.sigma});
		rows.push_back({"dispersion", "nu (df)", "dispersion", "nu", m.nu});
	}
	rows.push_back({"fit", "n", "fit", "nobs", static_cast<double>(m.nobs)});
	rows.push_back({"fit", "log-likelihood", "fit", "loglik", m.loglik});
	rows.push_back({"fit", "AIC", "fit", "aic", m.aic});
	rows.push_back({"fit", "BIC", "fit", "bic", m.bic});
	return rows;
}

std::string make_estimates_csv(const Export &ex)
{
	std::string out = "\"model\",\"block\",\"term\",\"key\",\"quantity\",\"value\"\n";
	for (size_t k = 0; k < ex.plans.size(); k++)
		for (auto &r : make_estimates(ex.plans[k]))
			out += csv_text(ex.ids[k]) + "," + csv_text(r.block) + "," + csv_text(r.term) + "," + csv_text(r.key) +
			       "," + csv_text(r.quantity) + "," + fmt_num(r.value) + "\n";
	return out;
}


// ── fit.R ────────────────────────────────────────────────────────────

std::string r_char_vector(const std::vector<std::string> &items, size_t indent)
{
	// c("a", "b", ...) wrapped at ~100 columns; `indent` is the column at which "c(" starts.
	std::string out = "c(";
	size_t col = indent + 2;
	for (size_t i = 0; i < items.size(); i++)
	{
		std::string lit = r_string(items[i]);
		if (i)
		{
			out += ",";
			col += 1;
			if (col + lit.size() + 1 > 100)
			{
				out += "\n" + std::string(indent + 2, ' ');
				col = indent + 2;
			}
			else
			{
				out += " ";
				col += 1;
			}
		}
		out += lit;
		col += lit.size();
	}
	out += ")";
	return out;
}

std::string col_ref(const std::string &df, const std::string &name)
{
	return df + "[[" + r_string(name) + "]]";
}

// df[["x"]] <- factor(df[["x"]], levels = c(...)), on one line if it fits.
std::string factor_line(const std::string &df, const std::string &name, const std::vector<std::string> &levels)
{
	std::string head = col_ref(df, name) + " <- factor(" + col_ref(df, name) + ", levels = ";
	std::string one = head + r_char_vector(levels, head.size()) + ")";
	if (one.size() <= 100 && one.find('\n') == std::string::npos) return one + "\n";
	return col_ref(df, name) + " <- factor(" + col_ref(df, name) + ",\n    levels = " + r_char_vector(levels, 13) +
	       ")\n";
}

std::string rule(const std::string &title)
{
	std::string line = "# ── " + title + " ";
	// Pad with box-drawing characters (3 bytes each in UTF-8) to ~78 columns.
	size_t visible = 5 + title.size();
	for (size_t i = visible; i < 78; i++) line += "─";
	return line + "\n";
}

std::string engine_name(Engine e)
{
	switch (e)
	{
		case Engine::Lm: return "lm()";
		case Engine::Glm: return "glm()";
		case Engine::GlmmTMB: return "glmmTMB()";
	}
	return "";
}

std::string one_line(std::string s)
{
	for (auto &c : s)
		if (c == '\n' || c == '\r') c = ' ';
	return s;
}

// The block of fit.R for one model.
std::string make_model_block(const Export &ex, size_t k, const ExportROptions &opts)
{
	const Plan &plan = ex.plans[k];
	const Model &m = *plan.model;
	const Formula &f = plan.formula;
	const bool mixed = m.has_random_effects();
	const std::string idx = ex.ids[k].substr(1);   // "3" from "m3"
	const std::string df = "d" + idx, mod = "m" + idx;
	const std::string resp = str(f.response);
	std::string out;

	const std::string title = "Model " + idx;
	out += "\n\n" + rule(ex.labels[k] == title ? title : title + ": " + ex.labels[k]);
	out += "# Phonometrica formula: " + str(m.formula) + "\n";
	out += "# Family: " + str(m.family) + " (" + plan.fam->link + ")   Method: " + method_label(m) +
	       "   n = " + fmt_int(m.nobs) + "\n\n";

	// Rows: complete cases of this model's columns, as in Phonometrica.
	std::vector<std::string> cols;
	for (auto &v : plan.vars) cols.push_back(v.name);
	{
		std::string head = df + " <- droplevels(d[complete.cases(d[";
		out += "# This model's rows: the complete cases of its own columns (as in Phonometrica),\n";
		out += "# with levels that do not occur in them dropped.\n";
		out += head + r_char_vector(cols, head.size()) + "]), ])\n";
		out += "stopifnot(nrow(" + df + ") == " + fmt_int(m.nobs) + ")\n";
	}

	// Columns whose type differs between models.
	for (auto &v : plan.vars)
	{
		const Column *c = const_cast<Export &>(ex).column(v.name);
		if (!c || !c->mixed()) continue;
		if (v.numeric)
			out += col_ref(df, v.name) + " <- as.numeric(as.character(" + col_ref(df, v.name) +
			       "))   # numeric in this model\n";
		else
			out += "# '" + v.name + "' is categorical in this model.\n" +
			       col_ref(df, v.name) + " <- droplevels(factor(" + col_ref(df, v.name) + ", levels = " +
			       r_char_vector(c->levels, 0) + "))\n";
	}

	// Aliases for non-syntactic random-slope names.
	std::string aliases;
	for (auto &v : plan.vars)
		if (!v.alias.empty()) aliases += col_ref(df, v.alias) + " <- " + col_ref(df, v.name) + "\n";
	if (!aliases.empty())
	{
		out += "# glmmTMB cannot fit a random slope on a column whose name is not a syntactic R\n";
		out += "# name (it stops with \"fnames == names(mf) is not TRUE\"), so the formula uses a\n";
		out += "# copy under a syntactic name.\n";
		out += aliases;
	}
	out += "\n";

	std::string r_formula = str(plan.r_formula.to_r_string());
	const bool compare = opts.include_comparison;
	std::string beta_e, se_e, sd_e = "numeric(0)", disp_e = "numeric(0)", fit_e, conv_e, tol_e;

	if (plan.student_scaled)
	{
		std::string z_formula = r_formula;
		z_formula = ".y_z" + z_formula.substr(z_formula.find(" ~ "));
		if (f.has_offset())
		{
			std::string off = "offset(" + r_name(str(f.offset)) + ")";
			auto pos = z_formula.rfind(off);
			if (pos != std::string::npos) z_formula.replace(pos, off.size(), "offset(.offset_z)");
		}
		out += "# glmmTMB's t_family does not converge on a Hz-scale response: free estimation of\n";
		out += "# nu is badly conditioned and diverges (nu -> 1e20 and beyond). Student t is a\n";
		out += "# location-scale family, so fitting on a standardized response and transforming\n";
		out += "# back is exact. nu is invariant under this transform.\n";
		if (f.intercept)
			out += ".m <- mean(" + col_ref(df, resp) + "); .s <- sd(" + col_ref(df, resp) + ")\n";
		else
			out += "# No intercept: scale only, since a shift could not be absorbed.\n"
			       ".m <- 0; .s <- sd(" + col_ref(df, resp) + ")\n";
		out += df + "$.y_z <- (" + col_ref(df, resp) + " - .m) / .s\n";
		if (f.has_offset()) out += df + "$.offset_z <- " + col_ref(df, str(f.offset)) + " / .s\n";
		out += mod + " <- glmmTMB(" + z_formula + ",\n";
		out += std::string(mod.size() + 12, ' ') + "data = " + df + ", family = " + plan.fam->glmmtmb +
		       ", REML = FALSE)\n";
		out += "print(summary(" + mod + "))\n\n";
		out += "# Back-transform to the response scale.\n";
		out += ".beta <- fixef(" + mod + ")$cond * .s\n";
		if (f.intercept) out += ".beta[\"(Intercept)\"] <- .beta[\"(Intercept)\"] + .m\n";
		out += ".se <- sqrt(diag(vcov(" + mod + ")$cond)) * .s\n";
		out += ".sigma <- sigma(" + mod + ") * .s\n";
		out += ".nu <- unname(glmmTMB::family_params(" + mod + "))[1]        # invariant\n";
		out += ".loglik <- as.numeric(logLik(" + mod + ")) - nobs(" + mod + ") * log(.s)   # Jacobian\n";
		out += "cat(\"\\nOn the response scale:\\n\")\n";
		out += "print(cbind(estimate = .beta, se = .se))\n";
		out += "cat(sprintf(\"sigma = %g   nu = %g   log-likelihood = %g\\n\", .sigma, .nu, .loglik))\n";
		beta_e = ".beta";
		se_e = ".se";
		disp_e = "c(sigma = .sigma, nu = .nu)";
		fit_e = "c(nobs = nobs(" + mod + "), loglik = .loglik, aic = AIC(" + mod + ") + 2 * nobs(" + mod +
		        ") * log(.s),\n              bic = BIC(" + mod + ") + 2 * nobs(" + mod + ") * log(.s))";
	}
	else if (plan.engine == Engine::Lm)
	{
		out += mod + " <- lm(" + r_formula + ", data = " + df + ")\n";
		out += "print(summary(" + mod + "))\n";
		out += "print(logLik(" + mod + "))   # ML log-likelihood, as reported by Phonometrica\n";
		beta_e = "coef(summary(" + mod + "))[, \"Estimate\"]";
		se_e = "coef(summary(" + mod + "))[, \"Std. Error\"]";
		disp_e = "c(sigma = summary(" + mod + ")$sigma)";
	}
	else if (plan.engine == Engine::Glm)
	{
		out += mod + " <- glm(" + r_formula + ",\n";
		out += std::string(mod.size() + 8, ' ') + "data = " + df + ", family = " + plan.fam->glm + ")\n";
		out += "print(summary(" + mod + "))\n";
		out += "print(logLik(" + mod + "))\n";
		beta_e = "coef(summary(" + mod + "))[, \"Estimate\"]";
		se_e = "coef(summary(" + mod + "))[, \"Std. Error\"]";
	}
	else
	{
		out += mod + " <- glmmTMB(" + r_formula + ",\n";
		out += std::string(mod.size() + 12, ' ') + "data = " + df + ", family = " + plan.fam->glmmtmb +
		       ", REML = " + (m.method == Method::REML ? "TRUE" : "FALSE") + ")\n";
		out += "print(summary(" + mod + "))\n";
		beta_e = "fixef(" + mod + ")$cond";
		se_e = "sqrt(diag(vcov(" + mod + ")$cond))";
		if (m.family == "gaussian") disp_e = "c(sigma = sigma(" + mod + "))";
		else if (m.family == "negbin") disp_e = "c(theta = sigma(" + mod + "))";
		else if (m.family == "beta") disp_e = "c(phi = sigma(" + mod + "))";
	}

	if (!compare) return out;

	if (fit_e.empty())
		fit_e = "c(nobs = nobs(" + mod + "), loglik = as.numeric(logLik(" + mod + ")), aic = AIC(" + mod +
		        "), bic = BIC(" + mod + "))";
	if (mixed) sd_e = ".sds(" + mod + (plan.student_scaled ? ", .s" : "") + ")";
	switch (plan.engine)
	{
		case Engine::Lm: conv_e = "TRUE"; tol_e = ".tol_exact"; break;
		case Engine::Glm: conv_e = "isTRUE(" + mod + "$converged)"; tol_e = ".tol_exact"; break;
		case Engine::GlmmTMB:
			conv_e = "isTRUE(" + mod + "$fit$convergence == 0) && isTRUE(" + mod + "$sdr$pdHess)";
			tol_e = mixed ? ".tol_mixed" : ".tol_laplace";
			break;
	}
	std::string note = "NULL";
	if (!m.well_identified)
		note = r_string(one_line(m.fit_warning.empty() ? std::string("the model is weakly identified by the data")
		                                               : str(m.fit_warning)));

	out += "\ncmp" + idx + " <- .compare(" + r_string(ex.ids[k]) + ", " + r_string(ex.labels[k]) + ",\n";
	out += "    beta = " + beta_e + ",\n";
	out += "    se = " + se_e + ",\n";
	out += "    sd = " + sd_e + ",\n";
	out += "    disp = " + disp_e + ",\n";
	out += "    fit = " + fit_e + ",\n";
	out += "    tol = " + tol_e + ", converged = " + conv_e + ",\n";
	out += "    weak = " + note + ")\n";
	return out;
}

std::string make_fit_r(const Export &ex, const ExportROptions &opts)
{
	std::string out;

	// ── Header ───────────────────────────────────────────────────────
	out += "# Generated by Phonometrica " + phon_version() + " on " + today() + "\n";
	if (!opts.analysis_path.empty()) out += "# Analysis: " + str(opts.analysis_path) + "\n";
	out += "#\n# Models in this script:\n";
	for (size_t k = 0; k < ex.plans.size(); k++)
		out += "#   " + (ex.labels[k] == "Model " + ex.ids[k].substr(1) ? ex.labels[k] : ex.ids[k].substr(1) + ". " + ex.labels[k]) +
		       ": " + str(ex.plans[k].model->formula) + " [" +
		       str(ex.plans[k].model->family) + ", " + engine_name(ex.plans[k].engine) + "]\n";
	if (!ex.skipped.empty())
	{
		out += "#\n# Not exported:\n";
		for (auto &s : ex.skipped) out += "#   " + s.first + ": " + one_line(s.second) + "\n";
	}
	out += "#\n";
	out += "# Run this script from its own folder (in RStudio: Session > Set Working Directory >\n";
	out += "# To Source File Location), e.g. with source(\"fit.R\") or Rscript fit.R. Each model\n";
	out += "# has its own block below, which can be copied and run on its own after the\n";
	out += "# \"Data\" section. See README.txt for what the comparison means.\n\n";

	out += "# Treatment contrasts, as in Phonometrica, whatever your .Rprofile says.\n";
	out += "options(contrasts = c(\"contr.treatment\", \"contr.poly\"))\n";
	if (ex.any_glmmtmb())
	{
		out += "library(glmmTMB)\n";
		out += "if (packageVersion(\"glmmTMB\") < \"1.1.8\")\n";
		out += "    warning(\"This script was generated for glmmTMB >= 1.1.8.\")\n";
	}

	// ── Data ─────────────────────────────────────────────────────────
	out += "\n" + rule("Data");
	out += "# Every row used by at least one model, and every column used by at least one\n";
	out += "# model. check.names = FALSE keeps column names that are not syntactic R names, and\n";
	out += "# colClasses stops read.csv from guessing types (a categorical code such as \"01\"\n";
	out += "# must stay a string).\n";
	{
		// colClasses = c("a" = "numeric", ...), wrapped at ~100 columns.
		const std::string head = "              colClasses = c(";
		std::string named = head;
		size_t col = head.size();
		for (size_t j = 0; j < ex.columns.size(); j++)
		{
			std::string item = r_string(ex.columns[j].name) + " = " +
			                   r_string(ex.columns[j].text() ? "character" : "numeric");
			if (j)
			{
				named += ",";
				if (col + item.size() + 2 > 100)
				{
					named += "\n" + std::string(head.size(), ' ');
					col = head.size();
				}
				else
				{
					named += " ";
					col += 2;
				}
			}
			named += item;
			col += item.size();
		}
		out += "d <- read.csv(\"data.csv\", check.names = FALSE, fileEncoding = \"UTF-8\",\n";
		out += named + "))\n\n";
	}
	{
		std::string lines;
		for (auto &c : ex.columns)
			if (c.text() && !c.mixed()) lines += factor_line("d", c.name, c.levels);
		if (!lines.empty())
		{
			out += "# Factor levels in Phonometrica's order: a level chosen as reference comes first, the\n";
			out += "# others follow in alphabetical (byte) order. For a binary response, the second\n";
			out += "# level is coded 1.\n";
			out += lines;
		}
	}

	if (opts.include_comparison)
	{
		out += "\n" + rule("Comparison helpers");
		out += "# Phonometrica's estimates, compared with R's after each fit.\n";
		out += ".ref <- read.csv(\"phonometrica_estimates.csv\", check.names = FALSE, fileEncoding = \"UTF-8\",\n";
		out += "                 colClasses = c(rep(\"character\", 5), \"numeric\"))\n\n";
		out += "# R's coefficient name without backticks, interaction components sorted bytewise\n";
		out += "# (enc2utf8: the radix sort rejects non-ASCII strings in the native encoding).\n";
		out += ".canon <- function(x)\n";
		out += "    vapply(strsplit(gsub(\"`\", \"\", enc2utf8(x), fixed = TRUE), \":\", fixed = TRUE),\n";
		out += "           function(p) paste(sort(enc2utf8(p), method = \"radix\"), collapse = \":\"), \"\")\n\n";
		out += "# Random-effect SDs keyed by the position of their term in the formula: glmmTMB\n";
		out += "# passes group names through make.names(), so they cannot be matched by name.\n";
		out += ".sds <- function(m, scale = 1) {\n";
		out += "    vc <- VarCorr(m)$cond\n";
		out += "    unlist(lapply(seq_along(vc), function(k) {\n";
		out += "        s <- attr(vc[[k]], \"stddev\") * scale\n";
		out += "        setNames(unname(s), paste0(\"re\", k, \"|\", .canon(names(s))))\n";
		out += "    }))\n";
		out += "}\n\n";
		out += "# Tolerance on |Phonometrica - R| / max(1, |R|): lm/glm fits are exact; Laplace fits\n";
		out += "# agree to the optimizers' tolerance; random-effect SDs are the most sensitive.\n";
		out += ".tol_exact <- c(estimate = 1e-4, se = 1e-4, sd = 1e-3, sigma = 1e-4, nobs = 0,\n";
		out += "                loglik = 1e-4, aic = 1e-4, bic = 1e-4)\n";
		out += ".tol_laplace <- c(estimate = 5e-3, se = 5e-3, sd = 5e-2, sigma = 1e-2, theta = 2e-2,\n";
		out += "                  phi = 1e-2, nu = 2e-2, nobs = 0, loglik = 1e-3, aic = 1e-3, bic = 1e-3)\n";
		out += ".tol_mixed <- c(estimate = 2e-2, se = 2e-2, sd = 1e-1, sigma = 2e-2, theta = 5e-2,\n";
		out += "                phi = 2e-2, nu = 5e-2, nobs = 0, loglik = 1e-3, aic = 1e-3, bic = 1e-3)\n";
		out += "# An estimate also agrees if it differs by at most 2% of its standard error: a\n";
		out += "# coefficient close to zero can differ by much more than 2% of its value while the\n";
		out += "# two programs agree on everything that matters for inference.\n";
		out += ".tol_in_se <- 0.02\n\n";
		out += ".compare <- function(model, label, beta, se, sd, disp, fit, tol, converged, weak = NULL) {\n";
		out += "    r <- rbind(\n";
		out += "        data.frame(key = .canon(names(beta)), quantity = \"estimate\", R = unname(beta)),\n";
		out += "        data.frame(key = .canon(names(se)), quantity = \"se\", R = unname(se)),\n";
		out += "        data.frame(key = as.character(names(sd)), quantity = rep(\"sd\", length(sd)),\n";
		out += "                   R = unname(sd)),\n";
		out += "        data.frame(key = rep(\"dispersion\", length(disp)), quantity = as.character(names(disp)),\n";
		out += "                   R = unname(disp)),\n";
		out += "        data.frame(key = \"fit\", quantity = names(fit), R = unname(fit)))\n";
		out += "    ref <- .ref[.ref$model == model, ]\n";
		out += "    i <- match(paste(ref$key, ref$quantity, sep = \"\\r\"), paste(r$key, r$quantity, sep = \"\\r\"))\n";
		out += "    out <- data.frame(model = model, label = label, term = ref$term, quantity = ref$quantity,\n";
		out += "                      phonometrica = ref$value, R = r$R[i])\n";
		out += "    out$difference <- out$phonometrica - out$R\n";
		out += "    out$rel.difference <- out$difference / pmax(1, abs(out$R))\n";
		out += "    # For an estimate, the difference in units of R's standard error: the scale on\n";
		out += "    # which it matters for inference.\n";
		out += "    se_r <- r$R[match(paste(ref$key, \"se\", sep = \"\\r\"), paste(r$key, r$quantity, sep = \"\\r\"))]\n";
		out += "    out$diff.in.se <- ifelse(out$quantity == \"estimate\", out$difference / se_r, NA)\n";
		out += "    close <- abs(out$rel.difference) <= tol[out$quantity] + 1e-12\n";
		out += "    tiny <- out$quantity == \"estimate\" & abs(out$diff.in.se) <= .tol_in_se\n";
		out += "    ok <- (!is.na(close) & close) | (!is.na(tiny) & tiny)\n";
		out += "    out$status <- ifelse(is.na(i), \"not in R\", ifelse(is.na(out$R), \"NA in R\",\n";
		out += "                  ifelse(ok, \"ok\", \"CHECK\")))\n";
		out += "    out$converged <- converged\n";
		out += "    cat(\"\\n==== \", label, \": Phonometrica vs R ====\\n\\n\", sep = \"\")\n";
		out += "    if (!converged)\n";
		out += "        cat(\"NOTE: R reports that its own fit did not converge (or that its Hessian is not\\n\",\n";
		out += "            \"      positive definite). Differences below then reflect R's fit, not a defect\\n\",\n";
		out += "            \"      in Phonometrica's estimates.\\n\\n\", sep = \"\")\n";
		out += "    if (!is.null(weak)) {\n";
		out += "        cat(\"NOTE: Phonometrica flagged this fit as weakly identified:\\n\")\n";
		out += "        cat(strwrap(weak, indent = 6, exdent = 6), sep = \"\\n\")\n";
		out += "        cat(\"      Estimates along a flat direction of the likelihood (typically a random-effect\\n\",\n";
		out += "            \"      SD at or near zero) are not expected to agree between programs.\\n\\n\", sep = \"\")\n";
		out += "    }\n";
		out += "    shown <- format(out[c(\"term\", \"quantity\", \"phonometrica\", \"R\", \"difference\",\n";
		out += "                          \"rel.difference\", \"diff.in.se\", \"status\")], digits = 6)\n";
		out += "    shown$diff.in.se[is.na(out$diff.in.se)] <- \"\"   # only defined for estimates\n";
		out += "    print(shown, row.names = FALSE)\n";
		out += "    invisible(out)\n";
		out += "}\n";
	}

	for (size_t k = 0; k < ex.plans.size(); k++) out += make_model_block(ex, k, opts);

	if (opts.include_comparison)
	{
		out += "\n\n" + rule("Summary");
		out += "cmp <- rbind(";
		for (size_t k = 0; k < ex.plans.size(); k++)
		{
			if (k) out += ", ";
			out += "cmp" + ex.ids[k].substr(1);
		}
		out += ")\n";
		out += "cat(\"\\n==== Summary: Phonometrica vs R ====\\n\\n\")\n";
		out += "print(do.call(rbind, lapply(split(cmp, factor(cmp$model, levels = unique(cmp$model))),\n";
		out += "    function(x) data.frame(model = x$label[1], values = nrow(x), ok = sum(x$status == \"ok\"),\n";
		out += "                           check = sum(x$status != \"ok\"), R.converged = x$converged[1]))),\n";
		out += "    row.names = FALSE)\n";
		out += "cat(\"\\nrel.difference is relative to max(1, |R|); diff.in.se is the difference between the\\n\",\n";
		out += "    \"estimates in units of R's standard error, and an estimate within 0.02 SE counts as\\n\",\n";
		out += "    \"agreeing. Coefficients and SEs should agree to\\n\",\n";
		out += "    \"several significant digits. For non-Gaussian mixed models the two programs use\\n\",\n";
		out += "    \"different Laplace implementations, so log-likelihoods may differ in the second\\n\",\n";
		out += "    \"decimal; random-effect SDs are the most sensitive quantity. The data frame `cmp`\\n\",\n";
		out += "    \"holds every comparison.\\n\", sep = \"\")\n";
		if (!ex.skipped.empty())
		{
			out += "cat(\"\\nNot exported:\\n\")\n";
			for (auto &s : ex.skipped)
				out += "cat(" + r_string("  " + s.first + ": " + one_line(s.second) + "\n") + ")\n";
		}
	}

	out += "\n" + rule("Session");
	out += "sessionInfo()\n";
	return out;
}


// ── README.txt ───────────────────────────────────────────────────────

std::string make_readme(const Export &ex, const ExportROptions &opts)
{
	std::string out;
	out += "Phonometrica analysis exported to R\n";
	out += "===================================\n\n";
	if (!opts.analysis_path.empty()) out += "Analysis: " + str(opts.analysis_path) + "\n";
	out += "Exported: " + today() + " by Phonometrica " + phon_version() + "\n\n";

	out += "Models\n------\n\n";
	for (size_t k = 0; k < ex.plans.size(); k++)
	{
		const Model &m = *ex.plans[k].model;
		out += "  " + ex.ids[k].substr(1) + ". " + ex.labels[k] + "\n";
		out += "     " + str(m.formula) + "\n";
		out += "     " + str(m.family) + " (" + ex.plans[k].fam->link + "), " + method_label(m) + ", n = " +
		       fmt_int(m.nobs) + ", fitted in R with " + engine_name(ex.plans[k].engine) + "\n";
	}
	if (!ex.skipped.empty())
	{
		out += "\nNot exported:\n\n";
		for (auto &s : ex.skipped) out += "  " + s.first + ": " + one_line(s.second) + "\n";
	}
	out += "\n";

	out += "Files\n-----\n\n";
	out += "  data.csv                    the data the models were fitted on\n";
	out += "  fit.R                       fits the same models in R\n";
	if (opts.include_comparison)
		out += "  phonometrica_estimates.csv  Phonometrica's estimates, read by fit.R\n";
	out += "  README.txt                  this file\n\n";

	out += "How to run\n----------\n\n";
	out += "Open fit.R in RStudio and use \"Source\", or run from a terminal in this folder:\n\n";
	out += "    Rscript fit.R\n\n";
	out += "The script must run with this folder as its working directory.\n";
	if (ex.any_glmmtmb())
		out += "It needs the glmmTMB package (install.packages(\"glmmTMB\")), version 1.1.8 or later.\n";
	out += "Each model has its own block in fit.R, which you can copy and run on its own after\n";
	out += "the \"Data\" section.\n\n";

	out += "About the data\n--------------\n\n";
	out += "data.csv holds every row used by at least one model and every column used by at\n";
	out += "least one model. Each block of fit.R selects its model's rows the way Phonometrica\n";
	out += "did -- the rows with no missing value in that model's columns -- and checks that it\n";
	out += "finds exactly as many as Phonometrica used.\n\n";
	out += "Phonometrica fits models on the whole table, ignoring any filter applied in the\n";
	out += "table view: filtering is a display matter, and a filtered table is turned into a\n";
	out += "separate dataset with Subset.\n\n";
	out += "Categorical variables are given their levels explicitly in fit.R, with\n";
	out += "Phonometrica's reference level first, so that coefficients have the same meaning in\n";
	out += "both programs.\n\n";

	bool student = false;
	for (auto &p : ex.plans) student = student || p.student_scaled;
	if (student)
	{
		out += "About Student t fits\n--------------------\n\n";
		out += "fit.R fits Student t models to a standardized response and transforms the\n";
		out += "estimates back, because glmmTMB's t_family does not converge reliably on a\n";
		out += "response in its original units (e.g. Hz). Student t is a location-scale family,\n";
		out += "so this is exact; nu is unchanged by the transform.\n\n";
	}

	if (opts.include_comparison)
	{
		out += "Reading the comparison\n----------------------\n\n";
		out += "After each model, fit.R prints Phonometrica's and R's values side by side, with\n";
		out += "their difference and a status column, and it ends with a summary of all models.\n";
		out += "\"ok\" means the two agree within the tolerance expected for this kind of model;\n";
		out += "\"CHECK\" means they differ by more than that. Fixed-effects linear and generalized\n";
		out += "linear models should agree almost exactly. Mixed models are fitted by numerical\n";
		out += "optimization in both programs, so small differences are normal, and random-effect\n";
		out += "standard deviations are the most sensitive quantity. If R reports that its own fit\n";
		out += "did not converge, the differences reflect R's fit rather than Phonometrica's.\n\n";
		out += "The data frame `cmp` holds all comparisons after the script has run.\n";
	}
	return out;
}

} // anonymous namespace


// =====================================================================
// Public API
// =====================================================================

String r_export_blocker(const Model &model)
{
	if (model.is_bayesian())
		return "Bayesian fits cannot be exported to R. Refit with frequentist estimation to export, "
		       "or see the documentation on checking Bayesian results.";
	if (model.has_smooth_terms())
		return "Models with smooth terms cannot be exported: Phonometrica's spline basis is not mgcv's "
		       "parameterization, so the two fits are not directly comparable.";
	auto *fam = find_family(model.family);
	if (!fam)
		return String::format("The '%s' family cannot be exported to R.", model.family.data());
	if (!model.link.empty() && model.link != fam->link)
		return String::format("Only the %s link is supported for the %s family.", fam->link, fam->family);
	if (!model.has_source_rows())
		return "This model was saved by an older version of Phonometrica that did not record which rows "
		       "it was fitted on. Refit it to export it to R.";

	Formula f;
	try
	{
		f = Formula::parse(model.formula);
	}
	catch (std::exception &e)
	{
		return String::format("The model's formula cannot be read: %s", e.what());
	}
	if (f.has_smooth_terms())
		return "Models with smooth terms cannot be exported: Phonometrica's spline basis is not mgcv's "
		       "parameterization, so the two fits are not directly comparable.";

	auto msg = formula_coding_blocker(f, [&](const std::string &v) { return model_says_categorical(model, v); });
	if (!msg.empty()) return String(msg.c_str());

	return String();
}


String r_export_folder_name(const String &analysis_name)
{
	std::string out;
	for (unsigned char c : str(analysis_name))
	{
		bool bad = c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
		           c == '"' || c == '<' || c == '>' || c == '|' || c == ' ' || c == '\t';
		if (bad)
		{
			if (!out.empty() && out.back() != '_') out += '_';
		}
		else
		{
			out += static_cast<char>(c);  // UTF-8 bytes pass through
		}
	}
	while (!out.empty() && (out.back() == '_' || out.back() == '.')) out.pop_back();
	while (!out.empty() && (out.front() == '_' || out.front() == '.')) out.erase(out.begin());
	if (out.empty()) out = "analysis";
	out += "_R";
	return String(out.c_str());
}


ExportRResult export_to_r(const std::vector<ExportRModel> &models, const DataTable &data, const String &dest_dir,
                          const ExportROptions &options)
{
	Export ex;
	for (size_t i = 0; i < models.size(); i++)
	{
		const auto &em = models[i];
		std::string label = em.label.empty() ? "Model " + std::to_string(i + 1) : str(em.label);
		try
		{
			ex.plans.push_back(make_plan(*em.model, data, options));
			ex.ids.push_back("m" + std::to_string(i + 1));
			ex.labels.push_back(label);
		}
		catch (std::exception &e)
		{
			ex.skipped.push_back({label, e.what()});
		}
	}
	if (!ex.plans.empty()) build_columns(ex, data, options);

	ExportRResult result;
	for (auto &s : ex.skipped)
		result.skipped.push_back({String(s.first.c_str()), String(s.second.c_str())});
	if (ex.plans.empty())
	{
		std::string msg = models.size() == 1 ? std::string() : "None of the models can be exported to R.\n";
		for (auto &s : ex.skipped) msg += (models.size() == 1 ? "" : s.first + ": ") + s.second + "\n";
		if (!msg.empty() && msg.back() == '\n') msg.pop_back();
		fail(msg.empty() ? "There is no model to export." : msg);
	}

	// Generate everything before touching the disk, so that a failure leaves
	// no half-written folder behind.
	std::string data_csv = make_data_csv(ex, data);
	std::string fit_r = make_fit_r(ex, options);
	std::string readme = make_readme(ex, options);
	std::string estimates;
	if (options.include_comparison) estimates = make_estimates_csv(ex);

	if (filesystem::exists(dest_dir) && !filesystem::is_directory(dest_dir))
		fail("Cannot export to " + str(dest_dir) + ": a file with this name already exists.");
	make_directories(dest_dir);

	result.directory = dest_dir;
	for (auto &l : ex.labels) result.exported.append(String(l.c_str()));
	auto write_one = [&](const char *name, const std::string &content) {
		String path = filesystem::join(std::string_view(dest_dir.data(), dest_dir.size()), name);
		write_file(path, content);
		result.files.append(path);
	};
	write_one("README.txt", readme);
	write_one("data.csv", data_csv);
	write_one("fit.R", fit_r);
	if (options.include_comparison)
		write_one("phonometrica_estimates.csv", estimates);
	else
	{
		// A stale estimates file from an earlier export would no longer match.
		String stale = filesystem::join(std::string_view(dest_dir.data(), dest_dir.size()),
		                                "phonometrica_estimates.csv");
		if (filesystem::exists(stale)) filesystem::remove_file(stale);
	}
	return result;
}


ExportRResult export_to_r(const Model &model, const DataTable &data, const String &dest_dir,
                          const ExportROptions &options)
{
	return export_to_r(std::vector<ExportRModel>{{&model, model.label}}, data, dest_dir, options);
}

} // namespace phonometrica::stats
