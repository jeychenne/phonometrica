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
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/file.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/runtime/regex.hpp>
#include <phon/application/project.hpp>
#include <phon/application/data_table.hpp>
#include <phon/application/conc/concordance.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/analysis/fitting.hpp>

namespace phonometrica {


DataTable::DataTable(Class *klass, Directory *parent, String path) :
		Document(klass, parent, std::move(path))
{

}

void DataTable::from_xml(xml_node root, const String &project_dir)
{
	static const std::string_view path_tag("Path");

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == path_tag)
		{
			String path(node.text().get());
			Project::interpolate(path, project_dir);
			m_path = std::move(path);
		}
	}
}

void DataTable::save_metadata()
{
	// Metadata is now embedded in the project file for all file types.
	if (uses_external_metadata()) {
		Document::save_metadata();
	}
}

bool DataTable::uses_external_metadata() const
{
	// TODO: native datasets won't use external metadata
	return is<Dataset>();
}

void DataTable::to_csv(const String &path, const String &sep)
{
	File file(path, File::Write);
	auto nrow = this->row_count();
	auto ncol = this->column_count();

	for (intptr_t j = 1; j <= ncol; j++)
	{
		file.write(get_header(j));
		if (j == ncol) file.write('\n');
		else file.write(sep);
	}

	for (intptr_t i = 1; i <= nrow; i++)
	{
		for (intptr_t j = 1; j <= ncol; j++)
		{
			file.write(get_cell(i, j));
			if (j == ncol) file.write('\n');
			else file.write(sep);
		}
	}
}


// =====================================================================
// find_column: locate a column by header name (case-sensitive).
// =====================================================================

intptr_t DataTable::find_column(const String &name) const
{
	auto ncol = column_count();

	for (intptr_t j = 1; j <= ncol; j++)
	{
		if (get_header(j) == name)
			return j;
	}

	return 0;
}


// =====================================================================
// get_field helper: build a List of column headers.
// =====================================================================

static Variant make_headers_list(Runtime &rt, DataTable &table)
{
	auto ncol = table.column_count();
	Array<Variant> result;

	for (intptr_t j = 1; j <= ncol; j++) {
		result.append(table.get_header(j));
	}

	return make_handle<List>(&rt, std::move(result));
}


// =====================================================================
// Summarize a fitted model: format and print to console.
// =====================================================================

static void print_model_summary(Runtime &rt, const stats::Model &m)
{
	// Family display name
	const char *family_display = m.family.data();
	if (m.is_negbin()) family_display = "Negative binomial";

	rt.printf("\nFamily: %s (%s)\n", family_display, m.link.data());
	if (m.is_negbin()) {
		rt.printf("Theta (overdispersion): %.4f\n", m.theta);
	}
	rt.printf("Formula: %s\n", m.formula.data());
	rt.printf("Observations: %ld\n", (long)m.nobs);

	if (!m.response_levels.empty())
	{
		rt.printf("Response levels: %s = 0, %s = 1\n",
		          m.response_levels[1].data(), m.response_levels[2].data());
	}

	rt.printf("\n");

	// Fixed effects table header
	const char *stat_label = m.is_gaussian() ? "t value" : "z value";
	rt.printf("Fixed effects:\n");
	rt.printf("%-24s %12s %12s %12s %12s\n", "", "Estimate", "Std.Error", stat_label, "Pr(>|t|)");

	for (intptr_t i = 1; i <= m.nfixed; i++)
	{
		// Coefficient name
		const char *name = (i <= m.coef_names.size()) ? m.coef_names[i].data() : "?";

		// Format p-value
		char pbuf[16];
		if (m.p[i] < 0.001) {
			snprintf(pbuf, sizeof(pbuf), "< 0.001");
		} else {
			snprintf(pbuf, sizeof(pbuf), "%.4f", m.p[i]);
		}

		// Significance stars
		const char *stars = "";
		if (m.p[i] < 0.001) stars = " ***";
		else if (m.p[i] < 0.01) stars = " **";
		else if (m.p[i] < 0.05) stars = " *";
		else if (m.p[i] < 0.1) stars = " .";

		rt.printf("%-24s %12.4f %12.4f %12.3f %12s%s\n",
		          name, m.beta[i], m.se[i], m.stat[i], pbuf, stars);
	}

	rt.printf("---\n");
	rt.printf("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");

	// Random effects
	if (m.has_random_effects())
	{
		rt.printf("Random effects:\n");
		rt.printf("%-20s %12s %12s %8s\n", "Group", "Variance", "Std.Dev.", "Levels");

		for (intptr_t g = 1; g <= m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];

			for (intptr_t t = 1; t <= re.term_names.size(); t++)
			{
				double var = (t <= re.variance.size()) ? re.variance[t] : 0.0;
				double sd = std::sqrt(std::max(var, 0.0));

				if (t == 1) {
					rt.printf("%-20s %12.4f %12.4f %8ld\n",
					          re.group_name.data(), var, sd, (long)re.nlevels);
				} else {
					rt.printf("  %-18s %12.4f %12.4f\n",
					          re.term_names[t].data(), var, sd);
				}
			}
		}

		if (m.is_gaussian()) {
			rt.printf("%-20s %12.4f %12.4f\n", "Residual", m.rse * m.rse, m.rse);
		}

		rt.printf("\n");
	}
	else if (m.is_gaussian())
	{
		rt.printf("Residual standard error: %.4f on %ld degrees of freedom\n",
		          m.rse, (long)m.df_residual);
		rt.printf("R-squared: %.4f, Adjusted R-squared: %.4f\n", m.r2, m.adj_r2);
	}

	// Information criteria
	rt.printf("AIC: %.1f  BIC: %.1f  logLik: %.1f\n", m.aic, m.bic, m.loglik);

	// Convergence info for iterative methods
	if (m.niter > 0)
	{
		if (m.converged) {
			rt.printf("Converged in %d iterations\n", m.niter);
		} else {
			rt.printf("WARNING: did not converge after %d iterations\n", m.niter);
		}
	}

	rt.printf("\n");
}


