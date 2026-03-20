/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 23/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: List type (dynamic array of variants).                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_LIST_HPP
#define PHONOMETRICA_LIST_HPP

#include <phon/array.hpp>
#include <phon/runtime/variant.hpp>

namespace phonometrica {

class List final
{
public:

	using Storage = Array<Variant>;
	using iterator = Storage::iterator;
	using const_iterator = Storage::const_iterator;

	List() = default;

	List(std::initializer_list<Variant> lst) : _items(lst) { }

	explicit List(intptr_t size) : _items(size, Variant()) { }

	List(const List &other);

	List(List &&other) noexcept = default;

	List(Storage items) : _items(std::move(items)) { }

	List &operator=(List other);

	bool operator==(const List &other) const;

	intptr_t size() const { return _items.size(); }

	Variant *data() { return _items.data(); }

	const Variant *data() const { return _items.data(); }

	Variant &operator[](intptr_t i) { return _items[i]; }

	Variant &at(intptr_t i) { return _items.at(i); }

	iterator begin() { return _items.begin(); }
	const_iterator cbegin() { return _items.begin(); }

	iterator end() { return _items.end(); }
	const_iterator cend() { return _items.end(); }

	void traverse(const GCCallback &callback);

	Storage &items() { return _items; }
	const Storage &items() const { return _items; }

	String to_string() const;

	String to_json(int spacing) const;

	void swap(List &other) noexcept;

private:

	Storage _items;
	mutable bool seen = false; // for printing
};


//---------------------------------------------------------------------------------------------------------------------

namespace meta {

static inline void traverse(List &lst, const GCCallback &callback)
{
	lst.traverse(callback);
}


static inline String to_string(const List &lst)
{
	return lst.to_string();
}

} // namespace phonometrica::meta

} // namespace phonometrica

#endif // PHONOMETRICA_LIST_HPP
