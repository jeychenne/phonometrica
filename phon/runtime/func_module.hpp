/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 31/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: Module builtin functions.                                                                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONSCRIPT_FUNC_MODULE_HPP
#define PHONSCRIPT_FUNC_MODULE_HPP

#include <phon/runtime/module.hpp>

namespace phonometrica {

static Variant module_init(Runtime &rt, std::span<Variant>args)
{
	auto &name = cast<String>(args[0]);
	return make_handle<Module>(&rt, name);
}

static Variant module_get_attr(Runtime &, std::span<Variant> args)
{
	auto &m = cast<Module>(args[0]);
	auto key = cast<String>(args[1]);

	return m.get(key);
}

static Variant module_set_attr(Runtime &, std::span<Variant> args)
{
	auto &m = cast<Module>(args[0]);
	auto key = cast<String>(args[1]);
	m[key] = args[2].resolve();

	return Variant();
}

static Variant module_contains(Runtime &, std::span<Variant>args)
{
	auto &mod = cast<Module>(args[0]);
	auto &key = cast<String>(args[1]);

	return mod.contains(key);
}

} // namespace phonometrica

#endif // PHONSCRIPT_FUNC_MODULE_HPP
