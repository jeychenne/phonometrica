/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 31/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: Table builtin functions.                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_TABLE_HPP
#define PHONOMETRICA_FUNC_TABLE_HPP

#include <phon/runtime/table.hpp>
#include <phon/runtime.hpp>

namespace phonometrica {

static Variant table_init(Runtime &rt, std::span<Variant>)
{
	return make_handle<Table>(&rt);
}

static Variant table_get_item(Runtime &rt, std::span<Variant> args)
{
	auto &tab = cast<Table>(args[0]);
	auto &value = tab.get(args[1]);

	return rt.needs_reference() ?  value.make_alias() : value.resolve();
}

static Variant table_get_field(Runtime &rt, std::span<Variant> args)
{
	auto &tab = cast<Table>(args[0]);
	auto &key = cast<String>(args[1]);

	if (key == rt.length_string) {
		return tab.size();
	}
	else if (key == "keys") {
		return make_handle<List>(&rt, tab.keys());
	}
	else if (key == "values") {
		return make_handle<List>(&rt, tab.values());
	}

	throw error("[Index error] Table type has no member named \"%\"", key);
}

static Variant table_set_item(Runtime &, std::span<Variant> args)
{
	auto &map = cast<Table>(args[0]).map();
	map[args[1].resolve()] = std::move(args[2].resolve());

	return Variant();
}

static Variant table_contains(Runtime &, std::span<Variant> args)
{
	auto &map = cast<Table>(args[0]).map();
	return map.contains(args[1]);
}

static Variant table_is_empty(Runtime &, std::span<Variant> args)
{
	auto &map = cast<Table>(args[0]).map();
	return map.empty();
}

static Variant table_clear(Runtime &, std::span<Variant> args)
{
	auto &map = cast<Table>(args[0]).map();
	map.clear();

	return Variant();
}

static Variant table_remove(Runtime &, std::span<Variant> args)
{
	auto &map = cast<Table>(args[0]).map();
	map.erase(args[1].resolve());

	return Variant();
}

static Variant table_get1(Runtime &, std::span<Variant> args)
{
	auto &map = cast<Table>(args[0]).map();
	auto it = map.find(args[1].resolve());

	return (it != map.end()) ? it->second : Variant();
}

static Variant table_get2(Runtime &, std::span<Variant> args)
{
	auto &map = cast<Table>(args[0]).map();
	auto it = map.find(args[1].resolve());

	return (it != map.end()) ? it->second : args[2];
}

} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_TABLE_HPP
