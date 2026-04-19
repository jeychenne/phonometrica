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
 * Purpose: time-aligned annotation.                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANNOTATION_HPP
#define PHONOMETRICA_ANNOTATION_HPP

#include <phon/application/sound.hpp>
#include <phon/application/annotation_data.hpp>
#include <phon/error.hpp>

namespace phonometrica {

class Runtime;
class Object;

class Annotation final : public Document
{
public:

	enum Type {
		Undefined,
		Native,
		TextGrid,
		WaveSurfer
	};

	// Constructor used to create a new annotation from a sound file.
	Annotation() :
		Annotation(nullptr, String())
	{ m_type = Native; }

	explicit Annotation(Directory *parent, String path = String());

	void set_path(String path, bool mutate) override;

	bool has_sound() const;

	const Handle<Sound> &sound() const;

	void set_sound(const Handle<Sound> &value, bool mutate = true);

	const Array<Event> &get_layer_events(intptr_t i) const;

	bool is_textgrid() const { return m_type == TextGrid; }

	bool is_native() const { return m_type == Native; }

	static void initialize(Runtime &rt);

	const Array<Layer> &layers() const { return m_layers; }

	intptr_t size() const { return m_layers.size(); }

	bool modified() const override;

	void set_event_text(intptr_t layer, intptr_t event, const String &new_text);

	String left_context(intptr_t layer, intptr_t event, intptr_t offset, intptr_t length, const String &separator = String()) const;

	String right_context(intptr_t layer, intptr_t event, intptr_t offset, intptr_t length, const String &separator = String()) const;

	void write_as_native(const String &path = String());

	void write_as_textgrid(const String &path = String());

	const Event &get_event(intptr_t layer, intptr_t event) const;

	intptr_t layer_count() const;

	// Create a new layer at `index`, pre-populated with a full-duration interval when
	// `has_instants` is false and the annotation has a bound sound. This seeded interval
	// is the historical contract of this method: manual-annotation workflows (the "New
	// annotation" and "Add layer" GUI actions, script-driven stubs for hand annotation)
	// start from that single interval and split it with anchors. Callers that will
	// populate the layer from data — transcription, silence detection, batch imports —
	// should use `create_empty_layer` instead; otherwise the seeded placeholder remains
	// underneath the real events and causes cache-order-based matching (e.g. clicks in
	// the annotation view) to resolve to the placeholder rather than the event on top.
	void create_layer(intptr_t index, const String &name, bool has_instants);

	// Create a new layer at `index` with no events. Use this when the layer will be
	// populated from data. For instant layers this is equivalent to `create_layer`;
	// the distinction matters only for interval layers.
	void create_empty_layer(intptr_t index, const String &name, bool has_instants);

	void remove_layer(intptr_t index);

	void clear_layer(intptr_t index);

	void discard_changes() override;

	std::span<const Event> get_slice(intptr_t layer_index, double start_time, double end_time) const;

	void duplicate_layer(intptr_t index, intptr_t new_index);

	bool layer_has_instants(intptr_t index) const;

	String get_layer_label(intptr_t index) const;

	void set_layer_label(intptr_t index, String value);

	const Event *find_enclosing_event(double start_time, double end_time, intptr_t layer) const;

	bool content_modified() const override;

	const Event *find_event_starting_at(intptr_t layer_index, double time) const;

	const Event *find_event_ending_at(intptr_t layer_index, double time) const;

	const Event *find_previous_event(intptr_t layer_index, double time) const;

	const Event *find_next_event(intptr_t layer_index, double time) const;

	intptr_t get_event_index(intptr_t layer_index, double time) const;

	intptr_t get_event_at_time(intptr_t layer_index, double time) const;

	void add_interval(intptr_t index, double start, double end, const String &text);

	void add_instant(intptr_t index, double time, const String &text);

	void remove_interval(intptr_t index, double start, double end);

	void remove_events(intptr_t index);

	// Anchor operations (delegate to Layer).
	void add_anchor(intptr_t layer_index, double time);
	bool remove_anchor(intptr_t layer_index, double time);

	// Non-const layer access for direct mutation (anchor move).
	Layer &mutable_layer(intptr_t index) { return m_layers[index]; }

	bool graph_modified() const { return m_modified; }

	void set_graph_modified(bool value) { m_modified = value; }

	bool needs_metadata_node() const override;

protected:

	void read_from_native();

	void preload();

	void load() override;

	void write() override;

	void metadata_to_xml(xml_node meta_node) override;

	void metadata_from_xml(xml_node meta_node) override;

private:

	Annotation::Type guess_type();

	Handle<Sound> m_sound;

	Array<Layer> m_layers;

	Type m_type = Undefined;

	bool m_modified = false;
};


//----------------------------------------------------------------------------------------------------------------------

struct AnnotationLessComparator
{
	bool operator()(const Handle<Annotation> &lhs, const Handle<Annotation> &rhs) const
	{
		return lhs->path() < rhs->path();
	}
};


namespace traits {
template<> struct maybe_cyclic<Annotation> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_ANNOTATION_HPP
