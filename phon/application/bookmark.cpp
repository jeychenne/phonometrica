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

#include <phon/runtime.hpp>
#include <phon/application/bookmark.hpp>
#include <phon/application/project.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

Bookmark::Bookmark(Class *klass, Directory *parent) :
		Element(klass, parent)
{

}


Bookmark::Bookmark(Class *klass, Directory *parent, String title) :
		Element(klass, parent), m_title(std::move(title))
{

}

String Bookmark::label() const
{
	return m_title;
}

void Bookmark::set_notes(const String &value, bool mutate)
{
	m_notes = value;
	m_content_modified |= mutate;
}

bool Bookmark::quick_search(const String &text) const
{
	return m_title.to_lower().contains(text) || m_notes.to_lower().contains(text);
}

void Bookmark::set_title(const String &value)
{
	m_title = value;
	m_content_modified = true;
}

TimeStamp::TimeStamp(Directory *parent, String title, Handle<Annotation> annot, size_t layer,
					 double start, double end, String match, std::pair<String, String> context) :
		Bookmark(meta::get_class<TimeStamp>(), parent, std::move(title)), m_annot(std::move(annot)), m_target(std::move(match)),
		m_context(std::move(context))
{
	m_layer = layer;
	m_start = start;
	m_end = end;
}

void TimeStamp::to_xml(xml_node root)
{
	auto node = root.append_child("Bookmark");
	auto attr = node.append_attribute("type");
	attr.set_value(class_name().data());

	String path(m_annot->path());
	Project::compress(path, Project::get()->directory());

	add_data_node(node, "Title", m_title);
	add_data_node(node, "Notes", m_notes);
	add_data_node(node, "LeftContext", m_context.first);
	add_data_node(node, "Match", m_target);
	add_data_node(node, "RightContext", m_context.second);
	add_data_node(node, "Annotation", path);
	add_data_node(node, "Layer", String::convert(intptr_t(m_layer)));
	add_data_node(node, "Start", String::convert(m_start));
	add_data_node(node, "End", String::convert(m_end));
}

String TimeStamp::tooltip() const
{
	String s("File:\n");
	s.append(filesystem::base_name(m_annot->path()));
	s.append("\nMatch:\n");
	s.append(m_context.first);
	s.append(' ');
	s.append(m_target);
	s.append(' ');
	s.append(m_context.second);

	if (!m_notes.empty())
	{
		s.append("\nNotes:\n");
		s.append(m_notes);
	}

	return s;
}

void TimeStamp::initialize(Runtime &rt)
{

}

} // namespace phonometrica
