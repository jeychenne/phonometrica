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

#include <cmath>
#include <phon/file.hpp>
#include <phon/runtime.hpp>
#include <phon/regex.hpp>
#include <phon/application/bindings.hpp>
#include <phon/engine/vm/interpreter.hpp> // stringify()
#include <phon/index_conversion.hpp>
#include <phon/application/project.hpp>
#include <phon/analysis/model_comparison.hpp>
#include <phon/analysis/mixed_model.hpp>
#include <phon/application/data_table.hpp>
#include <phon/application/dataset.hpp>
#include <phon/application/conc/concordance.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/analysis/fitting.hpp>
#include <phon/analysis/emmeans.hpp>
#include <phon/analysis/predict.hpp>
#include <phon/analysis/scaled_residuals.hpp>
#include <phon/analysis/statistics.hpp>

namespace phonometrica {


DataTable::DataTable(Directory *parent, String path) :
		Document(parent, std::move(path))
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
	auto logic_attr = filter_node.append_attribute("logic");
	logic_attr.set_value((m_filter_logic == "or") ? "or" : "and");

	for (intptr_t i = 0; i < m_filter_rules.size(); i++)
	{
		auto &rule = m_filter_rules[i];
		auto rule_node = filter_node.append_child("Rule");
		rule_node.append_attribute("column").set_value(rule.column.data());
		rule_node.append_attribute("op").set_value(rule.op.data());

		if (rule.op == "in")
		{
			for (intptr_t k = 0; k < rule.set_values.size(); k++) {
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

	// Read filter rules. Clear first so that repeated calls (preload, from_xml,
	// load) replace rather than accumulate.
	m_filter_rules.clear();

	static const std::string_view filter_rules_tag("FilterRules");
	static const std::string_view rule_tag("Rule");
	static const std::string_view value_tag("Value");

	for (auto node = meta_node.first_child(); node; node = node.next_sibling())
	{
		if (node.name() != filter_rules_tag) continue;

		auto enabled_attr = node.attribute("enabled");
		m_filter_enabled = !enabled_attr || std::string_view(enabled_attr.value()) != "false";

		auto logic_attr = node.attribute("logic");
		if (logic_attr && std::string_view(logic_attr.value()) == "or") {
			m_filter_logic = String("or");
		}
		else {
			m_filter_logic = String("and");  // default for older files
		}

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

	// Defensive deduplication: legacy data (from earlier versions, or any
	// future path that produces duplicates) can leave m_filter_rules with
	// repeated entries — typically the same rule N times. Drop exact dupes
	// while preserving first-occurrence order. O(n²) but n is tiny in practice.
	if (m_filter_rules.size() > 1)
	{
		Array<FilterRuleData> deduped;
		for (intptr_t i = 0; i < m_filter_rules.size(); i++)
		{
			auto &candidate = m_filter_rules[i];
			bool seen = false;
			for (intptr_t j = 0; j < deduped.size(); j++)
			{
				auto &existing = deduped[j];
				if (existing.column == candidate.column &&
				    existing.op == candidate.op &&
				    existing.value == candidate.value &&
				    existing.set_values == candidate.set_values)
				{
					seen = true;
					break;
				}
			}
			if (!seen) deduped.append(candidate);
		}
		if (deduped.size() != m_filter_rules.size())
			m_filter_rules = std::move(deduped);
	}
}

void DataTable::set_filter_rules(Array<FilterRuleData> rules, bool enabled, const String &logic)
{
	m_filter_rules = std::move(rules);
	m_filter_enabled = enabled;
	m_filter_logic = (logic == "or") ? String("or") : String("and");
	// Intentionally no modified flag: filter rules are view metadata, not data
	// changes. They live exclusively in the project file — see
	// Concordance::write/preload, which bypass filter-rule serialization in the
	// .phon-conc file. They are silently persisted when the project is saved.
}

void DataTable::clear_filter_rules()
{
	m_filter_rules.clear();
	m_filter_enabled = true;
	m_filter_logic = String("and");
}


void DataTable::to_csv(const String &path, const String &sep)
{
	File file(path, File::Write);
	auto nrow = this->row_count();
	auto ncol = this->column_count();

	for (intptr_t j = 0; j < ncol; j++)
	{
		file.write(get_header(j));
		if (j == ncol - 1) file.write('\n');
		else file.write(sep);
	}

	for (intptr_t i = 0; i < nrow; i++)
	{
		for (intptr_t j = 0; j < ncol; j++)
		{
			file.write(get_cell(i, j));
			if (j == ncol - 1) file.write('\n');
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

	for (intptr_t j = 0; j < ncol; j++)
	{
		if (get_header(j) == name)
			return j;
	}

	return -1;
}


// =====================================================================
// Numeric cell serialization helpers.
// =====================================================================

bool DataTable::is_missing_value_token(std::string_view cell)
{
	return cell.empty()
	    || cell == "nan"
	    || cell == "NaN"
	    || cell == "NA"
	    || cell == "undefined";
}

double DataTable::parse_numeric_cell(std::string_view cell)
{
	if (is_missing_value_token(cell)) return std::nan("");

	bool ok = false;
	double val = String::to_float(cell, &ok);
	return ok ? val : std::nan("");
}

String DataTable::format_numeric_cell(double value)
{
	// Canonical NaN token: "nan". Matches scripting-engine output and is
	// portable (some runtimes format NaN as "-nan(ind)" via %g).
	if (std::isnan(value)) return String("nan");
	return String::format("%.17g", value);
}


// =====================================================================
// get_field helper: build a List of column headers.
// =====================================================================

static Variant make_headers_list(Runtime &, DataTable &table)
{
	auto ncol = table.column_count();
	List result;

	for (intptr_t j = 0; j < ncol; j++) {
		result.append(Variant::make(table.get_header(j)));
	}

	return Variant::make(result);
}


// =====================================================================
// Summarize a fitted model: format and print to console.
// =====================================================================

// First-column width helper for printf-style summary tables.
//
// Returns max(min_w, longest_name + pad).  All summary tables
// (fixed effects, hyperparameters, smooth terms, random effects, …)
// use this so a long term name like
// "cor(Intercept,man.dist:subsystem[vowels]|language)" no longer
// pushes value columns out of alignment.
static int summary_column_width(const Array<String> &names, int min_w, int pad = 2)
{
	int w = min_w;
	for (intptr_t i = 0; i < names.size(); i++) {
		int len = (int)names[i].size() + pad;
		if (len > w) w = len;
	}
	return w;
}

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
		if (!m.laplace_method.empty()) {
			const char *method_label =
				(m.laplace_method == "exact")
				    ? "exact"
				    : "Fisher-information (robust fallback)";
			rt.printf("Laplace correction: %s\n", method_label);
		}
	}
	rt.printf("Formula: %s\n", m.formula.data());
	if (m.is_bayesian()) {
		rt.printf("Estimation: Bayesian (Gaussian approximation)\n");
	} else if (m.method == stats::Method::REML) {
		rt.printf("Estimation: Frequentist (restricted maximum likelihood)\n");
	} else {
		rt.printf("Estimation: Frequentist (maximum likelihood)\n");
	}
	rt.printf("Observations: %ld\n", (long)m.nobs);

	// Experimental notice: smooth terms are not yet at production parity.
	// Printed once per summary so users inspecting GAM output are aware
	// that EDF and effective penalty values may differ from mgcv.
	if (m.smooth_terms.size() > 0) {
		rt.printf("\nNote: GAM support (s() smooth terms) is experimental in this release.\n"
		          "      Fitted curves and inference are qualitatively reliable, but smooth\n"
		          "      EDF and lambda values may differ numerically from reference\n"
		          "      implementations such as R's mgcv\n");
	}

	if (m.is_bayesian())
	{
		auto prior_str = stats::format_prior_summary(m.priors, m.family);
		rt.printf("\n%s", prior_str.c_str());
	}

	if (!m.response_levels.empty())
	{
		rt.printf("Response levels: %s = 0, %s = 1\n",
		          m.response_levels[0].data(), m.response_levels[1].data());
	}

	rt.printf("\n");

	if (m.is_bayesian())
	{
		// ── Bayesian summary ────────────────────────────────────
		rt.printf("Fixed effects (posterior):\n");

		bool has_mode = !m.posterior_mode.empty();
		bool has_median = !m.posterior_median.empty();

		int name_w = summary_column_width(m.coef_names, 24);
		std::string lbl_fmt = "%-" + std::to_string(name_w) + "s";

		if (has_mode && has_median)
		{
			rt.printf((lbl_fmt + " %12s %12s %12s %12s %12s %12s %8s\n").c_str(),
			          "", "Post.Mean", "Post.Mode", "Post.Median",
			          "Post.SD", "CI.lower", "CI.upper", "pd");
		}
		else
		{
			rt.printf((lbl_fmt + " %12s %12s %12s %12s %8s\n").c_str(),
			          "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper", "pd");
		}

		std::string row_full = lbl_fmt + " %12.4f %12.4f %12.4f %12.4f %12.4f %12.4f %8s%s\n";
		std::string row_brief = lbl_fmt + " %12.4f %12.4f %12.4f %12.4f %8s%s\n";

		for (intptr_t i = 0; i < m.nfixed; i++)
		{
			const char *name = (i < m.coef_names.size()) ? m.coef_names[i].data() : "?";

			double pd_val = (i < m.pd.size()) ? m.pd[i] : 0.0;
			char pdbuf[16];
			snprintf(pdbuf, sizeof(pdbuf), "%.4f", pd_val);

			const char *stars = "";
			if (pd_val > 0.999) stars = " ***";
			else if (pd_val > 0.99) stars = " **";
			else if (pd_val > 0.975) stars = " *";
			else if (pd_val > 0.95) stars = " .";

			if (has_mode && has_median)
			{
				rt.printf(row_full.c_str(),
				          name,
				          m.posterior_mean[i], m.posterior_mode[i], m.posterior_median[i],
				          m.posterior_sd[i],
				          m.ci_lower[i], m.ci_upper[i],
				          pdbuf, stars);
			}
			else
			{
				rt.printf(row_brief.c_str(),
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
			                 && !std::isnan(m.hyper_posterior_sd[0]);

			int hyper_w = summary_column_width(m.hyper_names, 30);
			std::string hyper_fmt = "%-" + std::to_string(hyper_w) + "s";

			if (has_hyper_sd)
			{
				rt.printf("Hyperparameters (posterior):\n");
				rt.printf((hyper_fmt + " %12s %12s %12s %12s\n").c_str(),
				          "", "Post.Mean", "Post.SD", "CI.lower", "CI.upper");

				std::string hyper_row = hyper_fmt + " %12.4f %12.4f %12.4f %12.4f\n";
				for (intptr_t i = 0; i < m.hyper_names.size(); i++)
				{
					rt.printf(hyper_row.c_str(),
					          m.hyper_names[i].data(),
					          m.hyper_posterior_mean[i], m.hyper_posterior_sd[i],
					          m.hyper_ci_lower[i], m.hyper_ci_upper[i]);
				}
			}
			else
			{
				rt.printf("Hyperparameters (posterior):\n");
				rt.printf((hyper_fmt + " %12s\n").c_str(), "", "Post.Mean");

				std::string hyper_row = hyper_fmt + " %12.4f\n";
				for (intptr_t i = 0; i < m.hyper_names.size(); i++)
				{
					rt.printf(hyper_row.c_str(),
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

		int name_w = summary_column_width(m.coef_names, 24);
		std::string lbl_fmt = "%-" + std::to_string(name_w) + "s";

		rt.printf((lbl_fmt + " %12s %12s %12s %12s\n").c_str(),
		          "", "Estimate", "Std.Error", stat_label, "Pr(>|t|)");

		std::string row_fmt = lbl_fmt + " %12.4f %12.4f %12.3f %12s%s\n";

		for (intptr_t i = 0; i < m.nfixed; i++)
		{
			const char *name = (i < m.coef_names.size()) ? m.coef_names[i].data() : "?";
			char pbuf[16];
			if (m.p[i] < 0.001) snprintf(pbuf, sizeof(pbuf), "< 0.001");
			else snprintf(pbuf, sizeof(pbuf), "%.4f", m.p[i]);

			const char *stars = "";
			if (m.p[i] < 0.001) stars = " ***";
			else if (m.p[i] < 0.01) stars = " **";
			else if (m.p[i] < 0.05) stars = " *";
			else if (m.p[i] < 0.1) stars = " .";

			rt.printf(row_fmt.c_str(),
			          name, m.beta[i], m.se[i], m.stat[i], pbuf, stars);
		}

		rt.printf("---\n");
		rt.printf("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");
	}

	// ── Smooth terms (GAM / penalized regression) ───────────────
	// Mirrors the smooth-terms table shown in the GUI analysis view.
	// Applies to both frequentist and Bayesian GAMs; empty for plain
	// linear, GLM, and mixed-model fits.
	if (m.has_smooth_terms())
	{
		// Build labels first so we can both size the column and reuse them.
		// mgcv-style labels: s(x), s(x, bs=re), s(group):slope for
		// random slopes s(group, by=x, bs=re).
		auto build_label = [](const auto &sm) -> String {
			String label("s(");
			label.append(sm.variable);
			if (sm.basis == "re") {
				if (!sm.by.empty()) {
					label.append("):");
					label.append(sm.by);
				} else {
					label.append(", bs=re)");
				}
			} else {
				label.append(")");
			}
			return label;
		};

		Array<String> labels;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++) {
			labels.append(build_label(m.smooth_terms[i]));
		}

		int name_w = summary_column_width(labels, 24);
		std::string lbl_fmt = "%-" + std::to_string(name_w) + "s";

		rt.printf("Approximate significance of smooth terms:\n");
		rt.printf((lbl_fmt + " %8s %8s %10s %12s\n").c_str(),
		          "", "edf", "Ref.df", "F", "p-value");

		std::string row_fmt = lbl_fmt + " %8.3f %8.3f %10.2f %12s%s\n";

		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
		{
			auto &sm = m.smooth_terms[i];

			char pbuf[16];
			if (sm.p_value < 0.001) snprintf(pbuf, sizeof(pbuf), "< 0.001");
			else snprintf(pbuf, sizeof(pbuf), "%.4f", sm.p_value);

			const char *stars = "";
			if (sm.p_value < 0.001) stars = " ***";
			else if (sm.p_value < 0.01) stars = " **";
			else if (sm.p_value < 0.05) stars = " *";
			else if (sm.p_value < 0.1) stars = " .";

			rt.printf(row_fmt.c_str(),
			          labels[i].data(), sm.edf, sm.ref_df, sm.F_stat, pbuf, stars);
		}
		rt.printf("---\n");
		rt.printf("Signif. codes: 0 '***' 0.001 '**' 0.01 '*' 0.05 '.' 0.1 ' ' 1\n\n");
	}

	if (m.has_random_effects())
	{
		// Show a "Corr" column when any group has q > 1 (random slopes).
		// For q = 1 all the way through we keep the legacy header.
		bool show_corr = false;
		for (intptr_t g = 0; g < m.random_effects.size(); g++) {
			if (m.random_effects[g].term_names.size() > 1) {
				show_corr = true;
				break;
			}
		}

		// cov_chol is the packed lower-triangular raw Cholesky factor L (NOT
		// log-diagonal) stored row by row. Element (r, c) with
		// 0-indexed r ≥ c lives at cov_chol[r*(r+1)/2 + c].
		// Covariance  Σ(s, t) = Σ_{k ≤ min(s,t)} L(s,k) · L(t,k).
		auto chol_at = [](const Array<double> &cc, intptr_t r0, intptr_t c0) -> double {
			intptr_t idx = r0 * (r0 + 1) / 2 + c0;
			return (idx < cc.size()) ? cc[idx] : 0.0;
		};
		auto cov_st = [&](const Array<double> &cc, intptr_t s0, intptr_t t0) -> double {
			if (s0 > t0) std::swap(s0, t0);
			double sum = 0.0;
			for (intptr_t k = 0; k <= s0; k++) {
				sum += chol_at(cc, s0, k) * chol_at(cc, t0, k);
			}
			return sum;
		};

		// Compute first-column width.  Two kinds of labels share this slot:
		// the (un-indented) group name and the (indented by 2) term names.
		// We need w ≥ max(min, longest_group + pad, 2 + longest_term + pad)
		// so values stay aligned regardless of which row holds the longest
		// label.  "Residual" (8 chars) trivially fits any min ≥ 10.
		constexpr int re_min_w = 20;
		constexpr int re_pad = 2;
		constexpr int re_indent = 2;
		int name_w = re_min_w;
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			int gw = (int)re.group_name.size() + re_pad;
			if (gw > name_w) name_w = gw;
			for (intptr_t t = 0; t < re.term_names.size(); t++) {
				int tw = re_indent + (int)re.term_names[t].size() + re_pad;
				if (tw > name_w) name_w = tw;
			}
		}
		int term_w = name_w - re_indent;
		std::string grp_fmt  = "%-" + std::to_string(name_w) + "s";
		std::string term_fmt = "%-" + std::to_string(term_w) + "s";

		rt.printf("Random effects:\n");
		if (show_corr) {
			rt.printf((grp_fmt + " %12s %12s %8s   %s\n").c_str(),
				"Group", "Variance", "Std.Dev.", "Levels", "Corr");
		} else {
			rt.printf((grp_fmt + " %12s %12s %8s\n").c_str(),
				"Group", "Variance", "Std.Dev.", "Levels");
		}

		std::string grp_row  = grp_fmt + " %12.4f %12.4f %8ld\n";
		std::string term_row = "  " + term_fmt + " %12.4f %12.4f %8s";

		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			intptr_t q = re.term_names.size();
			for (intptr_t t = 0; t < q; t++)
			{
				double var = (t < re.variance.size()) ? re.variance[t] : 0.0;
				double sd = std::sqrt(std::max(var, 0.0));

				if (t == 0) {
					rt.printf(grp_row.c_str(),
						re.group_name.data(), var, sd, (long)re.nlevels);
				} else {
					// Indented term name, blank Levels slot, then one corr per
					// previous term in the same group.
					rt.printf(term_row.c_str(),
						re.term_names[t].data(), var, sd, "");
					for (intptr_t s = 0; s < t; s++) {
						double var_s = (s < re.variance.size()) ? re.variance[s] : 0.0;
						double denom = std::sqrt(std::max(var_s, 1e-30)
						                       * std::max(var,   1e-30));
						double corr  = cov_st(re.cov_chol, s, t) / denom;
						rt.printf(" %+7.4f", corr);
					}
					rt.printf("\n");
				}
			}
		}

		if (m.is_gaussian()) {
			std::string res_fmt = grp_fmt + " %12.4f %12.4f\n";
			rt.printf(res_fmt.c_str(), "Residual", m.rse * m.rse, m.rse);
		}
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
			for (intptr_t j = 0; j < m.pareto_k.size(); j++)
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
		const char *opt_suffix = "";
		if (m.optimizer == "newton")     opt_suffix = " (Newton)";
		else if (m.optimizer == "lbfgs") opt_suffix = " (L-BFGS)";

		if (m.converged)
			rt.printf("Converged in %d iterations%s\n", m.niter, opt_suffix);
		else
			rt.printf("WARNING: did not converge after %d iterations%s\n", m.niter, opt_suffix);
	}

	// Identifiability diagnostic.  Independent of the optimizer's
	// convergence flag: a model can converge to a well-defined mode
	// of the prior-regularized objective while still being weakly
	// identified by the data (flat joint Hessian, pinned variance
	// component, etc.).  Only printed when the fit set the flag.
	if (!m.well_identified && !m.fit_warning.empty())
	{
		rt.printf("Note: %s\n", m.fit_warning.data());
	}

	// Prior-scale diagnostic (Bayesian only).  Separate from the
	// identifiability warning: the fit can be well-identified yet still
	// have its posterior driven by a prior whose scale does not match
	// the response scale (see Model::prior_warning).
	if (!m.prior_warning.empty())
	{
		rt.printf("Warning (prior scale): %s\n", m.prior_warning.data());
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
			if (pc.col < 0) throw error("[Index error] Table has no column named \"%\"", c.column);
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

	for (intptr_t i = 0; i < nrow; i++)
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
		if (row_pass) matching.push_back(static_cast<int>(i));
	}

	if (table.is<Dataset>()) {
		auto &ds = dynamic_cast<Dataset &>(table);
		auto result = ds.subset(matching, label);
		Project::updated();
		return Variant::make(result);
	}
	else if (table.is<Concordance>()) {
		auto &conc = dynamic_cast<Concordance &>(table);
		auto result = conc.subset(matching, label);
		Project::updated();
		return Variant::make(result);
	}
	throw error("[Type error] filter() is not supported for this table type");
}


// =====================================================================
// Scripting bindings
// =====================================================================

void DataTable::initialize(Runtime &rt)
{
	using namespace bindings;
	using stats::Model;

	// ── Local helpers ───────────────────────────────────────────────────

	auto note_gam = [](Isolate &iso, const Model &model) {
		if (model.smooth_terms.size() > 0)
			iso.write_output("Note: GAM support (s() smooth terms) is experimental in this release.\n");
	};

	auto key = [](const char *k) { return Variant::make(String(k)); };

	// Format a p-value for display.
	auto format_p = [](double p) -> std::string {
		if (std::isnan(p)) return "NA";
		if (p < 0.001)     return "< 0.001";
		char buf[16];
		snprintf(buf, sizeof(buf), "%.4f", p);
		return buf;
	};

	// ── fit() ───────────────────────────────────────────────────────────
	//
	// Options-table validation is strict: any unknown key, or any
	// unrecognized value, is a hard error — silently ignoring typos like
	// "RELM" would silently demote the fit to ML without the user noticing.
	// The option key is "fit_method" rather than "method" because "method"
	// is a reserved keyword in the scripting language.

	auto parse_fit_options = [](Isolate &iso, const Table &tab) -> stats::FitOptions {
		stats::FitOptions opts;
		List keys = tab.keys();
		for (intptr_t n = 1; n <= keys.size(); n++)
		{
			Variant k = keys.get(n);
			String key;
			try
			{
				key = k.to<String>();
			}
			catch (std::exception &)
			{
				iso.raise(String("[Type error] fit() options table: keys must be strings"), 0);
			}
			if (key == "fit_method")
			{
				String val = tab.get(k).to<String>();
				// Case-insensitive match on common spellings.
				String upper = val.to_upper();
				if (upper == "ML")
					opts.method = stats::Method::ML;
				else if (upper == "REML")
					opts.method = stats::Method::REML;
				else
					iso.raise(String::format("[Value error] fit() options: \"fit_method\" must be "
					                         "\"ML\" or \"REML\" (got \"%s\")", val.data()), 0);
			}
			else
			{
				iso.raise(String::format("[Value error] fit() options: unknown key \"%s\". "
				                         "Supported keys: \"fit_method\".", key.data()), 0);
			}
		}
		return opts;
	};

	// fit(formula, data) — Gaussian.
	rt.add_function("fit", [note_gam](Isolate &iso, const String &formula, DataTable &data) -> Handle<Model> {
		return guarded(iso, [&] {
			data.open();
			auto model = stats::fit(data, stats::Formula::parse(formula), "gaussian");
			model.compute_pseudo_r2();
			note_gam(iso, model);
			return Handle<Model>::make(std::move(model));
		});
	});
	// fit(formula, data, family).
	rt.add_function("fit",
	                [note_gam](Isolate &iso, const String &formula, DataTable &data, const String &family) -> Handle<Model> {
		return guarded(iso, [&] {
			data.open();
			auto model = stats::fit(data, stats::Formula::parse(formula), family);
			model.compute_pseudo_r2();
			note_gam(iso, model);
			return Handle<Model>::make(std::move(model));
		});
	});
	// fit(formula, data, options) — Gaussian + options.
	rt.add_function("fit",
	                [note_gam, parse_fit_options](Isolate &iso, const String &formula, DataTable &data,
	                                              const Table &options) -> Handle<Model> {
		auto opts = parse_fit_options(iso, options);
		return guarded(iso, [&] {
			data.open();
			auto model = stats::fit(data, stats::Formula::parse(formula), "gaussian", opts);
			model.compute_pseudo_r2();
			note_gam(iso, model);
			return Handle<Model>::make(std::move(model));
		});
	});
	// fit(formula, data, family, options).
	rt.add_function("fit",
	                [note_gam, parse_fit_options](Isolate &iso, const String &formula, DataTable &data,
	                                              const String &family, const Table &options) -> Handle<Model> {
		auto opts = parse_fit_options(iso, options);
		return guarded(iso, [&] {
			data.open();
			auto model = stats::fit(data, stats::Formula::parse(formula), family, opts);
			model.compute_pseudo_r2();
			note_gam(iso, model);
			return Handle<Model>::make(std::move(model));
		});
	});
	// fit(formula, data, prior) — Bayesian, Gaussian.
	rt.add_function("fit",
	                [note_gam](Isolate &iso, const String &formula, DataTable &data,
	                           const stats::PriorSpec &priors) -> Handle<Model> {
		return guarded(iso, [&] {
			data.open();
			auto model = stats::fit(data, stats::Formula::parse(formula), "gaussian", priors);
			model.compute_pseudo_r2();
			note_gam(iso, model);
			return Handle<Model>::make(std::move(model));
		});
	});
	// fit(formula, data, family, prior) — Bayesian.
	rt.add_function("fit",
	                [note_gam](Isolate &iso, const String &formula, DataTable &data, const String &family,
	                           const stats::PriorSpec &priors) -> Handle<Model> {
		return guarded(iso, [&] {
			data.open();
			auto model = stats::fit(data, stats::Formula::parse(formula), family, priors);
			model.compute_pseudo_r2();
			note_gam(iso, model);
			return Handle<Model>::make(std::move(model));
		});
	});

	// ── filter() ────────────────────────────────────────────────────────

	rt.add_function("filter", [](Isolate &iso, DataTable &table, const String &expr) -> Variant {
		return guarded(iso, [&] {
			String label("filter: ");
			label.append(expr);
			return filter_rows(table, expr, label);
		});
	});
	rt.add_function("filter",
	                [](Isolate &iso, DataTable &table, const String &expr, const String &label) -> Variant {
		return guarded(iso, [&] { return filter_rows(table, expr, label); });
	});

	// ── Model inspection ────────────────────────────────────────────────

	rt.add_function("summarize", [&rt](const Model &m) {
		print_model_summary(rt, m);
	});
	rt.add_function("get_coef", [](const Model &m) -> NumArray {
		return to_numarray(m.beta);
	});

	rt.add_function("compare", [&rt, format_p](Isolate &iso, const Model &m1, const Model &m2) {
		// Both models must use the same estimation method.
		if (m1.estimation != m2.estimation)
		{
			iso.raise(String::format("Cannot compare a %s model with a %s model. "
			                         "Both models must use the same estimation method.",
			                         stats::estimation_name(m1.estimation),
			                         stats::estimation_name(m2.estimation)), 0);
		}

		guarded(iso, [&] {
		std::vector<const Model *> models = { &m1, &m2 };

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
				char label[32];
				snprintf(label, sizeof(label), "%d vs %d",
				         result.rows[p.index_b].original_index + 1,
				         result.rows[p.index_a].original_index + 1);
				rt.printf("%-12s %8ld %12.4f %12s\n", label, (long)p.df_diff,
				          std::isnan(p.chisq) ? 0.0 : p.chisq, format_p(p.p_value).c_str());
			}
			rt.printf("---\nNote: a significant test favours the more complex model (named first).\n\n");
		}
		return 0;
		});
	});

	// ── Diagnostic Laplace-NLL evaluation ───────────────────────────
	//
	// `evaluate(model)`               re-evaluate at the fitted point.
	// `evaluate(model, opts_table)`   evaluate at a user-supplied point
	//   (beta / Sigma / sigma / nu / theta_nb / phi / u / refit_u).
	// Returns a Table:
	//   { loglik, laplace_nll, cond_nll, prior_nll, log_det_Huu,
	//     const_term, u, u_refit }

	auto build_eval_table = [key](Isolate &iso, const stats::EvaluationResult &result) -> Table {
		if (!result.ok) {
			iso.raise(String::format("evaluate(): %s", result.error.data()), 0);
		}
		Table out;
		out.set(key("loglik"),      Variant::make(-result.laplace_nll));
		out.set(key("laplace_nll"), Variant::make(result.laplace_nll));
		out.set(key("cond_nll"),    Variant::make(result.cond_nll));
		out.set(key("prior_nll"),   Variant::make(result.prior_nll));
		out.set(key("log_det_Huu"), Variant::make(result.log_det_Huu));
		out.set(key("const_term"),  Variant::make(result.const_term));
		out.set(key("u_refit"),     Variant::make(result.u_refit));
		out.set(key("laplace_method"), Variant::make(result.laplace_method));

		List u_items;
		for (intptr_t k = 0; k < result.u_used.size(); k++) {
			u_items.append(Variant::make(result.u_used.data()[k]));
		}
		out.set(key("u"), Variant::make(u_items));
		return out;
	};

	rt.add_function("evaluate", [build_eval_table](Isolate &iso, const Model &model) -> Table {
		stats::EvaluationOverrides ov;
		auto result = guarded(iso, [&] { return stats::evaluate_at(model, ov); });
		return build_eval_table(iso, result);
	});

	rt.add_function("evaluate", [build_eval_table, key](Isolate &iso, const Model &model, const Table &table) -> Table {
		// Storage outlasts evaluate_at — declared up front so override
		// pointers in `ov` stay valid until the call returns.
		Array<double> beta_arr;
		Array<double> u_arr;
		std::vector<Array<double>> sigma_vec;

		stats::EvaluationOverrides ov;

		return guarded(iso, [&] {
			// beta
			if (auto v = table.get(key("beta")); !v.is_null()) {
				auto lst = v.to<List>();
				beta_arr = Array<double>(lst.size(), 0.0);
				for (intptr_t k = 1; k <= lst.size(); k++) {
					beta_arr[k - 1] = lst.get(k).to<double>();
				}
				ov.beta = &beta_arr;
			}
			// Sigma: List of numeric matrices, one per RE group
			if (auto v = table.get(key("Sigma")); !v.is_null()) {
				auto lst = v.to<List>();
				sigma_vec.reserve(lst.size());
				for (intptr_t k = 1; k <= lst.size(); k++) {
					sigma_vec.push_back(to_array_double(lst.get(k).to<NumArray>()));
				}
				ov.Sigma = &sigma_vec;
			}
			// Scalar dispersion overrides
			if (auto v = table.get(key("sigma")); !v.is_null()) {
				ov.has_sigma = true;
				ov.sigma_val = v.to<double>();
			}
			if (auto v = table.get(key("nu")); !v.is_null()) {
				ov.has_nu = true;
				ov.nu_val = v.to<double>();
			}
			if (auto v = table.get(key("theta_nb")); !v.is_null()) {
				ov.has_theta_nb = true;
				ov.theta_nb_val = v.to<double>();
			}
			if (auto v = table.get(key("phi")); !v.is_null()) {
				ov.has_phi = true;
				ov.phi_val = v.to<double>();
			}
			// u
			if (auto v = table.get(key("u")); !v.is_null()) {
				auto lst = v.to<List>();
				u_arr = Array<double>(lst.size(), 0.0);
				for (intptr_t k = 1; k <= lst.size(); k++) {
					u_arr[k - 1] = lst.get(k).to<double>();
				}
				ov.u = &u_arr;
			}
			// refit_u
			if (auto v = table.get(key("refit_u")); !v.is_null()) {
				ov.refit_u = v.to<bool>();
			}

			auto result = stats::evaluate_at(model, ov);
			return build_eval_table(iso, result);
		});
	});

	// ── Diagnostic polish ──────────────────────────────────────────
	//
	// `polish(model)` re-runs the Student-t outer optimization from the
	// model's converged σ and ν with tighter tolerances; returns a Table
	// { delta, loglik, ok, message }. Only meaningful for Student-t.

	rt.add_function("polish", [key](const Model &model) -> Table {
		Table out;
		auto set = [&out, key](const char *k, Variant v) { out.set(key(k), std::move(v)); };

		if (model.family != "student") {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			set("message", Variant::make(String("polish() is only meaningful for "
			                                    "Student-t models in the current "
			                                    "iteration.")));
			return out;
		}

		if (model.X.empty() || model.y.empty()) {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			set("message", Variant::make(String("polish() requires a model with "
			                                    "stored X and y.")));
			return out;
		}

		// Reconstruct GroupingInfo from model.random_effects.
		std::vector<stats::GroupingInfo> groups;
		bool re_info_ok = true;
		for (intptr_t g = 0; g < model.random_effects.size(); g++) {
			auto &re = model.random_effects[g];
			if (re.indices.empty() || re.Z_design.empty()) {
				re_info_ok = false;
				break;
			}
			stats::GroupingInfo gi;
			gi.name = re.group_name;
			gi.levels = re.level_names;
			gi.indices = re.indices;
			gi.nlevels = re.nlevels;
			gi.nterms = re.nterms;
			gi.term_names = re.term_names;
			gi.Z_design = re.Z_design;
			groups.push_back(std::move(gi));
		}
		if (!re_info_ok) {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			set("message", Variant::make(String("polish() requires a model fitted "
			                                    "in the current session (Z design "
			                                    "info is not serialized to file).")));
			return out;
		}

		auto fam = stats::Family::student(model.sigma, model.nu);
		stats::FitOptions opts;
		opts.n_starts = 1;     // No multi-start; polish only.
		opts.polish = true;

		// Drive starting σ and ν from the converged values via overrides.
		stats::InitOverrides ov;
		ov.has_sigma_init = true;  ov.sigma_init = model.sigma;
		ov.has_nu_init    = true;  ov.nu_init    = model.nu;
		ov.tight_tolerance = true;

		try {
			auto polished = stats::mixed_model(model.y, model.X, groups, fam,
			                                    nullptr, nullptr, nullptr,
			                                    200, model.offset, &ov);

			double original_ll = model.loglik;
			double polished_ll = polished.loglik;
			double delta = polished_ll - original_ll;
			bool converged = polished.converged
			                 && std::isfinite(polished_ll);

			set("ok", Variant::make(converged));
			set("delta", Variant::make(delta));
			set("loglik", Variant::make(polished_ll));

			String msg;
			if (converged) {
				msg.append(String::format(
					"Polish: ΔlogLik = %+.4f (original %.4f → polished %.4f). ",
					delta, original_ll, polished_ll));
				if (delta > 0.5) {
					msg.append(Substring("Significant improvement: original "
					                     "convergence was likely FD-gradient-"
					                     "noise-limited."));
				} else if (delta > 0.05) {
					msg.append(Substring("Modest improvement: minor FD-noise "
					                     "effect on convergence."));
				} else {
					msg.append(Substring("No meaningful improvement: optimum "
					                     "is at the function-value precision "
					                     "floor."));
				}
			} else {
				msg.append(Substring("Polish optimization did not converge."));
			}
			set("message", Variant::make(msg));
		}
		catch (std::exception &e) {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			String msg("Polish failed: ");
			msg.append(Substring(e.what()));
			set("message", Variant::make(msg));
		}

		return out;
	});

	// ── Diagnostic Phase 2 retry ──────────────────────────────────
	//
	// `try_phase2(model)` re-fits the same Student-t model with Phase 2
	// (joint β + θ + σ + ν optimization) enabled; returns a Table
	// { ok, delta, loglik, message }.

	rt.add_function("try_phase2", [key](const Model &model) -> Table {
		Table out;
		auto set = [&out, key](const char *k, Variant v) { out.set(key(k), std::move(v)); };

		if (model.family != "student") {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			set("message", Variant::make(String("try_phase2() applies only to "
			                                    "Student-t models.")));
			return out;
		}

		if (model.X.empty() || model.y.empty()) {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			set("message", Variant::make(String("try_phase2() requires a model "
			                                    "with stored X and y.")));
			return out;
		}

		std::vector<stats::GroupingInfo> groups;
		bool re_info_ok = true;
		for (intptr_t g = 0; g < model.random_effects.size(); g++) {
			auto &re = model.random_effects[g];
			if (re.indices.empty() || re.Z_design.empty()) {
				re_info_ok = false;
				break;
			}
			stats::GroupingInfo gi;
			gi.name = re.group_name;
			gi.levels = re.level_names;
			gi.indices = re.indices;
			gi.nlevels = re.nlevels;
			gi.nterms = re.nterms;
			gi.term_names = re.term_names;
			gi.Z_design = re.Z_design;
			groups.push_back(std::move(gi));
		}
		if (!re_info_ok) {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			set("message", Variant::make(String("try_phase2() requires a model "
			                                    "fitted in the current session.")));
			return out;
		}

		auto fam = stats::Family::student(model.sigma, model.nu);

		stats::InitOverrides ov;
		ov.has_sigma_init  = true;  ov.sigma_init = model.sigma;
		ov.has_nu_init     = true;  ov.nu_init    = model.nu;
		ov.phase2_student  = true;

		try {
			auto refit = stats::mixed_model(model.y, model.X, groups, fam,
			                                 nullptr, nullptr, nullptr,
			                                 200, model.offset, &ov);

			double original_ll = model.loglik;
			double new_ll = refit.loglik;
			double delta = new_ll - original_ll;
			bool ok = refit.converged && std::isfinite(new_ll);

			set("ok", Variant::make(ok));
			set("delta", Variant::make(delta));
			set("loglik", Variant::make(new_ll));

			String msg;
			if (ok) {
				msg.append(String::format(
					"Phase 2: ΔlogLik = %+.4f (original %.4f → Phase 2 %.4f). ",
					delta, original_ll, new_ll));
				if (delta > 1.0) {
					msg.append(Substring("Significant improvement: Phase 2 "
					                     "joint optimization closes a real gap "
					                     "left by Phase 1 profiling."));
				} else if (delta > 0.05) {
					msg.append(Substring("Modest improvement."));
				} else if (delta > -0.05) {
					msg.append(Substring("No meaningful change: Phase 1 "
					                     "profiling was already at the joint "
					                     "optimum."));
				} else {
					msg.append(Substring("Phase 2 produced a worse fit, "
					                     "likely due to σ-ν correlation "
					                     "destabilizing the joint Hessian. "
					                     "Phase 1 result kept."));
				}
			} else {
				msg.append(Substring("Phase 2 did not converge. The σ-ν "
				                     "correlation in this dataset makes the "
				                     "joint optimization unstable; Phase 1 "
				                     "result was the right choice."));
			}
			set("message", Variant::make(msg));
		}
		catch (std::exception &e) {
			set("ok", Variant::make(false));
			set("delta", Variant::make(0.0));
			set("loglik", Variant::make(model.loglik));
			String msg("Phase 2 attempt failed: ");
			msg.append(Substring(e.what()));
			set("message", Variant::make(msg));
		}

		return out;
	});

	// ── predict() ───────────────────────────────────────────────────────
	//
	// `predict(model)`                 at training rows (in-session only).
	// `predict(model, newdata)`        at the rows of newdata (echoes columns).
	// `predict(model, newdata, opts)`  opts: type / scale / bare / ci_level / re_form.
	// Adds "Fit", "SE fit", "CI lower", "CI upper" columns; registers the
	// result Dataset under the project's Data folder.

	auto build_predict_options = [key](const Table &opts_tbl) -> stats::PredictOptions {
		stats::PredictOptions opts;
		if (auto v = opts_tbl.get(key("type")); !v.is_null()) {
			opts.type = v.to<String>();
		}
		if (auto v = opts_tbl.get(key("scale")); !v.is_null()) {
			opts.scale = v.to<String>();
		}
		if (auto v = opts_tbl.get(key("bare")); !v.is_null()) {
			opts.bare = v.to<bool>();
		}
		if (auto v = opts_tbl.get(key("ci_level")); !v.is_null()) {
			opts.ci_level = v.to<double>();
		}
		if (auto v = opts_tbl.get(key("re_form")); !v.is_null()) {
			opts.re_form = v.to<String>();
		}
		return opts;
	};

	// Append the four prediction columns to a Dataset (which already has
	// row_count() == result.fit.size() rows).
	auto append_prediction_columns = [](Dataset &ds, const stats::PredictResult &result)
	{
		intptr_t n = result.fit.size();
		std::vector<double> fit(n), se(n), lo(n), hi(n);
		for (intptr_t i = 0; i < n; i++) {
			fit[(size_t) i] = result.fit.data()[i];
			se [(size_t) i] = result.se_fit.data()[i];
			lo [(size_t) i] = result.ci_lower.data()[i];
			hi [(size_t) i] = result.ci_upper.data()[i];
		}
		ds.add_numeric_column(String("Fit"),      fit);
		ds.add_numeric_column(String("SE fit"),   se);
		ds.add_numeric_column(String("CI lower"), lo);
		ds.add_numeric_column(String("CI upper"), hi);
	};

	auto register_result_dataset = [](Handle<Dataset> ds, const String &label)
	{
		ds->set_label(label, false);
		auto parent = Project::get()->data().get();
		parent->append(ds, false);
		return ds;
	};

	rt.add_function("predict",
	                [append_prediction_columns, register_result_dataset](Isolate &iso, const Model &model) -> Handle<Dataset> {
		return guarded(iso, [&] {
			stats::PredictOptions opts;
			auto result = stats::predict_at_training(model, opts);
			if (!result.ok) throw error("predict(): %", result.error);

			auto ds = Dataset::create_empty(result.fit.size());
			append_prediction_columns(*ds, result);
			return register_result_dataset(std::move(ds), String("predict(model)"));
		});
	});
	rt.add_function("predict",
	                [append_prediction_columns, register_result_dataset](Isolate &iso, const Model &model,
	                                                                     Dataset &newdata) -> Handle<Dataset> {
		return guarded(iso, [&] {
			stats::PredictOptions opts;
			auto result = stats::predict_at(model, newdata, opts);
			if (!result.ok) throw error("predict(): %", result.error);

			// bare=false (default): clone newdata to echo all input columns,
			// then append predictions. The copy constructor leaves m_loaded
			// at the default false; mark_loaded() prevents .ncol / .nrow
			// access from triggering a spurious load().
			auto ds = Handle<Dataset>::make(newdata);
			ds->mark_loaded();
			append_prediction_columns(*ds, result);
			return register_result_dataset(std::move(ds), String("predict(model, newdata)"));
		});
	});
	rt.add_function("predict",
	                [build_predict_options, append_prediction_columns, register_result_dataset]
	                (Isolate &iso, const Model &model, Dataset &newdata, const Table &opts_tbl) -> Handle<Dataset> {
		return guarded(iso, [&] {
			auto opts = build_predict_options(opts_tbl);
			auto result = stats::predict_at(model, newdata, opts);
			if (!result.ok) throw error("predict(): %", result.error);

			Handle<Dataset> ds;
			if (opts.bare) {
				ds = Dataset::create_empty(result.fit.size());
			} else {
				ds = Handle<Dataset>::make(newdata);
				ds->mark_loaded();
			}
			append_prediction_columns(*ds, result);
			return register_result_dataset(std::move(ds), String("predict(model, newdata)"));
		});
	});

	// ── Cell and column access ──────────────────────────────────

	rt.add_function("get_cell", [](Isolate &iso, DataTable &table, intptr_t i, intptr_t j) -> String {
		return guarded(iso, [&] {
			table.open();
			i = dim_index_from_script(i, table.row_count());
			j = dim_index_from_script(j, table.column_count());
			return table.get_cell(i, j);
		});
	});
	rt.add_function("set_cell",
	                [](Isolate &iso, DataTable &table, intptr_t i, intptr_t j, const String &value) {
		guarded(iso, [&] {
			table.open();
			i = dim_index_from_script(i, table.row_count());
			j = dim_index_from_script(j, table.column_count());
			table.set_cell(i, j, value);
			return 0;
		});
	});
	rt.add_function("get_header", [](Isolate &iso, DataTable &table, intptr_t j) -> String {
		return guarded(iso, [&] {
			table.open();
			j = dim_index_from_script(j, table.column_count());
			return table.get_header(j);
		});
	});

	rt.add_function("get_column", [](Isolate &iso, Dataset &ds, intptr_t j) -> Variant {
		return guarded(iso, [&]() -> Variant {
			ds.open();
			j = dim_index_from_script(j, ds.column_count());
			if (ds.is_numeric(j)) {
				auto span = ds.numeric_column(j);
				NumArray result = NumArray::make_1d(static_cast<intptr_t>(span.size()));
				double *d = result.detach();
				for (intptr_t i = 0; i < static_cast<intptr_t>(span.size()); i++) {
					d[i] = span[i];
				}
				return Variant::make(result);
			}
			// Text or boolean: return as List of strings.
			List items;
			for (intptr_t i = 0; i < ds.row_count(); i++) {
				items.append(Variant::make(ds.get_cell(i, j)));
			}
			return Variant::make(items);
		});
	});

	rt.add_function("get_column_type", [](Isolate &iso, Dataset &ds, intptr_t j) -> String {
		return guarded(iso, [&] {
			ds.open();
			j = dim_index_from_script(j, ds.column_count());
			auto ct = ds.column_type(j);
			switch (ct) {
				case Dataset::ColumnType::Numeric: return String("numeric");
				case Dataset::ColumnType::Text:    return String("text");
				case Dataset::ColumnType::Boolean: return String("boolean");
			}
			return String("unknown");
		});
	});

	// Auto-detecting column extraction shared by the by-name and Concordance
	// paths: numeric if every cell parses (or is a missing-value sentinel).
	auto generic_column = [](DataTable &table, intptr_t j) -> Variant {
		auto nrow = table.row_count();
		Array<double> nums(nrow, 0.0);
		bool all_numeric = true;
		for (intptr_t i = 0; i < nrow && all_numeric; i++) {
			auto cell = table.get_cell(i, j);
			auto sv   = std::string_view(cell.data(), (size_t)cell.size());
			if (DataTable::is_missing_value_token(sv)) {
				nums[i] = std::nan("");
			} else {
				bool ok;
				double v = cell.to_float(&ok);
				if (ok)  nums[i] = v;
				else     all_numeric = false;
			}
		}
		if (all_numeric)
			return Variant::make(to_numarray(nums));
		List items;
		for (intptr_t i = 0; i < nrow; i++)
			items.append(Variant::make(table.get_cell(i, j)));
		return Variant::make(items);
	};

	// get_column(table, name): typed metadata for Dataset, auto-detection for
	// Concordance and any other DataTable subtype.
	rt.add_function("get_column",
	                [generic_column](Isolate &iso, DataTable &table, const String &name) -> Variant {
		return guarded(iso, [&]() -> Variant {
			table.open();
			auto j = table.find_column(name);
			if (j < 0)
				throw error("[Index error] Table has no column named \"%\"", name);

			// Dataset: use typed column metadata.
			if (table.is<Dataset>()) {
				auto &ds = static_cast<Dataset &>(table);
				if (ds.is_numeric(j)) {
					auto span = ds.numeric_column(j);
					NumArray result = NumArray::make_1d(static_cast<intptr_t>(span.size()));
					double *d = result.detach();
					for (intptr_t i = 0; i < static_cast<intptr_t>(span.size()); i++)
						d[i] = span[i];
					return Variant::make(result);
				}
				List items;
				for (intptr_t i = 0; i < ds.row_count(); i++)
					items.append(Variant::make(ds.get_cell(i, j)));
				return Variant::make(items);
			}

			// Generic path: Concordance and future subtypes.
			return generic_column(table, j);
		});
	});

	// get_column(concordance, index): the index space includes system columns
	// (file, match, context, metadata) as well as auxiliary measurement columns.
	rt.add_function("get_column", [generic_column](Isolate &iso, Concordance &conc, intptr_t j) -> Variant {
		return guarded(iso, [&] {
			conc.open();
			j = dim_index_from_script(j, conc.column_count());
			return generic_column(conc, j);
		});
	});

	// ── add_column(table, values, name) ─────────────────────────
	//
	// The old engine registered these as `append` with a write-back ref mask;
	// the new engine's per-generic ref-mask uniformity forbids that (gap G6a),
	// so the column appenders are named add_column (the 4b decision).

	rt.add_function("add_column",
	                [](Isolate &iso, DataTable &table, const List &list, const String &name) {
		guarded(iso, [&] {
			table.open();
			auto nrow = table.row_count();
			if (list.size() != nrow)
				throw error("add_column: list has % elements but table has % rows",
				            list.size(), nrow);

			// Convert List items to String the way `print`/interpolation do.
			std::vector<String> values;
			values.reserve((size_t)nrow);
			for (intptr_t i = 1; i <= nrow; i++)
				values.push_back(stringify(list.get(i).value()));

			if (table.is<Dataset>())
				static_cast<Dataset &>(table).add_text_column(name, values);
			else if (table.is<Concordance>())
				static_cast<Concordance &>(table).add_text_column(name, values);
			else
				throw error("add_column: unsupported table type");

			Project::updated();
			return 0;
		});
	});
	rt.add_function("add_column",
	                [](Isolate &iso, DataTable &table, const NumArray &arr, const String &name) {
		guarded(iso, [&] {
			table.open();
			auto nrow = table.row_count();
			if (arr.size() != nrow)
				throw error("add_column: array has % elements but table has % rows",
				            arr.size(), nrow);

			NumArray flat = arr.contiguous();
			const double *s = flat.data() + flat.offset();
			std::vector<double> values(s, s + flat.size());

			if (table.is<Dataset>())
				static_cast<Dataset &>(table).add_numeric_column(name, values);
			else if (table.is<Concordance>())
				static_cast<Concordance &>(table).add_numeric_column(name, values);
			else
				throw error("add_column: unsupported table type");

			Project::updated();
			return 0;
		});
	});

	// ── CSV export ──────────────────────────────────────────────

	rt.add_function("to_csv", [](Isolate &iso, DataTable &table, const String &path) {
		guarded(iso, [&] {
			table.open();
			table.to_csv(path, ",");
			return 0;
		});
	});
	rt.add_function("to_csv", [](Isolate &iso, DataTable &table, const String &path, const String &sep) {
		guarded(iso, [&] {
			table.open();
			table.to_csv(path, sep);
			return 0;
		});
	});

	// ── Estimated marginal means ────────────────────────────────

	auto print_emm_table = [&rt](const stats::EMMResult &emm, const char *value_header) {
		rt.printf("%-16s %12s %10s %12s %12s\n", "Level", value_header, "SE", "lower.CL", "upper.CL");
		for (intptr_t i = 0; i < emm.levels.size(); i++) {
			rt.printf("%-16s %12.4f %10.4f %12.4f %12.4f\n",
			          emm.levels[i].data(), emm.emmean[i], emm.se[i],
			          emm.lower_ci[i], emm.upper_ci[i]);
		}
		rt.printf("\n");
	};
	auto print_contrasts = [&rt, format_p](const Model &model, const stats::EMMResult &emm,
	                                       const String &adjustment, const char *what) {
		auto contrasts = stats::pairwise_contrasts(emm, model, adjustment);
		rt.printf("Pairwise contrasts%s (p-value adjustment: %s):\n\n", what, adjustment.data());
		rt.printf("%-24s %12s %10s %10s %12s\n", "Contrast", "estimate", "SE", "z/t", "p.value");
		for (intptr_t i = 0; i < contrasts.label.size(); i++) {
			rt.printf("%-24s %12.4f %10.4f %10.4f %12s\n",
			          contrasts.label[i].data(), contrasts.estimate[i], contrasts.se[i],
			          contrasts.stat[i], format_p(contrasts.p_value[i]).c_str());
		}
		rt.printf("\n");
	};

	rt.add_function("emmeans", [&rt, print_emm_table](Isolate &iso, const Model &model, const String &factor) {
		guarded(iso, [&] {
			auto emm = stats::emmeans(model, factor);
			rt.printf("\nEstimated marginal means for '%s':\n\n", factor.data());
			print_emm_table(emm, "emmean");
			return 0;
		});
	});
	rt.add_function("emmeans",
	                [&rt, print_emm_table, print_contrasts](Isolate &iso, const Model &model, const String &factor,
	                                                        const String &adjustment) {
		guarded(iso, [&] {
			auto emm = stats::emmeans(model, factor);
			rt.printf("\nEstimated marginal means for '%s':\n\n", factor.data());
			print_emm_table(emm, "emmean");
			print_contrasts(model, emm, adjustment, "");
			return 0;
		});
	});
	rt.add_function("emtrends",
	                [&rt, print_emm_table](Isolate &iso, const Model &model, const String &factor, const String &var) {
		guarded(iso, [&] {
			auto emm = stats::emtrends(model, factor, var);
			rt.printf("\nEstimated trends for '%s' by '%s':\n\n", var.data(), factor.data());
			print_emm_table(emm, "trend");
			return 0;
		});
	});
	rt.add_function("emtrends",
	                [&rt, print_emm_table, print_contrasts](Isolate &iso, const Model &model, const String &factor,
	                                                        const String &var, const String &adjustment) {
		guarded(iso, [&] {
			auto emm = stats::emtrends(model, factor, var);
			rt.printf("\nEstimated trends for '%s' by '%s':\n\n", var.data(), factor.data());
			print_emm_table(emm, "trend");
			print_contrasts(model, emm, adjustment, " of trends");
			return 0;
		});
	});

	// ── DHARMa-style diagnostics ────────────────────────────────

	rt.add_function("dharma", [&rt, format_p](Isolate &iso, const Model &model) {
		guarded(iso, [&] {
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
			return 0;
		});
	});

	// ── Array statistics ────────────────────────────────────────

	rt.add_function("mean", [](const NumArray &a) -> double {
		return stats::mean(to_array_double(a));
	});
	rt.add_function("mean", [](const NumArray &a, intptr_t dim) -> NumArray {
		return to_numarray(stats::mean(to_array_double(a), (int) dim));
	});
	rt.add_function("std", [](const NumArray &a) -> double {
		return stats::stdev(to_array_double(a));
	});
	rt.add_function("std", [](const NumArray &a, intptr_t dim) -> NumArray {
		return to_numarray(stats::stdev(to_array_double(a), (int) dim));
	});
	rt.add_function("sum", [](const NumArray &a) -> double {
		return stats::sum(to_array_double(a));
	});
	rt.add_function("sum", [](const NumArray &a, intptr_t dim) -> NumArray {
		return to_numarray(stats::sum(to_array_double(a), (int) dim));
	});
	rt.add_function("vrc", [](const NumArray &a) -> double {
		return stats::sample_variance(to_array_double(a));
	});

	// ── Prior construction and configuration ────────────────────
	//
	// Prior() was an add_constructor on the old engine; new-engine classes are
	// not script-constructible, so the same call syntax is a factory generic
	// (class "PriorSpec" + factory "Prior", gap G6b).

	rt.add_function("Prior", []() -> Handle<stats::PriorSpec> {
		return Handle<stats::PriorSpec>::make(stats::PriorSpec::default_spec());
	});

	rt.add_function("set_fixed", [](stats::PriorSpec &prior, double mean, double sd) {
		prior.fixed_effects.mean = mean;
		prior.fixed_effects.sd = sd;
		prior.fixed_auto = false;
	});
	rt.add_function("set_fixed",
	                [](stats::PriorSpec &prior, const String &name, double mean, double sd) {
		stats::NormalPrior np;
		np.mean = mean;
		np.sd = sd;
		prior.coefficient_priors[name] = np;
	});

	auto parse_variance_type = [](Isolate &iso, const String &s) -> stats::VariancePriorType {
		if (s == "pc") return stats::VariancePriorType::PC;
		if (s == "half_cauchy") return stats::VariancePriorType::HalfCauchy;
		if (s == "half_normal") return stats::VariancePriorType::HalfNormal;
		iso.raise(String::format("Unknown variance prior type: \"%s\". Expected \"pc\", "
		                         "\"half_cauchy\", or \"half_normal\"", s.data()), 0);
	};

	rt.add_function("set_variance",
	                [parse_variance_type](Isolate &iso, stats::PriorSpec &prior, const String &type, double param1) {
		prior.variance_components.type = parse_variance_type(iso, type);
		prior.variance_components.param1 = param1;
		prior.variance_auto = false;
	});
	rt.add_function("set_variance",
	                [parse_variance_type](Isolate &iso, stats::PriorSpec &prior, const String &type,
	                                      double param1, double param2) {
		prior.variance_components.type = parse_variance_type(iso, type);
		prior.variance_components.param1 = param1;
		prior.variance_components.param2 = param2;
		prior.variance_auto = false;
	});
	rt.add_function("set_residual",
	                [parse_variance_type](Isolate &iso, stats::PriorSpec &prior, const String &type, double param1) {
		prior.residual.type = parse_variance_type(iso, type);
		prior.residual.param1 = param1;
		prior.residual_auto = false;
	});
	rt.add_function("set_residual",
	                [parse_variance_type](Isolate &iso, stats::PriorSpec &prior, const String &type,
	                                      double param1, double param2) {
		prior.residual.type = parse_variance_type(iso, type);
		prior.residual.param1 = param1;
		prior.residual.param2 = param2;
		prior.residual_auto = false;
	});
	rt.add_function("set_negbin_theta", [](stats::PriorSpec &prior, double shape, double rate) {
		prior.negbin_theta.shape = shape;
		prior.negbin_theta.rate = rate;
	});
	rt.add_function("set_beta_phi", [](stats::PriorSpec &prior, double shape, double rate) {
		prior.beta_phi.shape = shape;
		prior.beta_phi.rate = rate;
	});
	// LKJ prior on the random-effect correlation matrix. Density:
	// p(R | η) ∝ |R|^(η − 1). η = 1 is uniform over correlation matrices;
	// η > 1 concentrates toward the identity; η < 1 pushes toward strongly
	// correlated random terms. η = 2 gives a mildly regularising prior.
	rt.add_function("set_lkj", [](Isolate &iso, stats::PriorSpec &prior, double eta) {
		if (!(eta > 0.0)) {
			iso.raise(String::format("set_lkj: eta must be strictly positive (got %g)", eta), 0);
		}
		prior.lkj_eta = eta;
	});

	// ── Model fields (the old model_get_field dispatcher, ~60 keys) ─────

	auto string_list = [](const Array<String> &a) { return make_list(a); };

	rt.add_field<Model>("formula", [](const Model &m) { return m.formula; });
	rt.add_field<Model>("family", [](const Model &m) { return m.family; });
	rt.add_field<Model>("link", [](const Model &m) { return m.link; });
	rt.add_field<Model>("nobs", [](const Model &m) { return m.nobs; });
	rt.add_field<Model>("aic", [](const Model &m) { return m.aic; });
	rt.add_field<Model>("bic", [](const Model &m) { return m.bic; });
	rt.add_field<Model>("loglik", [](const Model &m) { return m.loglik; });
	rt.add_field<Model>("deviance", [](const Model &m) { return m.deviance; });
	rt.add_field<Model>("r2", [](const Model &m) { return m.r2; });
	rt.add_field<Model>("adj_r2", [](const Model &m) { return m.adj_r2; });
	rt.add_field<Model>("r2_marginal", [](const Model &m) { return m.r2_marginal; });
	rt.add_field<Model>("r2_conditional", [](const Model &m) { return m.r2_conditional; });
	rt.add_field<Model>("rse", [](const Model &m) { return m.rse; });
	rt.add_field<Model>("df", [](const Model &m) { return m.df_residual; });
	rt.add_field<Model>("theta", [](const Model &m) { return m.theta; });
	rt.add_field<Model>("phi", [](const Model &m) { return m.phi; });
	rt.add_field<Model>("sigma", [](const Model &m) { return m.sigma; });
	rt.add_field<Model>("nu", [](const Model &m) { return m.nu; });
	rt.add_field<Model>("converged", [](const Model &m) { return m.converged; });
	rt.add_field<Model>("niter", [](const Model &m) { return intptr_t(m.niter); });
	rt.add_field<Model>("optimizer", [](const Model &m) { return m.optimizer; });
	rt.add_field<Model>("well_identified", [](const Model &m) { return m.well_identified; });
	rt.add_field<Model>("warning", [](const Model &m) { return m.fit_warning; });
	rt.add_field<Model>("prior_warning", [](const Model &m) { return m.prior_warning; });
	rt.add_field<Model>("fitted", [](const Model &m) { return to_numarray(m.fitted); });
	rt.add_field<Model>("residuals", [](const Model &m) { return to_numarray(m.residuals); });
	rt.add_field<Model>("estimation",
	                    [](const Model &m) { return String(stats::estimation_name(m.estimation)); });
	// Surface the ML/REML choice for frequentist Gaussian LMMs. For Bayesian
	// fits this returns "ML" (the engine uses ML internally), and the user
	// should rely on `estimation` to distinguish Bayesian from frequentist.
	rt.add_field<Model>("fit_method", [](const Model &m) {
		return String(m.method == stats::Method::REML ? "REML" : "ML");
	});
	rt.add_field<Model>("log_marginal", [](const Model &m) { return m.log_marginal; });
	rt.add_field<Model>("waic", [](const Model &m) { return m.waic; });
	rt.add_field<Model>("p_waic", [](const Model &m) { return m.p_waic; });
	rt.add_field<Model>("lppd", [](const Model &m) { return m.lppd; });
	rt.add_field<Model>("se_waic", [](const Model &m) { return m.se_waic; });
	rt.add_field<Model>("loo_ic", [](const Model &m) { return m.loo_ic; });
	rt.add_field<Model>("p_loo", [](const Model &m) { return m.p_loo; });
	rt.add_field<Model>("se_loo", [](const Model &m) { return m.se_loo; });
	rt.add_field<Model>("pareto_k", [](const Model &m) { return to_numarray(m.pareto_k); });
	rt.add_field<Model>("posterior_mean", [](const Model &m) { return to_numarray(m.posterior_mean); });
	rt.add_field<Model>("posterior_mode", [](const Model &m) { return to_numarray(m.posterior_mode); });
	rt.add_field<Model>("posterior_median", [](const Model &m) { return to_numarray(m.posterior_median); });
	rt.add_field<Model>("posterior_sd", [](const Model &m) { return to_numarray(m.posterior_sd); });
	rt.add_field<Model>("ci_lower", [](const Model &m) { return to_numarray(m.ci_lower); });
	rt.add_field<Model>("ci_upper", [](const Model &m) { return to_numarray(m.ci_upper); });
	rt.add_field<Model>("pd", [](const Model &m) { return to_numarray(m.pd); });
	// Fixed-effect inference (frequentist).
	rt.add_field<Model>("se", [](const Model &m) { return to_numarray(m.se); });
	rt.add_field<Model>("stat", [](const Model &m) { return to_numarray(m.stat); });
	rt.add_field<Model>("p", [](const Model &m) { return to_numarray(m.p); });
	// Names: coefficients and (Bayesian) hyperparameters.
	rt.add_field<Model>("coef_names", [string_list](const Model &m) { return string_list(m.coef_names); });
	rt.add_field<Model>("hyper_names", [string_list](const Model &m) { return string_list(m.hyper_names); });
	// Hyperparameter posteriors (Bayesian mixed/Gaussian only).
	rt.add_field<Model>("hyper_posterior_mean", [](const Model &m) { return to_numarray(m.hyper_posterior_mean); });
	rt.add_field<Model>("hyper_posterior_sd", [](const Model &m) { return to_numarray(m.hyper_posterior_sd); });
	rt.add_field<Model>("hyper_ci_lower", [](const Model &m) { return to_numarray(m.hyper_ci_lower); });
	rt.add_field<Model>("hyper_ci_upper", [](const Model &m) { return to_numarray(m.hyper_ci_upper); });

	// Random-effects summary (frequentist mixed models): flat "sd(term|group)"
	// layout parallel to hyper_*, plus a final "sd(residual)" for Gaussian.
	rt.add_field<Model>("ranef_names", [](const Model &m) {
		List items;
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			for (intptr_t t = 0; t < re.term_names.size(); t++)
			{
				std::string name = "sd("
					+ std::string(re.term_names[t].data(), (size_t) re.term_names[t].size())
					+ "|"
					+ std::string(re.group_name.data(), (size_t) re.group_name.size())
					+ ")";
				items.append(Variant::make(String(name.data(), (intptr_t) name.size())));
			}
		}
		if (m.is_gaussian() && m.has_random_effects())
			items.append(Variant::make(String("sd(residual)")));
		return items;
	});
	rt.add_field<Model>("ranef_sd", [](const Model &m) {
		Array<double> sds;
		for (intptr_t g = 0; g < m.random_effects.size(); g++)
		{
			auto &re = m.random_effects[g];
			for (intptr_t t = 0; t < re.term_names.size(); t++)
			{
				double var = (t < re.variance.size()) ? re.variance[t] : 0.0;
				sds.append(std::sqrt(std::max(var, 0.0)));
			}
		}
		if (m.is_gaussian() && m.has_random_effects())
			sds.append(m.rse);
		return to_numarray(sds);
	});

	// Smooth terms (GAM only): parallel arrays, one entry per s() term in
	// formula order (by-factor smooths in by-level order, matching the summary).
	rt.add_field<Model>("smooth_names", [](const Model &m) {
		List items;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
		{
			auto &sm = m.smooth_terms[i];
			String label("s(");
			label.append(sm.variable);
			if (!sm.by.empty()) { label.append(Substring("):")); label.append(sm.by); }
			else                  label.append(Substring(")"));
			items.append(Variant::make(label));
		}
		return items;
	});
	rt.add_field<Model>("smooth_edf", [](const Model &m) {
		Array<double> edfs;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
			edfs.append(m.smooth_terms[i].edf);
		return to_numarray(edfs);
	});
	rt.add_field<Model>("smooth_F", [](const Model &m) {
		Array<double> Fs;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
			Fs.append(m.smooth_terms[i].F_stat);
		return to_numarray(Fs);
	});
	rt.add_field<Model>("smooth_p", [](const Model &m) {
		Array<double> ps;
		for (intptr_t i = 0; i < m.smooth_terms.size(); i++)
			ps.append(m.smooth_terms[i].p_value);
		return to_numarray(ps);
	});
	// The per-penalty-block log10(λ) selected by the GCV inner loop; empty for
	// non-GAM models.
	rt.add_field<Model>("smooth_log_lambda", [](const Model &m) {
		Array<double> lls;
		for (intptr_t i = 0; i < m.smooth_log_lambda.size(); i++)
			lls.append(m.smooth_log_lambda[i]);
		return to_numarray(lls);
	});
	rt.add_field<Model>("n_smooth", [](const Model &m) { return m.smooth_terms.size(); });

	// ── Dataset / Concordance fields ────────────────────────────

	rt.add_field<Dataset>("path", [](const Dataset &ds) { return ds.path(); });
	rt.add_field<Dataset>("label", [](const Dataset &ds) { return ds.label(); });
	rt.add_field<Dataset>("description", [](const Dataset &ds) { return ds.description(); });
	rt.add_field<Dataset>("nrow", [](Dataset &ds) { ds.open(); return ds.row_count(); });
	rt.add_field<Dataset>("length", [](Dataset &ds) { ds.open(); return ds.row_count(); });
	rt.add_field<Dataset>("ncol", [](Dataset &ds) { ds.open(); return ds.column_count(); });
	rt.add_field<Dataset>("empty", [](Dataset &ds) { ds.open(); return ds.empty(); });
	rt.add_field<Dataset>("headers", [](Dataset &ds) {
		ds.open();
		List out;
		for (intptr_t j = 0; j < ds.column_count(); j++)
			out.append(Variant::make(ds.get_header(j)));
		return out;
	});

	rt.add_field<Concordance>("path", [](const Concordance &conc) { return conc.path(); });
	rt.add_field<Concordance>("label", [](const Concordance &conc) { return conc.label(); });
	rt.add_field<Concordance>("description", [](const Concordance &conc) { return conc.description(); });
	rt.add_field<Concordance>("nrow", [](Concordance &conc) { conc.open(); return conc.row_count(); });
	rt.add_field<Concordance>("length", [](Concordance &conc) { conc.open(); return conc.row_count(); });
	rt.add_field<Concordance>("ncol", [](Concordance &conc) { conc.open(); return conc.column_count(); });
	rt.add_field<Concordance>("empty", [](Concordance &conc) { conc.open(); return conc.empty(); });
	rt.add_field<Concordance>("headers", [](Concordance &conc) {
		conc.open();
		List out;
		for (intptr_t j = 0; j < conc.column_count(); j++)
			out.append(Variant::make(conc.get_header(j)));
		return out;
	});
	rt.add_field<Concordance>("target_count", [](Concordance &conc) {
		conc.open();
		return intptr_t(conc.target_count());
	});
}

} // namespace phonometrica
