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
 * Created: 29/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime.hpp>
#include <phon/application/conc/intensity_query.hpp>
#include <phon/application/project.hpp>
#include <phon/application/sound.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

IntensityQuery::IntensityQuery(Directory *parent, String path) :
		Query(parent, String())
{
	m_path = std::move(path);
	if (!m_path.empty()) {
		load();
	}
}

int IntensityQuery::field_count() const
{
	int n = 0;
	if (m_method == Method::Midpoint)
	{
		n = 1;
	}
	else
	{
		int npoints = (int)m_points.size();
		if (npoints < 1) npoints = 1;
		if (m_series)  n += npoints;
		if (m_average) n += 1;
	}
	return n;
}

Array<String> IntensityQuery::build_headers() const
{
	Array<String> headers;

	if (m_method == Method::Midpoint)
	{
		headers.append("Intensity(dB)");
	}
	else
	{
		if (m_series)
		{
			for (intptr_t p = 0; p < m_points.size(); p++)
			{
				char suffix[32];
				snprintf(suffix, sizeof(suffix), "Intensity(dB)(%d%%)", (int)m_points[p]);
				headers.append(suffix);
			}
		}
		if (m_average)
		{
			headers.append("Intensity(dB)(avg)");
		}
	}
	return headers;
}

Array<String> IntensityQuery::build_base_headers() const
{
	Array<String> headers;
	headers.append("Intensity(dB)");
	return headers;
}

void IntensityQuery::clear()
{
	Query::clear();
	m_points.clear();
	m_method = Method::Midpoint;
	m_series = true;
	m_average = false;
	m_initial_layout = Concordance::Layout::Wide;
	m_output_time = false;
}

Handle<Concordance> IntensityQuery::execute()
{
	auto matches = search();

	measure_matches(matches, [this](QueryMatch &m) {
		try
		{
			measure_match(m);
		}
		catch (std::exception &)
		{
			// If measurement fails for a single match (e.g. sound file not bound),
			// fill with NaN and continue rather than aborting the whole query.
			m.measurements.assign(field_count(), std::nan(""));
		}
	});

	auto conc = Handle<Concordance>::make(m_constraints.size(), m_context, m_context_length, std::move(matches), nullptr);

	// Duration columns
	if (m_include_duration) {
		conc->set_has_duration(true);
		conc->set_duration_in_ms(m_duration_in_ms);
	}

	conc->set_intensity_meta();

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

void IntensityQuery::measure_match(QueryMatch &match) const
{
	// Bound by reference, not by value: several matches from one annotation are measured at the
	// same time, and copying either Handle would update a refcount that is not atomic.
	auto &annot = match.annotation();
	auto &sound = annot->sound();
	if (!sound)
	{
		throw error("Cannot measure intensity in annotation \"%\" because it is not bound to any sound file", annot->path());
	}

	auto *target = match.reference_target();
	if (!target) {
		target = match.get(1);
	}
	double t1 = target->start_time;
	double t2 = target->end_time;

	int total = field_count();
	match.measurements.resize(total, std::nan(""));

	int idx = 0;

	if (m_method == Method::Midpoint)
	{
		double t = (t1 + t2) / 2.0;
		try {
			match.measurements[idx++] = sound->get_intensity(channel(), t);
		}
		catch (...) {
			match.measurements[idx++] = std::nan("");
		}
	}
	else
	{
		double duration = t2 - t1;
		int npoints = (int)m_points.size();

		Array<double> point_data;
		for (auto p : m_points)
		{
			double t = t1 + (p / 100.0) * duration;
			try {
				point_data.append(sound->get_intensity(channel(), t));
			}
			catch (...) {
				point_data.append(std::nan(""));
			}
		}

		if (m_series)
		{
			for (intptr_t k = 0; k < npoints; k++) {
				match.measurements[idx++] = point_data[k];
			}
		}

		if (m_average)
		{
			double sum = 0;
			int n = 0;
			for (intptr_t k = 0; k < npoints; k++)
			{
				double v = point_data[k];
				if (std::isfinite(v)) {
					sum += v;
					n++;
				}
			}
			match.measurements[idx++] = (n > 0) ? (sum / n) : std::nan("");
		}
	}
}

Handle<Query> IntensityQuery::copy() const
{
	auto c = Handle<IntensityQuery>::make(this->parent(), String());

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
	c->m_series = m_series;
	c->m_average = m_average;
	c->m_initial_layout = m_initial_layout;
	c->m_output_time = m_output_time;
	c->m_content_modified = true;

	return c;
}

// ── XML serialization ────────────────────────────────────────────────────────

void IntensityQuery::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try {
		root = read_xml(doc, m_path);
	}
	catch (...) {
		throw error("Cannot open intensity query \"%\"", m_path);
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
		else if (node.name() == str("IntensitySettings"))
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
					for (auto &p : parts)
					{
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
				else if (child.name() == str("OutputTime"))
				{
					m_output_time = child.text().as_bool(false);
				}
			}
		}
	}

	m_loaded = true;
}

void IntensityQuery::write()
{
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value("IntensityQuery");
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

	auto is_node = root.append_child("IntensitySettings");
	add_data_node(is_node, "Method", m_method == Method::NPoint ? "npoint" : "midpoint");

	if (m_method == Method::NPoint && !m_points.empty())
	{
		String pts;
		for (intptr_t i = 0; i < m_points.size(); i++)
		{
			if (i > 0) pts.append(' ');
			pts.append(String::format("%.1f", m_points[i]));
		}
		add_data_node(is_node, "Points", pts);
		add_data_node(is_node, "Series", String::convert(m_series));
		add_data_node(is_node, "NPointAverage", String::convert(m_average));
		add_data_node(is_node, "Layout", m_initial_layout == Concordance::Layout::Long ? "long" : "wide");
	}

	add_data_node(is_node, "OutputTime", String::convert(m_output_time));

	write_xml(doc, m_path);
}

} // namespace phonometrica
