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
 * Created: 17/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime.hpp>
#include <phon/application/conc/voice_quality_query.hpp>
#include <phon/application/project.hpp>
#include <phon/application/property.hpp>
#include <phon/application/sound.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

VoiceQualityQuery::VoiceQualityQuery(Directory *parent, String path) :
		Query(parent, String())
{
	m_path = std::move(path);
	if (!m_path.empty()) {
		load();
	}
}

int VoiceQualityQuery::feature_count() const
{
	int n = 0;
	if (m_out_num_pulses)        ++n;
	if (m_out_voicing)           ++n;
	if (m_out_mean_period)       ++n;
	if (m_out_mean_f0)           ++n;
	if (m_out_jitter_local)      ++n;
	if (m_out_jitter_local_abs)  ++n;
	if (m_out_jitter_rap)        ++n;
	if (m_out_jitter_ppq5)       ++n;
	if (m_out_jitter_ddp)        ++n;
	if (m_out_shimmer_local)     ++n;
	if (m_out_shimmer_local_db)  ++n;
	if (m_out_shimmer_apq3)      ++n;
	if (m_out_shimmer_apq5)      ++n;
	if (m_out_shimmer_apq11)     ++n;
	if (m_out_hnr)               ++n;
	// Fallback: if the user somehow disabled everything, still produce
	// a "Pulses" column so the concordance isn't degenerate.
	return (n > 0) ? n : 1;
}

Array<String> VoiceQualityQuery::build_headers() const
{
	Array<String> headers;
	if (m_out_num_pulses)        headers.append("Pulses");
	if (m_out_voicing)           headers.append("Voicing(%)");
	if (m_out_mean_period)       headers.append("Period(ms)");
	if (m_out_mean_f0)           headers.append("F0(Hz)");
	if (m_out_jitter_local)      headers.append("Jitter(%)");
	if (m_out_jitter_local_abs)  headers.append("Jitter(\xC2\xB5s)"); // µs
	if (m_out_jitter_rap)        headers.append("RAP(%)");
	if (m_out_jitter_ppq5)       headers.append("PPQ5(%)");
	if (m_out_jitter_ddp)        headers.append("DDP(%)");
	if (m_out_shimmer_local)     headers.append("Shimmer(%)");
	if (m_out_shimmer_local_db)  headers.append("Shimmer(dB)");
	if (m_out_shimmer_apq3)      headers.append("APQ3(%)");
	if (m_out_shimmer_apq5)      headers.append("APQ5(%)");
	if (m_out_shimmer_apq11)     headers.append("APQ11(%)");
	if (m_out_hnr)               headers.append("HNR(dB)");
	if (headers.empty())         headers.append("Pulses");

	// Trailing per-match parameter columns when override is active.
	if (override_enabled()) {
		headers.append("Min pitch");
		headers.append("Max pitch");
	}

	return headers;
}

void VoiceQualityQuery::clear()
{
	Query::clear();
	m_f0_min = 75.0;
	m_f0_max = 600.0;
	m_out_num_pulses       = true;
	m_out_voicing          = true;
	m_out_mean_period      = false;
	m_out_mean_f0          = true;
	m_out_jitter_local     = true;
	m_out_jitter_local_abs = false;
	m_out_jitter_rap       = false;
	m_out_jitter_ppq5      = false;
	m_out_jitter_ddp       = false;
	m_out_shimmer_local    = true;
	m_out_shimmer_local_db = false;
	m_out_shimmer_apq3     = false;
	m_out_shimmer_apq5     = false;
	m_out_shimmer_apq11    = false;
	m_out_hnr              = true;
	m_instant_target_count.store(0, std::memory_order_relaxed);
	m_override_category.clear();
	m_override_levels.clear();
	m_show_params = true;
}

