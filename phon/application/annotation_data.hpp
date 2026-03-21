/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Core data structures for time-aligned annotations. Layers contain sorted arrays of events. Cross-layer     *
 * relations (dominance, alignment, precedence) are computed from time coordinates at query time.                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANNOTATION_DATA_HPP
#define PHONOMETRICA_ANNOTATION_DATA_HPP

#include <algorithm>
#include <cmath>
#include <span>
#include <phon/string.hpp>
#include <phon/array.hpp>
#include <phon/utils/xml.hpp>

namespace phonometrica {

// An event is a labeled time span in an annotation layer. For interval layers, start < end.
// For instant (point) layers, start == end.
struct Event
{
	double start;
	double end;
	String text;

	bool is_instant() const { return start == end; }
	bool is_interval() const { return start != end; }

	double duration() const { return end - start; }

	double center() const { return start + (end - start) / 2; }

	double center(double window_start, double window_end) const
	{
		double s = (std::max)(window_start, start);
		double e = (std::min)(window_end, end);
		return s + (e - s) / 2;
	}

	// Does this event contain the given time?
	bool contains_time(double time) const
	{
		return start <= time && time <= end;
	}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Layer
{
	String label;
	bool has_instants = false;

	// Sorted by start time.
	Array<Event> events;

	Layer() = default;

	Layer(String label, bool has_instants) :
		label(std::move(label)), has_instants(has_instants)
	{}

	intptr_t count() const { return events.size(); }

	bool empty() const { return events.empty(); }

	// Find the index (1-based) of the event containing the given time.
	// Returns 0 if not found.
	intptr_t find_index(double time) const;

	// Find the event containing the given time, or null span if not found.
	const Event *find_event(double time) const;

	// Find the event whose left boundary is exactly at the given time.
	// Returns nullptr if not found.
	const Event *find_event_starting_at(double time) const;

	// Find the event whose right boundary is exactly at the given time.
	// Returns nullptr if not found.
	const Event *find_event_ending_at(double time) const;

	// Find the event immediately before the given time.
	// Returns nullptr if no such event exists.
	const Event *find_previous_event(double time) const;

	// Find the event immediately after the given time.
	// Returns nullptr if no such event exists.
	const Event *find_next_event(double time) const;

	// Get the 1-based index of the event whose left boundary is exactly at the given time.
	// Returns 0 if not found.
	intptr_t get_event_index(double time) const;

	// Get a span of events within the given time range [start_time, end_time].
	std::span<const Event> get_slice(double start_time, double end_time) const;

	// Find the event on this layer that encloses the given time range.
	// Returns nullptr if no such event exists.
	const Event *find_enclosing_event(double start_time, double end_time) const;

	// Add an interval [start, end] with the given text. Maintains sort order.
	void add_interval(double start, double end, const String &text);

	// Add an instant at the given time with the given text. Maintains sort order.
	void add_instant(double time, const String &text);

	// Add an anchor (boundary) at the given time. For interval layers, this splits the
	// enclosing interval. For instant layers, this inserts an empty instant.
	void add_anchor(double time);

	// Remove the anchor (boundary) at the given time. For interval layers, this merges
	// the two adjacent intervals. For instant layers, this removes the instant.
	// Returns false if no anchor exists at the given time.
	bool remove_anchor(double time);

	// Remove the event at the given 1-based index.
	void remove_event(intptr_t index);

	// Set the text of the event at the given 1-based index.
	void set_event_text(intptr_t index, const String &text);

	// Clear all event texts without removing the events themselves.
	void clear_texts();

	// Remove all events.
	void clear();

	// Create a copy of this layer with a new label.
	Layer duplicate(const String &new_label) const;

private:

	// Binary search: find iterator to the first event whose end > time.
	Array<Event>::const_iterator lower(double time) const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Temporal relations between events, computed on the fly from time coordinates.
// These are used by the query engine to evaluate cross-layer constraints.

// Does the parent event temporally contain the child event?
inline bool dominates(const Event &parent, const Event &child)
{
	return parent.start <= child.start && child.end <= parent.end;
}

// Does the parent strictly contain the child (not sharing boundaries)?
inline bool strictly_dominates(const Event &parent, const Event &child)
{
	return parent.start < child.start && child.end < parent.end;
}

// Do two events share both boundaries?
inline bool aligns(const Event &a, const Event &b)
{
	return a.start == b.start && a.end == b.end;
}

// Do two events share their left boundary?
inline bool aligns_left(const Event &a, const Event &b)
{
	return a.start == b.start;
}

// Do two events share their right boundary?
inline bool aligns_right(const Event &a, const Event &b)
{
	return a.end == b.end;
}

// Does event a end before event b starts (strict precedence)?
inline bool precedes(const Event &a, const Event &b)
{
	return a.end <= b.start;
}

// Does event b end before event a starts (strict subsequence)?
inline bool follows(const Event &a, const Event &b)
{
	return b.end <= a.start;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// I/O functions for annotation layers.

// Current native annotation format version.
constexpr int ANNOTATION_FORMAT_VERSION = 2;

// Read a Praat TextGrid file into layers.
void read_textgrid(const String &path, Array<Layer> &layers);

// Write layers to a Praat TextGrid file.
void write_textgrid(const String &path, const Array<Layer> &layers);

// Serialize layers to an XML node (native format).
void layers_to_xml(xml_node graph_node, const Array<Layer> &layers);

// Deserialize layers from an XML node (native format).
void layers_from_xml(xml_node graph_node, Array<Layer> &layers);

} // namespace phonometrica

#endif // PHONOMETRICA_ANNOTATION_DATA_HPP