// =====================================================================
// filter() implementation: expression-based row filtering.
//
// Syntax:
//   filter(table, "vowel == 'a' and 'F1 (Hz)' > 500")
//   filter(table, "vowel == 'a' or vowel == 'e'")
//   filter(table, "vowel == 'a' and F1 > 500 or vowel == 'e' and F1 > 600")
//   filter(table, "vowel == 'a' and 'F1 (Hz)' > 500", "my label")
//
// Column names and string values may be single-quoted (required when
// they contain spaces or special characters). Bare numbers are parsed
// as numeric thresholds. Clauses are joined by `and` and/or `or`.
// `and` has higher precedence than `or` (standard boolean logic):
//   A and B or C and D  →  (A and B) or (C and D)
//
// Supported operators:
//   ==  !=  <  <=  >  >=  contains  !contains  matches
// =====================================================================

// --- Filter operator enum (file-local) --------------------------------

enum class ScriptFilterOp
{
	Eq,            // ==
	Ne,            // !=
	Lt,            // <
	Le,            // <=
	Gt,            // >
	Ge,            // >=
	Contains,      // contains (case-insensitive)
	NotContains,   // !contains (case-insensitive)
	Regex          // matches (PCRE2 regex)
};

static ScriptFilterOp parse_filter_op(const String &op)
{
	if (op == "==") return ScriptFilterOp::Eq;
	if (op == "!=") return ScriptFilterOp::Ne;
	if (op == "<")  return ScriptFilterOp::Lt;
	if (op == "<=") return ScriptFilterOp::Le;
	if (op == ">")  return ScriptFilterOp::Gt;
	if (op == ">=") return ScriptFilterOp::Ge;
	if (op == "contains")  return ScriptFilterOp::Contains;
	if (op == "!contains") return ScriptFilterOp::NotContains;
	if (op == "matches")   return ScriptFilterOp::Regex;

	throw error("[Type error] Unknown filter operator \"%\". "
	            "Expected one of: ==, !=, <, <=, >, >=, contains, !contains, matches", op);
}

// --- Test a single cell against a prepared clause ---------------------