Handle<Concordance> VoiceQualityQuery::execute()
{
	m_instant_target_count.store(0, std::memory_order_relaxed);

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

	auto matches = search();
	int count = (int)matches.size();

	measure_matches(matches, [this](QueryMatch &m) {
		try
		{
			measure_match(m);
		}
		catch (std::exception &)
		{
			m.measurements.assign(field_count(), std::nan(""));
		}
	});

	auto conc = Handle<Concordance>::make(m_constraints.size(), m_context, m_context_length, std::move(matches), nullptr);

	if (m_include_duration) {
		conc->set_has_duration(true);
		conc->set_duration_in_ms(m_duration_in_ms);
	}

	conc->set_voice_quality_meta(
		m_out_num_pulses, m_out_voicing, m_out_mean_period, m_out_mean_f0,
		m_out_jitter_local, m_out_jitter_local_abs,
		m_out_jitter_rap, m_out_jitter_ppq5, m_out_jitter_ddp,
		m_out_shimmer_local, m_out_shimmer_local_db,
		m_out_shimmer_apq3, m_out_shimmer_apq5, m_out_shimmer_apq11,
		m_out_hnr);

	// Per-match F0-range columns when override is active. Storage flag tracks
	// whether the data is in match.measurements (set once at execute time);
	// display flag tracks whether the columns are shown (initially gated by
	// m_show_params; toggleable post-hoc via the Display menu).
	conc->set_has_per_match_pitch_range(override_enabled() && m_show_params);
	conc->set_per_match_pitch_range_available(override_enabled());

	conc->rebuild_extra_headers();

	auto lbl = this->label();
	if (lbl.starts_with("Query ")) {
		lbl = String::format("Concordance %d", Concordance::next_id());
	}
	conc->set_label(lbl, false);
	Project::get()->add_temp_concordance(conc);

	return conc;
}

