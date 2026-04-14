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
#include <phon/analysis/model_comparison.hpp>
#include <phon/application/data_table.hpp>
#include <phon/application/conc/concordance.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/analysis/fitting.hpp>
#include <phon/analysis/emmeans.hpp>
#include <phon/analysis/scaled_residuals.hpp>
#include <phon/analysis/statistics.hpp>

namespace phonometrica {


DataTable::DataTable(Class *klass, Directory *parent, String path) :
		Document(klass, parent, std::move(path))
{

}

void DataTable::from_xml(xml_node root, const String &project_dir)
{
	static const std::string_view path_tag("Path");
	static const std::string_view metadata_tag("Metadata");

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == path_tag)
		{
			String path(node.text().get());
			Project::interpolate(path, project_dir);
			m_path = std::move(path);
		}
		else if (node.name() == metadata_tag)
		{
			metadata_from_xml(node);
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

bool DataTable::needs_metadata_node() const
{
	// Emit a <Metadata> node if there are filter rules or if the base class needs one.
	return !m_filter_rules.empty() || Document::needs_metadata_node();
}

void DataTable::metadata_to_xml(xml_node meta_node)
{
	// Write standard metadata (description, properties).
	Document::metadata_to_xml(meta_node);

	// Write filter rules.
	if (m_filter_rules.empty()) return;

	auto filter_node = meta_node.append_child("FilterRules");
	auto enabled_attr = filter_node.append_attribute("enabled");
	enabled_attr.set_value(m_filter_enabled ? "true" : "false");

	for (intptr_t i = 1; i <= m_filter_rules.size(); i++)
	{
		auto &rule = m_filter_rules[i];
		auto rule_node = filter_node.append_child("Rule");
		rule_node.append_attribute("column").set_value(rule.column.data());
		rule_node.append_attribute("op").set_value(rule.op.data());

		if (rule.op == "in")
		{
			for (intptr_t k = 1; k <= rule.set_values.size(); k++) {
				add_data_node(rule_node, "Value", rule.set_values[k]);
			}
		}
		else
		{
			rule_node.append_attribute("value").set_value(rule.value.data());
		}
	}
}

void DataTable::metadata_from_xml(xml_node meta_node)
{
	// Read standard metadata (description, properties).
	Document::metadata_from_xml(meta_node);

	// Read filter rules.
	static const std::string_view filter_rules_tag("FilterRules");
	static const std::string_view rule_tag("Rule");
	static const std::string_view value_tag("Value");

	for (auto node = meta_node.first_child(); node; node = node.next_sibling())
	{
		if (node.name() != filter_rules_tag) continue;

		auto enabled_attr = node.attribute("enabled");
		m_filter_enabled = !enabled_attr || std::string_view(enabled_attr.value()) != "false";

		for (auto rule_node = node.first_child(); rule_node; rule_node = rule_node.next_sibling())
		{
			if (rule_node.name() != rule_tag) continue;

			FilterRuleData rule;
			rule.column = rule_node.attribute("column").value();
			rule.op = rule_node.attribute("op").value();

			if (rule.op == "in")
			{
				for (auto val_node = rule_node.first_child(); val_node; val_node = val_node.next_sibling())
				{
					if (val_node.name() == value_tag) {
						rule.set_values.append(String(val_node.text().get()));
					}
				}
			}
			else
			{
				auto val_attr = rule_node.attribute("value");
				if (val_attr) {
					rule.value = val_attr.value();
				}
			}

			m_filter_rules.append(std::move(rule));
		}
	}
}

void DataTable::set_filter_rules(Array<FilterRuleData> rules, bool enabled)
{
	m_filter_rules = std::move(rules);
	m_filter_enabled = enabled;
	// Intentionally no modified flag: filter rules are view metadata,
	// not data changes. They are silently persisted when the project is saved.
}

void DataTable::clear_filter_rules()
{
	m_filter_rules.clear();
	m_filter_enabled = true;
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
	const char *family_display = m.family.data();
	if (m.is_negbin()) family_display = "Negative binomial";
	if (m.is_beta()) family_display = "Beta";
	if (m.is_student()) family_display = "Student t (robust)";

	rt.printf("\nFamily: %s (%s)\n", family_display, m.link.data());
	if (m.is_negbin()) {
		rt.printf("Theta (overdispersion): %.4f\n", m.theta);
	}
	if (m.is_beta()) {
		rt.printf("Phi (precision): %.4f\n", m.phi);
	}
	if (m.is_student()) {
		rt.printf("Sigma (scale): %.4f\n", m.sigma);
		rt.printf("Nu (df): %.4f\n", m.nu);
	}
	rt.printf("Formula: %s\n", m.formula.data());
	if (m.is_bayesian()) {
		rt.printf("Estimation: Bayesian (Gaussian approximation)\n");
	}
	rt.printf("Observations: %ld\n", (long)m.nobs);

	if (m.is_bayesian())
	{
		auto prior_str = stats::format_prior_summary(m.priors, m.family);
		rt.printf("\n%s", prior_str.c_str());
	}

	if (!m.response_levels.empty())
	{
		rt.printf("Response levels: %s = 0, %s = 1\n",
		          m.response_levels[1].data(), m.response_levels[2].data());
	}

	rt.printf("\n");

	if (m.is_bayesian())
	{
		// ── Bayesian summary ────────────────────────────────────
		rt.printf("Fixed effects (posterior):\n");

		bool has_mode = !m.posterior_mode.empty();
		bool has_median = !m.posterior_median.empty();

		if (has_mode && has_median)
		{
			rt.printf("%-24s %12s %12s %12s %12s %12s %12s %8s\n",
			          "", "Post.Mean", "Post.Mode", "Post.Median",
			          "Post.SD", "CI.lower", "CI.upper", "pd");
		}
		else
		{
			rt.printf("%-24s %12s %12s %12s %12s %8s\n",
			          "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper", "pd");
		}

		for (intptr_t i = 1; i <= m.nfixed; i++)
		{
			const char *name = (i <= m.coef_names.size()) ? m.coef_names[i].data() : "?";

			double pd_val = (i <= m.pd.size()) ? m.pd[i] : 0.0;
			char pdbuf[16];
			snprintf(pdbuf, sizeof(pdbuf), "%.4f", pd_val);

			const char *stars = "";
			if (pd_val > 0.999) stars = " ***";
			else if (pd_val > 0.99) stars = " **";
			else if (pd_val > 0.975) stars = " *";
			else if (pd_val > 0.95) stars = " .";

			if (has_mode && has_median)
			{
				rt.printf("%-24s %12.4f %12.4f %12.4f %12.4f %12.4f %12.4f %8s%s\n",
				          name,
				          m.posterior_mean[i], m.posterior_mode[i], m.posterior_median[i],
				          m.posterior_sd[i],
				          m.ci_lower[i], m.ci_upper[i],
				          pdbuf, stars);
			}
			else
			{
				rt.printf("%-24s %12.4f %12.4f %12.4f %12.4f %8s%s\n",
				          name,
				          m.posterior_mean[i], m.posterior_sd[i],
				          m.ci_lower[i], m.ci_upper[i],
				          pdbuf, stars);
			}
		}

		rt.printf("---\n");
		rt.printf("pd thresholds: 0.999 '***' 0.99 '**' 0.975 '*' 0.95 '.' (two-sided equivalents)\n\n");

		// Hyperparameters
		if (!m.hyper_names.empty())
		{
			bool has_hyper_sd = !m.hyper_posterior_sd.empty()
			                 && m.hyper_posterior_sd.size() == m.hyper_names.size()
			                 && !std::isnan(m.hyper_posterior_sd[1]);

			if (has_hyper_sd)
			{
				rt.printf("Hyperparameters (posterior):\n");
				rt.printf("%-30s %12s %12s %12s %12s\n",
				          "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper");

				for (intptr_t i = 1; i <= m.hyper_names.size(); i++)
				{
					rt.printf("%-30s %12.4f %12.4f %12.4f %12.4f\n",
					          m.hyper_names[i].data(),
					          m.hyper_posterior_mean[i], m.hyper_posterior_sd[i],
					          m.hyper_ci_lower[i], m.hyper_ci_upper[i]);
				}
			}
			else
			{
				rt.printf("Hyperparameters (posterior):\n");
				rt.printf("%-30s %12s\n", "", "Post.Mean");

				for (intptr_t i = 1; i <= m.hyper_names.size(); i++)
				{
					rt.printf("%-30s %12.4f\n",
					          m.hyper_names[i].data(), m.hyper_posterior_mean[i]);
				}
			}
			rt.printf("\n");
		}
	}
	else
	{
		// ── Frequentist summary ─────────────────────────────────
		const char *stat_label = (m.is_gaussian() || m.is_student()) ? "t value" : "z value";
		rt.printf("Fixed effects:\n");
		rt.printf("%-24s %12s %12s %12s %12s\n", "", "Estimate", "Std.Error", stat_label, "Pr(>|t|)");

		for (intptr_t i = 1; i <= m.nfixed; i++)
		{
			const char *name = (i <= m.coef_names.size()) ? m.coef_names[i].data() : "?";
			char pbuf[16];
			if (m.p[i] < 0.001) snprintf(pbuf, sizeof(pbuf), "< 0.001");
			else snprintf(pbuf, sizeof(pbuf), "%.4f", m.p[i]);

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
	}

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

				if (t == 1) rt.printf("%-20s %12.4f %12.4f %8ld\n", re.group_name.data(), var, sd, (long)re.nlevels);
				else rt.printf("  %-18s %12.4f %12.4f\n", re.term_names[t].data(), var, sd);
			}
		}

		if (m.is_gaussian()) rt.printf("%-20s %12.4f %12.4f\n", "Residual", m.rse * m.rse, m.rse);
		rt.printf("\n");
	}
	else if (m.is_gaussian() && !m.is_bayesian())
	{
		rt.printf("Residual standard error: %.4f on %ld degrees of freedom\n", m.rse, (long)m.df_residual);
		rt.printf("R-squared: %.4f, Adjusted R-squared: %.4f\n", m.r2, m.adj_r2);
	}

