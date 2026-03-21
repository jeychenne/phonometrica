/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
#include <phon/runtime/runtime.hpp>

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

	Protocol(Runtime &rt, const String &path);

	String version() const { return m_version; }

	String layer_pattern() const { return m_layer_pattern; }

	int layer_index() const { return m_layer_index; }

	bool case_sensitive() const { return m_case_sensitive; }

	String name() const { return m_name; }

	String field_separator() const { return m_separator; }

	intptr_t field_count() const { return m_fields.size(); }

	int fields_per_row() const { return m_fields_per_row; }

	const Array<SearchField> &fields() const { return m_fields; }

	String get_field_name(intptr_t i) const;

private:

	void parse();

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
