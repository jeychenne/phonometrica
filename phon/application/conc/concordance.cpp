/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 08/02/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <phon/application/conc/concordance.hpp>
#include <phon/application/project.hpp>
#include <phon/utils/xml.hpp>

namespace phonometrica {

static constexpr const char *EVENT_SEPARATOR = " ";
// file, layer, start time, end time
static const int FILE_INFO_COLUMN_COUNT = 4;


Concordance::Concordance(Directory *parent, const String &path) :
		DataTable(meta::get_class<Concordance>(), parent, path)
{
	preload();
}

Concordance::Concordance(intptr_t target_count, Context ctx, intptr_t context_length, Array <AutoMatch> matches, Directory *parent, const String &path) :
		DataTable(meta::get_class<Concordance>(), parent, path), m_matches(std::move(matches))
{
	m_target_count = (int) target_count;
	m_context_type = ctx;
	m_context_length = (int) context_length;
	m_context.reserve(m_matches.size());
	find_context();
	m_loaded = true;
	m_content_modified = true; // New concordance — prompt to save on close.
}

Concordance::Concordance(const Concordance &other) :
		DataTable(other.klass, other.parent(), String())
{
	m_target_count = other.m_target_count;
	m_context_type = other.m_context_type;
	m_context_length = other.m_context_length;
	m_extra_headers = other.m_extra_headers;
	m_measurement_points = other.m_measurement_points;
	m_base_headers = other.m_base_headers;
	m_fields_per_point = other.m_fields_per_point;
	m_has_average = other.m_has_average;
	m_layout = other.m_layout;

	m_matches.reserve(other.m_matches.size());

	for (auto &m : other.m_matches) {
		m_matches.append(std::make_unique<Match>(*m));
	}
	m_content_modified = true;
}

bool Concordance::empty() const
{
	return m_matches.empty();
}

String Concordance::get_header(intptr_t j) const
{
	// The logic of this function is the same as that of get_cell(). See comments there.
	if (j == 1) {
		return "File";
	}
	else if (j == 2) {
		return "Layer";
	}
	else if (j == 3) {
		return "Start time";
	}
	else if (j == 4) {
		return "End time";
	}
	else if (j == 5 && has_context()) {
		return "Left context";
	}

	j -= FILE_INFO_COLUMN_COUNT;
	if (has_context()) j--;

	// We are now ready to consume the match: j starts at 1.
	if (j <= m_target_count)
	{
		if (m_target_count == 1) {
			return "Target";
		}
		else {
			return String::format("Target %d", (int) j);
		}
	}

	j -= m_target_count;
	if (has_context())
	{
		if (j == 1) {
			return "Right context";
		}
		j--;
	}

	// Extra columns (formant measurements, etc.) — layout-dependent
	auto eff = effective_extra_count();
	if (eff > 0)
	{
		if (j <= eff)
		{
			if (m_layout == Layout::Long && has_measurement_data())
			{
				if (j == 1) return "Step";
				if (j == 2) return "Time";
				return m_base_headers[j - 2]; // 1-based
			}
			else
			{
				return m_extra_headers[j]; // 1-based
			}
		}
		j -= eff;
	}

	// We are now ready to consume the properties. Switch to base 0 because
	// we'll use an iterator.
	j--;

	if (j < Property::category_count())
	{
		auto it = Property::get_categories().begin();
		std::advance(it, j);
		return *it;
	}
	assert(j == Property::category_count());

	return "Description";
}

String Concordance::get_cell(intptr_t i, intptr_t j) const
{
	// In long mode, map display row to match index and point index.
	intptr_t mi = (m_layout == Layout::Long && has_measurement_data()) ? match_for_row(i) : i;

	// First handle information columns: these are fixed.
	if (j == 1) {
		return m_matches[mi]->annotation()->label();
	}
	else if (j == 2) {
		return String::convert(m_matches[mi]->get_layer(1));
	}
	else if (j == 3) {
        return String::format("%.4f", m_matches[mi]->get_start_time(1));
	}
	else if (j == 4) {
        return String::format("%.4f", m_matches[mi]->get_end_time(1));
	}
	else if (j == 5 && has_context()) {
		return get_left_context(mi);
	}

	// At this point, j == 5 if we have no context or 6 if we have one because we consumed the left context.
	j -= FILE_INFO_COLUMN_COUNT;
	if (has_context()) j--;

	// We are now ready to consume the match: j starts at 1.
	if (j <= m_target_count) {
		return m_matches[mi]->get_value(j);
	}

	// We now consume the right context if we have one
	j -= m_target_count;
	if (has_context())
	{
		if (j == 1) {
			return get_right_context(mi);
		}
		j--;
	}

	// Extra columns (formant measurements, etc.) — layout-dependent
	auto eff = effective_extra_count();
	if (eff > 0)
	{
		if (j <= eff)
		{
			if (m_layout == Layout::Long && has_measurement_data())
			{
				intptr_t pi = point_for_row(i); // 0-based point index

				if (j == 1) {
					// Step (1-based)
					return String::convert(intptr_t(pi + 1));
				}
				if (j == 2) {
					// Normalized time (0..1)
					return String::format("%.4f", m_measurement_points[pi + 1] / 100.0);
				}
				// Formant value at this point
				auto &meas = m_matches[mi]->measurements;
				intptr_t idx = pi * m_fields_per_point + (j - 3); // 0-based into measurements
				if (idx >= 0 && idx < (intptr_t)meas.size())
				{
					double val = meas[idx];
					if (std::isnan(val)) return "N/A";
					return String::format("%.1f", val);
				}
				return "N/A";
			}
			else
			{
				// Wide mode: direct index into measurements vector
				auto &meas = m_matches[mi]->measurements;
				intptr_t idx = j - 1; // 0-based
				if (idx < (intptr_t)meas.size())
				{
					double val = meas[idx];
					if (std::isnan(val)) return "N/A";
					return String::format("%.1f", val);
				}
				return "N/A";
			}
		}
		j -= eff;
	}

	// We are now ready to consume the properties. Switch to base 0 because
	// we'll use an iterator.
	j--;

	if (j < Property::category_count())
	{
		auto it = Property::get_categories().begin();
		std::advance(it, j);
		return m_matches[mi]->annotation()->get_property_value(*it);
	}

	// We now reach the description
	assert(j == Property::category_count());

	return m_matches[mi]->annotation()->description();
}

void Concordance::set_cell(intptr_t i, intptr_t j, const String &value)
{
	throw error("Cannot write cell value in concordance");
}

bool Concordance::is_left_context(intptr_t col) const
{
	// TODO: adjust context position for complex queries
	return has_context() && col == 5;
}

bool Concordance::is_right_context(intptr_t col) const
{
	return has_context() && col == 7;
}

bool Concordance::is_time(intptr_t col) const
{
	return col == 3 || col == 4;
}

intptr_t Concordance::row_count() const
{
	if (m_layout == Layout::Long && has_measurement_data())
	{
		return m_matches.size() * m_measurement_points.size();
	}
	return m_matches.size();
}

intptr_t Concordance::column_count() const
{
	// Add 1 for description.
	return FILE_INFO_COLUMN_COUNT + context_column_count() + m_target_count + effective_extra_count() + Property::category_count() + 1;
}


void Concordance::preload()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try
	{
		root = read_xml(doc, m_path);
	}
	catch (...)
	{
		throw error("Cannot open text query \"%\"", m_path);
	}

