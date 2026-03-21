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
#include <phon/runtime/runtime.hpp>
#include <phon/application/project.hpp>
#include <phon/application/data_table.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {


DataTable::DataTable(Class *klass, Directory *parent, String path) :
		Document(klass, parent, std::move(path))
{

}

void DataTable::from_xml(xml_node root, const String &project_dir)
{
	static const std::string_view path_tag("Path");

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == path_tag)
		{
			String path(node.text().get());
			Project::interpolate(path, project_dir);
			m_path = std::move(path);
		}
	}
}

void DataTable::save_metadata()
{
	// Native files store their metadata directly, other files need to write them to the database.
	if (uses_external_metadata()) {
		Document::save_metadata();
	}
}

bool DataTable::uses_external_metadata() const
{
	// TODO: native datasets won't use external metadata
	return is<Dataset>();
}

void DataTable::to_csv(const String &path, const String &sep)
{
	File file(path, File::Write);
	auto nrow = this->row_count();
	auto ncol = this->column_count();

	for (intptr_t j = 1; j <= ncol; j++)
	{
		file.write(get_header(j));
		if (j == ncol) file.write('\n');
		else file.write(sep);
	}

	for (intptr_t i = 1; i <= nrow; i++)
	{
		for (intptr_t j = 1; j <= ncol; j++)
		{
			file.write(get_cell(i, j));
			if (j == ncol) file.write('\n');
			else file.write(sep);
		}
	}
}

void DataTable::initialize(Runtime &rt)
{

}

} // namespace phonometrica