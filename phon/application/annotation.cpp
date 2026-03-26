/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/application/annotation.hpp>
#include <phon/application/constants.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/runtime/object.hpp>
#include <phon/application/project.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {


Annotation::Annotation(Directory *parent, String path) :
		Document(meta::get_class<Annotation>(), parent, std::move(path))
{
	m_type = guess_type();
	if (is_native() && has_path()) preload();
}

void Annotation::preload()
{
	assert(!m_path.empty());
	static std::string_view project_tag("Phonometrica");
	static std::string_view meta_tag = "Metadata";

	xml_document doc;
	auto root = read_xml(doc, m_path);

	if (root.name() != project_tag) {
		throw error("Invalid XML project root in %", m_path);
	}

	auto attr = root.attribute("class");

	if (!attr || class_name() != attr.as_string()) {
		throw error("[Input/Output] Expected an annotation file, got a % file instead", attr.as_string());
	}

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == meta_tag)
		{
			metadata_from_xml(node);
		}
	}
}

void Annotation::load()
{
	// Newly created annotations don't have a path yet.
	if (m_path.empty()) {
		if (m_type == Undefined) {
			m_type = Native;
		}
		return;
	}

	switch (m_type)
	{
		case Type::Native:
			read_from_native();
			break;
		case Type::TextGrid:
			read_textgrid(m_path, m_layers);
			break;
		default:
			throw error("Cannot load annotation: unsupported format");
	}
}

void Annotation::write()
{
	switch (m_type)
	{
		case Type::Native:
			write_as_native();
			break;
		case Type::TextGrid:
			write_as_textgrid(m_path);
			break;
		default:
			throw error("Cannot write annotation: unsupported format");
	}
}

bool Annotation::has_sound() const
{
	return bool(m_sound);
}

const Handle<Sound> &Annotation::sound() const
{
	return m_sound;
}

void Annotation::set_sound(const Handle<Sound> &value, bool mutate)
{
	m_sound = value;
	m_metadata_modified |= mutate;
}

Annotation::Type Annotation::guess_type()
{
	if (!m_path.empty())
	{
		auto ext = filesystem::ext(m_path, true);

		if (ext == PHON_EXT_ANNOTATION) {
			return Type::Native;
		}
		if (ext == ".textgrid") {
			return Type::TextGrid;
		}
		if (ext == ".lab") {
			return Type::WaveSurfer;
		}
	}

	return Type::Undefined;
}

const Array<Event> &Annotation::get_layer_events(intptr_t i) const
{
	return m_layers[i].events;
}

void Annotation::set_path(String path, bool mutate)
{
	Document::set_path(std::move(path), mutate);
	m_type = guess_type();
}

