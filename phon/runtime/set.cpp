/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 05/06/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime/set.hpp>

namespace phonometrica {

Set::Set(const Set &other)
{
	for (auto &val : other._items) {
		_items.insert(val.resolve());
	}
}

String Set::to_string() const
{
	if (this->seen)
	{
		return "{...}";
	}

	bool flag = this->seen;
	String s("{");

	for (auto &val : _items)
	{
		s.append(val.to_string(true));
		s.append(", ");
	}
	s.remove_last(", ");
	s.append('}');
	this->seen = flag;

	return s;
}

void Set::traverse(const GCCallback &callback)
{
	for (auto &val : _items) {
		const_cast<Variant&>(val).traverse(callback);
	}
}

bool Set::operator==(const Set &other) const
{
	return this->_items == other._items;
}

} // namespace phonometrica