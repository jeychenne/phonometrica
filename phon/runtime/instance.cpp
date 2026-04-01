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