void Annotation::initialize(Runtime &rt)
{
	auto annot_get_field = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto &key = cast<String>(args[1]);

		if (key == "path") {
			return annot.path();
		}
		annot.open();

		if (key == "sound")
		{
			if (annot.has_sound()) {
				return annot.sound();
			}
			return Variant();
		}
		if (key == "nlayer") {
			return annot.layer_count();
		}
		throw error("[Index error] Annotation type has no member named \"%\"", key);
	};

	auto bind_to_sound = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto &path = cast<String>(args[1]);
		auto project = Project::get();
		project->import_file(path);
		auto snd = recast<Sound>(project->get(path));
		if (snd) annot.set_sound(snd);
		return Variant();
	};

	auto get_event_count = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer_index = cast<intptr_t>(args[1]);
		annot.open();

		try
		{
			return annot.layers()[layer_index].count();
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find layer %", layer_index);
		}
	};

	auto get_layer_count = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		annot.open();
		return annot.layer_count();
	};

	auto get_event_start = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto event = cast<intptr_t>(args[2]);
		annot.open();

		try
		{
			return annot.get_event(layer, event).start;
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find event % on layer %", event, layer);
		}
	};

	auto get_event_end = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto event = cast<intptr_t>(args[2]);
		annot.open();

		try
		{
			return annot.get_event(layer, event).end;
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find event % on layer %", event, layer);
		}
	};

	auto get_event_text = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto event = cast<intptr_t>(args[2]);
		annot.open();

		try
		{
			return annot.get_event(layer, event).text;
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find event % on layer %", event, layer);
		}
	};

	auto get_event_index = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto time = cast<double>(args[2]);
		annot.open();

		try
		{
			return annot.get_event_at_time(layer, time);
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find event at time % on layer %", time, layer);
		}
	};

	auto set_event_text = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto event = cast<intptr_t>(args[2]);
		auto &text = cast<String>(args[3]);
		annot.open();

		try
		{
			annot.set_event_text(layer, event, text);
			return Variant();
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find event % on layer %", event, layer);
		}
	};

	auto get_layer_label = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		annot.open();
		try {
			return annot.get_layer_label(layer);
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find layer %", layer);
		}
	};

	auto set_layer_label = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto &value = cast<String>(args[2]);
		annot.open();
		try {
			annot.set_layer_label(layer, value);
			return Variant();
		}
		catch (...)
		{
			throw error("[Index error] Couldn't find layer %", layer);
		}
	};

	auto add_interval = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto start = cast<double>(args[2]);
		auto end = cast<double>(args[3]);
		auto &text = cast<String>(args[4]);

		annot.open();
		annot.add_interval(layer, start, end, text);
		return Variant();
	};

	auto add_instant = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto time = cast<double>(args[2]);
		auto &text = cast<String>(args[3]);

		annot.open();
		annot.add_instant(layer, time, text);
		return Variant();
	};

	auto remove_interval = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto layer = cast<intptr_t>(args[1]);
		auto start = cast<double>(args[2]);
		auto end = cast<double>(args[3]);

		annot.open();
		annot.remove_interval(layer, start, end);
		return Variant();
	};

	auto remove_events = [](Runtime &, std::span<Variant> args) -> Variant {
		auto &annot = cast<Annotation>(args[0]);
		auto i = cast<intptr_t>(args[1]);

		annot.open();
		annot.remove_events(i);
		return Variant();
	};

#define CLS(T) phonometrica::get_class<T>()
	auto cls = CLS(Annotation);
	cls->add_method(rt.get_field_string, annot_get_field, { CLS(Annotation), CLS(String) });
	rt.add_global("bind_to_sound", bind_to_sound, { CLS(Annotation), CLS(String) });
	rt.add_global("get_event_start", get_event_start, { CLS(Annotation), CLS(intptr_t), CLS(intptr_t) });
	rt.add_global("get_event_end", get_event_end,  { CLS(Annotation), CLS(intptr_t), CLS(intptr_t) });
	rt.add_global("get_event_text", get_event_text,  { CLS(Annotation), CLS(intptr_t), CLS(intptr_t) });
	rt.add_global("set_event_text", set_event_text,  { CLS(Annotation), CLS(intptr_t), CLS(intptr_t), CLS(String) });
	rt.add_global("get_event_count", get_event_count,  { CLS(Annotation), CLS(intptr_t) });
	rt.add_global("get_event_index", get_event_index,  { CLS(Annotation), CLS(intptr_t), CLS(Number) });
	rt.add_global("get_layer_count", get_layer_count,  { CLS(Annotation) });
	rt.add_global("get_layer_label", get_layer_label,  { CLS(Annotation), CLS(intptr_t) });
	rt.add_global("set_layer_label", set_layer_label,  { CLS(Annotation), CLS(intptr_t), CLS(String) });
	rt.add_global("add_interval", add_interval,  { CLS(Annotation), CLS(intptr_t), CLS(Number), CLS(Number), CLS(String) });
	rt.add_global("add_instant", add_instant,  { CLS(Annotation), CLS(intptr_t), CLS(Number), CLS(String) });
	rt.add_global("remove_interval", remove_interval,  { CLS(Annotation), CLS(intptr_t), CLS(Number), CLS(Number) });
	rt.add_global("remove_events", remove_events,  { CLS(Annotation), CLS(intptr_t) });
#undef CLS
}

bool Annotation::modified() const
{
	return Document::modified() || m_modified;
}

bool Annotation::content_modified() const
{
	return m_modified || Document::content_modified();
}