	if (root.name() != str("Phonometrica")) {
		throw error("Invalid XML project root in %", m_path);
	}

	auto attr = root.attribute("class");

	if (!attr || class_name() != attr.as_string()) {
		throw error("Expected a concordance, got a % file instead", attr.as_string());
	}
	attr = root.attribute("label");
	if (attr && attr.value()[0] != '\0') {
		set_label(attr.value(), false);
	}

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Metadata"))
		{
			metadata_from_xml(node);
		}
	}
}


void Concordance::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try
	{
		root = read_xml(doc, m_path);
	}
	catch (...)
	{
		throw error("Cannot open text query \"%\"", m_path);
	}

	if (root.name() != str("Phonometrica")) {
		throw error("Invalid XML project root in %", m_path);
	}

	auto attr = root.attribute("class");

	if (!attr || class_name() != attr.as_string()) {
		throw error("Expected a concordance, got a % file instead", attr.as_string());
	}

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Options"))
		{
			parse_options_from_xml(node);
		}
		else if (node.name() == str("ExtraHeaders"))
		{
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("Header")) {
					m_extra_headers.append(child.text().get());
				}
			}
		}
		else if (node.name() == str("MeasurementInfo"))
		{
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("Points"))
				{
					auto text = String(child.text().get());
					auto parts = text.split(" ");
					for (auto &s : parts)
					{
						bool ok;
						double v = s.to_float(&ok);
						if (ok) m_measurement_points.append(v);
					}
				}
				else if (child.name() == str("BaseHeaders"))
				{
					for (auto hdr = child.first_child(); hdr; hdr = hdr.next_sibling())
					{
						if (hdr.name() == str("Header")) {
							m_base_headers.append(hdr.text().get());
						}
					}
				}
				else if (child.name() == str("FieldsPerPoint"))
				{
					m_fields_per_point = child.text().as_int(0);
				}
				else if (child.name() == str("HasAverage"))
				{
					m_has_average = child.text().as_bool(false);
				}
				else if (child.name() == str("Layout"))
				{
					auto val = str(child.text().get());
					m_layout = (val == "long") ? Layout::Long : Layout::Wide;
				}
			}
		}
		else if (node.name() == str("Metadata"))
		{
			metadata_from_xml(node);
		}
		else if (node.name() == str("Matches"))
		{
			parse_matches_from_xml(node);
		}
	}

	find_context();
}

