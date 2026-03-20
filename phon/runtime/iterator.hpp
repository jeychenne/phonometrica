/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: Iterator object, to iterate over sequences such as List and Table.                                         *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ITERATOR_HPP
#define PHONOMETRICA_ITERATOR_HPP

#include <phon/runtime/list.hpp>
#include <phon/runtime/table.hpp>

namespace phonometrica {

// We can't make this an abstract interface because we need to store iterators in handles, and Handle<T> would complain that it can't
// instantiate an abstract class.
class Iterator
{
public:

	Iterator(Variant v, bool ref_val) : object(std::move(v)), ref_val(ref_val) { }

	virtual ~Iterator() = default;

	virtual Variant get_key() { return Variant(); }

	virtual Variant get_value();

	virtual bool at_end() const { return true; };

protected:

	Variant object;
	bool ref_val;
};

//---------------------------------------------------------------------------------------------------------------------

class ListIterator : public Iterator
{
public:

	ListIterator(Variant v, bool ref_val);

	Variant get_key() override;

	Variant get_value() override;

	bool at_end() const override;

private:

	List::Storage *lst;
	intptr_t pos;
};

//---------------------------------------------------------------------------------------------------------------------

class TableIterator : public Iterator
{
public:

	TableIterator(Variant v, bool ref_val);

	Variant get_key() override;

	Variant get_value() override;

	bool at_end() const override;

private:

	Table::Storage *map;
	Table::iterator it;
};

//---------------------------------------------------------------------------------------------------------------------

class StringIterator : public Iterator
{
public:

	StringIterator(Variant v, bool ref_val);

	Variant get_key() override;

	Variant get_value() override;

	bool at_end() const override;

private:

	String *str;
	intptr_t pos = 1;
};


//---------------------------------------------------------------------------------------------------------------------

class RegexIterator : public Iterator
{
public:

	RegexIterator(Variant v, bool ref_val);

	Variant get_key() override;

	Variant get_value() override;

	bool at_end() const override;

private:

	Regex *re;
	intptr_t pos = 1;
};


//---------------------------------------------------------------------------------------------------------------------

class FileIterator : public Iterator
{
public:

	FileIterator(Variant v, bool ref_val);

	Variant get_key() override;

	Variant get_value() override;

	bool at_end() const override;

private:

	File *file;
	intptr_t pos = 1;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ITERATOR_HPP
