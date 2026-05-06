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

#include <cmath>
#include <sstream>
#include <iomanip>
#include <limits>
#include <string>
#include <phon/application/analysis.hpp>
#include <phon/application/project.hpp>
#include <phon/application/dataset.hpp>
#include <phon/application/conc/concordance.hpp>
#include <phon/utils/xml.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

// =====================================================================
// Constructors
// =====================================================================

Analysis::Analysis(Directory *parent, Handle<DataTable> source) :
	Document(meta::get_class<Analysis>(), parent, String()),
	m_source(std::move(source))
{
	m_source->open();
	m_source_path = m_source->path();
	m_modified = true; // new analysis, needs saving
	m_content_modified = true;
	m_loaded = true; // already initialized — don't try to load from disk
}

Analysis::Analysis(Directory *parent, const String &path) :
	Document(meta::get_class<Analysis>(), parent, path)
{

}


// =====================================================================
// Fitting
// =====================================================================

int Analysis::fit(const String &formula_str, const String &family, stats::FittingCallback progress, const stats::PriorSpec *priors, int max_iter)
{
	if (!m_source) {
		throw error("Cannot fit model: source data is not available");
	}

	auto formula = stats::Formula::parse(formula_str);
	stats::Model model;
	if (priors) {
		model = stats::fit(*m_source, formula, family, *priors,
		                    m_reference_levels, std::move(progress), max_iter);
	} else {
		model = stats::fit(*m_source, formula, family, m_reference_levels, std::move(progress), max_iter);
	}
	model.formula = formula.to_string();
	model.compute_pseudo_r2();
	m_models.push_back(std::move(model));
	m_modified = true;
	m_content_modified = true;
	return (int)m_models.size() - 1;
}

void Analysis::remove_model(int index)
{
	if (index >= 0 && index < (int)m_models.size())
	{
		m_models.erase(m_models.begin() + index);
		m_modified = true;
		m_content_modified = true;
	}
}

Array<String> Analysis::column_names() const
{
	if (!m_source) return {};
	Array<String> names;
	intptr_t nc = m_source->column_count();
	for (intptr_t j = 1; j <= nc; j++) {
		names.append(m_source->get_header(j));
	}
	return names;
}

bool Analysis::content_modified() const
{
	return m_modified || Document::content_modified();
}


// =====================================================================
// Reference levels
// =====================================================================

void Analysis::set_reference_level(const String &variable, const String &level)
{
	m_reference_levels[variable] = level;
	m_modified = true;
	m_content_modified = true;
}

void Analysis::clear_reference_level(const String &variable)
{
	auto it = m_reference_levels.find(variable);
	if (it != m_reference_levels.end())
	{
		m_reference_levels.erase(it);
		m_modified = true;
		m_content_modified = true;
	}
}

String Analysis::reference_level(const String &variable) const
{
	auto it = m_reference_levels.find(variable);
	return (it != m_reference_levels.end()) ? it->second : String();
}


// =====================================================================
// Append model values to source
// =====================================================================

void Analysis::append_columns_to_source(const AppendColumnsRequest &request)
{
	if (!m_source) {
		throw error("Source data is not available");
	}
	if (request.columns.empty()) {
		return; // nothing to do
	}

	intptr_t nr = m_source->row_count();

	// Pre-flight validation: check for collisions and length mismatches up
	// front, so we either succeed completely or leave the source untouched.
	// (add_numeric_column on Dataset/Concordance mutates state incrementally,
	// so we cannot rely on them to roll back on a mid-request failure.)
	for (auto &col : request.columns)
	{
		if (col.values.size() != nr) {
			throw error("Cannot append column '%': expected % values, got %",
			            col.header, nr, col.values.size());
		}
		intptr_t nc = m_source->column_count();
		for (intptr_t j = 1; j <= nc; j++) {
			if (m_source->get_header(j) == col.header) {
				throw error("Source already has a column named '%'", col.header);
			}
		}
	}

	// Dispatch by concrete type. Both Dataset and Concordance expose an
	// identically-signed add_numeric_column(header, std::vector<double>).
	auto *ds   = dynamic_cast<Dataset*>(m_source.get());
	auto *conc = dynamic_cast<Concordance*>(m_source.get());
	if (!ds && !conc) {
		throw error("Source is not a dataset or a concordance; cannot append columns");
	}

	for (auto &col : request.columns)
	{
		// Array<double> is 1-indexed; add_numeric_column takes std::vector<double>
		// (0-indexed). Copy element-wise; NaN entries flow through unchanged.
		std::vector<double> vals;
		vals.reserve(nr);
		for (intptr_t i = 1; i <= nr; i++)
			vals.push_back(col.values[i]);

		if (ds) {
			ds->add_numeric_column(col.header, vals);
		} else {
			conc->add_numeric_column(col.header, vals);
		}
	}

	// Intentionally do NOT flip m_modified: the Analysis itself hasn't
	// changed, only its source. Dataset::add_numeric_column and
	// Concordance::add_numeric_column have already marked the source document
	// modified (m_content_modified / modify()).
}


