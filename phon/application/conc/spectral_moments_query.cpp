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
 * Created: 11/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime.hpp>
#include <phon/application/conc/spectral_moments_query.hpp>
#include <phon/application/spectral_moments.hpp>
#include <phon/application/project.hpp>
#include <phon/application/sound.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

SpectralMomentsQuery::SpectralMomentsQuery(Directory *parent, String path) :
		Query(parent, String())
{
	m_path = std::move(path);
	if (!m_path.empty()) {
		load();
	}
}

int SpectralMomentsQuery::moment_count() const
{
	int n = 0;
	if (m_out_cog)      ++n;
	if (m_out_spread)   ++n;
	if (m_out_skewness) ++n;
	if (m_out_kurtosis) ++n;
	return (n > 0) ? n : 4; // fallback: all four
}

int SpectralMomentsQuery::field_count() const
{
	int fpp = moment_count();
	if (m_method == Method::Midpoint)
	{
		return fpp;
	}
	else
	{
		int npoints = (int)m_points.size();
		if (npoints < 1) npoints = 1;
		int n = 0;
		if (m_series)  n += npoints * fpp;
		if (m_average) n += fpp;
		return n;
	}
}

Array<String> SpectralMomentsQuery::build_headers() const
{
	Array<String> headers;

	auto emit_group = [&](Array<String> &out, const char *suffix)
	{
		if (m_out_cog)      out.append(String::format("COG%s", suffix));
		if (m_out_spread)   out.append(String::format("Spread%s", suffix));
		if (m_out_skewness) out.append(String::format("Skewness%s", suffix));
		if (m_out_kurtosis) out.append(String::format("Kurtosis%s", suffix));
	};

	if (m_method == Method::Midpoint)
	{
		emit_group(headers, "");
	}
	else
	{
		if (m_series)
		{
			for (intptr_t p = 0; p < m_points.size(); p++)
			{
				char suffix[32];
				snprintf(suffix, sizeof(suffix), "(%d%%)", (int)m_points[p]);
				emit_group(headers, suffix);
			}
		}
		if (m_average)
		{
			emit_group(headers, "(avg)");
		}
	}
	return headers;
}

Array<String> SpectralMomentsQuery::build_base_headers() const
{
	Array<String> headers;
	if (m_out_cog)      headers.append("COG");
	if (m_out_spread)   headers.append("Spread");
	if (m_out_skewness) headers.append("Skewness");
	if (m_out_kurtosis) headers.append("Kurtosis");
	return headers;
}

void SpectralMomentsQuery::clear()
{
	Query::clear();
	m_points.clear();
	m_method = Method::Midpoint;
	m_window_duration = 0.025;
	m_window_type = speech::WindowType::Gaussian;
	m_min_freq = 0;
	m_max_freq = 0;
	m_preemph = 50.0;
	m_use_preemph = true;
	m_out_cog = true;
	m_out_spread = true;
	m_out_skewness = true;
	m_out_kurtosis = true;
	m_series = true;
	m_average = false;
	m_initial_layout = Concordance::Layout::Wide;
	m_output_time = false;
}

Handle<Concordance> SpectralMomentsQuery::execute()
{
	auto matches = search();
	int count = (int)matches.size();

	for (int i = 0; i < count; i++)
	{
		query_progress(i, count);
		if (m_cancel_requested) break;

		try
		{
			measure_match(*matches[i]);
		}
		catch (std::exception &)
		{
			auto &m = *matches[i];
			m.measurements.assign(field_count(), std::nan(""));
		}
	}

	auto conc = make_handle<Concordance>(m_constraints.size(), m_context, m_context_length, std::move(matches), nullptr);

	// Duration columns
	if (m_include_duration) {
		conc->set_has_duration(true);
		conc->set_duration_in_ms(m_duration_in_ms);
	}

	conc->set_spectral_moments_meta(m_out_cog, m_out_spread, m_out_skewness, m_out_kurtosis);

	// Measurement-time column(s)
	conc->set_has_time(m_output_time);

	if (m_method == Method::NPoint)
	{
		conc->set_measurement_info(m_points, m_average);
		conc->set_has_series(m_series);
		conc->set_layout(m_initial_layout);
	}

	conc->rebuild_extra_headers();

	auto lbl = this->label();
	if (lbl.starts_with("Query ")) {
		lbl = String::format("Concordance %d", Concordance::next_id());
	}
	conc->set_label(lbl, false);
	Project::get()->add_temp_concordance(conc);

	return conc;
}

