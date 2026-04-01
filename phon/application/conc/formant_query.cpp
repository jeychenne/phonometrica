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
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime.hpp>
#include <phon/application/conc/formant_query.hpp>
#include <phon/application/project.hpp>
#include <phon/application/sound.hpp>
#include <phon/analysis/speech_utils.hpp>
#include <phon/analysis/weenink.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

FormantQuery::FormantQuery(Directory *parent, String path) :
		Query(meta::get_class<FormantQuery>(), parent, String()) // Pass empty path to avoid vtable issue
{
	// Set the path ourselves and call our own load(), since during base-class construction
	// the vtable still points to Query, not FormantQuery.
	m_path = std::move(path);
	if (!m_path.empty()) {
		load();
	}
}

// Only raw formant + bandwidth fields are stored per measurement point.
// ERB and Bark are computed on the fly by the concordance.
int FormantQuery::fields_per_point() const
{
	return m_nformant + (m_bandwidth ? m_nformant : 0);
}

int FormantQuery::field_count() const
{
	int fpp = fields_per_point();
	int n = 0;

	if (m_method == Method::Midpoint)
	{
		n = fpp;
	}
	else
	{
		int npoints = (int)m_points.size();
		if (npoints < 1) npoints = 1;
		if (m_series)  n += npoints * fpp;
		if (m_average) n += fpp;
	}

	if (m_automatic) n += 2; // max_freq, lpc_order per match
	return n;
}

// Build wide-mode column headers for one measurement point, with suffix.
// Only emits F and B headers (ERB/Bark are handled by the concordance).
Array<String> FormantQuery::build_headers() const
{
	Array<String> headers;

	auto emit_group = [&](const char *suffix)
	{
		for (intptr_t i = 1; i <= m_nformant; i++) {
			headers.append(String::format("F%d%s", (int)i, suffix));
		}
		if (m_bandwidth) {
			for (intptr_t i = 1; i <= m_nformant; i++) {
				headers.append(String::format("B%d%s", (int)i, suffix));
			}
		}
	};

	if (m_method == Method::Midpoint)
	{
		emit_group("");
	}
	else
	{
		// Time series: one group per measurement point with percentage suffix
		if (m_series)
		{
			for (intptr_t p = 1; p <= m_points.size(); p++)
			{
				auto pct = (int)m_points[p];
				char suffix[16];
				snprintf(suffix, sizeof(suffix), "(%d%%)", pct);
				emit_group(suffix);
			}
		}

		// Average: one group with "(avg)" suffix
		if (m_average)
		{
			emit_group("(avg)");
		}
	}

	if (m_automatic) {
		headers.append("Max freq");
		headers.append("LPC order");
	}

	return headers;
}

// Un-suffixed column names for a single measurement point: F1, F2, ..., [B1, ...].
// Only raw stored columns (no ERB/Bark).
Array<String> FormantQuery::build_base_headers() const
{
	Array<String> headers;

	for (intptr_t i = 1; i <= m_nformant; i++) {
		headers.append(String::format("F%d", (int)i));
	}
	if (m_bandwidth) {
		for (intptr_t i = 1; i <= m_nformant; i++) {
			headers.append(String::format("B%d", (int)i));
		}
	}

	return headers;
}

void FormantQuery::clear()
{
	Query::clear();
	m_points.clear();
	m_method = Method::Midpoint;
	m_nformant = 3;
	m_win_size = 0.025;
	m_max_bandwidth = 400;
	m_max_freq = 5500;
	m_lpc_order = 11;
	m_automatic = false;
	m_max_freq1 = 4000;
	m_max_freq2 = 6000;
	m_freq_step = 500;
	m_lpc_order1 = 10;
	m_lpc_order2 = 12;
	m_series = true;
	m_average = false;
	m_initial_layout = Concordance::Layout::Wide;
	m_bandwidth = false;
	m_erb = false;
	m_bark = false;
}

