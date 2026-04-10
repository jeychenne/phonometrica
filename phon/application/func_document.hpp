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
 * Created: 09/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Document builtin functions for the scripting engine. Provides load() to import/retrieve files from the     *
 *          project, and field accessors (.path, .label) on Document handles.                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_DOCUMENT_HPP
#define PHONOMETRICA_FUNC_DOCUMENT_HPP

#include <phon/runtime.hpp>
#include <phon/application/project.hpp>
#include <phon/application/vfs.hpp>
#include <phon/application/data_table.hpp>

namespace phonometrica {

// ── to_string specialization for Document (must live in meta namespace) ────────

namespace meta {

template<>
inline String to_string(const Document &doc)
{
	return doc.path();
}

} // namespace meta


// ── Field access: doc.path, doc.label, doc.length ──────────────────────────────

static Variant document_get_field(Runtime &rt, std::span<Variant> args)
{
	auto &doc = cast<Document>(args[0]);
	auto &key = cast<String>(args[1]);

	if (key == "path")
		return doc.path();
	if (key == "label")
		return doc.label();
	if (key == rt.length_string)
	{
		// For DataTable subclasses (Dataset, Concordance), return row count.
		auto *dt = dynamic_cast<const DataTable*>(&doc);
		if (dt) return dt->row_count();
		throw error("[Index error] Document type \"%\" has no member \"length\"", doc.class_name());
	}

	throw error("[Index error] Document type \"%\" has no member named \"%\"", doc.class_name(), key);
}


// ── load(path) → Document ──────────────────────────────────────────────────────
//
// If the file is already in the project, return it.
// Otherwise, import it into the project first, then return it.

static Variant load_file(Runtime &, std::span<Variant> args)
{
	auto path = cast<String>(args[0]);  // copy — interpolate modifies in place
	auto *project = Project::get();
	if (!project) {
		throw error("No project is currently open");
	}

	Project::interpolate(path, project->directory());

	auto &files = project->files();
	if (!files.contains(path))
	{
		project->import_file(path);
	}

	auto it = files.find(path);
	if (it == files.end()) {
		throw error("Could not load file \"%\"", path);
	}

	return it->second;  // Handle<Document> → Variant
}


} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_DOCUMENT_HPP
