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

#include <phon/application/annotation.hpp>
#include <phon/application/annotation_ops.hpp>
#include <phon/application/constants.hpp>
#include <phon/runtime.hpp>

#include <phon/application/bindings.hpp>
#include <phon/index_conversion.hpp>
#include <phon/application/project.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {


Annotation::Annotation(Directory *parent, String path) :
		Document(parent, std::move(path))
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

	// Clear existing data so load() is idempotent on reload.
	m_layers.clear();

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
	using namespace bindings;

	// ── Fields (old annot_get_field dispatcher) ─────────────────

	rt.add_field<Annotation>("path", [](const Annotation &annot) -> String {
		return annot.path();
	});
	rt.add_field<Annotation>("sound", [](Annotation &annot) -> Variant {
		annot.open();
		if (annot.has_sound()) {
			return Variant::make(annot.sound());
		}
		return Variant();
	});
	rt.add_field<Annotation>("nlayer", [](Annotation &annot) -> intptr_t {
		annot.open();
		return annot.layer_count();
	});

	rt.add_function("bind_to_sound", [](Isolate &iso, Annotation &annot, const String &path) {
		guarded(iso, [&] {
			auto project = Project::get();
			project->import_file(path);
			auto snd = handle_cast<Sound>(project->get(path));
			if (snd) annot.set_sound(snd);
			return 0;
		});
	});

	// ── Events ──────────────────────────────────────────────────

	auto layer_index = [](Isolate &iso, Annotation &annot, intptr_t layer, bool allow_end = false) {
		annot.open();
		try
		{
			return index_from_script(layer, annot.layer_count(), allow_end);
		}
		catch (...)
		{
			iso.raise(String::format("[Index error] Couldn't find layer %ld", (long) layer), 0);
		}
	};
	auto event_index = [layer_index](Isolate &iso, Annotation &annot, intptr_t layer, intptr_t event)
	    -> std::pair<intptr_t, intptr_t> {
		auto i = layer_index(iso, annot, layer);
		try
		{
			return { i, index_from_script(event, annot.layers()[i].count()) };
		}
		catch (...)
		{
			iso.raise(String::format("[Index error] Couldn't find event %ld on layer %ld",
			                         (long) event, (long) layer), 0);
		}
	};

	rt.add_function("get_event_count", [layer_index](Isolate &iso, Annotation &annot, intptr_t layer) -> intptr_t {
		return annot.layers()[layer_index(iso, annot, layer)].count();
	});
	rt.add_function("get_layer_count", [](Annotation &annot) -> intptr_t {
		annot.open();
		return annot.layer_count();
	});
	rt.add_function("get_event_start", [event_index](Isolate &iso, Annotation &annot, intptr_t layer, intptr_t event) -> double {
		auto [i, j] = event_index(iso, annot, layer, event);
		return annot.get_event(i, j).start;
	});
	rt.add_function("get_event_end", [event_index](Isolate &iso, Annotation &annot, intptr_t layer, intptr_t event) -> double {
		auto [i, j] = event_index(iso, annot, layer, event);
		return annot.get_event(i, j).end;
	});
	rt.add_function("get_event_text", [event_index](Isolate &iso, Annotation &annot, intptr_t layer, intptr_t event) -> String {
		auto [i, j] = event_index(iso, annot, layer, event);
		return annot.get_event(i, j).text;
	});
	rt.add_function("get_event_index", [layer_index](Isolate &iso, Annotation &annot, intptr_t layer, double time) -> intptr_t {
		auto i = layer_index(iso, annot, layer);
		return guarded(iso, [&] { return index_to_script(annot.get_event_at_time(i, time)); });
	});
	rt.add_function("set_event_text",
	                [event_index](Isolate &iso, Annotation &annot, intptr_t layer, intptr_t event, const String &text) {
		auto [i, j] = event_index(iso, annot, layer, event);
		annot.set_event_text(i, j, text);
	});
	rt.add_function("get_layer_label", [layer_index](Isolate &iso, Annotation &annot, intptr_t layer) -> String {
		return annot.get_layer_label(layer_index(iso, annot, layer));
	});
	rt.add_function("set_layer_label",
	                [layer_index](Isolate &iso, Annotation &annot, intptr_t layer, const String &value) {
		annot.set_layer_label(layer_index(iso, annot, layer), value);
	});
	rt.add_function("add_interval",
	                [layer_index](Isolate &iso, Annotation &annot, intptr_t layer, double start, double end, const String &text) {
		auto i = layer_index(iso, annot, layer);
		guarded(iso, [&] { annot.add_interval(i, start, end, text); return 0; });
	});
	rt.add_function("add_instant",
	                [layer_index](Isolate &iso, Annotation &annot, intptr_t layer, double time, const String &text) {
		auto i = layer_index(iso, annot, layer);
		guarded(iso, [&] { annot.add_instant(i, time, text); return 0; });
	});
	rt.add_function("remove_interval",
	                [layer_index](Isolate &iso, Annotation &annot, intptr_t layer, double start, double end) {
		auto i = layer_index(iso, annot, layer);
		guarded(iso, [&] { annot.remove_interval(i, start, end); return 0; });
	});
	rt.add_function("remove_events", [layer_index](Isolate &iso, Annotation &annot, intptr_t layer) {
		auto i = layer_index(iso, annot, layer);
		guarded(iso, [&] { annot.remove_events(i); return 0; });
	});

	// ── Layer management ────────────────────────────────────────

	rt.add_function("create_layer",
	                [layer_index](Isolate &iso, Annotation &annot, intptr_t index, const String &name, bool has_instants) {
		auto i = layer_index(iso, annot, index, /*allow_end=*/true);
		guarded(iso, [&] { annot.create_layer(i, name, has_instants); return 0; });
	});
	rt.add_function("remove_layer", [layer_index](Isolate &iso, Annotation &annot, intptr_t index) {
		auto i = layer_index(iso, annot, index);
		guarded(iso, [&] { annot.remove_layer(i); return 0; });
	});
	rt.add_function("clear_layer", [layer_index](Isolate &iso, Annotation &annot, intptr_t index) {
		auto i = layer_index(iso, annot, index);
		guarded(iso, [&] { annot.clear_layer(i); return 0; });
	});
	rt.add_function("duplicate_layer",
	                [layer_index](Isolate &iso, Annotation &annot, intptr_t index, intptr_t new_index) {
		auto i = layer_index(iso, annot, index);
		auto k = layer_index(iso, annot, new_index, /*allow_end=*/true);
		guarded(iso, [&] { annot.duplicate_layer(i, k); return 0; });
	});
	rt.add_function("layer_has_instants", [layer_index](Isolate &iso, Annotation &annot, intptr_t index) -> bool {
		return annot.layer_has_instants(layer_index(iso, annot, index));
	});

	// ── Annotation I/O ──────────────────────────────────────────

	rt.add_function("save", [](Isolate &iso, Annotation &annot) {
		guarded(iso, [&] { annot.write(); return 0; });
	});
	rt.add_function("write_as_native", [](Isolate &iso, Annotation &annot) {
		guarded(iso, [&] { annot.open(); annot.write_as_native(); return 0; });
	});
	rt.add_function("write_as_native", [](Isolate &iso, Annotation &annot, const String &path) {
		guarded(iso, [&] { annot.open(); annot.write_as_native(path); return 0; });
	});
	rt.add_function("write_as_textgrid", [](Isolate &iso, Annotation &annot) {
		guarded(iso, [&] { annot.open(); annot.write_as_textgrid(); return 0; });
	});
	rt.add_function("write_as_textgrid", [](Isolate &iso, Annotation &annot, const String &path) {
		guarded(iso, [&] { annot.open(); annot.write_as_textgrid(path); return 0; });
	});

	// Construct a fresh empty annotation (native format, no path, no layers).
	// Layers and events are added via the normal `create_layer`/`add_interval`/
	// `add_instant` API. Useful for synthetic data generation and for tests
	// that need to round-trip through disk without setting up a fixture file
	// up front.
	rt.add_function("new_annotation", []() -> Handle<Annotation> {
		return Handle<Annotation>::make();
	});

	// ── Structural transformations (annotation_ops) ─────────────
	//
	// All of these accept an output path and return a freshly-allocated
	// Annotation/Sound. They do NOT add the result to the project — the
	// caller wanting project integration should do so explicitly (e.g. via
	// Project::import_file or add_file).

	auto to_annotations = [](Isolate &iso, const List &items) {
		std::vector<Handle<Annotation>> out;
		out.reserve(items.size());
		for (intptr_t n = 1; n <= items.size(); n++) {
			out.push_back(guarded(iso, [&] { return items.get(n).to<Handle<Annotation>>(); }));
		}
		return out;
	};

	rt.add_function("duplicate_annotation",
	                [](Isolate &iso, Annotation &annot, const String &path) -> Handle<Annotation> {
		return guarded(iso, [&] { return duplicate_annotation(annot, path); });
	});
	rt.add_function("extract_layers",
	                [](Isolate &iso, Annotation &annot, const List &items, const String &path) -> Handle<Annotation> {
		annot.open();
		std::vector<intptr_t> indices;
		indices.reserve(items.size());
		for (intptr_t n = 1; n <= items.size(); n++) {
			guarded(iso, [&] {
				indices.push_back(index_from_script(items.get(n).to<intptr_t>(), annot.layer_count()));
				return 0;
			});
		}
		return guarded(iso, [&] {
			return extract_layers(annot, std::span<const intptr_t>(indices.data(), indices.size()), path);
		});
	});
	rt.add_function("merge_annotations",
	                [to_annotations](Isolate &iso, Annotation &base, const List &items, const String &path) -> Handle<Annotation> {
		auto others = to_annotations(iso, items);
		return guarded(iso, [&] {
			return merge_annotations(base, std::span<const Handle<Annotation>>(others.data(), others.size()), path);
		});
	});
	// extract_annotation_slice(annot, t1, t2, [clip_partial,] out_path) — clip_partial defaults true
	rt.add_function("extract_annotation_slice",
	                [](Isolate &iso, Annotation &annot, double t1, double t2, const String &path) -> Handle<Annotation> {
		return guarded(iso, [&] { return extract_annotation_slice(annot, t1, t2, /*clip_partial=*/true, path); });
	});
	rt.add_function("extract_annotation_slice",
	                [](Isolate &iso, Annotation &annot, double t1, double t2, bool clip, const String &path) -> Handle<Annotation> {
		return guarded(iso, [&] { return extract_annotation_slice(annot, t1, t2, clip, path); });
	});
	// concatenate_annotations(sources, [durations,] out_path) — durations inferred from bound sounds
	rt.add_function("concatenate_annotations",
	                [to_annotations](Isolate &iso, const List &items, const String &path) -> Handle<Annotation> {
		auto srcs = to_annotations(iso, items);
		return guarded(iso, [&] {
			return concatenate_annotations(std::span<const Handle<Annotation>>(srcs.data(), srcs.size()),
			                               std::span<const double>(), path);
		});
	});
	rt.add_function("concatenate_annotations",
	                [to_annotations](Isolate &iso, const List &items, const List &durations, const String &path) -> Handle<Annotation> {
		auto srcs = to_annotations(iso, items);
		std::vector<double> durs;
		durs.reserve(durations.size());
		for (intptr_t n = 1; n <= durations.size(); n++) {
			durs.push_back(guarded(iso, [&] { return durations.get(n).to<double>(); }));
		}
		return guarded(iso, [&] {
			return concatenate_annotations(std::span<const Handle<Annotation>>(srcs.data(), srcs.size()),
			                               std::span<const double>(durs.data(), durs.size()), path);
		});
	});
	// extract_sound_slice(sound, t1, t2, out_path) — format inferred from extension
	rt.add_function("extract_sound_slice",
	                [](Isolate &iso, Sound &snd, double t1, double t2, const String &path) -> Handle<Sound> {
		return guarded(iso, [&] {
			auto fmt = sound_format_from_path(path);
			return extract_sound_slice(snd, t1, t2, path, fmt);
		});
	});
	rt.add_function("concatenate_sounds",
	                [](Isolate &iso, const List &items, const String &path) -> Handle<Sound> {
		std::vector<Handle<Sound>> srcs;
		srcs.reserve(items.size());
		for (intptr_t n = 1; n <= items.size(); n++) {
			srcs.push_back(guarded(iso, [&] { return items.get(n).to<Handle<Sound>>(); }));
		}
		return guarded(iso, [&] {
			auto fmt = sound_format_from_path(path);
			return concatenate_sounds(std::span<const Handle<Sound>>(srcs.data(), srcs.size()), path, fmt);
		});
	});
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

		while (context.grapheme_count() != length && --event >= 0)
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
					path(), event + 1, layer + 1, e.what());
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

		while (context.grapheme_count() != length && ++event < events.size())
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
					path(), event + 1, layer + 1, e.what());
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

			// An empty <Sound></Sound> means the annotation has no associated sound.
			// This is a legitimate state (e.g. an annotation that carries metadata but
			// no audio), so leave it unbound instead of registering a binding that can
			// never resolve — which previously produced a spurious per-file error.
			if (String(sound_path).trim().empty())
				return;

			Project::interpolate(sound_path, project->directory());

			// Try to bind immediately if the sound is already registered.
			auto it = project->files().find(sound_path);
			if (it != project->files().end())
			{
				auto snd = handle_cast<Sound>(it->second);
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

	// Seed interval layers with a full-duration interval. See the header for the
	// semantic contract: this is for manual-annotation workflows that start from a
	// single interval and split it with anchors. Data-driven callers should use
	// `create_empty_layer` instead — otherwise this placeholder stays underneath the
	// real events and wins cache-order-based matching in the annotation view.
	if (!has_instants && has_sound())
	{
		layer.add_interval(0, m_sound->duration(), String());
	}

	if (index >= m_layers.size()) {
		m_layers.append(std::move(layer));
	}
	else {
		m_layers.insert(index, std::move(layer));
	}
	m_modified = true;
}

void Annotation::create_empty_layer(intptr_t index, const String &name, bool has_instants)
{
	Layer layer(name, has_instants);

	if (index >= m_layers.size()) {
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

	if (new_index >= m_layers.size()) {
		m_layers.append(std::move(copy));
	}
	else {
		m_layers.insert(new_index, std::move(copy));
	}
	m_modified = true;
}

void Annotation::append_layer(Layer layer)
{
	m_layers.append(std::move(layer));
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
	if (idx >= 0) {
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
