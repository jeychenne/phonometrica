/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 04/06/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime/class.hpp>
#include <phon/runtime/module.hpp>
#include <phon/runtime/function.hpp>

namespace phonometrica {

Variant &Module::get(const String &key)
{
	auto it = members.find(key);

	if (it == members.end()) {
		throw error("[Index error] Missing key in module \"%\": \"%\"", _name, key);
	}

	return it->second;
}

bool Module::contains(const String &key) const
{
	return members.contains(key);
}

void Module::define(const String &name, Variant value)
{
	members[name] = std::move(value);
}

void Module::traverse(const GCCallback &callback)
{
	for (auto &pair : members) {
		pair.second.traverse(callback);
	}
}

void Module::define(Runtime *rt, const String &name, NativeCallback cb, std::initializer_list<Handle<Class>> sig, ParamBitset ref)
{
	members[name] = make_handle<Function>(rt, rt, name, std::move(cb), sig, ref);
}

} // namespace phonometrica