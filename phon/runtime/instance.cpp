/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 10/07/2025                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime/instance.hpp>
#include <phon/runtime/function.hpp>

namespace phonometrica {

void Instance::traverse_members(const GCCallback &callback)
{
	klass->traverse_members(callback);

	for (auto &v : locals) {
		v.traverse(callback);
	}

	for (auto &it : fields) {
		it.second.traverse(callback);
	}
}

String Instance::to_string() const
{
	//auto f = klass->get_to_string_method();
	return String::format("<instance of %s at %p>", klass->name().data(), klass.get());
}

Variant Instance::get_field(const String &name, bool by_ref) const
{
	auto it = fields.find(name);

	if (it == fields.end()) {
		field_error(name);
	}
	auto &result = it->second;

	if (by_ref)
	{
		result.unshare();
		result.make_alias();
	}

	return result;
}

void Instance::set_field(const String &name, Variant value, bool must_exist)
{
	if (must_exist)
	{
		auto it = fields.find(name);

		if (it == fields.end()) {
			field_error(name);
		}

		it->second = std::move(value);
	}
	else
	{
		fields[name] = std::move(value);
	}
}

void Instance::field_error(const String &name) const
{
	throw error("[Index error] \"%\" has no member named \"%\"", klass->name(), name);
}

bool Instance::clonable() const
{
	return klass->clonable();
}

bool Instance::equals(const Instance &other) const
{
	if (this == &other) {
		return true;
	}
	if (this->klass != other.klass) {
		return false;
	}
	if (this->klass->clonable()) {
		return *this == other;
	}

	return false;
}

} // namespace phonometrica

