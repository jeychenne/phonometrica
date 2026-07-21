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
 * purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/file.hpp>
#include <phon/error.hpp>
#include <phon/table.hpp>
#include <phon/list.hpp>
#include <phon/application/protocol.hpp>

namespace phonometrica {

Protocol::Protocol(Runtime &rt, const String &path) :
	runtime(rt), m_path(path)
{
	parse();
}

Protocol::Protocol(Runtime &rt, const String &json, FromString) :
	runtime(rt)
{
	// Drive parsing through the scripting engine the same way the file path constructor does.
	// do_string returns the parsed JSON as a Variant, which parse_variant then walks. m_path is
	// left empty; the protocol has no on-disk home when built from a string.
	auto result = runtime.do_string(json);
	parse_variant(std::move(result));
}

String Protocol::get_field_name(intptr_t i) const
{
	return m_fields[i].name;
}

void Protocol::parse()
{
	auto result = runtime.do_file(m_path);
	parse_variant(std::move(result));
}

void Protocol::parse_variant(Variant result)
{
	if (!result.is<Table>()) {
		throw error("File % must contain a table", m_path);
	}
	auto json = result.to<Table>();
	auto key = [](const char *k) { return Variant::make(String(k)); };

	auto type_v = json.get(key("type"));
	if (type_v.is_null()) {
		throw error("Protocol has no \"type\" key");
	}

	auto type = type_v.to<String>();
	if (type != "coding_protocol") {
		throw error("Invalid type in protocol: \"%\"", type);
	}

	auto name_v = json.get(key("name"));
	if (name_v.is_null()) {
		throw error("Protocol has no \"name\" key");
	}
	m_name = name_v.to<String>();

	auto version_v = json.get(key("version"));
	if (!version_v.is_null()) {
		m_version = version_v.to<String>();
	}

	auto separator_v = json.get(key("separator"));
	if (!separator_v.is_null()) {
		m_separator = separator_v.to<String>();
	}

	auto layer_index_v = json.get(key("layer_index"));
	if (!layer_index_v.is_null()) {
		m_layer_index = int(layer_index_v.to<int64_t>());
	}

	auto layer_name_v = json.get(key("layer_name"));
	if (!layer_name_v.is_null()) {
		m_layer_pattern = layer_name_v.to<String>();
	}

	auto layer_field_v = json.get(key("layer_field"));
	if (!layer_field_v.is_null()) {
		m_layer_field = int(layer_field_v.to<int64_t>());
	}

	// Don't use layer index if we have a valid name or if we use a layer field.
	if (!m_layer_pattern.empty() || m_layer_field != 0) m_layer_index = -1;
	// Sanity check.
	if (m_layer_pattern.empty() && layer_index() < 0) {
		throw error("Invalid negative layer index");
	}

	auto case_v = json.get(key("case_sensitive"));
	if (!case_v.is_null()) {
		m_case_sensitive = case_v.to<bool>();
	}

	auto fpr_v = json.get(key("fields_per_row"));
	if (!fpr_v.is_null()) {
		m_fields_per_row = int(fpr_v.to<int64_t>());
	}

	auto fields_v = json.get(key("fields"));
	if (fields_v.is_null()) {
		throw error("Protocol has no fields");
	}

	if (!fields_v.is<List>()) {
		throw error("\"fields\" must be a list");
	}
	auto fields = fields_v.to<List>();

	for (intptr_t fi = 1; fi <= fields.size(); fi++)
	{
		int f = int(fi); // for error reporting
		SearchField search_field;

		auto field_var = fields.get(fi);
		if (!field_var.is<Table>()) {
			throw error("Field % is not a table", f);
		}
		auto field = field_var.to<Table>();

		auto fname_v = field.get(key("name")); // can be anonymous
		if (!fname_v.is_null()) {
			search_field.name = fname_v.to<String>();
		}

		auto match_all_v = field.get(key("match_all")); // can be empty
		if (match_all_v.is_null()) {
			throw error("Field % has no \"match_all\" key", f);
		}
		search_field.match_all = match_all_v.to<String>();

		auto layer_pattern_v = field.get(key("layer_pattern"));
		if (!layer_pattern_v.is_null()) {
			if (f != m_layer_field) {
				throw error("Key \"layer_pattern\" can only be found in layer-selecting field");
			}
			search_field.layer_pattern = layer_pattern_v.to<String>();
		}

		auto values_v = field.get(key("values"));
		if (values_v.is_null()) {
			throw error("Field % has no values", f);
		}

		if (!values_v.is<List>()) {
			throw error("\"values\" must be a list in field %", f);
		}
		auto values = values_v.to<List>();

		for (intptr_t gi = 1; gi <= values.size(); gi++)
		{
			int g = int(gi); // for error reporting
			SearchValue search_value;

			auto value_var = values.get(gi);
			if (!value_var.is<Table>()) {
				throw error("Value % must be a ble in field %", g, f);
			}
			auto value = value_var.to<Table>();

			auto match_v = value.get(key("match"));
			if (match_v.is_null()) {
				throw error("Value % has no \"match\" key in field %", g, f);
			}
			search_value.match = match_v.to<String>();

			auto text_v = value.get(key("text"));
			if (text_v.is_null()) {
				throw error("Value % has no \"text\" key in field %", g, f);
			}
			search_value.text = text_v.to<String>();

			if (f == m_layer_field)
			{
				auto layer_name_val = value.get(key("layer_name"));
				if (layer_name_val.is_null()) {
					throw error("Value % has no \"layer_name\" key in field %", g, f);
				}
				search_value.layer_name = layer_name_val.to<String>();
			}

			auto choices_v = value.get(key("choices"));
			if (!choices_v.is_null())
			{
				String choices = choices_v.to<String>();
				auto display_v = value.get(key("display"));
				if (display_v.is_null()) {
					throw error("Value % in field % has choices but no \"display\" key", g, f);
				}
				// Deliberate fix vs. the old engine: the old code re-read the "choices"
				// key here (a stale-iterator bug), so display texts silently mirrored
				// the match patterns.
				String display = display_v.to<String>();

				auto choice_items = choices.split("|");
				auto display_items = display.split("|");

				if (choice_items.size() != display_items.size()) {
					throw error("Inconsistent number of choice and display items in value % in field %", g, f);
				}

				for (intptr_t i = 0; i < choice_items.size(); i++)
				{
					String m = choice_items[i];
					String t = display_items[i];
					search_value.choices.append({m, t});
				}
			}

			search_field.values.append(std::move(search_value));
		}

		m_fields.append(std::move(search_field));
	}
}
} // namespace phonometrica