	if (!m.is_bayesian())
	{
		rt.printf("AIC: %.1f  BIC: %.1f  logLik: %.1f\n", m.aic, m.bic, m.loglik);
	}
	else
	{
		if (!std::isnan(m.log_marginal))
			rt.printf("Log-marginal likelihood: %.2f  logLik: %.1f\n", m.log_marginal, m.loglik);
		else
			rt.printf("logLik: %.1f\n", m.loglik);

		if (!std::isnan(m.waic))
			rt.printf("WAIC: %.1f  p_WAIC: %.1f\n", m.waic, m.p_waic);
		if (!std::isnan(m.loo_ic))
			rt.printf("LOO-IC: %.1f  p_LOO: %.1f\n", m.loo_ic, m.p_loo);

		// Pareto k diagnostic summary.
		if (!m.pareto_k.empty())
		{
			int n_good = 0, n_ok = 0, n_bad = 0, n_verybad = 0;
			for (intptr_t j = 1; j <= m.pareto_k.size(); j++)
			{
				double k = m.pareto_k[j];
				if (k < 0.5)      n_good++;
				else if (k < 0.7) n_ok++;
				else if (k < 1.0) n_bad++;
				else              n_verybad++;
			}
			if (n_bad == 0 && n_verybad == 0 && n_ok == 0)
				rt.printf("Pareto k: all < 0.5 (good)\n");
			else if (n_bad == 0 && n_verybad == 0)
				rt.printf("Pareto k: %d/%ld > 0.5 (ok, LOO-IC reliable)\n",
				          n_ok, (long)m.pareto_k.size());
			else
				rt.printf("Pareto k: %d/%ld > 0.7 (LOO-IC may be unreliable; consider WAIC)\n",
				          n_bad + n_verybad, (long)m.pareto_k.size());
		}
	}