// =====================================================================
// Source resolution
// =====================================================================

void Analysis::resolve_source()
{
	if (m_source) return;
	if (m_source_path.empty()) return;

	auto project = Project::get();

	// Try exact path match first.
	try
	{
		auto doc = project->get(m_source_path);
		if (doc)
		{
			m_source = recast<DataTable>(doc);
			if (m_source) {
				m_source->open();
				return;
			}
		}
	}
	catch (...) { }

	// Fallback: search all registered files by matching filename.
	auto target = filesystem::base_name(m_source_path);
	for (auto &kv : project->files())
	{
		auto &doc = kv.second;
		if (filesystem::base_name(doc->path()) == target)
		{
			m_source = recast<DataTable>(doc);
			if (m_source) {
				m_source_path = doc->path(); // update to the resolved path
				m_source->open();
				return;
			}
		}
	}
}


// =====================================================================
// Serialization helpers
// =====================================================================

namespace {

// Write a vector of doubles as space-separated text with full precision.
String doubles_to_string(const Array<double> &arr)
{
	std::ostringstream oss;
	oss << std::setprecision(17);
	for (intptr_t i = 1; i <= arr.size(); i++)
	{
		if (i > 1) oss << ' ';
		double v = arr[i];
		if (std::isnan(v))
			oss << "nan";
		else if (std::isinf(v))
			oss << (v < 0 ? "-inf" : "inf");
		else
			oss << v;
	}
	return String(oss.str());
}

// Write a scalar double with the same conventions as doubles_to_string.
// NaN and Inf get portable tokens instead of the runtime-dependent output
// of %.17g (which renders "-nan(ind)" / "1.#INF" on some Windows builds);
// parse_double_safe() is the symmetric reader.
String format_scalar(double v)
{
	if (std::isnan(v)) return String("nan");
	if (std::isinf(v)) return String(v < 0 ? "-inf" : "inf");
	return String::format("%.17g", v);
}

// Write a vector of strings as comma-separated text.
String strings_to_csv(const Array<String> &arr)
{
	String result;
	for (intptr_t i = 1; i <= arr.size(); i++)
	{
		if (i > 1) result.append(",");
		result.append(arr[i]);
	}
	return result;
}

// Parse space-separated doubles, handling NaN and Inf.
Array<double> parse_doubles(const char *text)
{
	Array<double> result;
	std::istringstream iss(text);
	std::string token;
	while (iss >> token)
	{
		if (token == "nan" || token == "-nan" || token == "NaN" || token == "-NaN")
			result.append(std::nan(""));
		else if (token == "inf" || token == "Inf")
			result.append(std::numeric_limits<double>::infinity());
		else if (token == "-inf" || token == "-Inf")
			result.append(-std::numeric_limits<double>::infinity());
		else
		{
			try {
				result.append(std::stod(token));
			} catch (...) {
				result.append(std::nan(""));
			}
		}
	}
	return result;
}

// Write a 0-indexed vector of intptr_t as space-separated integers.
String intptrs_to_string(const std::vector<intptr_t> &arr)
{
	std::ostringstream oss;
	for (size_t i = 0; i < arr.size(); i++)
	{
		if (i > 0) oss << ' ';
		oss << arr[i];
	}
	return String(oss.str());
}

// Parse space-separated intptr_t values.
std::vector<intptr_t> parse_intptrs(const char *text)
{
	std::vector<intptr_t> result;
	std::istringstream iss(text);
	std::string token;
	while (iss >> token)
	{
		try {
			result.push_back((intptr_t)std::stoll(token));
		} catch (...) {
			result.push_back((intptr_t)0);
		}
	}
	return result;
}

// Parse a single double value, handling NaN and Inf.
double parse_double_safe(const char *text)
{
	std::string s(text);
	// Trim whitespace.
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return std::nan("");
	s = s.substr(start);

	if (s == "nan" || s == "-nan" || s == "NaN" || s == "-NaN")
		return std::nan("");
	if (s == "inf" || s == "Inf")
		return std::numeric_limits<double>::infinity();
	if (s == "-inf" || s == "-Inf")
		return -std::numeric_limits<double>::infinity();

	try {
		return std::stod(s);
	} catch (...) {
		return std::nan("");
	}
}

// Parse comma-separated strings.
Array<String> parse_csv_strings(const char *text)
{
	Array<String> result;
	String s(text);
	auto parts = s.split(",");
	for (auto &part : parts) {
		if (!part.empty()) result.append(part);
	}
	return result;
}

} // anonymous namespace


