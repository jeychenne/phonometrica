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
 * Created: 2026                                                                                                       *
 *                                                                                                                     *
 * Purpose: Document-level natives shared by Project::initialize (roadmap A3): the doc.path/label/length fields and    *
 * the polymorphic load() (import-or-retrieve by path). Header-only; included by project.cpp.                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_DOCUMENT_HPP
#define PHONOMETRICA_FUNC_DOCUMENT_HPP

#include <phon/runtime.hpp>
#include <phon/application/bindings.hpp>
#include <phon/application/project.hpp>
#include <phon/application/vfs.hpp>
#include <phon/application/data_table.hpp>

namespace phonometrica {

// ── Field access (doc.path, doc.label, doc.length) + load(path) ───────────────
//
// Fields registered on Document resolve for every subclass via the engine's
// base-chain lookup; a subclass's own field (e.g. Annotation's "path") wins.

inline void initialize_document_natives(Runtime &rt)
{
	using namespace bindings;

	rt.add_field<Document>("path", [](const Document &doc) -> String {
		return doc.path();
	});
	rt.add_field<Document>("label", [](const Document &doc) -> String {
		return doc.label();
	});
	rt.add_field<Document>("length", [](Isolate &iso, const Document &doc) -> intptr_t {
		// For DataTable subclasses (Dataset, Concordance), return row count.
		auto *dt = dynamic_cast<const DataTable*>(&doc);
		if (dt) return dt->row_count();
		iso.raise(String::format("[Index error] Document type \"%s\" has no member \"length\"",
		                         doc.class_name().data()), 0);
	});

	// load(path) -> Document: if the file is already in the project, return it;
	// otherwise import it into the project first, then return it. The return is
	// polymorphic (the dynamic class of the stored document).
	rt.add_function("load", [](Isolate &iso, const String &path_arg) -> Handle<Document> {
		return guarded(iso, [&] {
			String path = path_arg; // copy — interpolate modifies in place
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

			return it->second;
		});
	});
}

} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_DOCUMENT_HPP