void SpectralMomentsQuery::measure_match(QueryMatch &match) const
{
	auto annot = match.annotation();
	auto sound = annot->sound();
	if (!sound)
	{
		throw error("Cannot measure spectral moments in annotation \"%\" because it is not bound to any sound file", annot->path());
	}

	auto *target = match.reference_target();
	if (!target) {
		target = match.get(1);
	}
	double t1 = target->start_time;
	double t2 = target->end_time;

	int total = field_count();
	match.measurements.resize(total, std::nan(""));

	double effective_preemph = m_use_preemph ? m_preemph : 0.0;
	int fpp = moment_count();
	int idx = 0;

	auto store_moments = [&](const SpectralMoments &sm)
	{
		if (m_out_cog)      match.measurements[idx++] = sm.cog;
		if (m_out_spread)   match.measurements[idx++] = sm.spread;
		if (m_out_skewness) match.measurements[idx++] = sm.skewness;
		if (m_out_kurtosis) match.measurements[idx++] = sm.kurtosis;
	};

	if (m_method == Method::Midpoint)
	{
		double t = (t1 + t2) / 2.0;
		try {
			auto sm = compute_spectral_moments_at(sound, channel(), t,
				m_window_duration, m_window_type, m_min_freq, m_max_freq, effective_preemph);
			store_moments(sm);
		}
		catch (...) {
			for (int k = 0; k < fpp; k++) match.measurements[idx++] = std::nan("");
		}
	}
	else
	{
		double duration = t2 - t1;
		int npoints = (int)m_points.size();

		// Compute moments at each measurement point.
		Array<SpectralMoments> point_data;
		for (auto p : m_points)
		{
			double t = t1 + (p / 100.0) * duration;
			try {
				point_data.append(compute_spectral_moments_at(sound, channel(), t,
					m_window_duration, m_window_type, m_min_freq, m_max_freq, effective_preemph));
			}
			catch (...) {
				point_data.append(SpectralMoments());
			}
		}

		if (m_series)
		{
			for (intptr_t k = 0; k < npoints; k++) {
				store_moments(point_data[k]);
			}
		}

		if (m_average)
		{
			// Average each enabled moment independently.
			SpectralMoments avg;
			double sum_cog = 0, sum_spr = 0, sum_skw = 0, sum_krt = 0;
			int n_cog = 0, n_spr = 0, n_skw = 0, n_krt = 0;

			for (intptr_t k = 0; k < npoints; k++)
			{
				auto &sm = point_data[k];
				if (std::isfinite(sm.cog))      { sum_cog += sm.cog;      n_cog++; }
				if (std::isfinite(sm.spread))   { sum_spr += sm.spread;   n_spr++; }
				if (std::isfinite(sm.skewness)) { sum_skw += sm.skewness; n_skw++; }
				if (std::isfinite(sm.kurtosis)) { sum_krt += sm.kurtosis; n_krt++; }
			}

			avg.cog      = (n_cog > 0) ? (sum_cog / n_cog) : std::nan("");
			avg.spread   = (n_spr > 0) ? (sum_spr / n_spr) : std::nan("");
			avg.skewness = (n_skw > 0) ? (sum_skw / n_skw) : std::nan("");
			avg.kurtosis = (n_krt > 0) ? (sum_krt / n_krt) : std::nan("");

			store_moments(avg);
		}
	}
}