static bool test_cell(const String &cell, ScriptFilterOp op, const String &value,
                      double num_value, bool num_valid, phonometrica::Regex *re)
{
	switch (op)
	{
	case ScriptFilterOp::Eq:
	{
		if (num_valid) {
			bool ok;
			double cv = cell.to_float(&ok);
			if (ok) return cv == num_value;
		}
		return String::iequals(cell, value);
	}
	case ScriptFilterOp::Ne:
	{
		if (num_valid) {
			bool ok;
			double cv = cell.to_float(&ok);
			if (ok) return cv != num_value;
		}
		return !String::iequals(cell, value);
	}
	case ScriptFilterOp::Lt:
	{
		bool ok;
		double cv = cell.to_float(&ok);
		return ok && cv < num_value;
	}
	case ScriptFilterOp::Le:
	{
		bool ok;
		double cv = cell.to_float(&ok);
		return ok && cv <= num_value;
	}
	case ScriptFilterOp::Gt:
	{
		bool ok;
		double cv = cell.to_float(&ok);
		return ok && cv > num_value;
	}
	case ScriptFilterOp::Ge:
	{
		bool ok;
		double cv = cell.to_float(&ok);
		return ok && cv >= num_value;
	}
	case ScriptFilterOp::Contains:
		return cell.icontains(value);

	case ScriptFilterOp::NotContains:
		return !cell.icontains(value);

	case ScriptFilterOp::Regex:
		return re && re->match(cell);
	}

	return true;
}

// --- Tokenizer --------------------------------------------------------

static std::vector<String> tokenize_filter_expr(const String &expr)
{
	std::vector<String> tokens;
	const char *p   = expr.data();
	const char *end = p + expr.size();

	while (p < end)
	{
		while (p < end && std::isspace(static_cast<unsigned char>(*p)))
			p++;
		if (p >= end) break;

		if (*p == '\'')
		{
			p++;
			const char *start = p;

			while (p < end && *p != '\'')
				p++;
			if (p >= end) {
				throw error("Unterminated quoted string in filter expression");
			}

			tokens.emplace_back(start, intptr_t(p - start));
			p++;
		}
		else
		{
			const char *start = p;

			while (p < end && !std::isspace(static_cast<unsigned char>(*p)) && *p != '\'')
				p++;

			tokens.emplace_back(start, intptr_t(p - start));
		}
	}

	return tokens;
}

// --- Expression parser ------------------------------------------------
//
// Grammar (with standard precedence: `and` binds tighter than `or`):
//
//   expression → and_group ('or' and_group)*
//   and_group  → clause ('and' clause)*
//   clause     → column operator value

struct FilterClause
{
	String column;
	String op;
	String value;
};

using AndGroup = std::vector<FilterClause>;
using FilterExpr = std::vector<AndGroup>;

static FilterExpr parse_filter_expr(const String &expr)
{
	auto tokens = tokenize_filter_expr(expr);
	FilterExpr groups;
	AndGroup current_group;

	size_t i = 0;
	auto n = tokens.size();

	while (i < n)
	{
		if (i + 2 >= n) {
			throw error("Incomplete filter clause: expected 'column operator value' at end of expression");
		}

		FilterClause clause;

		clause.column = tokens[i++];

		clause.op = tokens[i++];
		if (clause.op == "!" && i < n && tokens[i] == "contains") {
			clause.op = "!contains";
			i++;
		}

		if (i >= n) {
			throw error("Missing value after operator \"%\" in filter expression", clause.op);
		}
		clause.value = tokens[i++];

		current_group.push_back(std::move(clause));

		if (i < n)
		{
			if (tokens[i] == "and") {
				i++;
				if (i >= n) {
					throw error("Trailing 'and' at end of filter expression");
				}
			}
			else if (tokens[i] == "or") {
				i++;
				if (i >= n) {
					throw error("Trailing 'or' at end of filter expression");
				}
				groups.push_back(std::move(current_group));
				current_group.clear();
			}
			else {
				throw error("Expected 'and' or 'or' between filter clauses, got \"%\"", tokens[i]);
			}
		}
	}

	if (!current_group.empty()) {
		groups.push_back(std::move(current_group));
	}

	if (groups.empty()) {
		throw error("Empty filter expression");
	}

	return groups;
}

// --- Prepared clause --------------------------------------------------

struct PreparedClause
{
	intptr_t col;
	ScriptFilterOp op;
	String value;
	double num_value;
	bool num_valid;
	std::unique_ptr<phonometrica::Regex> re;
};

using PreparedAndGroup = std::vector<PreparedClause>;

// --- Core filter logic ------------------------------------------------