Handle<Concordance> FormantQuery::execute()
{
	// Phase 1: text search (reuse the base class search engine)
	auto matches = search();

	// Phase 2: formant measurement on each match
	int count = (int)matches.size();

	for (int i = 0; i < count; i++)
	{
		query_progress(i, count);
		if (m_cancel_requested) break;

		try
		{
			measure_match(*matches[i+1]); // 1-based indexing
		}
		catch (std::exception &e)
		{
			// If measurement fails for a single match (e.g. sound file not bound),
			// fill with NaN and continue rather than aborting the whole query.
			auto &m = *matches[i+1];
			m.measurements.assign(field_count(), std::nan(""));
		}
	}

	// Build concordance with formant metadata
	auto conc = make_handle<Concordance>(m_constraints.size(), m_context, m_context_length, std::move(matches), nullptr);

	// Duration columns
	if (m_include_duration) {
		conc->set_has_duration(true);
		conc->set_duration_in_ms(m_duration_in_ms);
	}

	// Set formant metadata — ERB/Bark are computed on the fly by the concordance
	conc->set_formant_meta(m_nformant, m_bandwidth, m_erb, m_bark, m_automatic);

	// Provide measurement metadata so the concordance can toggle between wide and long layout.
	if (m_method == Method::NPoint)
	{
		conc->set_measurement_info(m_points, m_average);
		conc->set_has_series(m_series);
		conc->set_layout(m_initial_layout);
	}

	// Build all display headers from the metadata
	conc->rebuild_extra_headers();

	auto lbl = this->label();
	if (lbl.starts_with("Query ")) {
		lbl = String::format("Concordance %d", Concordance::next_id());
	}
	conc->set_label(lbl, false);
	Project::get()->add_temp_concordance(conc);

	return conc;
}

// Fill match.measurements with raw formant and bandwidth values only.
// ERB and Bark are computed on the fly by Concordance::get_cell().
void FormantQuery::measure_match(Match &match) const
{
	using namespace speech;

	auto annot = match.annotation();
	auto sound = annot->sound();
	if (!sound)
	{
		throw error("Cannot measure formants in annotation \"%\" because it is not bound to any sound file", annot->path());
	}

	// Get the reference target's time boundaries
	auto *target = match.reference_target();
	if (!target) {
		target = match.get(1);
	}
	double t1 = target->start_time;
	double t2 = target->end_time;

	// Determine LPC parameters (manual or automatic)
	double max_freq;
	int lpc_order;

	if (m_automatic)
	{
		auto params = find_lpc_parameters(sound.get(), channel(), m_nformant, m_win_size,
		                                  t1, t2, m_max_freq1, m_max_freq2, m_freq_step,
		                                  m_lpc_order1, m_lpc_order2);
		max_freq = params.first;
		lpc_order = (int)params.second;
	}
	else
	{
		max_freq = m_max_freq;
		lpc_order = m_lpc_order;
	}

	auto nf = m_nformant;
	int total = field_count();
	match.measurements.resize(total, std::nan(""));

	// If Weenink's method failed to find suitable parameters, leave as NaN
	if (max_freq == 0)
	{
		return;
	}

	int idx = 0;

	// Helper: fill one measurement point's worth of columns from an nformant×2 data matrix.
	// Layout per point: F1..Fn, [B1..Bn]. No ERB/Bark — they are computed on the fly.
	auto fill_point = [&](const Array<double> &data)
	{
		for (int i = 1; i <= nf; i++) {
			match.measurements[idx++] = data(i, 1);
		}
		if (m_bandwidth) {
			for (int i = 1; i <= nf; i++) {
				match.measurements[idx++] = data(i, 2);
			}
		}
	};

	if (m_method == Method::Midpoint)
	{
		double t = (t1 + t2) / 2.0;
		auto data = sound->get_formants(channel(), t, nf, max_freq, m_win_size, lpc_order);
		fill_point(data);
	}
	else
	{
		// Measure at each point individually
		double duration = t2 - t1;
		int npoints = (int)m_points.size();

		// We always need per-point data (either for the series output or to compute the average).
		// Collect all per-point matrices.
		Array<Array<double>> point_data;
		for (auto p : m_points)
		{
			double t = t1 + (p / 100.0) * duration;
			point_data.append(sound->get_formants(channel(), t, nf, max_freq, m_win_size, lpc_order));
		}

		// Time series: output each point's data
		if (m_series)
		{
			for (intptr_t k = 1; k <= npoints; k++) {
				fill_point(point_data[k]);
			}
		}

		// Average: compute mean across the per-point data and output one group
		if (m_average)
		{
			// Build an averaged nformant×2 matrix
			Array<double> avg(nf, 2, 0.0);
			for (intptr_t k = 1; k <= npoints; k++)
			{
				for (int i = 1; i <= nf; i++) {
					for (int j = 1; j <= 2; j++) {
						avg(i, j) += point_data[k](i, j);
					}
				}
			}
			for (int i = 1; i <= nf; i++) {
				for (int j = 1; j <= 2; j++) {
					avg(i, j) /= npoints;
				}
			}
			fill_point(avg);
		}
	}

	// Per-match LPC parameters (automatic mode)
	if (m_automatic) {
		match.measurements[idx++] = max_freq;
		match.measurements[idx++] = (double)lpc_order;
	}
}