// =====================================================================
// XML Write
// =====================================================================

void Analysis::write()
{
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value("Analysis");

	// Source reference
	{
		String rel_path = m_source_path;
		Project::compress(rel_path, Project::get()->directory());
		add_data_node(root, "Source", rel_path);
	}

	// Reference levels
	if (!m_reference_levels.empty())
	{
		auto rl_node = root.append_child("ReferenceLevels");
		for (auto &kv : m_reference_levels)
		{
			auto entry = rl_node.append_child("Ref");
			entry.append_attribute("variable").set_value(kv.first.data());
			entry.append_attribute("level").set_value(kv.second.data());
		}
	}

	// Models
	auto models_node = root.append_child("Models");

	for (auto &m : m_models)
	{
		auto mn = models_node.append_child("Model");

		add_data_node(mn, "Formula", m.formula);
		if (!m.label.empty())
			add_data_node(mn, "Label", m.label);
		add_data_node(mn, "Family", m.family);
		add_data_node(mn, "Link", m.link);
		add_data_node(mn, "Nobs", String::convert(m.nobs));
		add_data_node(mn, "Nfixed", String::convert(m.nfixed));

		add_data_node(mn, "CoefNames", strings_to_csv(m.coef_names));
		add_data_node(mn, "Beta", doubles_to_string(m.beta));
		add_data_node(mn, "Se", doubles_to_string(m.se));
		add_data_node(mn, "Stat", doubles_to_string(m.stat));
		add_data_node(mn, "P", doubles_to_string(m.p));
		add_data_node(mn, "Fitted", doubles_to_string(m.fitted));
		add_data_node(mn, "Residuals", doubles_to_string(m.residuals));
		add_data_node(mn, "Y", doubles_to_string(m.y));
		if (!m.source_rows.empty())
			add_data_node(mn, "SourceRows", intptrs_to_string(m.source_rows));

		add_data_node(mn, "LogLik", format_scalar(m.loglik));
		add_data_node(mn, "AIC", format_scalar(m.aic));
		add_data_node(mn, "BIC", format_scalar(m.bic));
		add_data_node(mn, "Deviance", format_scalar(m.deviance));
		add_data_node(mn, "LogMarginal", format_scalar(m.log_marginal));
		add_data_node(mn, "WAIC", format_scalar(m.waic));
		add_data_node(mn, "PWAIC", format_scalar(m.p_waic));
		add_data_node(mn, "LPPD", format_scalar(m.lppd));
		add_data_node(mn, "SEWAIC", format_scalar(m.se_waic));
		if (!m.elpd_i.empty())
			add_data_node(mn, "ElpdI", doubles_to_string(m.elpd_i));
		add_data_node(mn, "LOOIC", format_scalar(m.loo_ic));
		add_data_node(mn, "PLOO", format_scalar(m.p_loo));
		add_data_node(mn, "SELOO", format_scalar(m.se_loo));
		if (!m.elpd_loo_i.empty())
			add_data_node(mn, "ElpdLooI", doubles_to_string(m.elpd_loo_i));
		if (!m.pareto_k.empty())
			add_data_node(mn, "ParetoK", doubles_to_string(m.pareto_k));
		add_data_node(mn, "RSE", format_scalar(m.rse));
		add_data_node(mn, "DfResidual", String::convert(m.df_residual));
		add_data_node(mn, "R2", format_scalar(m.r2));
		add_data_node(mn, "AdjR2", format_scalar(m.adj_r2));
		add_data_node(mn, "R2Marginal", format_scalar(m.r2_marginal));
		add_data_node(mn, "R2Conditional", format_scalar(m.r2_conditional));
		add_data_node(mn, "Theta", format_scalar(m.theta));
		add_data_node(mn, "Phi", format_scalar(m.phi));
		add_data_node(mn, "StudentSigma", format_scalar(m.sigma));
		add_data_node(mn, "StudentNu", format_scalar(m.nu));
		add_data_node(mn, "Niter", String::convert(intptr_t(m.niter)));
		add_data_node(mn, "Converged", m.converged ? "true" : "false");
		if (!m.optimizer.empty())
			add_data_node(mn, "Optimizer", m.optimizer);

		if (!m.response_levels.empty()) {
			add_data_node(mn, "ResponseLevels", strings_to_csv(m.response_levels));
		}

		// Random effects
		if (m.has_random_effects())
		{
			auto re_node = mn.append_child("RandomEffects");

			for (intptr_t g = 1; g <= m.random_effects.size(); g++)
			{
				auto &re = m.random_effects[g];
				auto gn = re_node.append_child("Group");
				add_data_node(gn, "Name", re.group_name);
				add_data_node(gn, "TermNames", strings_to_csv(re.term_names));
				add_data_node(gn, "LevelNames", strings_to_csv(re.level_names));
				add_data_node(gn, "Nlevels", String::convert(re.nlevels));
				add_data_node(gn, "Variance", doubles_to_string(re.variance));
				add_data_node(gn, "CovChol", doubles_to_string(re.cov_chol));
				add_data_node(gn, "ConditionalModes", doubles_to_string(re.conditional_modes));
			}
		}

		// Smooth terms
		if (m.has_smooth_terms())
		{
			auto sm_node = mn.append_child("SmoothTerms");

			for (intptr_t i = 1; i <= m.smooth_terms.size(); i++)
			{
				auto &sm = m.smooth_terms[i];
				auto sn = sm_node.append_child("Smooth");
				add_data_node(sn, "Variable", sm.variable);
				if (!sm.by.empty())
					add_data_node(sn, "By", sm.by);
				add_data_node(sn, "Basis", sm.basis);
				add_data_node(sn, "K", String::convert(sm.k));
				add_data_node(sn, "Edf", format_scalar(sm.edf));
				add_data_node(sn, "RefDf", format_scalar(sm.ref_df));
				add_data_node(sn, "FStat", format_scalar(sm.F_stat));
				add_data_node(sn, "PValue", format_scalar(sm.p_value));
				add_data_node(sn, "ColStart", String::convert(sm.col_start));
				add_data_node(sn, "ColCount", String::convert(sm.col_count));

				// Persisted basis state for predict() at new x-values.
				// Emitted only when non-empty: omitted for empty basis_data
				// (e.g. models built directly without going through fit()).
				auto &bd = sm.basis_data;
				if (!bd.type.empty())
				{
					add_data_node(sn, "BasisType", bd.type);
					add_data_node(sn, "BasisKEff", String::convert(bd.k_eff));
					add_data_node(sn, "BasisPenaltyRank", String::convert(bd.penalty_rank));
					add_data_node(sn, "BasisNullDim", String::convert(bd.null_dim));
					if (bd.knots.size() > 0)
						add_data_node(sn, "BasisKnots", doubles_to_string(bd.knots));
					if (bd.F_deriv2.size() > 0)
						add_data_node(sn, "BasisFDeriv2", doubles_to_string(bd.F_deriv2));
					if (bd.Z_absorb.size() > 0)
						add_data_node(sn, "BasisZAbsorb", doubles_to_string(bd.Z_absorb));
					if (bd.levels.size() > 0)
						add_data_node(sn, "BasisLevels", strings_to_csv(bd.levels));
				}
			}
		}

		// Variance-covariance matrix (stored as flat array; dimensions = nfixed × nfixed)
		if (m.has_vcov())
		{
			add_data_node(mn, "Vcov", doubles_to_string(m.vcov));
		}

		// Column means of design matrix (for EMMs after reload)
		if (m.has_col_means())
		{
			add_data_node(mn, "ColMeans", doubles_to_string(m.col_means));
		}

		// Variable metadata for post-hoc analysis
		if (m.has_variable_info())
		{
			auto vi_node = mn.append_child("VariableInfo");

			for (intptr_t i = 1; i <= m.variable_info.size(); i++)
			{
				auto &vi = m.variable_info[i];
				auto vn = vi_node.append_child("Var");
				add_data_node(vn, "Name", vi.name);
				add_data_node(vn, "Numeric", vi.numeric ? "true" : "false");
				if (!vi.numeric && !vi.levels.empty()) {
					add_data_node(vn, "Levels", strings_to_csv(vi.levels));
				}
			}
		}

		// Estimation method and Bayesian posterior
		if (m.is_bayesian())
		{
			add_data_node(mn, "Estimation", "Bayesian");
			add_data_node(mn, "PosteriorMean", doubles_to_string(m.posterior_mean));
			add_data_node(mn, "PosteriorSd", doubles_to_string(m.posterior_sd));
			add_data_node(mn, "CiLower", doubles_to_string(m.ci_lower));
			add_data_node(mn, "CiUpper", doubles_to_string(m.ci_upper));
			add_data_node(mn, "Pd", doubles_to_string(m.pd));

			if (!m.hyper_names.empty())
			{
				add_data_node(mn, "HyperNames", strings_to_csv(m.hyper_names));
				add_data_node(mn, "HyperPosteriorMean", doubles_to_string(m.hyper_posterior_mean));
				add_data_node(mn, "HyperPosteriorSd", doubles_to_string(m.hyper_posterior_sd));
				add_data_node(mn, "HyperCiLower", doubles_to_string(m.hyper_ci_lower));
				add_data_node(mn, "HyperCiUpper", doubles_to_string(m.hyper_ci_upper));
			}

			// Prior specification (the auto-scaled values actually used)
			{
				auto pr_node = mn.append_child("Priors");
				auto &pr = m.priors;

				auto fe_node = pr_node.append_child("FixedEffects");
				add_data_node(fe_node, "Mean", format_scalar(pr.fixed_effects.mean));
				add_data_node(fe_node, "Sd", format_scalar(pr.fixed_effects.sd));
				add_data_node(fe_node, "Auto", pr.fixed_auto ? "true" : "false");

				auto vc_node = pr_node.append_child("VarianceComponents");
				add_data_node(vc_node, "Type", stats::variance_prior_type_name(pr.variance_components.type));
				add_data_node(vc_node, "Param1", format_scalar(pr.variance_components.param1));
				add_data_node(vc_node, "Param2", format_scalar(pr.variance_components.param2));
				add_data_node(vc_node, "Auto", pr.variance_auto ? "true" : "false");

				auto res_node = pr_node.append_child("Residual");
				add_data_node(res_node, "Type", stats::variance_prior_type_name(pr.residual.type));
				add_data_node(res_node, "Param1", format_scalar(pr.residual.param1));
				add_data_node(res_node, "Param2", format_scalar(pr.residual.param2));
				add_data_node(res_node, "Auto", pr.residual_auto ? "true" : "false");

				// LKJ prior on random-effect correlation matrix (scalar).
				// η = 1 is the default (uniform over correlation matrices).
				add_data_node(pr_node, "LkjEta", format_scalar(pr.lkj_eta));
			}
		}
	}

	write_xml(doc, m_path);
	m_modified = false;
}