	if (m.niter > 0)
	{
		if (m.converged) rt.printf("Converged in %d iterations\n", m.niter);
		else rt.printf("WARNING: did not converge after %d iterations\n", m.niter);
	}

	rt.printf("\n");
}


// =====================================================================
// filter() — expression-based row filtering
// =====================================================================

enum class ScriptFilterOp
{
	Eq, Ne, Lt, Le, Gt, Ge, Contains, NotContains, Regex
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

static bool test_cell(const String &cell, ScriptFilterOp op, const String &value,
                      double num_value, bool num_valid, phonometrica::Regex *re)
{
	switch (op)
	{
	case ScriptFilterOp::Eq:
		if (num_valid) { bool ok; double cv = cell.to_float(&ok); if (ok) return cv == num_value; }
		return String::iequals(cell, value);
	case ScriptFilterOp::Ne:
		if (num_valid) { bool ok; double cv = cell.to_float(&ok); if (ok) return cv != num_value; }
		return !String::iequals(cell, value);
	case ScriptFilterOp::Lt: { bool ok; double cv = cell.to_float(&ok); return ok && cv < num_value; }
	case ScriptFilterOp::Le: { bool ok; double cv = cell.to_float(&ok); return ok && cv <= num_value; }
	case ScriptFilterOp::Gt: { bool ok; double cv = cell.to_float(&ok); return ok && cv > num_value; }
	case ScriptFilterOp::Ge: { bool ok; double cv = cell.to_float(&ok); return ok && cv >= num_value; }
	case ScriptFilterOp::Contains:    return cell.icontains(value);
	case ScriptFilterOp::NotContains: return !cell.icontains(value);
	case ScriptFilterOp::Regex:       return re && re->match(cell);
	}
	return true;
}

static std::vector<String> tokenize_filter_expr(const String &expr)
{
	std::vector<String> tokens;
	const char *p = expr.data(), *end = p + expr.size();

	while (p < end)
	{
		while (p < end && std::isspace(static_cast<unsigned char>(*p))) p++;
		if (p >= end) break;

		if (*p == '\'') {
			p++;
			const char *start = p;
			while (p < end && *p != '\'') p++;
			if (p >= end) throw error("Unterminated quoted string in filter expression");
			tokens.emplace_back(start, intptr_t(p - start));
			p++;
		} else {
			const char *start = p;
			while (p < end && !std::isspace(static_cast<unsigned char>(*p)) && *p != '\'') p++;
			tokens.emplace_back(start, intptr_t(p - start));
		}
	}
	return tokens;
}

struct FilterClause { String column, op, value; };
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
		if (i + 2 >= n) throw error("Incomplete filter clause at end of expression");
		FilterClause clause;
		clause.column = tokens[i++];
		clause.op = tokens[i++];
		if (clause.op == "!" && i < n && tokens[i] == "contains") { clause.op = "!contains"; i++; }
		if (i >= n) throw error("Missing value after operator \"%\" in filter expression", clause.op);
		clause.value = tokens[i++];
		current_group.push_back(std::move(clause));

		if (i < n) {
			if (tokens[i] == "and") { i++; if (i >= n) throw error("Trailing 'and'"); }
			else if (tokens[i] == "or") { i++; if (i >= n) throw error("Trailing 'or'"); groups.push_back(std::move(current_group)); current_group.clear(); }
			else throw error("Expected 'and' or 'or', got \"%\"", tokens[i]);
		}
	}
	if (!current_group.empty()) groups.push_back(std::move(current_group));
	if (groups.empty()) throw error("Empty filter expression");
	return groups;
}

struct PreparedClause
{
	intptr_t col;
	ScriptFilterOp op;
	String value;
	double num_value;
	bool num_valid;
	std::unique_ptr<phonometrica::Regex> re;
};