static Variant filter_rows(DataTable &table, const String &expr, const String &label)
{
	table.open();

	auto groups = parse_filter_expr(expr);

	std::vector<PreparedAndGroup> prepared_groups;
	prepared_groups.reserve(groups.size());

	for (auto &group : groups)
	{
		PreparedAndGroup pg;
		pg.reserve(group.size());

		for (auto &c : group)
		{
			PreparedClause pc;

			pc.col = table.find_column(c.column);
			if (pc.col == 0) {
				throw error("[Index error] Table has no column named \"%\"", c.column);
			}

			pc.op = parse_filter_op(c.op);
			pc.value = std::move(c.value);
			pc.num_value = 0.0;
			pc.num_valid = false;

			if (pc.op == ScriptFilterOp::Eq || pc.op == ScriptFilterOp::Ne ||
			    pc.op == ScriptFilterOp::Lt || pc.op == ScriptFilterOp::Le ||
			    pc.op == ScriptFilterOp::Gt || pc.op == ScriptFilterOp::Ge)
			{
				bool ok;
				pc.num_value = pc.value.to_float(&ok);
				pc.num_valid = ok;

				if (!pc.num_valid && (pc.op == ScriptFilterOp::Lt || pc.op == ScriptFilterOp::Le ||
				                      pc.op == ScriptFilterOp::Gt || pc.op == ScriptFilterOp::Ge))
				{
					throw error("[Type error] Operator \"%\" requires a numeric value, got \"%\"", c.op, pc.value);
				}
			}

			if (pc.op == ScriptFilterOp::Regex) {
				pc.re = std::make_unique<phonometrica::Regex>(pc.value);
			}

			pg.push_back(std::move(pc));
		}

		prepared_groups.push_back(std::move(pg));
	}

	// Scan all rows — a row passes if ANY group matches (OR of ANDs).
	auto nrow = table.row_count();
	std::vector<int> matching;
	matching.reserve(nrow);

	for (intptr_t i = 1; i <= nrow; i++)
	{
		bool row_pass = false;

		for (auto &pg : prepared_groups)
		{
			bool group_pass = true;

			for (auto &pc : pg)
			{
				auto cell = table.get_cell(i, pc.col);

				if (!test_cell(cell, pc.op, pc.value, pc.num_value, pc.num_valid, pc.re.get()))
				{
					group_pass = false;
					break;
				}
			}

			if (group_pass) {
				row_pass = true;
				break;
			}
		}

		if (row_pass) {
			matching.push_back(static_cast<int>(i - 1));
		}
	}

	// Dispatch to the concrete subclass's subset() method.
	if (table.is<Dataset>())
	{
		auto &ds = dynamic_cast<Dataset &>(table);
		auto result = ds.subset(matching, label);
		Project::updated();
		return result;
	}
	else if (table.is<Concordance>())
	{
		auto &conc = dynamic_cast<Concordance &>(table);
		auto result = conc.subset(matching, label);
		Project::updated();
		return result;
	}

	throw error("[Type error] filter() is not supported for this table type");
}


// =====================================================================
// Scripting bindings
// =====================================================================