void VoiceQualityQuery::measure_match(QueryMatch &match) const
{
	int total = field_count();
	match.measurements.assign(total, std::nan(""));

	// Bound by reference, not by value: several matches from one annotation are measured at the
	// same time, and copying either Handle would update a refcount that is not atomic.
	auto &annot = match.annotation();
	auto &sound = annot->sound();
	if (!sound) {
		throw error("Cannot measure voice quality in annotation \"%\" because it is not bound to any sound file",
		            annot->path());
	}

	auto *target = match.reference_target();
	if (!target) {
		target = match.get(1);
	}
	if (!target) return;

	double t1 = target->start_time;
	double t2 = target->end_time;

	// Voice quality is undefined on instants — the kernel needs a span of
	// samples to detect pulses. We flag these but don't throw, so the rest
	// of the concordance still measures correctly.
	if (!(t2 > t1)) {
		m_instant_target_count.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	// Per-file F0-range override lookup. Local copies so the const member
	// fields stay untouched (and concurrent measurement remains safe should
	// we ever parallelize).
	double local_f0_min = m_f0_min;
	double local_f0_max = m_f0_max;

	if (override_enabled())
	{
		auto value = annot->get_property_value(m_override_category);
		if (!value.empty())
		{
			auto it = m_override_levels.find(value);
			if (it != m_override_levels.end())
			{
				const auto &ov = it->second;
				if (ov.f0_min > 0) local_f0_min = ov.f0_min;
				if (ov.f0_max > 0) local_f0_max = ov.f0_max;
			}
		}
	}

	speech::VoiceReport r;
	try {
		r = sound->compute_voice_report(channel(), t1, t2, local_f0_min, local_f0_max);
	}
	catch (...) {
		// Leave the NaN-filled vector as-is, but still write trailing slots below
		// so the per-match columns reflect what range *would* have been applied.
	}

	// Store the same scaled values the GUI "Voice report" displays, so CSV
	// exports are immediately readable without per-column scaling on the
	// reader side.
	int idx = 0;
	if (m_out_num_pulses)        match.measurements[idx++] = static_cast<double>(r.num_pulses);
	if (m_out_voicing)           match.measurements[idx++] = r.voiced_frame_fraction * 100.0; // [0,1] → %
	if (m_out_mean_period)       match.measurements[idx++] = r.mean_period    * 1000.0;       // s  → ms
	if (m_out_mean_f0)           match.measurements[idx++] = r.mean_f0;                       // Hz
	if (m_out_jitter_local)      match.measurements[idx++] = r.jitter_local   * 100.0;        // rel → %
	if (m_out_jitter_local_abs)  match.measurements[idx++] = r.jitter_local_abs * 1.0e6;      // s  → µs
	if (m_out_jitter_rap)        match.measurements[idx++] = r.jitter_rap     * 100.0;
	if (m_out_jitter_ppq5)       match.measurements[idx++] = r.jitter_ppq5    * 100.0;
	if (m_out_jitter_ddp)        match.measurements[idx++] = r.jitter_ddp     * 100.0;
	if (m_out_shimmer_local)     match.measurements[idx++] = r.shimmer_local  * 100.0;
	if (m_out_shimmer_local_db)  match.measurements[idx++] = r.shimmer_local_db;              // dB
	if (m_out_shimmer_apq3)      match.measurements[idx++] = r.shimmer_apq3   * 100.0;
	if (m_out_shimmer_apq5)      match.measurements[idx++] = r.shimmer_apq5   * 100.0;
	if (m_out_shimmer_apq11)     match.measurements[idx++] = r.shimmer_apq11  * 100.0;
	if (m_out_hnr)               match.measurements[idx++] = r.hnr;                           // dB

	// Trailing per-match parameter columns when override is active. We jump to
	// feature_count() rather than relying on idx, because if all features were
	// somehow disabled feature_count() returns 1 ("Pulses" fallback) and idx
	// would be 0 — they'd disagree. feature_count() is authoritative.
	if (override_enabled()) {
		int trailing_base = feature_count();
		match.measurements[trailing_base]     = local_f0_min;
		match.measurements[trailing_base + 1] = local_f0_max;
	}
}

Handle<Query> VoiceQualityQuery::copy() const
{
	auto c = Handle<VoiceQualityQuery>::make(this->parent(), String());

	c->m_constraints       = m_constraints;
	c->m_metaconstraints   = m_metaconstraints;
	c->selected_annotations = selected_annotations;
	c->m_label             = m_label;
	c->m_context           = m_context;
	c->m_context_length    = m_context_length;
	c->m_ref_constraint    = m_ref_constraint;
	c->m_include_duration  = m_include_duration;
	c->m_duration_in_ms    = m_duration_in_ms;

	c->m_f0_min                = m_f0_min;
	c->m_f0_max                = m_f0_max;
	c->m_out_num_pulses        = m_out_num_pulses;
	c->m_out_voicing           = m_out_voicing;
	c->m_out_mean_period       = m_out_mean_period;
	c->m_out_mean_f0           = m_out_mean_f0;
	c->m_out_jitter_local      = m_out_jitter_local;
	c->m_out_jitter_local_abs  = m_out_jitter_local_abs;
	c->m_out_jitter_rap        = m_out_jitter_rap;
	c->m_out_jitter_ppq5       = m_out_jitter_ppq5;
	c->m_out_jitter_ddp        = m_out_jitter_ddp;
	c->m_out_shimmer_local     = m_out_shimmer_local;
	c->m_out_shimmer_local_db  = m_out_shimmer_local_db;
	c->m_out_shimmer_apq3      = m_out_shimmer_apq3;
	c->m_out_shimmer_apq5      = m_out_shimmer_apq5;
	c->m_out_shimmer_apq11     = m_out_shimmer_apq11;
	c->m_out_hnr               = m_out_hnr;

	c->m_override_category     = m_override_category;
	c->m_override_levels       = m_override_levels;
	c->m_show_params           = m_show_params;

	c->m_content_modified = true;
	return c;
}

// ── XML serialization ────────────────────────────────────────────────────────

void VoiceQualityQuery::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try {
		root = read_xml(doc, m_path);
	}
	catch (...) {
		throw error("Cannot open voice quality query \"%\"", m_path);
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
		else if (node.name() == str("VoiceQualitySettings"))
		{
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if      (child.name() == str("F0Min"))                m_f0_min = child.text().as_double(75.0);
				else if (child.name() == str("F0Max"))                m_f0_max = child.text().as_double(600.0);
				else if (child.name() == str("OutputNumPulses"))      m_out_num_pulses       = child.text().as_bool(true);
				else if (child.name() == str("OutputVoicing"))        m_out_voicing          = child.text().as_bool(true);
				else if (child.name() == str("OutputMeanPeriod"))     m_out_mean_period      = child.text().as_bool(false);
				else if (child.name() == str("OutputMeanF0"))         m_out_mean_f0          = child.text().as_bool(true);
				else if (child.name() == str("OutputJitterLocal"))    m_out_jitter_local     = child.text().as_bool(true);
				else if (child.name() == str("OutputJitterLocalAbs")) m_out_jitter_local_abs = child.text().as_bool(false);
				else if (child.name() == str("OutputJitterRap"))      m_out_jitter_rap       = child.text().as_bool(false);
				else if (child.name() == str("OutputJitterPpq5"))     m_out_jitter_ppq5      = child.text().as_bool(false);
				else if (child.name() == str("OutputJitterDdp"))      m_out_jitter_ddp       = child.text().as_bool(false);
				else if (child.name() == str("OutputShimmerLocal"))   m_out_shimmer_local    = child.text().as_bool(true);
				else if (child.name() == str("OutputShimmerLocalDb")) m_out_shimmer_local_db = child.text().as_bool(false);
				else if (child.name() == str("OutputShimmerApq3"))    m_out_shimmer_apq3     = child.text().as_bool(false);
				else if (child.name() == str("OutputShimmerApq5"))    m_out_shimmer_apq5     = child.text().as_bool(false);
				else if (child.name() == str("OutputShimmerApq11"))   m_out_shimmer_apq11    = child.text().as_bool(false);
				else if (child.name() == str("OutputHnr"))            m_out_hnr              = child.text().as_bool(true);
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
					if      (name == "F0Min") ov.f0_min = v;
					else if (name == "F0Max") ov.f0_max = v;
				}
				m_override_levels[value] = ov;
			}
		}
	}

	m_loaded = true;
}