// =====================================================================
// XML Read
// =====================================================================

void Analysis::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try {
		root = read_xml(doc, m_path);
	}
	catch (...) {
		throw error("Cannot open analysis file \"%\"", m_path);
	}

	if (root.name() != str("Phonometrica")) {
		throw error("Invalid XML root in %", m_path);
	}

	auto attr = root.attribute("class");
	if (!attr || str(attr.as_string()) != str("Analysis")) {
		throw error("Expected an Analysis file, got %", attr.as_string());
	}

	m_models.clear();
	m_reference_levels.clear();

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Source"))
		{
			m_source_path = node.text().get();
			Project::interpolate(m_source_path, Project::get()->directory());
		}
		else if (node.name() == str("ReferenceLevels"))
		{
			for (auto entry = node.first_child(); entry; entry = entry.next_sibling())
			{
				if (entry.name() != str("Ref")) continue;
				auto var_attr = entry.attribute("variable");
				auto lvl_attr = entry.attribute("level");
				if (var_attr && lvl_attr) {
					m_reference_levels[String(var_attr.as_string())] = String(lvl_attr.as_string());
				}
			}
		}
		else if (node.name() == str("Models"))
		{
			for (auto mn = node.first_child(); mn; mn = mn.next_sibling())
			{
				if (mn.name() != str("Model")) continue;

				stats::Model m;

				for (auto field = mn.first_child(); field; field = field.next_sibling())
				{
					auto name = str(field.name());
					auto text = field.text().get();

					if (name == "Formula")      m.formula = text;
					else if (name == "Label")    m.label = text;
					else if (name == "Family")   m.family = text;
					else if (name == "Link")     m.link = text;
					else if (name == "Nobs")     m.nobs = String(text).to_int();
					else if (name == "Nfixed")   m.nfixed = String(text).to_int();
					else if (name == "CoefNames") m.coef_names = parse_csv_strings(text);
					else if (name == "Beta")     m.beta = parse_doubles(text);
					else if (name == "Se")       m.se = parse_doubles(text);
					else if (name == "Stat")     m.stat = parse_doubles(text);
					else if (name == "P")        m.p = parse_doubles(text);
					else if (name == "Fitted")   m.fitted = parse_doubles(text);
					else if (name == "Residuals") m.residuals = parse_doubles(text);
					else if (name == "Y")        m.y = parse_doubles(text);
					else if (name == "SourceRows") m.source_rows = parse_intptrs(text);
					else if (name == "LogLik")   m.loglik = parse_double_safe(text);
					else if (name == "AIC")      m.aic = parse_double_safe(text);
					else if (name == "BIC")      m.bic = parse_double_safe(text);
					else if (name == "Deviance") m.deviance = parse_double_safe(text);
					else if (name == "LogMarginal") m.log_marginal = parse_double_safe(text);
					else if (name == "WAIC")     m.waic = parse_double_safe(text);
					else if (name == "PWAIC")    m.p_waic = parse_double_safe(text);
					else if (name == "LPPD")     m.lppd = parse_double_safe(text);
					else if (name == "SEWAIC")   m.se_waic = parse_double_safe(text);
					else if (name == "ElpdI")    m.elpd_i = parse_doubles(text);
					else if (name == "LOOIC")    m.loo_ic = parse_double_safe(text);
					else if (name == "PLOO")     m.p_loo = parse_double_safe(text);
					else if (name == "SELOO")    m.se_loo = parse_double_safe(text);
					else if (name == "ElpdLooI") m.elpd_loo_i = parse_doubles(text);
					else if (name == "ParetoK")  m.pareto_k = parse_doubles(text);
					else if (name == "RSE")      m.rse = parse_double_safe(text);
					else if (name == "DfResidual") m.df_residual = String(text).to_int();
					else if (name == "R2")       m.r2 = parse_double_safe(text);
					else if (name == "AdjR2")    m.adj_r2 = parse_double_safe(text);
					else if (name == "R2Marginal")     m.r2_marginal = parse_double_safe(text);
					else if (name == "R2Conditional")   m.r2_conditional = parse_double_safe(text);
					else if (name == "Theta")    m.theta = parse_double_safe(text);
					else if (name == "Phi")      m.phi = parse_double_safe(text);
					else if (name == "StudentSigma") m.sigma = parse_double_safe(text);
					else if (name == "StudentNu")    m.nu = parse_double_safe(text);
					else if (name == "Niter")    m.niter = (int)String(text).to_int();
					else if (name == "Converged") m.converged = (str(text) == str("true"));
					else if (name == "Optimizer") m.optimizer = String(text);
					else if (name == "ResponseLevels") m.response_levels = parse_csv_strings(text);
					else if (name == "RandomEffects")
					{
						for (auto gn = field.first_child(); gn; gn = gn.next_sibling())
						{
							if (gn.name() != str("Group")) continue;

							stats::RandomEffectGroup re;

							for (auto gf = gn.first_child(); gf; gf = gf.next_sibling())
							{
								auto gfn = str(gf.name());
								auto gft = gf.text().get();

								if (gfn == "Name")               re.group_name = gft;
								else if (gfn == "TermNames")      re.term_names = parse_csv_strings(gft);
								else if (gfn == "LevelNames")     re.level_names = parse_csv_strings(gft);
								else if (gfn == "Nlevels")         re.nlevels = String(gft).to_int();
								else if (gfn == "Variance")        re.variance = parse_doubles(gft);
								else if (gfn == "CovChol")         re.cov_chol = parse_doubles(gft);
								else if (gfn == "ConditionalModes") re.conditional_modes = parse_doubles(gft);
							}

							m.random_effects.append(std::move(re));
						}
					}
					else if (name == "SmoothTerms")
					{
						for (auto sn = field.first_child(); sn; sn = sn.next_sibling())
						{
							if (sn.name() != str("Smooth")) continue;

							stats::Model::SmoothResult sm;

							// Buffers for basis fields. We can't reshape until
							// k and k_eff have been read from the same XML node,
							// and the order in which children appear is not
							// guaranteed by pugixml.
							Array<double> flat_knots;
							Array<double> flat_fderiv2;
							Array<double> flat_zabsorb;
							Array<String> bd_levels;
							String bd_type;
							intptr_t bd_k_eff = 0;
							intptr_t bd_penalty_rank = 0;
							intptr_t bd_null_dim = 0;
							bool have_basis_data = false;

							for (auto sf = sn.first_child(); sf; sf = sf.next_sibling())
							{
								auto sfn = str(sf.name());
								auto sft = sf.text().get();

								if (sfn == "Variable")       sm.variable = sft;
								else if (sfn == "By")        sm.by = sft;
								else if (sfn == "Basis")     sm.basis = sft;
								else if (sfn == "K")         sm.k = String(sft).to_int();
								else if (sfn == "Edf")       sm.edf = parse_double_safe(sft);
								else if (sfn == "RefDf")     sm.ref_df = parse_double_safe(sft);
								else if (sfn == "FStat")     sm.F_stat = parse_double_safe(sft);
								else if (sfn == "PValue")    sm.p_value = parse_double_safe(sft);
								else if (sfn == "ColStart")  sm.col_start = String(sft).to_int();
								else if (sfn == "ColCount")  sm.col_count = String(sft).to_int();
								else if (sfn == "BasisType")        { bd_type = sft; have_basis_data = true; }
								else if (sfn == "BasisKEff")        bd_k_eff = String(sft).to_int();
								else if (sfn == "BasisPenaltyRank") bd_penalty_rank = String(sft).to_int();
								else if (sfn == "BasisNullDim")     bd_null_dim = String(sft).to_int();
								else if (sfn == "BasisKnots")       flat_knots = parse_doubles(sft);
								else if (sfn == "BasisFDeriv2")     flat_fderiv2 = parse_doubles(sft);
								else if (sfn == "BasisZAbsorb")     flat_zabsorb = parse_doubles(sft);
								else if (sfn == "BasisLevels")      bd_levels = parse_csv_strings(sft);
							}

							// Reconstruct basis_data if a BasisType was present.
							// Older save files predate this and yield an empty
							// basis_data; predict() will treat that as a refit
							// signal rather than crashing.
							if (have_basis_data)
							{
								auto &bd = sm.basis_data;
								bd.type = bd_type;
								bd.variable = sm.variable;
								bd.k = sm.k;
								bd.k_eff = bd_k_eff;
								bd.penalty_rank = bd_penalty_rank;
								bd.null_dim = bd_null_dim;

								// knots: 1D length k
								if (flat_knots.size() == sm.k)
									bd.knots = flat_knots;

								// F_deriv2: k × k
								if (sm.k > 0 && flat_fderiv2.size() == sm.k * sm.k)
								{
									bd.F_deriv2 = Array<double>(sm.k, sm.k, 0.0);
									for (intptr_t kk = 1; kk <= flat_fderiv2.size(); kk++)
										bd.F_deriv2.data()[kk - 1] = flat_fderiv2[kk];
								}

								// Z_absorb: k × k_eff
								if (sm.k > 0 && bd_k_eff > 0
								    && flat_zabsorb.size() == sm.k * bd_k_eff)
								{
									bd.Z_absorb = Array<double>(sm.k, bd_k_eff, 0.0);
									for (intptr_t kk = 1; kk <= flat_zabsorb.size(); kk++)
										bd.Z_absorb.data()[kk - 1] = flat_zabsorb[kk];
								}

								// Levels: re-basis only.
								bd.levels = bd_levels;
							}

							m.smooth_terms.append(std::move(sm));
						}
					}
					else if (name == "Vcov")
					{
						auto flat = parse_doubles(text);
						// Reconstruct as nfixed × nfixed 2D array.
						if (m.nfixed > 0 && flat.size() == m.nfixed * m.nfixed)
						{
							m.vcov = Array<double>(m.nfixed, m.nfixed, 0.0);
							for (intptr_t k = 1; k <= flat.size(); k++) {
								m.vcov.data()[k - 1] = flat[k];
							}
						}
					}
					else if (name == "ColMeans")
					{
						auto flat = parse_doubles(text);
						if (m.nfixed > 0 && flat.size() == m.nfixed)
						{
							m.col_means = Array<double>(m.nfixed, 0.0);
							for (intptr_t k = 1; k <= flat.size(); k++) {
								m.col_means[k] = flat[k];
							}
						}
					}
					else if (name == "VariableInfo")
					{
						for (auto vn = field.first_child(); vn; vn = vn.next_sibling())
						{
							if (vn.name() != str("Var")) continue;

							stats::Model::VariableInfo vi;

							for (auto vf = vn.first_child(); vf; vf = vf.next_sibling())
							{
								auto vfn = str(vf.name());
								auto vft = vf.text().get();

								if (vfn == "Name")         vi.name = vft;
								else if (vfn == "Numeric")  vi.numeric = (str(vft) == str("true"));
								else if (vfn == "Levels")   vi.levels = parse_csv_strings(vft);
							}

							m.variable_info.append(std::move(vi));
						}
					}
					else if (name == "Estimation")
					{
						if (str(text) == str("Bayesian"))
							m.estimation = stats::Estimation::Bayesian;
					}
					else if (name == "PosteriorMean")   m.posterior_mean = parse_doubles(text);
					else if (name == "PosteriorSd")     m.posterior_sd = parse_doubles(text);
					else if (name == "CiLower")         m.ci_lower = parse_doubles(text);
					else if (name == "CiUpper")         m.ci_upper = parse_doubles(text);
					else if (name == "Pd")              m.pd = parse_doubles(text);
					else if (name == "HyperNames")      m.hyper_names = parse_csv_strings(text);
					else if (name == "HyperPosteriorMean") m.hyper_posterior_mean = parse_doubles(text);
					else if (name == "HyperPosteriorSd")   m.hyper_posterior_sd = parse_doubles(text);
					else if (name == "HyperCiLower")       m.hyper_ci_lower = parse_doubles(text);
					else if (name == "HyperCiUpper")       m.hyper_ci_upper = parse_doubles(text);
					else if (name == "Priors")
					{
						auto parse_var_type = [](const char *t) -> stats::VariancePriorType {
							std::string_view s(t);
							if (s == "Half-Cauchy") return stats::VariancePriorType::HalfCauchy;
							if (s == "Half-Normal") return stats::VariancePriorType::HalfNormal;
							return stats::VariancePriorType::PC;
						};

						for (auto pr_child = field.first_child(); pr_child; pr_child = pr_child.next_sibling())
						{
							auto pr_name = str(pr_child.name());

							if (pr_name == "FixedEffects")
							{
								for (auto fe = pr_child.first_child(); fe; fe = fe.next_sibling())
								{
									auto fen = str(fe.name());
									auto fet = fe.text().get();
									if (fen == "Mean")       m.priors.fixed_effects.mean = parse_double_safe(fet);
									else if (fen == "Sd")    m.priors.fixed_effects.sd = parse_double_safe(fet);
									else if (fen == "Auto")  m.priors.fixed_auto = (str(fet) == str("true"));
								}
							}
							else if (pr_name == "VarianceComponents")
							{
								for (auto vc = pr_child.first_child(); vc; vc = vc.next_sibling())
								{
									auto vcn = str(vc.name());
									auto vct = vc.text().get();
									if (vcn == "Type")       m.priors.variance_components.type = parse_var_type(vct);
									else if (vcn == "Param1") m.priors.variance_components.param1 = parse_double_safe(vct);
									else if (vcn == "Param2") m.priors.variance_components.param2 = parse_double_safe(vct);
									else if (vcn == "Auto")   m.priors.variance_auto = (str(vct) == str("true"));
								}
							}
							else if (pr_name == "Residual")
							{
								for (auto rs = pr_child.first_child(); rs; rs = rs.next_sibling())
								{
									auto rsn = str(rs.name());
									auto rst = rs.text().get();
									if (rsn == "Type")       m.priors.residual.type = parse_var_type(rst);
									else if (rsn == "Param1") m.priors.residual.param1 = parse_double_safe(rst);
									else if (rsn == "Param2") m.priors.residual.param2 = parse_double_safe(rst);
									else if (rsn == "Auto")   m.priors.residual_auto = (str(rst) == str("true"));
								}
							}
							else if (pr_name == "LkjEta")
							{
								m.priors.lkj_eta = parse_double_safe(pr_child.text().get());
							}
						}
					}
				}

				m_models.push_back(std::move(m));
			}
		}
	}

	// Try to resolve the source
	resolve_source();

	m_modified = false;
}

} // namespace phonometrica
