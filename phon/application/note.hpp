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
 * Created: 06/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Research note stored as an HTML fragment (.phon-note).                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_NOTE_HPP
#define PHONOMETRICA_NOTE_HPP

#include <phon/application/vfs.hpp>

namespace phonometrica {

class Note final : public Document
{
public:

	String class_name() const override { return "Note"; }

	explicit Note(Directory *parent, String path = String());

	const String &content() const;

	void set_content(String value, bool mutate = true);

	// Like Script: modifications are deferred until the view is closed.
	void set_pending_modifications() { m_content_modified = true; }

	String label() const override;

	String browser_label() const override;

	static void initialize(Runtime &rt);

private:

	void load() override;

	void write() override;

	String m_content;

};


namespace traits {
template<> struct maybe_cyclic<Note> : std::false_type { };
template<> struct is_clonable<Note> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_NOTE_HPP