void Concordance::parse_options_from_xml(xml_node root)
{
	using str = std::string_view;

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Context"))
		{
			auto attr = node.attribute("type");

			if (attr.value() == str("labels"))
			{
				m_context_type = Context::Labels;
			}
			else if (attr.value() == str("kwic"))
			{
				m_context_type = Context::KWIC;
				attr = node.attribute("length");
				if (!attr) {
					throw error("KWIC context in concordance requires a length attribute");
				}
				m_context_length = attr.as_int();
				if (m_context_length < 1) {
					throw error("Invalid context length in KWIC concordance: %", m_context_length);
				}
			}
			else
			{
				m_context_type = Context::None;
			}
		}
		else
		{
			throw error("Invalid option for concordance: %", node.name());
		}
	}
}

void Concordance::parse_matches_from_xml(xml_node root)
{
	using str = std::string_view;

#ifdef PHON_TIMING
	auto first_time = clock();
#endif

	auto attr = root.attribute("count");
	if (!attr){
		throw error("Matches node has no 'count' attribute");
	}
	int size = attr.as_int();
	if (size > 0) m_matches.reserve(size);
	auto msg = String("Opening concordance %1").arg(label());
	request_progress(msg, "Loading matches...", size);

	attr = root.attribute("length");
	if (!attr) {
		throw error("Matches node has no 'length' attribute");
	}
	m_target_count = attr.as_int();
	if (m_target_count < 1) {
		throw error("Invalid length in Match node");
	}
	int count = 1;
	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() != str("Match")) {
			throw error("Expected a Match, got a % in concordance", node.name());
		}
		update_progress(count++);

		Handle<Annotation> annot;
		std::unique_ptr<Match::Target> first_target;
		Match::Target *last_target = nullptr;
		String path;
		std::vector<double> measurements;

		for (auto subnode = node.first_child(); subnode; subnode = subnode.next_sibling())
		{
			if (subnode.name() == str("Annotation"))
			{
				path = subnode.text().get();
				annot = recast<Annotation>(Project::get()->get(path));
			}
			else if (subnode.name() == str("Targets"))
			{
				if (!annot) {
					throw error("A match was found in file '%' but this file is no longer in the current project", path);
				}
				annot->open();

				for (auto target_node = subnode.first_child(); target_node; target_node = target_node.next_sibling())
				{
					attr = target_node.attribute("layer");
					if (!attr) {
						throw error("Missing 'layer' attribute in concordance target");
					}
					int layer = attr.as_int();
					attr = target_node.attribute("event");
					if (layer < 1 || layer > annot->size()) {
						throw error("Invalid layer index (%) in match", layer);
					}
					if (!attr) {
						throw error("Missing 'event' attribute in concordance target");
					}
					int index = attr.as_int();
					auto &events = annot->get_layer_events(layer);
					if (index < 1 || index > events.size()) {
						throw error("Invalid event index (%) in layer with % events", index, events.size());
					}
					auto event = events[index];
					attr = target_node.attribute("offset");
					if (!attr) {
						throw error("Missing 'offset' attribute in concordance target");
					}
					int offset = attr.as_int();
					attr = target_node.attribute("ref");
					if (!attr) {
						throw error("Missing 'ref' attribute in concordance target");
					}
					bool is_ref = attr.as_bool();
					String value = target_node.text().get();

					if (last_target)
					{
                        last_target->next = std::make_unique<Match::Target>(event.start, event.end, value, layer, offset, is_ref);
						last_target = last_target->next.get();
					}
					else
					{
                        first_target = std::make_unique<Match::Target>(event.start, event.end, value, layer, offset, is_ref);
						last_target = first_target.get();
					}
				}
			}
			else if (subnode.name() == str("Measurements"))
			{
				auto text = String(subnode.text().get());
				auto parts = text.split(" ");
				for (auto &s : parts)
				{
					if (s == "NaN") {
						measurements.push_back(std::nan(""));
					}
					else {
						bool ok;
						measurements.push_back(s.to_float(&ok));
						if (!ok) measurements.back() = std::nan("");
					}
				}
			}
			else
			{
				throw error("Invalid node in Match: %", subnode.name());
			}
		}

		assert(first_target);
		auto match = std::make_unique<Match>(annot, std::move(first_target));
		match->measurements = std::move(measurements);
		m_matches.append(std::move(match));
	}

