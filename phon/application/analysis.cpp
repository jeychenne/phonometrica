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

int Analysis::fit(const String &formula_str, const String &family, stats::FittingCallback progress)
{
	if (!m_source) {
		throw error("Cannot fit model: source data is not available");
	}

	auto formula = stats::Formula::parse(formula_str);
	auto model = stats::fit(*m_source, formula, family, m_reference_levels, std::move(progress));
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

		add_data_node(mn, "LogLik", String::format("%.17g", m.loglik));
		add_data_node(mn, "AIC", String::format("%.17g", m.aic));
		add_data_node(mn, "BIC", String::format("%.17g", m.bic));
		add_data_node(mn, "Deviance", String::format("%.17g", m.deviance));
		add_data_node(mn, "RSE", String::format("%.17g", m.rse));
		add_data_node(mn, "DfResidual", String::convert(m.df_residual));
		add_data_node(mn, "R2", String::format("%.17g", m.r2));
		add_data_node(mn, "AdjR2", String::format("%.17g", m.adj_r2));
		add_data_node(mn, "R2Marginal", String::format("%.17g", m.r2_marginal));
		add_data_node(mn, "R2Conditional", String::format("%.17g", m.r2_conditional));
		add_data_node(mn, "Theta", String::format("%.17g", m.theta));
		add_data_node(mn, "Niter", String::convert(intptr_t(m.niter)));
		add_data_node(mn, "Converged", m.converged ? "true" : "false");

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
				add_data_node(sn, "Edf", String::format("%.17g", sm.edf));
				add_data_node(sn, "RefDf", String::format("%.17g", sm.ref_df));
				add_data_node(sn, "FStat", String::format("%.17g", sm.F_stat));
				add_data_node(sn, "PValue", String::format("%.17g", sm.p_value));
				add_data_node(sn, "ColStart", String::convert(sm.col_start));
				add_data_node(sn, "ColCount", String::convert(sm.col_count));
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
					else if (name == "LogLik")   m.loglik = parse_double_safe(text);
					else if (name == "AIC")      m.aic = parse_double_safe(text);
					else if (name == "BIC")      m.bic = parse_double_safe(text);
					else if (name == "Deviance") m.deviance = parse_double_safe(text);
					else if (name == "RSE")      m.rse = parse_double_safe(text);
					else if (name == "DfResidual") m.df_residual = String(text).to_int();
					else if (name == "R2")       m.r2 = parse_double_safe(text);
					else if (name == "AdjR2")    m.adj_r2 = parse_double_safe(text);
					else if (name == "R2Marginal")     m.r2_marginal = parse_double_safe(text);
					else if (name == "R2Conditional")   m.r2_conditional = parse_double_safe(text);
					else if (name == "Theta")    m.theta = parse_double_safe(text);
					else if (name == "Niter")    m.niter = (int)String(text).to_int();
					else if (name == "Converged") m.converged = (str(text) == str("true"));
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