Handle<Query> SpectralMomentsQuery::copy() const
{
	auto c = make_handle<SpectralMomentsQuery>(this->parent(), String());

	c->m_constraints = m_constraints;
	c->m_metaconstraints = m_metaconstraints;
	c->selected_annotations = selected_annotations;
	c->m_label = m_label;
	c->m_context = m_context;
	c->m_context_length = m_context_length;
	c->m_ref_constraint = m_ref_constraint;
	c->m_include_duration = m_include_duration;
	c->m_duration_in_ms = m_duration_in_ms;

	c->m_method = m_method;
	c->m_points = m_points;
	c->m_window_duration = m_window_duration;
	c->m_window_type = m_window_type;
	c->m_min_freq = m_min_freq;
	c->m_max_freq = m_max_freq;
	c->m_preemph = m_preemph;
	c->m_use_preemph = m_use_preemph;
	c->m_out_cog = m_out_cog;
	c->m_out_spread = m_out_spread;
	c->m_out_skewness = m_out_skewness;
	c->m_out_kurtosis = m_out_kurtosis;
	c->m_series = m_series;
	c->m_average = m_average;
	c->m_initial_layout = m_initial_layout;
	c->m_output_time = m_output_time;
	c->m_content_modified = true;

	return c;
}

// ── XML serialization ────────────────────────────────────────────────────────

static const char *window_type_to_string(speech::WindowType wt)
{
	switch (wt)
	{
		case speech::WindowType::Bartlett:    return "Bartlett";
		case speech::WindowType::Blackman:    return "Blackman";
		case speech::WindowType::Gaussian:    return "Gaussian";
		case speech::WindowType::Hamming:     return "Hamming";
		case speech::WindowType::Hann:        return "Hann";
		case speech::WindowType::Kaiser:      return "Kaiser";
		case speech::WindowType::Rectangular: return "Rectangular";
		default:                              return "Hamming";
	}
}

static speech::WindowType string_to_window_type(std::string_view s)
{
	if (s == "Bartlett")    return speech::WindowType::Bartlett;
	if (s == "Blackman")    return speech::WindowType::Blackman;
	if (s == "Gaussian")    return speech::WindowType::Gaussian;
	if (s == "Hamming")     return speech::WindowType::Hamming;
	if (s == "Hann")        return speech::WindowType::Hann;
	if (s == "Kaiser")      return speech::WindowType::Kaiser;
	if (s == "Rectangular") return speech::WindowType::Rectangular;
	return speech::WindowType::Hamming;
}

void SpectralMomentsQuery::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try {
		root = read_xml(doc, m_path);
	}
	catch (...) {
		throw error("Cannot open spectral moments query \"%\"", m_path);
	}

	if (root.name() != str("Phonometrica")) {
		throw error("Invalid XML root in %", m_path);
	}

	auto attr = root.attribute("label");
	if (attr) {
		set_label(attr.value(), false);
	}
	else {
		set_label(filesystem::base_name(m_path), false);
	}

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Metadata"))
		{
			metadata_from_xml(node);
		}
		else if (node.name() == str("MetaConstraints"))
		{
			parse_metaconstraints_from_xml(node);
		}
		else if (node.name() == str("Constraints"))
		{
			parse_constraints_from_xml(node);
		}
		else if (node.name() == str("Options"))
		{
			parse_options_from_xml(node);
		}
		else if (node.name() == str("SpectralMomentsSettings"))
		{
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("Method"))
				{
					auto val = str(child.text().get());
					m_method = (val == "npoint") ? Method::NPoint : Method::Midpoint;
				}
				else if (child.name() == str("Points"))
				{
					m_points.clear();
					auto text = String(child.text().get());
					auto parts = text.split(" ");
					for (auto &p : parts) {
						bool ok;
						double v = p.to_float(&ok);
						if (ok) m_points.append(v);
					}
				}
				else if (child.name() == str("Series"))
				{
					m_series = child.text().as_bool(true);
				}
				else if (child.name() == str("NPointAverage"))
				{
					m_average = child.text().as_bool(false);
				}
				else if (child.name() == str("Layout"))
				{
					auto val = str(child.text().get());
					m_initial_layout = (val == "long") ? Concordance::Layout::Long : Concordance::Layout::Wide;
				}
				else if (child.name() == str("WindowDuration"))
				{
					m_window_duration = child.text().as_double(0.025);
				}
				else if (child.name() == str("WindowType"))
				{
					m_window_type = string_to_window_type(child.text().get());
				}
				else if (child.name() == str("MinFrequency"))
				{
					m_min_freq = child.text().as_double(0);
				}
				else if (child.name() == str("MaxFrequency"))
				{
					m_max_freq = child.text().as_double(0);
				}
				else if (child.name() == str("Preemphasis"))
				{
					m_preemph = child.text().as_double(50.0);
				}
				else if (child.name() == str("UsePreemphasis"))
				{
					m_use_preemph = child.text().as_bool(true);
				}
				else if (child.name() == str("OutputCOG"))
				{
					m_out_cog = child.text().as_bool(true);
				}
				else if (child.name() == str("OutputSpread"))
				{
					m_out_spread = child.text().as_bool(true);
				}
				else if (child.name() == str("OutputSkewness"))
				{
					m_out_skewness = child.text().as_bool(true);
				}
				else if (child.name() == str("OutputKurtosis"))
				{
					m_out_kurtosis = child.text().as_bool(true);
				}
				else if (child.name() == str("OutputTime"))
				{
					m_output_time = child.text().as_bool(false);
				}
			}
		}
	}

	m_loaded = true;
}

