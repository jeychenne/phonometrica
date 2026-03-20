/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 04/06/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: a module provides a namespace for Phonometrica code.                                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_MODULE_HPP
#define PHONOMETRICA_MODULE_HPP

#include <phon/runtime/variant.hpp>
#include <phon/dictionary.hpp>

namespace phonometrica {

class Module final
{
public:

	using Storage = Dictionary<Variant>;
	using iterator = Storage::iterator;
	using value_type = Storage::value_type;

	explicit Module(const String &name) : _name(name) { }

	Module(const Module &) = delete;


	String name() const { return _name; }

	Variant &operator[](const String &key) { return members[key]; }

	iterator find(const String &key) { return members.find(key); }

	iterator begin() { return members.begin(); }

	iterator end() { return members.end(); }

	void insert(value_type v) { members.insert(std::move(v)); }

	Variant &get(const String &key);

	bool contains(const String &key) const;

	void define(const String &name, Variant value);

	void define(Runtime *rt, const String &name, NativeCallback cb, std::initializer_list<Handle<Class>> sig, ParamBitset ref = ParamBitset());

	void traverse(const GCCallback &callback);

private:

	friend class Runtime;

	String _name;

	Storage members;
};

namespace meta {

static inline
void traverse(Module &value, const GCCallback &callback)
{
	value.traverse(callback);
}
} // meta

} // namespace phonometrica

#endif // PHONOMETRICA_MODULE_HPP
