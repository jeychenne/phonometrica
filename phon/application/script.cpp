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

#include <phon/file.hpp>
#include <phon/runtime.hpp>
#include <phon/application/script.hpp>
#include <phon/application/project.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

Script::Script(Directory *parent, String path) :
		Document(meta::get_class<Script>(), parent, std::move(path))
{

}

void Script::load()
{
	m_content = File::read_all(m_path);
}

void Script::write()
{
	File file(m_path, File::Write, Encoding::Utf8);
	file.write(m_content);
	file.close();
}

const String &Script::content() const
{
	return m_content;
}

void Script::set_content(String value, bool mutate)
{
	m_content = std::move(value);
	m_content_modified |= mutate;
}

String Script::label() const
{
	using namespace filesystem;
	return m_path.empty() ? "Untitled script" :  split_ext(base_name(m_path)).first;
}

void Script::initialize(Runtime &rt)
{

}

} // namespace phonometrica
