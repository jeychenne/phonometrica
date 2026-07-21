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
 * Created: 17/09/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: a query protocol defines the semantics of a coding scheme. It is translated as a widget with clickable     *
 * buttons, which is presented to the user in a query editor. The user's input is translated back into a regular       *
 * expression which is passed as a search pattern to a query.                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PROTOCOL_HPP
#define PHONOMETRICA_PROTOCOL_HPP

#include <memory>
#include <phon/string.hpp>
#include <phon/runtime.hpp>

namespace phonometrica {

// Helper structures.

struct SearchChoice final
{
	String match, text;
};

struct SearchValue final
{
	String match, text, layer_name;
	Array<SearchChoice> choices;
};

struct SearchField final
{
	String name, match_all, layer_pattern;
	Array<SearchValue> values;
};

//----------------------------------------------------------------------------------------------------------------------

class Protocol final
{
public:

	// Tag type used to disambiguate the JSON-string constructor from the path constructor.
	struct FromString {};

	Protocol(Runtime &rt, const String &path);

	// Parse a coding protocol from an in-memory JSON string. Used by the protocol-builder dialog
	// to drive the live preview without touching the filesystem.
	Protocol(Runtime &rt, const String &json, FromString);

	String version() const { return m_version; }

	String layer_pattern() const { return m_layer_pattern; }

	int layer_index() const { return m_layer_index; }

	bool case_sensitive() const { return m_case_sensitive; }

	String name() const { return m_name; }

	String separator() const { return m_separator; }

	intptr_t field_count() const { return m_fields.size(); }

	int fields_per_row() const { return m_fields_per_row; }

	const Array<SearchField> &fields() const { return m_fields; }

	String get_field_name(intptr_t i) const;

private:

	void parse();

	// Parse protocol fields from an already-evaluated JSON Variant (Table at the top level).
	// Shared by both the path-based and string-based constructors.
	void parse_variant(Variant result);

	Runtime &runtime;

	Array<SearchField> m_fields;

	String m_path;

	String m_name;

	String m_version;

	String m_layer_pattern;

	String m_separator;

	int m_layer_index = 0; // search everywhere by default

	int m_fields_per_row = 3;

	int m_layer_field = 0; // if this is positive, indicates which field is used to select the tier.

	bool m_case_sensitive = false;
};

using AutoProtocol = std::shared_ptr<Protocol>;

} // namespace phonometrica

#endif //PHONOMETRICA_PROTOCOL_HPP