#ifdef PHON_TIMING
	auto last_time = clock();
	auto total = double(last_time-first_time) * 1000 / CLOCKS_PER_SEC;
	std::cerr << "Total loading time concordance: " << total << " ms\n";
#endif
}

void Concordance::write()
{
	open();
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value(class_name().data());
	// Only write an explicit label if one was set. Otherwise, the filename is used.
	if (!m_label.empty()) {
		root.append_attribute("label").set_value(m_label.data());
	}
	root.append_attribute("type").set_value("text");
	auto metadata_node = root.append_child("Metadata");
	metadata_to_xml(metadata_node);

	auto option_node = root.append_child("Options");
	auto ctx_node = option_node.append_child("Context");
	auto type_attr = ctx_node.append_attribute("type");

	switch (m_context_type)
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

	auto matches_node = root.append_child("Matches");
	matches_node.append_attribute("count").set_value(m_matches.size());
	matches_node.append_attribute("length").set_value(m_target_count);

	// Serialize extra column headers (formant measurements, etc.)
	if (has_extra_columns())
	{
		auto extra_node = root.append_child("ExtraHeaders");
		for (intptr_t i = 1; i <= m_extra_headers.size(); i++) {
			add_data_node(extra_node, "Header", m_extra_headers[i]);
		}
	}

	// Serialize measurement metadata for wide/long toggle
	if (has_measurement_data())
	{
		auto minfo = root.append_child("MeasurementInfo");

		// Measurement points (percentages)
		String pts;
		for (intptr_t i = 1; i <= m_measurement_points.size(); i++)
		{
			if (i > 1) pts.append(' ');
			pts.append(String::format("%.1f", m_measurement_points[i]));
		}
		add_data_node(minfo, "Points", pts);

		// Base headers (un-suffixed)
		auto bh_node = minfo.append_child("BaseHeaders");
		for (intptr_t i = 1; i <= m_base_headers.size(); i++) {
			add_data_node(bh_node, "Header", m_base_headers[i]);
		}

		minfo.append_child("FieldsPerPoint").append_child(node_pcdata)
			.set_value(String::convert(intptr_t(m_fields_per_point)).data());
		minfo.append_child("HasAverage").append_child(node_pcdata)
			.set_value(m_has_average ? "true" : "false");
		minfo.append_child("Layout").append_child(node_pcdata)
			.set_value(m_layout == Layout::Long ? "long" : "wide");
	}

	auto msg = String("Writing concordance %1").arg(label());
	request_progress(msg, "Writing matches...", (int)m_matches.size());
	int count = 1;
	for (auto &match : m_matches)
	{
		update_progress(count++);
		match->to_xml(matches_node);
	}

	write_xml(doc, m_path);
}