String Annotation::left_context(intptr_t layer, intptr_t event, intptr_t offset, intptr_t length, const String &separator) const
{
	try
	{
		String context(length);
		auto &events = get_layer_events(layer);
		auto it = events[event].text.begin() + offset;
		context.append(events[event].text.rmid(it, length));

		while (context.grapheme_count() != length && --event > 0)
		{
			auto &label = events[event].text;
			auto prefix = label.right(length - context.grapheme_count() - separator.size());
			context.prepend(separator);
			context.prepend(prefix);
		}

		return context;
	}
	catch (std::exception &e)
	{
		throw error("Could not extract left context in annotation % in event % on layer %: %",
					path(), event, layer, e.what());
	}
}

String Annotation::right_context(intptr_t layer, intptr_t event, intptr_t offset, intptr_t length, const String &separator) const
{
	try
	{
		String context(length);
		auto &events = get_layer_events(layer);
		auto it = events[event].text.begin() + offset;
		context.append(events[event].text.mid(it, length));

		while (context.grapheme_count() != length && ++event <= events.size())
		{
			auto &label = events[event].text;
			auto suffix = label.left(length - context.grapheme_count() - separator.size());
			context.append(separator);
			context.append(suffix);
		}

		return context;
	}
	catch (std::exception &e)
	{
		throw error("Could not extract right context in annotation % in event % on layer %: %",
					path(), event, layer, e.what());
	}
}

void Annotation::set_event_text(intptr_t layer, intptr_t event, const String &new_text)
{
	m_layers[layer].set_event_text(event, new_text);
	m_modified = true;
}

void Annotation::metadata_to_xml(xml_node meta_node)
{
	Document::metadata_to_xml(meta_node);
	String snd = has_sound() ? sound()->path() : String();
	auto project = Project::get();
	Project::compress(snd, project->directory());
	add_data_node(meta_node, "Sound", snd);
}

bool Annotation::needs_metadata_node() const
{
	// Annotations always need a metadata node if they have a bound sound,
	// so that the binding is persisted in the project file.
	return Document::needs_metadata_node() || has_sound();
}

void Annotation::write_as_native(const String &path)
{
	open();
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	auto attr = root.append_attribute("class");
	attr.set_value(class_name().data());
	auto meta_node = root.append_child("Metadata");
	metadata_to_xml(meta_node);
	auto graph_node = root.append_child("Graph");
	layers_to_xml(graph_node, m_layers);

	auto &p = path.empty() ? m_path : path;
	if (p.empty()) {
		throw error("Cannot write annotation: no file path specified");
	}
	write_xml(doc, p);
	m_modified = false;
}

void Annotation::write_as_textgrid(const String &path)
{
	open();
	phonometrica::write_textgrid(path, m_layers);
	m_modified = false;
}

void Annotation::read_from_native()
{
	assert(!m_path.empty());
	static std::string_view project_tag("Phonometrica");
	static std::string_view graph_tag = "Graph";

	xml_document doc;
	auto root = read_xml(doc, m_path);

	if (root.name() != project_tag) {
		throw error("Invalid XML project root in %", m_path);
	}

	auto attr = root.attribute("class");

	if (!attr || class_name() != attr.as_string()) {
		throw error("[Input/Output] Expected an annotation file, got a % file instead", attr.as_string());
	}

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == graph_tag)
		{
			layers_from_xml(node, m_layers);
		}
	}
}

void Annotation::metadata_from_xml(xml_node meta_node)
{
	static std::string_view sound_tag = "Sound";
	Document::metadata_from_xml(meta_node);

	for (auto node = meta_node.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == sound_tag)
		{
			// Don't import the sound file here — it's already in the corpus (or will be
			// once loading finishes). Instead, record the binding so that bind_annotations()
			// can resolve it after the entire corpus is loaded.
			auto project = Project::get();
			String sound_path = node.text().get();
			Project::interpolate(sound_path, project->directory());

			// Try to bind immediately if the sound is already registered.
			auto it = project->files().find(sound_path);
			if (it != project->files().end())
			{
				auto snd = recast<Sound>(it->second);
				if (snd)
					set_sound(snd, false);
			}
			else
			{
				// Defer: the sound might not be registered yet (depends on parse order).
				project->defer_annotation_binding(this, sound_path);
			}
			return;
		}
	}
}