Handle<Query> FormantQuery::copy() const
{
	auto c = make_handle<FormantQuery>(this->parent(), String());

	// Copy base class fields
	c->m_constraints = m_constraints;
	c->m_metaconstraints = m_metaconstraints;
	c->selected_annotations = selected_annotations;
	c->m_label = m_label;
	c->m_context = m_context;
	c->m_context_length = m_context_length;
	c->m_ref_constraint = m_ref_constraint;
	c->m_include_duration = m_include_duration;
	c->m_duration_in_ms = m_duration_in_ms;

	// Copy formant-specific fields
	c->m_method = m_method;
	c->m_points = m_points;
	c->m_nformant = m_nformant;
	c->m_win_size = m_win_size;
	c->m_max_bandwidth = m_max_bandwidth;
	c->m_max_freq = m_max_freq;
	c->m_lpc_order = m_lpc_order;
	c->m_automatic = m_automatic;
	c->m_max_freq1 = m_max_freq1;
	c->m_max_freq2 = m_max_freq2;
	c->m_freq_step = m_freq_step;
	c->m_lpc_order1 = m_lpc_order1;
	c->m_lpc_order2 = m_lpc_order2;
	c->m_series = m_series;
	c->m_average = m_average;
	c->m_initial_layout = m_initial_layout;
	c->m_bandwidth = m_bandwidth;
	c->m_erb = m_erb;
	c->m_bark = m_bark;
	c->m_content_modified = true;

	return c;
}

// ── XML serialization ────────────────────────────────────────────────────────

void FormantQuery::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try {
		root = read_xml(doc, m_path);
	}
	catch (...) {
		throw error("Cannot open formant query \"%\"", m_path);
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
		else if (node.name() == str("FormantSettings"))
		{
			// Parse formant-specific settings
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
				else if (child.name() == str("Nformant"))
				{
					m_nformant = child.text().as_int(3);
				}
				else if (child.name() == str("WindowSize"))
				{
					m_win_size = child.text().as_double(0.025);
				}
				else if (child.name() == str("MaxBandwidth"))
				{
					m_max_bandwidth = child.text().as_double(400);
				}
				else if (child.name() == str("Automatic"))
				{
					m_automatic = child.text().as_bool(false);
				}
				else if (child.name() == str("MaxFrequency"))
				{
					m_max_freq = child.text().as_double(5500);
				}
				else if (child.name() == str("LpcOrder"))
				{
					m_lpc_order = child.text().as_int(11);
				}
				else if (child.name() == str("MaxFreqLow"))
				{
					m_max_freq1 = child.text().as_double(4000);
				}
				else if (child.name() == str("MaxFreqHigh"))
				{
					m_max_freq2 = child.text().as_double(6000);
				}
				else if (child.name() == str("FreqStep"))
				{
					m_freq_step = child.text().as_double(500);
				}
				else if (child.name() == str("LpcOrderLow"))
				{
					m_lpc_order1 = child.text().as_int(10);
				}
				else if (child.name() == str("LpcOrderHigh"))
				{
					m_lpc_order2 = child.text().as_int(12);
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
				else if (child.name() == str("Bandwidth"))
				{
					m_bandwidth = child.text().as_bool(false);
				}
				else if (child.name() == str("ERB"))
				{
					m_erb = child.text().as_bool(false);
				}
				else if (child.name() == str("Bark"))
				{
					m_bark = child.text().as_bool(false);
				}
			}
		}
	}

	m_loaded = true;
}