bool Concordance::has_context() const
{
	return m_context_type != Context::None;
}

void Concordance::find_context()
{
	m_context.clear();

	switch (m_context_type)
	{
		case Context::Labels:
			find_labels_context();
			break;
		case Context::KWIC:
			find_kwic_context();
			break;
		default:
			break;
	}
}

void Concordance::find_kwic_context()
{
	String sep(EVENT_SEPARATOR);
	// FIXME: the progress dialog slows things down absurdly on macOS.
	//auto msg = String("Extracting KWIC context for concordance %1").arg(label());
	//request_progress(msg, "Loading matches", (int)m_matches.size());
	//int count = 1;

	for (auto &match : m_matches)
	{
		//update_progress(count++);
		m_context.append(get_kwic_context(*match, sep));
	}
}

bool Concordance::is_file_info_column(intptr_t col) const
{
	return col <= FILE_INFO_COLUMN_COUNT;
}

bool Concordance::is_metadata_column(intptr_t col) const
{
	intptr_t bound = FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count() + effective_extra_count();
	return col > bound;
}

bool Concordance::is_measurement_column(intptr_t col) const
{
	auto eff = effective_extra_count();
	if (eff == 0) return false;
	intptr_t lower = FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count();
	intptr_t upper = lower + eff;
	return col > lower && col <= upper;
}

void Concordance::find_labels_context()
{
//	auto msg = String("Extracting surrounding labels for concordance %1").arg(label());
	//request_progress(msg, "Loading matches", (int)m_matches.size());
//	int count = 1;

	for (auto &match : m_matches)
	{
		//update_progress(count++);
		m_context.append(get_labels_context(*match));
	}
}

String Concordance::get_left_context(intptr_t i) const
{
	return has_context() ? m_context[i].first : String();
}

String Concordance::get_right_context(intptr_t i) const
{
	return has_context() ? m_context[i].second : String();
}

bool Concordance::is_target(intptr_t col) const
{
	intptr_t lower = FILE_INFO_COLUMN_COUNT + int(has_context()); // add 1 column for the left context
	intptr_t upper = lower + m_target_count;

	return col > lower && col <= upper;
}

Match &Concordance::get_match(intptr_t i)
{
	// In long mode, i is a display row — map to the actual match.
	if (m_layout == Layout::Long && has_measurement_data()) {
		return *m_matches[match_for_row(i)];
	}
	return *m_matches[i];
}

int Concordance::match_region_size() const
{
	return m_target_count + context_column_count();
}