const Event &Annotation::get_event(intptr_t layer, intptr_t event) const
{
	return m_layers.at(layer).events.at(event);
}

intptr_t Annotation::layer_count() const
{
	return m_layers.size();
}

void Annotation::create_layer(intptr_t index, const String &name, bool has_instants)
{
	Layer layer(name, has_instants);

	// Create one empty interval that spans the whole file.
	if (!has_instants && has_sound())
	{
		layer.add_interval(0, m_sound->duration(), String());
	}

	if (index > m_layers.size()) {
		m_layers.append(std::move(layer));
	}
	else {
		m_layers.insert(index, std::move(layer));
	}
	m_modified = true;
}

void Annotation::remove_layer(intptr_t index)
{
	m_layers.remove_at(index);
	m_modified = true;
}

void Annotation::clear_layer(intptr_t index)
{
	m_layers[index].clear_texts();
	m_modified = true;
}

void Annotation::discard_changes()
{
	Element::discard_changes();
	m_modified = false;
}

void Annotation::duplicate_layer(intptr_t index, intptr_t new_index)
{
	auto copy = m_layers[index].duplicate(m_layers[index].label);

	if (new_index > m_layers.size()) {
		m_layers.append(std::move(copy));
	}
	else {
		m_layers.insert(new_index, std::move(copy));
	}
	m_modified = true;
}

String Annotation::get_layer_label(intptr_t index) const
{
	return m_layers.at(index).label;
}

void Annotation::set_layer_label(intptr_t index, String value)
{
	m_layers.at(index).label = std::move(value);
	m_modified = true;
}

const Event *Annotation::find_enclosing_event(double start_time, double end_time, intptr_t layer) const
{
	return m_layers.at(layer).find_enclosing_event(start_time, end_time);
}

std::span<const Event> Annotation::get_slice(intptr_t layer_index, double start_time, double end_time) const
{
	return m_layers.at(layer_index).get_slice(start_time, end_time);
}

const Event *Annotation::find_event_starting_at(intptr_t layer_index, double time) const
{
	return m_layers.at(layer_index).find_event_starting_at(time);
}

const Event *Annotation::find_event_ending_at(intptr_t layer_index, double time) const
{
	return m_layers.at(layer_index).find_event_ending_at(time);
}

const Event *Annotation::find_previous_event(intptr_t layer_index, double time) const
{
	return m_layers.at(layer_index).find_previous_event(time);
}

const Event *Annotation::find_next_event(intptr_t layer_index, double time) const
{
	return m_layers.at(layer_index).find_next_event(time);
}

intptr_t Annotation::get_event_index(intptr_t layer_index, double time) const
{
	return m_layers.at(layer_index).get_event_index(time);
}

intptr_t Annotation::get_event_at_time(intptr_t layer_index, double time) const
{
	return m_layers.at(layer_index).find_index(time);
}

bool Annotation::layer_has_instants(intptr_t index) const
{
	return m_layers.at(index).has_instants;
}

void Annotation::add_interval(intptr_t index, double start, double end, const String &text)
{
	m_layers[index].add_interval(start, end, text);
	m_modified = true;
}

void Annotation::add_instant(intptr_t index, double time, const String &text)
{
	m_layers[index].add_instant(time, text);
	m_modified = true;
}

void Annotation::remove_interval(intptr_t index, double start, double end)
{
	double mid = start + (end - start) / 2;
	auto idx = m_layers[index].find_index(mid);
	if (idx > 0) {
		m_layers[index].remove_event(idx);
	}
	m_modified = true;
}

void Annotation::remove_events(intptr_t index)
{
	m_layers[index].clear();
	m_modified = true;
}

void Annotation::add_anchor(intptr_t layer_index, double time)
{
	m_layers[layer_index].add_anchor(time);
	m_modified = true;
}

bool Annotation::remove_anchor(intptr_t layer_index, double time)
{
	bool ok = m_layers[layer_index].remove_anchor(time);
	if (ok) m_modified = true;
	return ok;
}

} // namespace phonometrica