static Variant filter_rows(DataTable &table, const String &expr, const String &label)
{
	table.open();
	auto groups = parse_filter_expr(expr);

	std::vector<std::vector<PreparedClause>> prepared_groups;
	prepared_groups.reserve(groups.size());

	for (auto &group : groups)
	{
		std::vector<PreparedClause> pg;
		pg.reserve(group.size());

		for (auto &c : group)
		{
			PreparedClause pc;
			pc.col = table.find_column(c.column);
			if (pc.col == 0) throw error("[Index error] Table has no column named \"%\"", c.column);
			pc.op = parse_filter_op(c.op);
			pc.value = std::move(c.value);
			pc.num_value = 0.0;
			pc.num_valid = false;

			if (pc.op == ScriptFilterOp::Eq || pc.op == ScriptFilterOp::Ne ||
			    pc.op == ScriptFilterOp::Lt || pc.op == ScriptFilterOp::Le ||
			    pc.op == ScriptFilterOp::Gt || pc.op == ScriptFilterOp::Ge)
			{
				bool ok; pc.num_value = pc.value.to_float(&ok); pc.num_valid = ok;
				if (!pc.num_valid && (pc.op == ScriptFilterOp::Lt || pc.op == ScriptFilterOp::Le ||
				                      pc.op == ScriptFilterOp::Gt || pc.op == ScriptFilterOp::Ge))
					throw error("[Type error] Operator \"%\" requires a numeric value, got \"%\"", c.op, pc.value);
			}
			if (pc.op == ScriptFilterOp::Regex)
				pc.re = std::make_unique<phonometrica::Regex>(pc.value);
			pg.push_back(std::move(pc));
		}
		prepared_groups.push_back(std::move(pg));
	}

	auto nrow = table.row_count();
	std::vector<int> matching;
	matching.reserve(nrow);

	for (intptr_t i = 1; i <= nrow; i++)
	{
		bool row_pass = false;
		for (auto &pg : prepared_groups) {
			bool group_pass = true;
			for (auto &pc : pg) {
				if (!test_cell(table.get_cell(i, pc.col), pc.op, pc.value, pc.num_value, pc.num_valid, pc.re.get()))
				{ group_pass = false; break; }
			}
			if (group_pass) { row_pass = true; break; }
		}
		if (row_pass) matching.push_back(static_cast<int>(i - 1));
	}

	if (table.is<Dataset>()) {
		auto &ds = dynamic_cast<Dataset &>(table);
		auto result = ds.subset(matching, label);
		Project::updated();
		return result;
	}
	else if (table.is<Concordance>()) {
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
	auto fit2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		data.open();
		auto model = stats::fit(data, stats::Formula::parse(formula_str), "gaussian");
		model.compute_pseudo_r2();
		return make_handle<stats::Model>(std::move(model));
	};

	auto fit3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		auto &family_str = cast<String>(args[2]);
		data.open();
		auto model = stats::fit(data, stats::Formula::parse(formula_str), family_str);
		model.compute_pseudo_r2();
		return make_handle<stats::Model>(std::move(model));
	};

	auto summarize_model = [](Runtime &rt, std::span<Variant> args) -> Variant {
		print_model_summary(rt, cast<stats::Model>(args[0]));
		return Variant();
	};

	auto coef_model = [](Runtime &, std::span<Variant> args) -> Variant {
		return make_handle<Array<double>>(cast<stats::Model>(args[0]).beta);
	};

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
		if (key == "r2_marginal") return model.r2_marginal;
		if (key == "r2_conditional") return model.r2_conditional;
		if (key == "rse") return model.rse;
		if (key == "df") return model.df_residual;
		if (key == "theta") return model.theta;
		if (key == "phi") return model.phi;
		if (key == "sigma") return model.sigma;
		if (key == "nu") return model.nu;
		if (key == "converged") return model.converged;
		if (key == "niter") return intptr_t(model.niter);
		if (key == "fitted") return make_handle<Array<double>>(model.fitted);
		if (key == "residuals") return make_handle<Array<double>>(model.residuals);
		if (key == "estimation") return String(stats::estimation_name(model.estimation));
		if (key == "log_marginal") return model.log_marginal;
		if (key == "waic") return model.waic;
		if (key == "p_waic") return model.p_waic;
		if (key == "lppd") return model.lppd;
		if (key == "se_waic") return model.se_waic;
		if (key == "loo_ic") return model.loo_ic;
		if (key == "p_loo") return model.p_loo;
		if (key == "se_loo") return model.se_loo;
		if (key == "pareto_k") return make_handle<Array<double>>(model.pareto_k);
		if (key == "posterior_mean") return make_handle<Array<double>>(model.posterior_mean);
		if (key == "posterior_mode") return make_handle<Array<double>>(model.posterior_mode);
		if (key == "posterior_median") return make_handle<Array<double>>(model.posterior_median);
		if (key == "posterior_sd") return make_handle<Array<double>>(model.posterior_sd);
		if (key == "ci_lower") return make_handle<Array<double>>(model.ci_lower);
		if (key == "ci_upper") return make_handle<Array<double>>(model.ci_upper);
		if (key == "pd") return make_handle<Array<double>>(model.pd);
		throw error("[Index error] Model type has no member named \"%\"", key);
	};

	auto filter2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto &expr  = cast<String>(args[1]);
		String label("filter: ");
		label.append(expr);
		return filter_rows(table, expr, label);
	};

	auto filter3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto &expr  = cast<String>(args[1]);
		auto &label = cast<String>(args[2]);
		return filter_rows(table, expr, label);
	};

	auto dataset_get_field = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &ds = cast<Dataset>(args[0]);
		auto &key = cast<String>(args[1]);
		if (key == "path") return ds.path();
		if (key == "label") return ds.label();
		if (key == "description") return ds.description();
		ds.open();
		if (key == "nrow" || key == "length") return ds.row_count();
		if (key == "ncol") return ds.column_count();
		if (key == "empty") return ds.empty();
		if (key == "headers") return make_headers_list(rt, ds);
		throw error("[Index error] Dataset type has no member named \"%\"", key);
	};

	auto conc_get_field = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &conc = cast<Concordance>(args[0]);
		auto &key = cast<String>(args[1]);
		if (key == "path") return conc.path();
		if (key == "label") return conc.label();
		if (key == "description") return conc.description();
		conc.open();
		if (key == "nrow" || key == "length") return conc.row_count();
		if (key == "ncol") return conc.column_count();
		if (key == "empty") return conc.empty();
		if (key == "headers") return make_headers_list(rt, conc);
		if (key == "target_count") return intptr_t(conc.target_count());
		throw error("[Index error] Concordance type has no member named \"%\"", key);
	};

	auto compare_models = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &m1 = cast<stats::Model>(args[0]);
		auto &m2 = cast<stats::Model>(args[1]);

		// Both models must use the same estimation method.
		if (m1.estimation != m2.estimation)
		{
			throw error("Cannot compare a % model with a % model. "
			            "Both models must use the same estimation method.",
			            stats::estimation_name(m1.estimation),
			            stats::estimation_name(m2.estimation));
		}

		std::vector<const stats::Model *> models = { &m1, &m2 };

		if (m1.is_bayesian())
		{
			// ── Bayesian comparison ────────────────────────────────
			auto result = stats::bayesian_compare(models);

			for (auto &w : result.warnings)
				rt.printf("Warning: %s\n", w.data());

			// Summary table.
			bool show_marginal = result.has_bayes_factors;
			bool show_waic = !std::isnan(m1.waic) || !std::isnan(m2.waic);
			bool show_loo = !std::isnan(m1.loo_ic) || !std::isnan(m2.loo_ic);

			// Header.
			rt.printf("\n%-8s %6s %12s", "Model", "npar", "logLik");
			if (show_marginal) rt.printf(" %14s", "log p(y|M)");
			if (show_waic)     rt.printf(" %10s %8s", "WAIC", "p_WAIC");
			if (show_loo)      rt.printf(" %10s %8s", "LOO-IC", "p_LOO");
			rt.printf("\n");

			for (size_t i = 0; i < result.rows.size(); i++)
			{
				auto &r = result.rows[i];
				rt.printf("%-8d %6ld %12.1f", (int)r.original_index + 1, (long)r.npar, r.loglik);
				if (show_marginal)
					rt.printf(" %14.2f", r.log_marginal);
				if (show_waic) {
					if (std::isnan(r.waic)) rt.printf(" %10s %8s", "--", "--");
					else rt.printf(" %10.1f %8.1f", r.waic, r.p_waic);
				}
				if (show_loo) {
					if (std::isnan(r.loo_ic)) rt.printf(" %10s %8s", "--", "--");
					else rt.printf(" %10.1f %8.1f", r.loo_ic, r.p_loo);
				}
				rt.printf("\n");
			}

			// Bayes factors.
			if (show_marginal)
			{
				rt.printf("\nPairwise log Bayes factors:\n");
				for (auto &p : result.pairs)
				{
					if (std::isnan(p.log_bf)) continue;
					rt.printf("  1 vs 2:  log BF = %.2f", p.log_bf);
					if (p.log_bf > 0)      rt.printf("  (favours model 1)");
					else if (p.log_bf < 0) rt.printf("  (favours model 2)");
					rt.printf("\n");
				}
			}

			// Pairwise IC differences.
			if (show_waic || show_loo)
			{
				rt.printf("\nPairwise information criteria (negative favours model 1):\n");
				for (auto &p : result.pairs)
				{
					if (show_waic && !std::isnan(p.delta_waic))
						rt.printf("  WAIC:    delta = %10.1f  SE = %10.1f\n", p.delta_waic, p.se_diff);
					if (show_loo && !std::isnan(p.delta_loo))
						rt.printf("  LOO-IC:  delta = %10.1f  SE = %10.1f\n", p.delta_loo, p.se_loo_diff);
				}
			}

			rt.printf("\n");
		}
		else
		{
			// ── Frequentist comparison ─────────────────────────────
			auto result = stats::anova_compare(models);

			for (auto &w : result.warnings)
				rt.printf("Warning: %s\n", w.data());

			rt.printf("\n%-8s %6s %12s %12s %12s %12s\n", "Model", "npar", "logLik", "AIC", "BIC", "deviance");
			for (size_t i = 0; i < result.rows.size(); i++) {
				auto &r = result.rows[i];
				rt.printf("%-8d %6ld %12.4f %12.4f %12.4f %12.4f\n",
				          r.original_index + 1, (long)r.npar, r.loglik, r.aic, r.bic, r.deviance);
			}

			rt.printf("\n%-12s %8s %12s %12s\n", "Comparison", "df", "Chi-sq", "Pr(>Chisq)");
			for (auto &p : result.pairs) {
				char pbuf[16];
				if (std::isnan(p.p_value)) snprintf(pbuf, sizeof(pbuf), "NA");
				else if (p.p_value < 0.001) snprintf(pbuf, sizeof(pbuf), "< 0.001");
				else snprintf(pbuf, sizeof(pbuf), "%.6f", p.p_value);

				char label[32];
				snprintf(label, sizeof(label), "%d vs %d",
				         result.rows[p.index_a].original_index + 1,
				         result.rows[p.index_b].original_index + 1);
				rt.printf("%-12s %8ld %12.4f %12s\n", label, (long)p.df_diff,
				          std::isnan(p.chisq) ? 0.0 : p.chisq, pbuf);
			}
			rt.printf("\n");
		}

		return Variant();
	};

	// ── Cell and column access ──────────────────────────────────

	auto get_cell = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto i = cast<intptr_t>(args[1]);
		auto j = cast<intptr_t>(args[2]);
		table.open();
		if (i < 1 || i > table.row_count() || j < 1 || j > table.column_count()) {
			throw error("Cell index (%, %) is out of range", i, j);
		}
		return table.get_cell(i, j);
	};

	auto set_cell_func = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto i = cast<intptr_t>(args[1]);
		auto j = cast<intptr_t>(args[2]);
		auto &value = cast<String>(args[3]);
		table.open();
		if (i < 1 || i > table.row_count() || j < 1 || j > table.column_count()) {
			throw error("Cell index (%, %) is out of range", i, j);
		}
		table.set_cell(i, j, value);
		return Variant();
	};

	auto get_header = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto j = cast<intptr_t>(args[1]);
		table.open();
		if (j < 1 || j > table.column_count()) {
			throw error("Column index % is out of range", j);
		}
		return table.get_header(j);
	};

	auto get_column = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &ds = cast<Dataset>(args[0]);
		auto j = cast<intptr_t>(args[1]);
		ds.open();
		if (j < 1 || j > ds.column_count()) {
			throw error("Column index % is out of range", j);
		}
		if (ds.is_numeric(j)) {
			auto span = ds.numeric_column(j);
			Array<double> result(static_cast<intptr_t>(span.size()), 0.0);
			for (intptr_t i = 0; i < static_cast<intptr_t>(span.size()); i++) {
				result[i + 1] = span[i];
			}
			return make_handle<Array<double>>(std::move(result));
		}
		else {
			// Text or boolean: return as List of strings.
			Array<Variant> items;
			for (intptr_t i = 1; i <= ds.row_count(); i++) {
				items.append(ds.get_cell(i, j));
			}
			return make_handle<List>(&rt, std::move(items));
		}
	};

	auto column_type_func = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &ds = cast<Dataset>(args[0]);
		auto j = cast<intptr_t>(args[1]);
		ds.open();
		if (j < 1 || j > ds.column_count()) {
			throw error("Column index % is out of range", j);
		}
		auto ct = ds.column_type(j);
		switch (ct) {
			case Dataset::ColumnType::Numeric: return String("numeric");
			case Dataset::ColumnType::Text:    return String("text");
			case Dataset::ColumnType::Boolean: return String("boolean");
		}
		return String("unknown");
	};

	// ── CSV export ──────────────────────────────────────────────

	auto to_csv2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto &path = cast<String>(args[1]);
		table.open();
		table.to_csv(path, ",");
		return Variant();
	};

	auto to_csv3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &table = cast<DataTable>(args[0]);
		auto &path = cast<String>(args[1]);
		auto &sep = cast<String>(args[2]);
		table.open();
		table.to_csv(path, sep);
		return Variant();
	};

	// ── Estimated marginal means ────────────────────────────────

	// Helper: format a p-value for display.
	auto format_p = [](double p) -> std::string {
		if (std::isnan(p)) return "NA";
		if (p < 0.001)     return "< 0.001";
		char buf[16];
		snprintf(buf, sizeof(buf), "%.4f", p);
		return buf;
	};

	auto emmeans2 = [format_p](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		auto &factor = cast<String>(args[1]);
		auto emm = stats::emmeans(model, factor);

		rt.printf("\nEstimated marginal means for '%s':\n\n", factor.data());
		rt.printf("%-16s %12s %10s %12s %12s\n", "Level", "emmean", "SE", "lower.CL", "upper.CL");
		for (intptr_t i = 1; i <= emm.levels.size(); i++) {
			rt.printf("%-16s %12.4f %10.4f %12.4f %12.4f\n",
			          emm.levels[i].data(), emm.emmean[i], emm.se[i],
			          emm.lower_ci[i], emm.upper_ci[i]);
		}
		rt.printf("\n");
		return Variant();
	};

	auto emmeans3 = [format_p](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		auto &factor = cast<String>(args[1]);
		auto &adjustment = cast<String>(args[2]);
		auto emm = stats::emmeans(model, factor);

		rt.printf("\nEstimated marginal means for '%s':\n\n", factor.data());
		rt.printf("%-16s %12s %10s %12s %12s\n", "Level", "emmean", "SE", "lower.CL", "upper.CL");
		for (intptr_t i = 1; i <= emm.levels.size(); i++) {
			rt.printf("%-16s %12.4f %10.4f %12.4f %12.4f\n",
			          emm.levels[i].data(), emm.emmean[i], emm.se[i],
			          emm.lower_ci[i], emm.upper_ci[i]);
		}
		rt.printf("\n");

		auto contrasts = stats::pairwise_contrasts(emm, model, adjustment);
		rt.printf("Pairwise contrasts (p-value adjustment: %s):\n\n", adjustment.data());
		rt.printf("%-24s %12s %10s %10s %12s\n", "Contrast", "estimate", "SE", "z/t", "p.value");
		for (intptr_t i = 1; i <= contrasts.label.size(); i++) {
			rt.printf("%-24s %12.4f %10.4f %10.4f %12s\n",
			          contrasts.label[i].data(), contrasts.estimate[i], contrasts.se[i],
			          contrasts.stat[i], format_p(contrasts.p_value[i]).c_str());
		}
		rt.printf("\n");
		return Variant();
	};

	auto emtrends3 = [](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		auto &factor = cast<String>(args[1]);
		auto &var = cast<String>(args[2]);
		auto emm = stats::emtrends(model, factor, var);

		rt.printf("\nEstimated trends for '%s' by '%s':\n\n", var.data(), factor.data());
		rt.printf("%-16s %12s %10s %12s %12s\n", "Level", "trend", "SE", "lower.CL", "upper.CL");
		for (intptr_t i = 1; i <= emm.levels.size(); i++) {
			rt.printf("%-16s %12.4f %10.4f %12.4f %12.4f\n",
			          emm.levels[i].data(), emm.emmean[i], emm.se[i],
			          emm.lower_ci[i], emm.upper_ci[i]);
		}
		rt.printf("\n");
		return Variant();
	};

	auto emtrends4 = [format_p](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		auto &factor = cast<String>(args[1]);
		auto &var = cast<String>(args[2]);
		auto &adjustment = cast<String>(args[3]);
		auto emm = stats::emtrends(model, factor, var);

		rt.printf("\nEstimated trends for '%s' by '%s':\n\n", var.data(), factor.data());
		rt.printf("%-16s %12s %10s %12s %12s\n", "Level", "trend", "SE", "lower.CL", "upper.CL");
		for (intptr_t i = 1; i <= emm.levels.size(); i++) {
			rt.printf("%-16s %12.4f %10.4f %12.4f %12.4f\n",
			          emm.levels[i].data(), emm.emmean[i], emm.se[i],
			          emm.lower_ci[i], emm.upper_ci[i]);
		}
		rt.printf("\n");

		auto contrasts = stats::pairwise_contrasts(emm, model, adjustment);
		rt.printf("Pairwise contrasts of trends (p-value adjustment: %s):\n\n", adjustment.data());
		rt.printf("%-24s %12s %10s %10s %12s\n", "Contrast", "estimate", "SE", "z/t", "p.value");
		for (intptr_t i = 1; i <= contrasts.label.size(); i++) {
			rt.printf("%-24s %12.4f %10.4f %10.4f %12s\n",
			          contrasts.label[i].data(), contrasts.estimate[i], contrasts.se[i],
			          contrasts.stat[i], format_p(contrasts.p_value[i]).c_str());
		}
		rt.printf("\n");
		return Variant();
	};

	// ── DHARMa-style diagnostics ────────────────────────────────

	auto dharma_func = [format_p](Runtime &rt, std::span<Variant> args) -> Variant {
		auto &model = cast<stats::Model>(args[0]);
		auto result = stats::compute_scaled_residuals(model);

		rt.printf("\nDHARMa-style simulation-based residual diagnostics\n");
		rt.printf("==================================================\n\n");
		rt.printf("Uniformity test (KS test against U(0,1)):\n");
		rt.printf("  statistic = %.4f,  p-value = %s\n\n",
		          result.ks_statistic, format_p(result.ks_pvalue).c_str());
		rt.printf("Dispersion test:\n");
		rt.printf("  ratio = %.4f,  p-value = %s\n\n",
		          result.dispersion_ratio, format_p(result.dispersion_pvalue).c_str());
		rt.printf("Outlier test:\n");
		rt.printf("  n = %d,  p-value = %s\n\n",
		          result.n_outliers, format_p(result.outlier_pvalue).c_str());
		return Variant();
	};

	// ── Array statistics ────────────────────────────────────────

	auto array_mean1 = [](Runtime &, std::span<Variant> args) -> Variant {
		return stats::mean(cast<Array<double>>(args[0]));
	};

	auto array_mean2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto dim = (int) cast<intptr_t>(args[1]);
		return make_handle<Array<double>>(stats::mean(cast<Array<double>>(args[0]), dim));
	};

	auto array_std1 = [](Runtime &, std::span<Variant> args) -> Variant {
		return stats::stdev(cast<Array<double>>(args[0]));
	};

	auto array_std2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto dim = (int) cast<intptr_t>(args[1]);
		return make_handle<Array<double>>(stats::stdev(cast<Array<double>>(args[0]), dim));
	};

	auto array_sum1 = [](Runtime &, std::span<Variant> args) -> Variant {
		return stats::sum(cast<Array<double>>(args[0]));
	};

	auto array_sum2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto dim = cast<intptr_t>(args[1]);
		return make_handle<Array<double>>(stats::sum(cast<Array<double>>(args[0]), dim));
	};

	auto array_vrc = [](Runtime &, std::span<Variant> args) -> Variant {
		return stats::sample_variance(cast<Array<double>>(args[0]));
	};

	// ── Prior construction and configuration ────────────────────

	auto prior_init = [](Runtime &, std::span<Variant>) -> Variant {
		return make_handle<stats::PriorSpec>(stats::PriorSpec::default_spec());
	};

	// set_fixed(prior, mean, sd) — default prior for all fixed effects
	auto set_fixed3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		prior.fixed_effects.mean = args[1].to_float();
		prior.fixed_effects.sd = args[2].to_float();
		prior.fixed_auto = false;
		return Variant();
	};

	// set_fixed(prior, name, mean, sd) — per-coefficient override
	auto set_fixed4 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		auto &name = cast<String>(args[1]);
		stats::NormalPrior np;
		np.mean = args[2].to_float();
		np.sd = args[3].to_float();
		prior.coefficient_priors[name] = np;
		return Variant();
	};

	// Helper to parse a variance prior type string.
	auto parse_variance_type = [](const String &s) -> stats::VariancePriorType {
		if (s == "pc") return stats::VariancePriorType::PC;
		if (s == "half_cauchy") return stats::VariancePriorType::HalfCauchy;
		if (s == "half_normal") return stats::VariancePriorType::HalfNormal;
		throw error("Unknown variance prior type: \"%\". Expected \"pc\", \"half_cauchy\", or \"half_normal\"", s);
	};

	// set_variance(prior, type, param1, param2) — variance components prior
	auto set_variance4 = [&parse_variance_type](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		auto &type_str = cast<String>(args[1]);
		prior.variance_components.type = parse_variance_type(type_str);
		prior.variance_components.param1 = args[2].to_float();
		prior.variance_components.param2 = args[3].to_float();
		prior.variance_auto = false;
		return Variant();
	};

	// set_variance(prior, type, param1) — for HalfCauchy/HalfNormal (single param)
	auto set_variance3 = [&parse_variance_type](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		auto &type_str = cast<String>(args[1]);
		prior.variance_components.type = parse_variance_type(type_str);
		prior.variance_components.param1 = args[2].to_float();
		prior.variance_auto = false;
		return Variant();
	};

	// set_residual(prior, type, param1, param2)
	auto set_residual4 = [&parse_variance_type](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		auto &type_str = cast<String>(args[1]);
		prior.residual.type = parse_variance_type(type_str);
		prior.residual.param1 = args[2].to_float();
		prior.residual.param2 = args[3].to_float();
		prior.residual_auto = false;
		return Variant();
	};

	// set_residual(prior, type, param1)
	auto set_residual3 = [&parse_variance_type](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		auto &type_str = cast<String>(args[1]);
		prior.residual.type = parse_variance_type(type_str);
		prior.residual.param1 = args[2].to_float();
		prior.residual_auto = false;
		return Variant();
	};

	// set_negbin_theta(prior, shape, rate)
	auto set_negbin_theta = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		prior.negbin_theta.shape = args[1].to_float();
		prior.negbin_theta.rate = args[2].to_float();
		return Variant();
	};

	// set_beta_phi(prior, shape, rate)
	auto set_beta_phi = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &prior = cast<stats::PriorSpec>(args[0]);
		prior.beta_phi.shape = args[1].to_float();
		prior.beta_phi.rate = args[2].to_float();
		return Variant();
	};

	// Bayesian fit: fit(formula, data, prior) — Gaussian
	auto fit_bayes2 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		auto &priors = cast<stats::PriorSpec>(args[2]);
		data.open();
		auto model = stats::fit(data, stats::Formula::parse(formula_str), "gaussian", priors);
		model.compute_pseudo_r2();
		return make_handle<stats::Model>(std::move(model));
	};

	// Bayesian fit: fit(formula, data, family, prior)
	auto fit_bayes3 = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &formula_str = cast<String>(args[0]);
		auto &data = cast<DataTable>(args[1]);
		auto &family_str = cast<String>(args[2]);
		auto &priors = cast<stats::PriorSpec>(args[3]);
		data.open();
		auto model = stats::fit(data, stats::Formula::parse(formula_str), family_str, priors);
		model.compute_pseudo_r2();
		return make_handle<stats::Model>(std::move(model));
	};

