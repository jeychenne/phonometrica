/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 22/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime/class.hpp>
#include <phon/runtime/function.hpp>
#include <phon/runtime/instance.hpp>

namespace phonometrica {

String Class::ctor_string(PHON_CTOR_STRING);
String Class::init_string(PHON_INIT_STRING);
String Class::tostr_string(PHON_TOSTR_STRING);

Class::Class(String name, Class *parent, const std::type_info *info, Index index) :
	_name(std::move(name)), _info(info), _bases(parent ? parent->_bases : std::vector<Class*>()), index(index)
{
	_depth = _bases.size();
	_bases.push_back(this);

	if (parent)
	{
		for (auto &it : parent->methods)
		{
			// We don't have an object yet, so we use the parent's to get a reference to the runtime.
			assert(parent->object());
			assert(check_type<Function>(it.second));
			auto rt = static_cast<Collectable*>(parent->object())->runtime;
			auto f = raw_cast<Function>(it.second).copy(rt);
			methods.insert({it.first, std::move(f)});
		}

		for (auto &it : parent->instance_fields) {
			this->instance_fields.insert(it);
		}
	}
}

bool Class::inherits(const Class *base) const
{
	return _bases[base->depth()] == base && base->depth() <= this->depth();
}

int Class::get_distance(const Class *base) const
{
	return _bases[base->depth()] == base ? int(this->depth() - base->depth()) : -1;
}

Handle<Function> Class::get_method(const String &name, bool must_exist)
{
	auto it = methods.find(name);
	if (it == methods.end())
	{
		if (must_exist) {
			throw error("[Index error] Class % does not have a method called \"%\"", this->name(), name);
		}
		else {
			return nullptr;
		}
	}

	return it->second.handle<Function>();
}

Handle<Function> Class::get_to_string_method()
{
	return get_method(tostr_string, false);
}

Handle<Function> Class::get_constructor()
{
	auto it = methods.find(ctor_string);
	if (it == methods.end()) {
		return nullptr;
	}

	return it->second.handle<Function>();
}

Handle<Function> Class::get_initializer()
{
	return get_method(init_string);
}

void Class::add_constructor(NativeCallback cb, std::initializer_list<Handle<Class>> sig, ParamBitset ref)
{
	add_method(ctor_string, std::move(cb), sig, ref);
}

void Class::add_constructor(Handle<Function> f)
{
	add_method(ctor_string, std::move(f));
}

void Class::add_method(const String &name, NativeCallback cb, std::initializer_list<Handle<Class>> sig, ParamBitset ref)
{
	auto it = methods.find(name);
	// This is fine as long as we have a single runtime.
	auto rt = static_cast<Collectable*>(object())->runtime;
	if (it == methods.end())
	{
		methods.insert({ name, make_handle<Function>(rt, rt, name, std::move(cb), sig, ref) });
	}
	else
	{
		auto ctor = it->second.handle<Function>();
		ctor->add_closure(make_handle<Closure>(rt, std::make_shared<NativeRoutine>(name, std::move(cb), sig, ref)));
	}

}

void Class::add_method(const String &name, Handle<Function> f)
{
	methods[name] = std::move(f);
}

void Class::finalize()
{
	for (auto &pair : methods) {
		pair.second.clear();
	}
}

void Class::traverse_members(const GCCallback &callback)
{
	for (auto &pair : methods) {
		pair.second.traverse(callback);
	}
	for (auto &pair : instance_fields) {
		pair.second.traverse(callback);
	}
}

bool Class::is_user_defined() const
{
	return index == Index::Instance;
}

void Class::set_instance_field(const String &key, Variant value)
{
	instance_fields[key] = std::move(value);
}

void Class::initialize_instance(Instance &instance)
{
	// Skip Object: it doesn't have any field.
	for (size_t i = 1; i < _bases.size() - 1; ++i)
	{
		_bases[i]->initialize_instance(instance);
	}
	for (auto &it : instance_fields) {
		instance.set_field(it.first, it.second, false);
	}
}
void Class::make_unclonable()
{
	is_clonable = false;
}

bool Class::clonable() const
{
	return is_clonable;
}
} // namespace phonometrica