void FormantQuery::write()
{
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value("FormantQuery");
	root.append_attribute("label").set_value(m_label.data());

	auto metadata_node = root.append_child("Metadata");
	metadata_to_xml(metadata_node);

	// MetaConstraints (reuse base class pattern)
	auto meta_node = root.append_child("MetaConstraints");
	auto file_sel_node = meta_node.append_child("FileSelection");
	for (auto &file : selected_annotations) {
		add_data_node(file_sel_node, "File", file->path());
	}
	for (auto &mc : m_metaconstraints) {
		mc->to_xml(meta_node);
	}

	// Options (context)
	auto option_node = root.append_child("Options");
	auto ctx_node = option_node.append_child("Context");
	auto type_attr = ctx_node.append_attribute("type");
	ctx_node.append_attribute("ref").set_value(m_ref_constraint);
	switch (m_context)
	{
		case Context::Labels:
		{
			type_attr.set_value("labels");
		} break;
		case Context::KWIC:
		{
			type_attr.set_value("kwic");
			ctx_node.append_attribute("length").set_value(m_context_length);
		} break;
		default:
			type_attr.set_value("none");
	}

	if (m_include_duration) {
		auto dur_node = option_node.append_child("Duration");
		dur_node.append_attribute("enabled").set_value(true);
		dur_node.append_attribute("unit").set_value(m_duration_in_ms ? "ms" : "s");
	}

	// Constraints
	auto data_node = root.append_child("Constraints");
	for (auto &constraint : m_constraints) {
		constraint.to_xml(data_node);
	}

	// Formant settings
	auto fs_node = root.append_child("FormantSettings");
	add_data_node(fs_node, "Method", m_method == Method::NPoint ? "npoint" : "midpoint");

	if (m_method == Method::NPoint && !m_points.empty())
	{
		String pts;
		for (intptr_t i = 1; i <= m_points.size(); i++)
		{
			if (i > 1) pts.append(' ');
			pts.append(String::format("%.1f", m_points[i]));
		}
		add_data_node(fs_node, "Points", pts);
		add_data_node(fs_node, "Series", String::convert(m_series));
		add_data_node(fs_node, "NPointAverage", String::convert(m_average));
		add_data_node(fs_node, "Layout", m_initial_layout == Concordance::Layout::Long ? "long" : "wide");
	}

	add_data_node(fs_node, "Nformant", String::convert((intptr_t)m_nformant));
	add_data_node(fs_node, "WindowSize", String::format("%.4f", m_win_size));
	add_data_node(fs_node, "MaxBandwidth", String::format("%.1f", m_max_bandwidth));
	add_data_node(fs_node, "Automatic", String::convert(m_automatic));

	if (m_automatic)
	{
		add_data_node(fs_node, "MaxFreqLow", String::format("%.1f", m_max_freq1));
		add_data_node(fs_node, "MaxFreqHigh", String::format("%.1f", m_max_freq2));
		add_data_node(fs_node, "FreqStep", String::format("%.1f", m_freq_step));
		add_data_node(fs_node, "LpcOrderLow", String::convert((intptr_t)m_lpc_order1));
		add_data_node(fs_node, "LpcOrderHigh", String::convert((intptr_t)m_lpc_order2));
	}
	else
	{
		add_data_node(fs_node, "MaxFrequency", String::format("%.1f", m_max_freq));
		add_data_node(fs_node, "LpcOrder", String::convert((intptr_t)m_lpc_order));
	}

	add_data_node(fs_node, "Bandwidth", String::convert(m_bandwidth));
	add_data_node(fs_node, "ERB", String::convert(m_erb));
	add_data_node(fs_node, "Bark", String::convert(m_bark));

	write_xml(doc, m_path);
}

} // namespace phonometrica