#define CLS(T) phonometrica::get_class<T>()

	rt.add_global("fit", fit2, { CLS(String), CLS(DataTable) });
	rt.add_global("fit", fit3, { CLS(String), CLS(DataTable), CLS(String) });
	rt.add_global("fit", fit_bayes2, { CLS(String), CLS(DataTable), CLS(stats::PriorSpec) });
	rt.add_global("fit", fit_bayes3, { CLS(String), CLS(DataTable), CLS(String), CLS(stats::PriorSpec) });
	rt.add_global("filter", filter2, { CLS(DataTable), CLS(String) });
	rt.add_global("filter", filter3, { CLS(DataTable), CLS(String), CLS(String) });

	rt.add_global("summarize", summarize_model, { CLS(stats::Model) });
	rt.add_global("get_coef", coef_model, { CLS(stats::Model) });
	rt.add_global("compare", compare_models, { CLS(stats::Model), CLS(stats::Model) });

	// Cell / column access
	rt.add_global("get_cell", get_cell, { CLS(DataTable), CLS(intptr_t), CLS(intptr_t) });
	rt.add_global("set_cell", set_cell_func, { CLS(DataTable), CLS(intptr_t), CLS(intptr_t), CLS(String) });
	rt.add_global("get_header", get_header, { CLS(DataTable), CLS(intptr_t) });
	rt.add_global("get_column", get_column, { CLS(Dataset), CLS(intptr_t) });
	rt.add_global("get_column_type", column_type_func, { CLS(Dataset), CLS(intptr_t) });

	// CSV export
	rt.add_global("to_csv", to_csv2, { CLS(DataTable), CLS(String) });
	rt.add_global("to_csv", to_csv3, { CLS(DataTable), CLS(String), CLS(String) });

	// EMMs and contrasts
	rt.add_global("emmeans", emmeans2, { CLS(stats::Model), CLS(String) });
	rt.add_global("emmeans", emmeans3, { CLS(stats::Model), CLS(String), CLS(String) });
	rt.add_global("emtrends", emtrends3, { CLS(stats::Model), CLS(String), CLS(String) });
	rt.add_global("emtrends", emtrends4, { CLS(stats::Model), CLS(String), CLS(String), CLS(String) });

	// Array statistics
	rt.add_global("mean", array_mean1, { CLS(Array<double>) });
	rt.add_global("mean", array_mean2, { CLS(Array<double>), CLS(intptr_t) });
	rt.add_global("std", array_std1, { CLS(Array<double>) });
	rt.add_global("std", array_std2, { CLS(Array<double>), CLS(intptr_t) });
	rt.add_global("sum", array_sum1, { CLS(Array<double>) });
	rt.add_global("sum", array_sum2, { CLS(Array<double>), CLS(intptr_t) });
	rt.add_global("vrc", array_vrc, { CLS(Array<double>) });

	// Diagnostics
	rt.add_global("dharma", dharma_func, { CLS(stats::Model) });

	// Prior construction and configuration
	auto prior_cls = CLS(stats::PriorSpec);
	prior_cls->add_constructor(prior_init, {});

	rt.add_global("set_fixed", set_fixed3, { CLS(stats::PriorSpec), CLS(Number), CLS(Number) });
	rt.add_global("set_fixed", set_fixed4, { CLS(stats::PriorSpec), CLS(String), CLS(Number), CLS(Number) });
	rt.add_global("set_variance", set_variance3, { CLS(stats::PriorSpec), CLS(String), CLS(Number) });
	rt.add_global("set_variance", set_variance4, { CLS(stats::PriorSpec), CLS(String), CLS(Number), CLS(Number) });
	rt.add_global("set_residual", set_residual3, { CLS(stats::PriorSpec), CLS(String), CLS(Number) });
	rt.add_global("set_residual", set_residual4, { CLS(stats::PriorSpec), CLS(String), CLS(Number), CLS(Number) });
	rt.add_global("set_negbin_theta", set_negbin_theta, { CLS(stats::PriorSpec), CLS(Number), CLS(Number) });
	rt.add_global("set_beta_phi", set_beta_phi, { CLS(stats::PriorSpec), CLS(Number), CLS(Number) });

	auto model_cls = CLS(stats::Model);
	model_cls->add_method(rt.get_field_string, model_get_field, { CLS(stats::Model), CLS(String) });

	auto dataset_cls = CLS(Dataset);
	dataset_cls->add_method(rt.get_field_string, dataset_get_field, { CLS(Dataset), CLS(String) });

	auto conc_cls = CLS(Concordance);
	conc_cls->add_method(rt.get_field_string, conc_get_field, { CLS(Concordance), CLS(String) });

#undef CLS
}

} // namespace phonometrica