void VoiceQualityQuery::write()
{
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value("VoiceQualityQuery");
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

	auto vq_node = root.append_child("VoiceQualitySettings");
	add_data_node(vq_node, "F0Min", String::format("%.1f", m_f0_min));
	add_data_node(vq_node, "F0Max", String::format("%.1f", m_f0_max));
	add_data_node(vq_node, "OutputNumPulses",      String::convert(m_out_num_pulses));
	add_data_node(vq_node, "OutputVoicing",        String::convert(m_out_voicing));
	add_data_node(vq_node, "OutputMeanPeriod",     String::convert(m_out_mean_period));
	add_data_node(vq_node, "OutputMeanF0",         String::convert(m_out_mean_f0));
	add_data_node(vq_node, "OutputJitterLocal",    String::convert(m_out_jitter_local));
	add_data_node(vq_node, "OutputJitterLocalAbs", String::convert(m_out_jitter_local_abs));
	add_data_node(vq_node, "OutputJitterRap",      String::convert(m_out_jitter_rap));
	add_data_node(vq_node, "OutputJitterPpq5",     String::convert(m_out_jitter_ppq5));
	add_data_node(vq_node, "OutputJitterDdp",      String::convert(m_out_jitter_ddp));
	add_data_node(vq_node, "OutputShimmerLocal",   String::convert(m_out_shimmer_local));
	add_data_node(vq_node, "OutputShimmerLocalDb", String::convert(m_out_shimmer_local_db));
	add_data_node(vq_node, "OutputShimmerApq3",    String::convert(m_out_shimmer_apq3));
	add_data_node(vq_node, "OutputShimmerApq5",    String::convert(m_out_shimmer_apq5));
	add_data_node(vq_node, "OutputShimmerApq11",   String::convert(m_out_shimmer_apq11));
	add_data_node(vq_node, "OutputHnr",            String::convert(m_out_hnr));

	// Per-file F0-range override
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
			emit_param("F0Min", entry.second.f0_min);
			emit_param("F0Max", entry.second.f0_max);
		}
	}

	write_xml(doc, m_path);
}

} // namespace phonometrica