void DataTable::initialize(Runtime &rt)
{
	// fit(data, formula_string) → Model (gaussian by default)
	auto fit2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		data.open();

		auto formula = stats::Formula::parse(formula_str);
		auto model = stats::fit(data, formula, "gaussian");

		return make_handle<stats::Model>(std::move(model));
	};

	// fit(data, formula_string, family_string) → Model
	auto fit3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		auto &family_str = cast<String>(args[2]);
		data.open();

		auto formula = stats::Formula::parse(formula_str);
		auto model = stats::fit(data, formula, family_str);

		return make_handle<stats::Model>(std::move(model));
	};

	// summarize(model) → prints summary to console
	auto summarize_model = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		print_model_summary(rt, model);
		return Variant();
	};

	// coef(model) → Array of coefficients
	auto coef_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return make_handle<Array<double>>(model.beta);
	};

	// nobs(model) → number of observations
	auto nobs_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.nobs;
	};

	// aic(model) → AIC
	auto aic_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.aic;
	};

	// bic(model) → BIC
	auto bic_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.bic;
	};

	// loglik(model) → log-likelihood
	auto loglik_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return model.loglik;
	};

	// fitted(model) → fitted values array
	auto fitted_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return make_handle<Array<double>>(model.fitted);
	};

	// residuals(model) → residuals array
	auto residuals_model = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		return make_handle<Array<double>>(model.residuals);
	};

	// Model field access: m.formula, m.family, m.aic, m.bic, m.r2, etc.
	auto model_get_field = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		auto &key = cast<String>(args[1]);

		if (key == "formula") return model.formula;
		if (key == "family") return model.family;
		if (key == "link") return model.link;
		if (key == "nobs") return model.nobs;
		if (key == "aic") return model.aic;
		if (key == "bic") return model.bic;
		if (key == "loglik") return model.loglik;
		if (key == "deviance") return model.deviance;
		if (key == "r2") return model.r2;
		if (key == "adj_r2") return model.adj_r2;
		if (key == "rse") return model.rse;
		if (key == "df") return model.df_residual;
		if (key == "theta") return model.theta;
		if (key == "converged") return model.converged;
		if (key == "niter") return intptr_t(model.niter);

		throw error("[Index error] Model type has no member named \"%\"", key);
	};

	// ─── filter(table, expression) ────────────────────────────────────

	auto filter2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto &expr  = cast<String>(args[1]);

		String label("filter: ");
		label.append(expr);

		return filter_rows(table, expr, label);
	};

	// ─── filter(table, expression, label) ─────────────────────────────

	auto filter3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto &expr  = cast<String>(args[1]);
		auto &label = cast<String>(args[2]);

		return filter_rows(table, expr, label);
	};

	// ─── Dataset field access ─────────────────────────────────────────

	auto dataset_get_field = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &ds = cast<Dataset>(args[0]);
		auto &key = cast<String>(args[1]);

		if (key == "path") {
			return ds.path();
		}
		if (key == "label") {
			return ds.label();
		}
		if (key == "description") {
			return ds.description();
		}
		ds.open();
		if (key == "nrow" || key == "length") {
			return ds.row_count();
		}
		if (key == "ncol") {
			return ds.column_count();
		}
		if (key == "empty") {
			return ds.empty();
		}
		if (key == "headers") {
			return make_headers_list(rt, ds);
		}

		throw error("[Index error] Dataset type has no member named \"%\"", key);
	};

	// ─── Concordance field access ─────────────────────────────────────

	auto conc_get_field = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &conc = cast<Concordance>(args[0]);
		auto &key = cast<String>(args[1]);

		if (key == "path") {
			return conc.path();
		}
		if (key == "label") {
			return conc.label();
		}
		if (key == "description") {
			return conc.description();
		}
		conc.open();
		if (key == "nrow" || key == "length") {
			return conc.row_count();
		}
		if (key == "ncol") {
			return conc.column_count();
		}
		if (key == "empty") {
			return conc.empty();
		}
		if (key == "headers") {
			return make_headers_list(rt, conc);
		}
		if (key == "target_count") {
			return intptr_t(conc.target_count());
		}

		throw error("[Index error] Concordance type has no member named \"%\"", key);
	};

#define CLS(T) phonometrica::get_class<T>()

	// Register fit()
	rt.add_global("fit", fit2, { CLS(String), CLS(DataTable) });
	rt.add_global("fit", fit3, { CLS(String), CLS(DataTable), CLS(String) });

	// Register filter()
	rt.add_global("filter", filter2, { CLS(DataTable), CLS(String) });
	rt.add_global("filter", filter3, { CLS(DataTable), CLS(String), CLS(String) });

	// Register model functions
	rt.add_global("summarize", summarize_model, { CLS(stats::Model) });
	rt.add_global("coef", coef_model, { CLS(stats::Model) });
	rt.add_global("nobs", nobs_model, { CLS(stats::Model) });
	rt.add_global("aic", aic_model, { CLS(stats::Model) });
	rt.add_global("bic", bic_model, { CLS(stats::Model) });
	rt.add_global("loglik", loglik_model, { CLS(stats::Model) });
	rt.add_global("fitted", fitted_model, { CLS(stats::Model) });
	rt.add_global("residuals", residuals_model, { CLS(stats::Model) });

	// Register Model field access
	auto model_cls = CLS(stats::Model);
	model_cls->add_method(rt.get_field_string, model_get_field, { CLS(stats::Model), CLS(String) });

	// Register Dataset field access
	auto dataset_cls = CLS(Dataset);
	dataset_cls->add_method(rt.get_field_string, dataset_get_field, { CLS(Dataset), CLS(String) });

	// Register Concordance field access
	auto conc_cls = CLS(Concordance);
	conc_cls->add_method(rt.get_field_string, conc_get_field, { CLS(Concordance), CLS(String) });

#undef CLS
}

} // namespace phonometrica