void SpectralMomentsQuery::write()
{
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value("SpectralMomentsQuery");
	root.append_attribute("label").set_value(m_label.data());

	auto metadata_node = root.append_child("Metadata");
	metadata_to_xml(metadata_node);

	auto meta_node = root.append_child("MetaConstraints");
	auto file_sel_node = meta_node.append_child("FileSelection");
	for (auto &file : selected_annotations) {
		add_data_node(file_sel_node, "File", file->path());
	}
	for (auto &mc : m_metaconstraints) {
		mc->to_xml(meta_node);
	}

	auto option_node = root.append_child("Options");
	auto ctx_node = option_node.append_child("Context");
	auto type_attr = ctx_node.append_attribute("type");
	ctx_node.append_attribute("ref").set_value(m_ref_constraint);
	switch (m_context)
	{
		case Context::Labels:
			type_attr.set_value("labels");
			break;
		case Context::KWIC:
			type_attr.set_value("kwic");
			ctx_node.append_attribute("length").set_value(m_context_length);
			break;
		case Context::WithinEvent:
			type_attr.set_value("event");
			break;
		default:
			type_attr.set_value("none");
	}

	if (m_include_duration) {
		auto dur_node = option_node.append_child("Duration");
		dur_node.append_attribute("enabled").set_value(true);
		dur_node.append_attribute("unit").set_value(m_duration_in_ms ? "ms" : "s");
	}

	auto data_node = root.append_child("Constraints");
	for (auto &constraint : m_constraints) {
		constraint.to_xml(data_node);
	}

	auto sm_node = root.append_child("SpectralMomentsSettings");
	add_data_node(sm_node, "Method", m_method == Method::NPoint ? "npoint" : "midpoint");
	add_data_node(sm_node, "WindowDuration", String::format("%.4f", m_window_duration));
	add_data_node(sm_node, "WindowType", window_type_to_string(m_window_type));
	add_data_node(sm_node, "MinFrequency", String::format("%.1f", m_min_freq));
	add_data_node(sm_node, "MaxFrequency", String::format("%.1f", m_max_freq));
	add_data_node(sm_node, "Preemphasis", String::format("%.1f", m_preemph));
	add_data_node(sm_node, "UsePreemphasis", String::convert(m_use_preemph));
	add_data_node(sm_node, "OutputCOG", String::convert(m_out_cog));
	add_data_node(sm_node, "OutputSpread", String::convert(m_out_spread));
	add_data_node(sm_node, "OutputSkewness", String::convert(m_out_skewness));
	add_data_node(sm_node, "OutputKurtosis", String::convert(m_out_kurtosis));
	add_data_node(sm_node, "OutputTime", String::convert(m_output_time));

	if (m_method == Method::NPoint && !m_points.empty())
	{
		String pts;
		for (intptr_t i = 0; i < m_points.size(); i++)
		{
			if (i > 0) pts.append(' ');
			pts.append(String::format("%.1f", m_points[i]));
		}
		add_data_node(sm_node, "Points", pts);
		add_data_node(sm_node, "Series", String::convert(m_series));
		add_data_node(sm_node, "NPointAverage", String::convert(m_average));
		add_data_node(sm_node, "Layout", m_initial_layout == Concordance::Layout::Long ? "long" : "wide");
	}

	write_xml(doc, m_path);
}

} // namespace phonometrica
