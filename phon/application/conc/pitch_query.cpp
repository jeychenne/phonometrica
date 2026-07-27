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
#include <phon/application/conc/pitch_query.hpp>
#include <phon/application/project.hpp>
#include <phon/application/property.hpp>
#include <phon/application/sound.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

PitchQuery::PitchQuery(Directory *parent, String path) :
		Query(parent, String()) // Pass empty path to avoid vtable issue
{
	// Set the path ourselves and call our own load(), since during base-class construction
	// the vtable still points to Query, not PitchQuery.
	m_path = std::move(path);
	if (!m_path.empty()) {
		load();
	}
}

int PitchQuery::field_count() const
{
	int fpp = 1; // always 1 stored field per point (F0 in Hz)
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

	// Per-match trailing columns: Min pitch + Max pitch when override is active
	if (override_enabled()) n += 2;
	return n;
}

Array<String> PitchQuery::build_headers() const
{
	Array<String> headers;

	auto emit_group = [&](const char *suffix)
	{
		headers.append(String::format("F0%s", suffix));
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
			for (intptr_t p = 0; p < m_points.size(); p++)
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

	if (override_enabled()) {
		headers.append("Min pitch");
		headers.append("Max pitch");
	}

	return headers;
}

Array<String> PitchQuery::build_base_headers() const
{
	Array<String> headers;
	headers.append("F0");
	return headers;
}

void PitchQuery::clear()
{
	Query::clear();
	m_points.clear();
	m_method = Method::Midpoint;
	m_algorithm = speech::PitchTracker::Praat;
	m_min_pitch = 75;
	m_max_pitch = 600;
	m_voicing_threshold = 0.5;
	m_time_step = 0.01;
	m_octave_jump_cost = 0.35;
	m_voicing_cost = 0.14;
	m_silence_threshold = 0.03;
	m_octave_cost = 0.01;
	m_series = true;
	m_average = false;
	m_initial_layout = Concordance::Layout::Wide;
	m_semitones = false;
	m_semitone_ref = 100;
	m_erb = false;
	m_output_time = false;
	m_override_category.clear();
	m_override_levels.clear();
}

Handle<Concordance> PitchQuery::execute()
{
	// Optional: warn about parameter-override coverage before doing any measurement.
	// Messages are routed through the runtime's error callback so they render in
	// red in the console panel (and fall back to print if no error sink is set).
	if (override_enabled())
	{
		auto &rt = Project::get()->runtime();
		auto emit_msg = [&](const String &msg) {
			rt.print_error(msg);
		};

		auto known = Property::get_values(m_override_category);
		if (known.empty())
		{
			emit_msg(String::format(
				"Warning: parameter override category \"%s\" has no known values "
				"in the project; all matches will use the default parameters.",
				m_override_category.data()));
		}
		else
		{
			Array<String> uncovered;
			for (auto &v : known) {
				if (m_override_levels.find(v) == m_override_levels.end()) {
					uncovered.append(v);
				}
			}
			if (!uncovered.empty())
			{
				String msg = String::format(
					"Warning: parameter override is enabled on category \"%s\" "
					"but the following level(s) have no override and will fall "
					"back to the default parameters: ",
					m_override_category.data());
				for (intptr_t i = 0; i < uncovered.size(); i++) {
					if (i > 0) msg.append(", ");
					msg.append(uncovered[i]);
				}
				emit_msg(msg);
			}
		}
	}

	// Phase 1: text search (reuse the base class search engine)
	auto matches = search();

	// Phase 2: pitch measurement on each match
	int count = (int)matches.size();

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

	// Build concordance with pitch metadata
	auto conc = Handle<Concordance>::make(m_constraints.size(), m_context, m_context_length, std::move(matches), nullptr);

	// Duration columns
	if (m_include_duration) {
		conc->set_has_duration(true);
		conc->set_duration_in_ms(m_duration_in_ms);
	}

	// Set pitch metadata — semitones and ERB are computed on the fly by the concordance
	conc->set_pitch_meta(m_semitones, m_semitone_ref, m_erb);
	// Per-match pitch-range columns when overrides are active
	conc->set_has_per_match_pitch_range(override_enabled() && m_show_params);
	conc->set_per_match_pitch_range_available(override_enabled());

	// Measurement-time column(s)
	conc->set_has_time(m_output_time);

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

void PitchQuery::measure_match(QueryMatch &match) const
{
	// Bound by reference, not by value: several matches from one annotation are measured at the
	// same time, and copying either Handle would update a refcount that is not atomic.
	auto &annot = match.annotation();
	auto &sound = annot->sound();
	if (!sound)
	{
		throw error("Cannot measure pitch in annotation \"%\" because it is not bound to any sound file", annot->path());
	}

	// Get the reference target's time boundaries
	auto *target = match.reference_target();
	if (!target) {
		target = match.get(1);
	}
	double t1 = target->start_time;
	double t2 = target->end_time;

	// Per-file parameter override lookup. Local copies of the bounds so the
	// const member fields stay untouched (and concurrent measurement remains
	// safe should we ever parallelize).
	double local_min_pitch = m_min_pitch;
	double local_max_pitch = m_max_pitch;

	if (override_enabled())
	{
		auto value = annot->get_property_value(m_override_category);
		if (!value.empty())
		{
			auto it = m_override_levels.find(value);
			if (it != m_override_levels.end())
			{
				const auto &ov = it->second;
				if (ov.min_pitch > 0) local_min_pitch = ov.min_pitch;
				if (ov.max_pitch > 0) local_max_pitch = ov.max_pitch;
			}
		}
	}

	int total = field_count();
	match.measurements.resize(total, std::nan(""));

	int idx = 0;

	if (m_method == Method::Midpoint)
	{
		double t = (t1 + t2) / 2.0;
		double f0 = sound->get_pitch(channel(), m_algorithm, t, local_min_pitch, local_max_pitch, m_voicing_threshold,
		                             m_octave_jump_cost, m_voicing_cost, m_silence_threshold, m_octave_cost, m_use_gaussian);
		match.measurements[idx++] = (f0 > 0) ? f0 : std::nan("");
	}
	else
	{
		// Measure at each point individually
		double duration = t2 - t1;
		int npoints = (int)m_points.size();

		// Collect all per-point F0 values
		Array<double> point_data;
		for (auto p : m_points)
		{
			double t = t1 + (p / 100.0) * duration;
			double f0 = sound->get_pitch(channel(), m_algorithm, t, local_min_pitch, local_max_pitch, m_voicing_threshold,
			                              m_octave_jump_cost, m_voicing_cost, m_silence_threshold, m_octave_cost, m_use_gaussian);
			point_data.append((f0 > 0) ? f0 : std::nan(""));
		}

		// Time series: output each point's data
		if (m_series)
		{
			for (intptr_t k = 0; k < npoints; k++) {
				match.measurements[idx++] = point_data[k];
			}
		}

		// Average: compute mean across voiced points
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

	// Trailing per-match columns when override is active
	if (override_enabled()) {
		match.measurements[idx++] = local_min_pitch;
		match.measurements[idx++] = local_max_pitch;
	}
}

Handle<Query> PitchQuery::copy() const
{
	auto c = Handle<PitchQuery>::make(this->parent(), String());

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

	// Copy pitch-specific fields
	c->m_method = m_method;
	c->m_points = m_points;
	c->m_algorithm = m_algorithm;
	c->m_min_pitch = m_min_pitch;
	c->m_max_pitch = m_max_pitch;
	c->m_voicing_threshold = m_voicing_threshold;
	c->m_time_step = m_time_step;
	c->m_octave_jump_cost = m_octave_jump_cost;
	c->m_voicing_cost = m_voicing_cost;
	c->m_silence_threshold = m_silence_threshold;
	c->m_octave_cost = m_octave_cost;
	c->m_use_gaussian = m_use_gaussian;
	c->m_series = m_series;
	c->m_average = m_average;
	c->m_initial_layout = m_initial_layout;
	c->m_semitones = m_semitones;
	c->m_semitone_ref = m_semitone_ref;
	c->m_erb = m_erb;
	c->m_output_time = m_output_time;
	c->m_override_category = m_override_category;
	c->m_override_levels = m_override_levels;
	c->m_show_params = m_show_params;
	c->m_content_modified = true;

	return c;
}

// ── XML serialization ────────────────────────────────────────────────────────

static const char *algorithm_to_string(speech::PitchTracker algo)
{
	switch (algo) {
		case speech::PitchTracker::Harvest: return "harvest";
		case speech::PitchTracker::Rapt:    return "rapt";
		case speech::PitchTracker::Reaper:  return "reaper";
		case speech::PitchTracker::Swipe:   return "swipe";
		case speech::PitchTracker::Praat:   return "praat";
		default:                            return "praat";
	}
}

static speech::PitchTracker string_to_algorithm(std::string_view s)
{
	if (s == "harvest") return speech::PitchTracker::Harvest;
	if (s == "rapt")    return speech::PitchTracker::Rapt;
	if (s == "reaper")  return speech::PitchTracker::Reaper;
	if (s == "swipe")   return speech::PitchTracker::Swipe;
	if (s == "praat")   return speech::PitchTracker::Praat;
	return speech::PitchTracker::Praat;
}

void PitchQuery::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try {
		root = read_xml(doc, m_path);
	}
	catch (...) {
		throw error("Cannot open pitch query \"%\"", m_path);
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
		else if (node.name() == str("PitchSettings"))
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
				else if (child.name() == str("Algorithm"))
				{
					m_algorithm = string_to_algorithm(child.text().get());
				}
				else if (child.name() == str("MinPitch"))
				{
					m_min_pitch = child.text().as_double(75);
				}
				else if (child.name() == str("MaxPitch"))
				{
					m_max_pitch = child.text().as_double(600);
				}
				else if (child.name() == str("VoicingThreshold"))
				{
					m_voicing_threshold = child.text().as_double(0.5);
				}
				else if (child.name() == str("TimeStep"))
				{
					m_time_step = child.text().as_double(0.01);
				}
				else if (child.name() == str("OctaveJumpCost"))
				{
					m_octave_jump_cost = child.text().as_double(0.35);
				}
				else if (child.name() == str("VoicingCost"))
				{
					m_voicing_cost = child.text().as_double(0.14);
				}
				else if (child.name() == str("SilenceThreshold"))
				{
					m_silence_threshold = child.text().as_double(0.03);
				}
				else if (child.name() == str("OctaveCost"))
				{
					m_octave_cost = child.text().as_double(0.01);
				}
				else if (child.name() == str("UseGaussian"))
				{
					m_use_gaussian = child.text().as_bool(false);
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
				else if (child.name() == str("Semitones"))
				{
					m_semitones = child.text().as_bool(false);
				}
				else if (child.name() == str("SemitoneReference"))
				{
					m_semitone_ref = child.text().as_double(100);
				}
				else if (child.name() == str("ERB"))
				{
					m_erb = child.text().as_bool(false);
				}
				else if (child.name() == str("OutputTime"))
				{
					m_output_time = child.text().as_bool(false);
				}
			}
		}
		else if (node.name() == str("ParameterOverride"))
		{
			auto cat_attr = node.attribute("category");
			if (cat_attr) {
				m_override_category = String(cat_attr.value());
			}
			auto show_attr = node.attribute("show");
			if (show_attr) {
				m_show_params = show_attr.as_bool(true);
			}
			m_override_levels.clear();
			for (auto level = node.first_child(); level; level = level.next_sibling())
			{
				if (level.name() != str("Level")) continue;
				auto val_attr = level.attribute("value");
				if (!val_attr) continue;
				String value(val_attr.value());
				LevelOverride ov;
				for (auto param = level.first_child(); param; param = param.next_sibling())
				{
					if (param.name() != str("Param")) continue;
					auto name_attr = param.attribute("name");
					if (!name_attr) continue;
					std::string_view name = name_attr.value();
					double v = param.text().as_double(0);
					if      (name == "MinPitch") ov.min_pitch = v;
					else if (name == "MaxPitch") ov.max_pitch = v;
				}
				m_override_levels[value] = ov;
			}
		}
	}

	m_loaded = true;
}

void PitchQuery::write()
{
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value("PitchQuery");
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
		case Context::WithinEvent:
		{
			type_attr.set_value("event");
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

	// Pitch settings
	auto ps_node = root.append_child("PitchSettings");
	add_data_node(ps_node, "Method", m_method == Method::NPoint ? "npoint" : "midpoint");

	if (m_method == Method::NPoint && !m_points.empty())
	{
		String pts;
		for (intptr_t i = 0; i < m_points.size(); i++)
		{
			if (i > 0) pts.append(' ');
			pts.append(String::format("%.1f", m_points[i]));
		}
		add_data_node(ps_node, "Points", pts);
		add_data_node(ps_node, "Series", String::convert(m_series));
		add_data_node(ps_node, "NPointAverage", String::convert(m_average));
		add_data_node(ps_node, "Layout", m_initial_layout == Concordance::Layout::Long ? "long" : "wide");
	}

	add_data_node(ps_node, "Algorithm", algorithm_to_string(m_algorithm));
	add_data_node(ps_node, "MinPitch", String::format("%.1f", m_min_pitch));
	add_data_node(ps_node, "MaxPitch", String::format("%.1f", m_max_pitch));
	add_data_node(ps_node, "VoicingThreshold", String::format("%.2f", m_voicing_threshold));
	add_data_node(ps_node, "TimeStep", String::format("%.4f", m_time_step));

	if (m_algorithm == speech::PitchTracker::Praat)
	{
		add_data_node(ps_node, "OctaveJumpCost", String::format("%.2f", m_octave_jump_cost));
		add_data_node(ps_node, "VoicingCost", String::format("%.2f", m_voicing_cost));
		add_data_node(ps_node, "SilenceThreshold", String::format("%.2f", m_silence_threshold));
		add_data_node(ps_node, "OctaveCost", String::format("%.2f", m_octave_cost));
		add_data_node(ps_node, "UseGaussian", String::convert(m_use_gaussian));
	}

	add_data_node(ps_node, "Semitones", String::convert(m_semitones));
	if (m_semitones) {
		add_data_node(ps_node, "SemitoneReference", String::format("%.1f", m_semitone_ref));
	}
	add_data_node(ps_node, "ERB", String::convert(m_erb));
	add_data_node(ps_node, "OutputTime", String::convert(m_output_time));

	// Per-file pitch-range override
	if (override_enabled())
	{
		auto ov_node = root.append_child("ParameterOverride");
		ov_node.append_attribute("category").set_value(m_override_category.data());
		ov_node.append_attribute("show").set_value(m_show_params ? "true" : "false");
		for (auto &entry : m_override_levels)
		{
			auto level_node = ov_node.append_child("Level");
			level_node.append_attribute("value").set_value(entry.first.data());
			auto emit_param = [&](const char *name, double v) {
				if (v > 0) {
					auto p = level_node.append_child("Param");
					p.append_attribute("name").set_value(name);
					p.text().set(String::format("%.1f", v).data());
				}
			};
			emit_param("MinPitch", entry.second.min_pitch);
			emit_param("MaxPitch", entry.second.max_pitch);
		}
	}

	write_xml(doc, m_path);
}

} // namespace phonometrica
