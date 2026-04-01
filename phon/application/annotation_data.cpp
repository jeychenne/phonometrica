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
 * Created: 21/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/error.hpp>
#include <phon/application/annotation_data.hpp>
#include <phon/application/praat.hpp>

namespace phonometrica {

// Find the first event whose end > time. For interval layers with no gaps, this is the
// event containing the time point (if start <= time). For instant layers, this finds the
// first instant at or after the time.
Array<Event>::const_iterator Layer::lower(double time) const
{
	return std::lower_bound(events.begin(), events.end(), time,
        [](const Event &e, double t) { return e.end < t; });
}

intptr_t Layer::find_index(double time) const
{
	auto it = lower(time);

	if (it == events.end()) {
		return 0;
	}

	if (has_instants)
	{
		if (it->start == time) {
			return intptr_t(it - events.begin()) + 1;
		}
		return 0;
	}

	// Interval: check that the event actually contains the time.
	if (it->start <= time && time <= it->end) {
		return intptr_t(it - events.begin()) + 1;
	}

	return 0;
}

const Event *Layer::find_event(double time) const
{
	auto index = find_index(time);
	return (index > 0) ? &events[index] : nullptr;
}

const Event *Layer::find_event_starting_at(double time) const
{
	// Find the first event whose start >= time.
	auto it = std::lower_bound(events.begin(), events.end(), time,
		[](const Event &e, double t) { return e.start < t; });

	if (it != events.end() && it->start == time) {
		return &(*it);
	}

	return nullptr;
}

const Event *Layer::find_event_ending_at(double time) const
{
	auto it = lower(time);

	if (it != events.end())
	{
		// For instants, start == end.
		if (it->end == time) {
			return &(*it);
		}
	}

	// For intervals, the event ending at this time is the one before the event
	// starting at this time.
	if (!has_instants && it != events.begin())
	{
		--it;
		if (it->end == time) {
			return &(*it);
		}
	}

	return nullptr;
}

const Event *Layer::find_previous_event(double time) const
{
	auto it = lower(time);

	if (it == events.begin()) {
		return nullptr;
	}

	--it;
	return &(*it);
}

const Event *Layer::find_next_event(double time) const
{
	// Find the first event whose start > time.
	auto it = std::upper_bound(events.begin(), events.end(), time,
		[](double t, const Event &e) { return t < e.start; });

	if (it == events.end()) {
		return nullptr;
	}

	return &(*it);
}

intptr_t Layer::get_event_index(double time) const
{
	auto it = std::lower_bound(events.begin(), events.end(), time,
		[](const Event &e, double t) { return e.start < t; });

	if (it != events.end() && it->start == time) {
		return intptr_t(it - events.begin()) + 1;
	}

	return 0;
}

std::span<const Event> Layer::get_slice(double start_time, double end_time) const
{
	// Find the first event that overlaps with the range.
	auto first = lower(start_time);

	// Find the first event that starts at or after the range end.
	// This ensures partially visible events at the right edge are included:
	// an event whose start is within the window but whose end extends past it.
	auto last = first;
	while (last != events.end() && last->start < end_time) {
		++last;
	}

	return { first, last };
}

const Event *Layer::find_enclosing_event(double start_time, double end_time) const
{
	double mid = start_time + (end_time - start_time) / 2;
	auto it = lower(mid);

	if (it != events.end())
	{
		if (it->start <= start_time && it->end >= end_time) {
			return &(*it);
		}
	}

	return nullptr;
}

void Layer::add_interval(double start, double end, const String &text)
{
	Event e { start, end, text };

	auto it = std::lower_bound(events.begin(), events.end(), start,
		[](const Event &ev, double t) { return ev.start < t; });

	events.insert(it, std::move(e));
}

void Layer::add_instant(double time, const String &text)
{
	Event e { time, time, text };

	auto it = std::lower_bound(events.begin(), events.end(), time,
		[](const Event &ev, double t) { return ev.start < t; });

	events.insert(it, std::move(e));
}

void Layer::add_anchor(double time)
{
	if (has_instants)
	{
		add_instant(time, String());
	}
	else
	{
		// Split the interval containing the time.
		auto index = find_index(time);

		if (index == 0) {
			throw error("No interval to split at time %", time);
		}

		auto &old_event = events[index];

		if (old_event.start == time || old_event.end == time) {
			throw error("Anchor already exists at time %", time);
		}

		double old_end = old_event.end;
		old_event.end = time;

		Event new_event { time, old_end, String() };

		// Insert after the current event (1-based index, so index+1 maps to iterator at index).
		auto it = events.begin() + index; // 0-based position after old_event
		events.insert(it, std::move(new_event));
	}
}

bool Layer::remove_anchor(double time)
{
	if (has_instants)
	{
		auto it = std::lower_bound(events.begin(), events.end(), time,
			[](const Event &e, double t) { return e.start < t; });

		if (it == events.end() || it->start != time) {
			return false;
		}

		events.remove_at(it);
		return true;
	}
	else
	{
		// Merge two adjacent intervals at the boundary.
		// Find the event ending at this time and the event starting at this time.
		auto it = std::lower_bound(events.begin(), events.end(), time,
			[](const Event &e, double t) { return e.start < t; });

		if (it == events.end() || it == events.begin() || it->start != time) {
			return false;
		}

		auto prev = it - 1;

		if (prev->end != time) {
			return false;
		}

		// Merge: extend the previous event to cover both, concatenate texts.
		auto text = prev->text;
		auto text2 = it->text;
		if (!text.empty() && !text2.empty()) text.append(' ');
		text.append(text2);

		prev->end = it->end;
		prev->text = std::move(text);

		events.remove_at(it);
		return true;
	}
}

void Layer::remove_event(intptr_t index)
{
	events.remove_at(index);
}

void Layer::set_event_text(intptr_t index, const String &text)
{
	events[index].text = text;
}

void Layer::clear_texts()
{
	for (auto &e : events) {
		e.text = String();
	}
}

void Layer::clear()
{
	events.clear();
}

Layer Layer::duplicate(const String &new_label) const
{
	Layer copy(new_label, has_instants);
	copy.events.reserve(events.size());

	for (auto &e : events) {
		copy.events.append(Event { e.start, e.end, e.text });
	}

	return copy;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void read_textgrid(const String &path, Array<Layer> &layers)
{
    File infile(path);
    layers.clear();

    do
    {
        auto line = infile.read_line();
        praat::TierHeader header;

        if (praat::parse_tier_header(infile, line, header))
        {
            layers.append(Layer(header.label, header.has_points));
            layers.last().events.reserve(header.size);
        }
        else if (!layers.empty())
        {
            praat::Interval interval;

            if (praat::parse_interval(infile, line, interval))
            {
                layers.last().add_interval(interval.xmin, interval.xmax, interval.text);
            }
            else
            {
                praat::Point point;

                if (praat::parse_point(infile, line, point))
                {
                    layers.last().add_instant(point.time, point.text);
                }
            }
        }
    }
    while (!infile.at_end());
}

void write_textgrid(const String &path, const Array<Layer> &layers)
{
    File outfile(path, File::Write, Encoding::Utf8);

    // Determine global time range from all layers.
    double start_time = 0, end_time = 0;

    for (intptr_t i = 1; i <= layers.size(); ++i)
    {
        auto &layer = layers[i];
        if (!layer.empty())
        {
            if (i == 1 || layer.events[1].start < start_time) {
                start_time = layer.events[1].start;
            }
            if (i == 1 || layer.events[layer.count()].end > end_time) {
                end_time = layer.events[layer.count()].end;
            }
        }
    }

    outfile.write("File type = \"ooTextFile\"\nObject class = \"TextGrid\"\n\n");
    outfile.format("xmin = %.16f\nxmax = %.16f\n", start_time, end_time);
    outfile.format("tiers? <exists>\nsize = %lu\nitem []:\n", layers.size());

    for (intptr_t i = 1; i <= layers.size(); ++i)
    {
        auto &layer = layers[i];
        String kind = layer.has_instants ? "TextTier" : "IntervalTier";

        outfile.format("    item [%lu]:\n", i);
        outfile.format("        class = \"%s\"\n", kind.data());
        outfile.format("        name = \"%s\"\n", layer.label.data());

        if (layer.empty()) {
            outfile.write("        xmin = 0\n        xmax = 0\n");
        }
        else {
            outfile.format("        xmin = %.16f\n        xmax = %.16f\n",
                           layer.events[1].start, layer.events[layer.count()].end);
        }

        if (layer.has_instants)
        {
            outfile.format("        points: size = %lu\n", layer.count());

            for (intptr_t j = 1; j <= layer.count(); j++)
            {
                auto &e = layer.events[j];
                auto text = e.text;
                text.replace("\"", "\"\"");

                outfile.format("        points [%lu]:\n", j);
                outfile.format("            number = %.16f\n", e.start);
                outfile.format("            mark = \"%s\"\n", text.data());
            }
        }
        else
        {
            outfile.format("        intervals: size = %lu\n", layer.count());

            for (intptr_t j = 1; j <= layer.count(); ++j)
            {
                auto &e = layer.events[j];
                auto text = e.text;
                text.replace("\"", "\"\"");

                outfile.format("        intervals [%lu]:\n", j);
                outfile.format("            xmin = %.16f\n", e.start);
                outfile.format("            xmax = %.16f\n", e.end);
                outfile.format("            text = \"%s\"\n", text.data());
            }
        }
    }
}

void layers_to_xml(xml_node graph_node, const Array<Layer> &layers)
{
    auto attr = graph_node.append_attribute("format");
    attr.set_value(ANNOTATION_FORMAT_VERSION);

    auto layers_node = graph_node.append_child("Layers");

    for (intptr_t i = 1; i <= layers.size(); ++i)
    {
        auto &layer = layers[i];
        auto layer_node = layers_node.append_child("Layer");
        auto a = layer_node.append_attribute("index");
        a.set_value(i);
        a = layer_node.append_attribute("label");
        a.set_value(layer.label.data());
        a = layer_node.append_attribute("instants");
        a.set_value(layer.has_instants);

        for (intptr_t j = 1; j <= layer.count(); ++j)
        {
            auto &e = layer.events[j];
            auto event_node = layer_node.append_child("Event");
            auto ea = event_node.append_attribute("start");
            ea.set_value(e.start);
            ea = event_node.append_attribute("end");
            ea.set_value(e.end);
            add_data_node(event_node, "Text", e.text.data());
        }
    }
}

void layers_from_xml(xml_node graph_node, Array<Layer> &layers)
{
    // Check format version.
    auto fmt = graph_node.attribute("format");
    if (!fmt || fmt.as_int() < ANNOTATION_FORMAT_VERSION)
    {
        throw error("This annotation uses an older format (version %s) that is no longer supported. "
                    "Please contact the developer for help converting your files.",
                    fmt ? fmt.as_string() : "1");
    }

    layers.clear();

    for (auto layer_node = graph_node.child("Layers").first_child();
         layer_node; layer_node = layer_node.next_sibling())
    {
        String label = layer_node.attribute("label").as_string();
        bool instants = layer_node.attribute("instants").as_bool();

        layers.append(Layer(std::move(label), instants));
        auto &layer = layers.last();

        for (auto event_node = layer_node.first_child();
             event_node; event_node = event_node.next_sibling())
        {
            double start = event_node.attribute("start").as_double();
            double end = event_node.attribute("end").as_double();
            String text = event_node.child_value("Text");

            layer.events.append(Event { start, end, std::move(text) });
        }
    }
}

} // namespace phonometrica
