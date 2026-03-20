/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 10/07/2025                                                                                                 *
 *                                                                                                                     *
 * Purpose: Instance of a user-defined type.                                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INSTANCE_HPP
#define PHONOMETRICA_INSTANCE_HPP

#include <vector>
#include <phon/hashmap.hpp>
#include <phon/runtime/class.hpp>
#include <phon/runtime/variant.hpp>

namespace phonometrica {

class Instance final
{
public:

	explicit Instance(Handle<Class> klass) : klass(std::move(klass)) { }

	void traverse_members(const GCCallback &callback);

	[[nodiscard]] Handle<Class> get_class() const { return klass; }

	String to_string() const;

	Variant get_field(const String &name, bool by_ref) const;

	void set_field(const String &name, Variant value, bool must_exist = true);

	bool clonable() const;

	bool equals(const Instance &other) const;

private:

	bool operator==(const Instance &other) const = default;

	void field_error(const String &name) const;

	Handle<Class> klass;

	// Public members
	Hashmap<String,Variant> fields;

	// Private members (only visible within an instance of a class)
	std::vector<Variant> locals;
};

//---------------------------------------------------------------------------------------------------------------------

namespace meta {

static inline
String to_string(const Instance &i)
{
	return i.to_string();
}

static inline
void traverse(Instance &i, const GCCallback &callback)
{
	i.traverse_members(callback);
}

template<>
inline bool equal<Instance>(const Instance &i1, const Instance &i2)
{
	return i1.equals(i2);
}

} // namespace phonometrica::meta

} // namespace phonometrica

#endif //PHONOMETRICA_INSTANCE_HPP