int Concordance::context_column_count() const
{
	return has_context() ? 2 : 0;
}

String Concordance::label() const
{
	return m_label.empty() ? Document::label() : m_label;
}

void Concordance::set_label(String value, bool mutate)
{
	m_label = std::move(value);
	if (mutate) m_content_modified = true;
}

void Concordance::modify()
{
	m_content_modified = true;
}

AutoMatch Concordance::remove_match(intptr_t row)
{
	auto m = m_matches.at(row).release();
	m_matches.remove_at(row);
	modify();
	file_modified();

	return AutoMatch(m);
}

void Concordance::restore_match(intptr_t row, AutoMatch m)
{
	m_matches.insert(row, std::move(m));
	modify();
	file_modified();
}

Handle<Concordance> Concordance::unite(const Concordance &other, const String &label) const
{
	if (m_target_count != other.m_target_count) {
		throw error("Cannot unite get_concordances with different numbers of targets");
	}
	if (m_context_type != other.m_context_type) {
		throw error("Cannot unite get_concordances with different contexts");
	}
	if (m_context_length != other.m_context_length) {
		throw error("Cannot unite get_concordances with different context lengths");
	}

	std::set<AutoMatch, MatchLess> buffer;

	for (auto &match : m_matches) {
		buffer.insert(std::make_unique<Match>(*match));
	}
	for (auto &match : other.m_matches) {
		buffer.insert(std::make_unique<Match>(*match));
	}
	Array<AutoMatch> result;
	result.reserve((intptr_t)buffer.size());
	for (auto &match : buffer)
	{
		auto m = const_cast<AutoMatch&>(match).release();
		result.append(std::unique_ptr<Match>(m));
	}
	auto conc = make_handle<Concordance>(m_target_count, m_context_type, m_context_length, std::move(result), nullptr);
	conc->set_label(label, false);
	conc->set_extra_headers(m_extra_headers);
	if (has_measurement_data()) {
		conc->set_measurement_info(m_measurement_points, m_base_headers, m_fields_per_point, m_has_average);
		conc->set_layout(m_layout);
	}
	auto parent = Project::get()->data().get();
	parent->append(conc, false);

	return conc;
}

Handle<Concordance> Concordance::intersect(const Concordance &other, const String &label) const
{
	if (m_target_count != other.m_target_count) {
		throw error("Cannot intersect get_concordances with different numbers of targets");
	}
	if (m_context_type != other.m_context_type) {
		throw error("Cannot intersect get_concordances with different contexts");
	}
	if (m_context_length != other.m_context_length) {
		throw error("Cannot intersect get_concordances with different context lengths");
	}

	Array<AutoMatch> result;

	for (auto &match : m_matches)
	{
		// Matches are guaranteed to be sorted
		auto it = std::lower_bound(other.m_matches.begin(), other.m_matches.end(), match, MatchLess());

		if (it != other.m_matches.end() && **it == *match) {
			result.append(std::make_unique<Match>(*match));
		}
	}

	auto conc = make_handle<Concordance>(m_target_count, m_context_type, m_context_length, std::move(result), nullptr);
	conc->set_label(label, false);
	conc->set_extra_headers(m_extra_headers);
	if (has_measurement_data()) {
		conc->set_measurement_info(m_measurement_points, m_base_headers, m_fields_per_point, m_has_average);
		conc->set_layout(m_layout);
	}
	auto parent = Project::get()->data().get();
	parent->append(conc, false);

	return conc;
}

