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
 * Created: 01/02/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <phon/application/conc/match.hpp>

namespace phonometrica {

Match::Target::Target(double start_time, double end_time, String value, intptr_t layer, intptr_t offset, bool is_ref) :
		start_time(start_time), end_time(end_time), value(std::move(value)),
		layer((int)layer), offset((int)offset), is_reference(is_ref)
{

}

bool Match::Target::operator==(const Match::Target &other) const
{
	return (this->start_time == other.start_time && this->end_time == other.end_time
	        && this->offset == other.offset && this->value == other.value);
}

bool Match::Target::operator!=(const Match::Target &other) const
{
	return !(*this == other);
}

bool Match::Target::operator<(const Match::Target &other) const
{
	if (this->layer < other.layer) {
		return true;
	}
	if (this->layer > other.layer) {
		return false;
	}

	if (this->start_time < other.start_time) {
		return true;
	}
	if (this->start_time > other.start_time) {
		return false;
	}

	return this->offset < other.offset;
}

Match::Match(const Handle<Annotation> &annot, std::unique_ptr<Target> t) :
	m_annot(annot), m_target(std::move(t))
{

}

Match::Match(const Match &other) : m_annot(other.annotation()), measurements(other.measurements)
{
	auto target = other.m_target.get();
	m_target = std::make_unique<Target>(target->start_time, target->end_time, target->value,
	                                    target->layer, target->offset, target->is_reference);
	target = target->next.get();
	auto new_target = m_target.get();

	while (target)
	{
		new_target->next = std::make_unique<Target>(target->start_time, target->end_time, target->value,
		                                            target->layer, target->offset, target->is_reference);
		target = target->next.get();
		new_target = new_target->next.get();
	}
}

Match::Target *Match::get(intptr_t i) const
{
	intptr_t n = 0;
	auto t = m_target.get();

	while (++n < i)
	{
		assert(t->next);
		t = t->next.get();
	}

	return t;
}

intptr_t Match::get_layer(intptr_t i) const
{
	return get(i)->layer;
}

intptr_t Match::get_offset(intptr_t i) const
{
	return get(i)->offset;
}

String Match::get_value(intptr_t i) const
{
	return get(i)->value;
}

const Handle<Annotation> &Match::annotation() const
{
	return m_annot;
}

Match::Target &Match::last_target()
{
	auto t = m_target.get();

	while (t->next)
	{
		t = t->next.get();
	}

	return *t;
}

Match::Target *Match::reference_target() const
{
	auto t = m_target.get();

	do {
		if (t->is_reference) {
			return t;
		}
		t = t->next.get();
	}
	while (t);

	return nullptr;
}

void Match::to_xml(xml_node root) const
{
	auto node = root.append_child("Match");
	add_data_node(node, "Annotation", m_annot->path().data());
	auto targets_node = node.append_child("Targets");
	auto target = m_target.get();

	while (target)
	{
		auto index = m_annot->get_event_index(target->layer, target->start_time);
		assert(index != 0);
		auto subnode = targets_node.append_child("Target");
		subnode.append_attribute("layer").set_value(target->layer);
		subnode.append_attribute("event").set_value(index);
		subnode.append_attribute("offset").set_value(target->offset);
		subnode.append_attribute("ref").set_value(target->is_reference ? "true" : "false");
		subnode.append_child(node_pcdata).set_value(target->value.data());
		target = target->next.get();
	}

	// Serialize acoustic measurements if present
	if (!measurements.empty())
	{
		String meas_str;
		for (size_t k = 0; k < measurements.size(); k++)
		{
			if (k > 0) meas_str.append(' ');
			if (std::isnan(measurements[k])) {
				meas_str.append("NaN");
			}
			else {
				meas_str.append(String::format("%.6f", measurements[k]));
			}
		}
		add_data_node(node, "Measurements", meas_str);
	}
}

bool Match::valid()
{
	return m_annot && m_target != nullptr;
}

void Match::append(std::unique_ptr<Target> next)
{
	auto &t = last_target();
	t.next = std::move(next);
}

double Match::get_start_time(intptr_t i) const
{
	return get(i)->start_time;
}

double Match::get_end_time(intptr_t i) const
{
	return get(i)->end_time;
}

bool Match::operator==(const Match &other) const
{
	if (m_annot->path() != other.annotation()->path()) {
		return false;
	}

	Target *t1 = m_target.get();
	Target *t2 = other.m_target.get();

	while (t1)
	{
		if (!t2) {
			return false;
		}
		if (*t1 != *t2) {
			return false;
		}

		t1 = t1->next.get();
		t2 = t2->next.get();
	}

	return true;
}

bool Match::operator!=(const Match &other) const
{
	return !(*this == other);
}

bool Match::operator<(const Match &other) const
{
	if (this->annotation()->path() < other.annotation()->path()) {
		return true;
	}
	if (this->annotation()->path() > other.annotation()->path()) {
		return false;
	}

	auto t1 = m_target.get();
	auto t2 = other.m_target.get();

	while (t1)
	{
		if (!t2) {
			return true;
		}
		if (*t1 < *t2) {
			return true;
		}
		if (*t1 != *t2) {
			return false;
		}
		t1 = t1->next.get();
		t2 = t2->next.get();
	}

	return false;
}

bool Match::update(intptr_t target_index, bool &modified)
{
	auto target = this->get(target_index);

	// Re-read the event text from the annotation to check if it still matches.
	auto &event = m_annot->get_event(target->layer, 
		m_annot->get_event_at_time(target->layer, target->start_time));

	modified = false;

	if (target->offset + target->value.size() > event.text.size()) {
		return false;
	}

	auto start = event.text.begin() + target->offset;
	String new_value(start, target->value.size());
	modified = (new_value != target->value);

	if (modified) {
		target->value = new_value;
	}

	return true;
}

Handle<Bookmark> Match::to_bookmark(intptr_t target_index, const String &title, const String &notes, std::pair<String, String> context) const
{
	auto target = get(target_index);
	auto b = make_handle<TimeStamp>(nullptr, title, m_annot, target->layer, target->start_time,
			target->end_time, target->value, std::move(context));
	b->set_notes(notes);

	return b;
}

} // namespace phonometrica
