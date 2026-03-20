/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 05/06/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Set type (ordered set).                                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SET_HPP
#define PHONOMETRICA_SET_HPP

#include <set>
#include <phon/runtime/variant.hpp>

namespace phonometrica {

class Set final
{
public:

	using Storage = std::set<Variant>;
	using iterator = Storage::iterator;

	Set() = default;

	Set(Storage items) : _items(std::move(items)) { }

	Set(const Set &other);

	Set(Set &&other) = default;

	bool operator==(const Set &other) const;

	iterator begin() { return _items.begin(); }

	iterator end() { return _items.end(); }

	Storage &items() { return _items; }

	String to_string() const;

	void traverse(const GCCallback &callback);

	bool contains(const Variant &v) const { return _items.find(v) != _items.end(); }

	intptr_t size() const { return intptr_t(_items.size()); }

private:

	Storage _items;
	mutable bool seen = false;
};



//---------------------------------------------------------------------------------------------------------------------

namespace meta {

static inline void traverse(Set &set, const GCCallback &callback)
{
	set.traverse(callback);
}


static inline String to_string(const Set &set)
{
	return set.to_string();
}
} // namespace phonometrica::meta
} // namespace phonometrica

#endif // PHONOMETRICA_SET_HPP