Handle<Concordance> Concordance::complement(const Concordance &other, const String &label) const
{
	if (m_target_count != other.m_target_count) {
		throw error("Cannot compute concordance complement for get_concordances with different numbers of targets");
	}
	if (m_context_type != other.m_context_type) {
		throw error("Cannot compute concordance complement for get_concordances with different contexts");
	}
	if (m_context_length != other.m_context_length) {
		throw error("Cannot compute concordance complement for get_concordances with different context lengths");
	}

	Array<AutoMatch> result;

	for (auto &match : other.m_matches)
	{
		// Matches are guaranteed to be sorted
		auto it = std::lower_bound(m_matches.begin(), m_matches.end(), match, MatchLess());

		if (it == m_matches.end() || **it != *match) {
			result.append(std::make_unique<Match>(*match));
		}
	}

	auto conc = make_handle<Concordance>(m_target_count, m_context_type, m_context_length, std::move(result), nullptr);
	conc->set_label(label, false);
	conc->set_extra_headers(m_extra_headers);
	if (has_measurement_data()) {
		conc->set_measurement_info(m_measurement_points, m_base_headers, m_fields_per_point, m_has_average);
		conc->set_layout(m_layout);
	}
	auto parent = Project::get()->data().get();
	parent->append(conc, false);

	return conc;
}

bool Concordance::update_match(intptr_t i, intptr_t target)
{
	bool modified;
	auto result = m_matches[i]->update(target, modified);
	if (modified) modify();

	return result;
}

bool Concordance::is_layer(intptr_t col) const
{
	return col == 2;
}

void Concordance::update_context(intptr_t i)
{
	if (this->has_context())
	{
		if (m_context_type == Context::KWIC)
		{
			m_context[i] = get_kwic_context(*m_matches[i], EVENT_SEPARATOR);
			m_content_modified = true;
		}
		else if (m_context_type == Context::Labels)
		{
			m_context[i] = get_labels_context(*m_matches[i]);
			m_content_modified = true;
		}
	}
}

std::pair<String, String> Concordance::get_kwic_context(const Match &match, const String &sep) const
{
	auto target = match.reference_target();
	auto annot = match.annotation().get();
	std::pair<String, String> ctx;

	if (target)
	{
        auto i = annot->get_event_index(target->layer, target->start_time);
		assert(i != 0);
		auto offset = target->offset;
		ctx.first = annot->left_context(target->layer, i, offset, m_context_length, sep);
		offset += target->value.size();
		ctx.second = annot->right_context(target->layer, i, offset, m_context_length, sep);
	}

	return ctx;
}

std::pair<String, String> Concordance::get_labels_context(const Match &match) const
{
	auto target = match.reference_target();
	std::pair<String, String> ctx;

	if (target)
	{
		auto &annot = *match.annotation();
		auto &events = annot.get_layer_events(target->layer);
        auto i = annot.get_event_index(target->layer, target->start_time);
		assert(i != 0);
        ctx.first = (i == 1) ? String() : events[i-1].text;
        ctx.second = (i == events.size()) ? String() : events[i+1].text;
	}

	return ctx;
}

std::pair<String, String> Concordance::get_context(intptr_t i) const
{
	if (has_context()) {
		return m_context[i];
	}

	return std::pair<String, String>();
}

void Concordance::set_measurement_info(Array<double> points, Array<String> base_headers, int fields_per_point, bool has_average)
{
	m_measurement_points = std::move(points);
	m_base_headers = std::move(base_headers);
	m_fields_per_point = fields_per_point;
	m_has_average = has_average;
}

intptr_t Concordance::effective_extra_count() const
{
	if (m_layout == Layout::Long && has_measurement_data())
	{
		// Step + Time + one group of base headers (un-suffixed)
		return 2 + m_base_headers.size();
	}
	return m_extra_headers.size();
}

intptr_t Concordance::match_for_row(intptr_t i) const
{
	// i is 1-based display row. Each match expands to npoints rows.
	intptr_t npoints = m_measurement_points.size();
	return ((i - 1) / npoints) + 1; // 1-based match index
}

intptr_t Concordance::point_for_row(intptr_t i) const
{
	// i is 1-based display row. Returns 0-based point index within the match.
	intptr_t npoints = m_measurement_points.size();
	return (i - 1) % npoints;
}

} // namespace phonometrica
