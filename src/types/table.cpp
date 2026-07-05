// Phonometrica engine — Table and Set implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Both wrap the engine's one hash table (FlatHashMap/FlatHashSet) in a stable,
// copy-on-write cell. The wrapper drives retain/release on the contained cells;
// the table drives the hashing. Structural-equality keys mean the *stored* cell
// may differ from a query cell, so mutators release the stored value found via
// the table, never the caller's argument.

#include "types/table.hpp"
#include "types/set.hpp"

#include "object/class.hpp"
#include "object/value_ops.hpp"
#include "types/list.hpp"

#include <new>

namespace phonometrica {

namespace {

PHON_FORCE_INLINE void retain_value(Value v) noexcept
{
	if (v.is_cell())
		retain(v.as_cell());
}
PHON_FORCE_INLINE void release_value(Value v) noexcept
{
	if (v.is_cell())
		release(v.as_cell());
}

// --- Table cell hooks ---

void table_finalize(Cell *c)
{
	auto *m = reinterpret_cast<TableCell *>(c);
	for (auto &e : m->table)
	{
		release_value(e.first);
		release_value(e.second);
	}
	m->table.~TableStorage();
}

TableCell *table_alloc()
{
	Cell *c = cell_alloc(CID_TABLE, static_cast<intptr_t>(sizeof(TableCell)));
	auto *m = reinterpret_cast<TableCell *>(c);
	::new (&m->table) TableStorage();
	return m;
}

TableCell *table_clone(TableCell *s)
{
	Cell *c = cell_alloc(CID_TABLE, static_cast<intptr_t>(sizeof(TableCell)));
	auto *m = reinterpret_cast<TableCell *>(c);
	::new (&m->table) TableStorage(s->table); // copies Value bits (no refcount)
	for (auto &e : m->table)               // now claim references on the copies
	{
		retain_value(e.first);
		retain_value(e.second);
	}
	return m;
}

bool table_structural_equals(const Cell *a, const Cell *b)
{
	auto *ma = reinterpret_cast<TableCell *>(const_cast<Cell *>(a));
	auto *mb = reinterpret_cast<TableCell *>(const_cast<Cell *>(b));
	if (ma->table.size() != mb->table.size())
		return false;
	for (auto &e : ma->table)
	{
		auto it = mb->table.find(e.first);
		if (it == mb->table.end() || !value_equals(e.second, it->second))
			return false;
	}
	return true;
}

// --- Set cell hooks ---

void set_finalize(Cell *c)
{
	auto *s = reinterpret_cast<SetCell *>(c);
	for (const Value &k : s->table)
		release_value(k);
	s->table.~SetTable();
}

SetCell *set_alloc()
{
	Cell *c = cell_alloc(CID_SET, static_cast<intptr_t>(sizeof(SetCell)));
	auto *s = reinterpret_cast<SetCell *>(c);
	::new (&s->table) SetTable();
	return s;
}

SetCell *set_clone(SetCell *src)
{
	Cell *c = cell_alloc(CID_SET, static_cast<intptr_t>(sizeof(SetCell)));
	auto *s = reinterpret_cast<SetCell *>(c);
	::new (&s->table) SetTable(src->table);
	for (const Value &k : s->table)
		retain_value(k);
	return s;
}

bool set_structural_equals(const Cell *a, const Cell *b)
{
	auto *sa = reinterpret_cast<SetCell *>(const_cast<Cell *>(a));
	auto *sb = reinterpret_cast<SetCell *>(const_cast<Cell *>(b));
	if (sa->table.size() != sb->table.size())
		return false;
	for (const Value &k : sa->table)
		if (!sb->table.contains(k))
			return false;
	return true;
}

Class g_table_class;
Class g_set_class;

} // namespace

void register_table_class()
{
	g_table_class.id = CID_TABLE;
	g_table_class.name = "Table";
	g_table_class.base = get_class(CID_OBJECT);
	g_table_class.flags = CLASS_BUILTIN | CLASS_VALUE; // may contain itself -> cyclic
	g_table_class.instance_size = static_cast<intptr_t>(sizeof(TableCell));
	g_table_class.finalize = &table_finalize;
	g_table_class.equals = &table_structural_equals;
	register_class(&g_table_class);
}

void register_set_class()
{
	g_set_class.id = CID_SET;
	g_set_class.name = "Set";
	g_set_class.base = get_class(CID_OBJECT);
	g_set_class.flags = CLASS_BUILTIN | CLASS_VALUE;
	g_set_class.instance_size = static_cast<intptr_t>(sizeof(SetCell));
	g_set_class.finalize = &set_finalize;
	g_set_class.equals = &set_structural_equals;
	register_class(&g_set_class);
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------

Table::Table() : m_impl(Handle<TableCell>::adopt(table_alloc())) {}

Table Table::from_value(Value v) noexcept
{
	PHON_ASSERT(v.is_cell() && v.as_cell()->class_id() == CID_TABLE);
	return Table(Handle<TableCell>(reinterpret_cast<TableCell *>(v.as_cell())));
}

TableCell *Table::detach()
{
	if (m_impl.unique())
		return m_impl.get();
	TableCell *clone = table_clone(m_impl.get());
	m_impl = Handle<TableCell>::adopt(clone);
	return clone;
}

void Table::set(const Variant &key, const Variant &value)
{
	TableCell *m = detach();
	Value k = key.value();
	Value v = value.value();
	auto it = m->table.find(k);
	if (it != m->table.end())
	{
		retain_value(v);
		release_value(it->second);
		it->second = v;
	}
	else
	{
		retain_value(k);
		retain_value(v);
		m->table.insert(k, v);
	}
}

Variant Table::get(const Variant &key) const
{
	TableCell *m = m_impl.get();
	auto it = m->table.find(key.value());
	if (it == m->table.end())
		return Variant();
	return Variant(it->second);
}

bool Table::contains(const Variant &key) const
{
	return m_impl->table.contains(key.value());
}

bool Table::remove(const Variant &key)
{
	TableCell *m = detach();
	auto it = m->table.find(key.value());
	if (it == m->table.end())
		return false;
	release_value(it->first); // stored key (may differ from the query cell)
	release_value(it->second);
	m->table.erase(key.value());
	return true;
}

void Table::clear()
{
	if (m_impl->table.empty())
		return;
	TableCell *m = detach();
	for (auto &e : m->table)
	{
		release_value(e.first);
		release_value(e.second);
	}
	m->table.clear();
}

List Table::keys() const
{
	List out;
	for (auto &e : m_impl->table)
		out.append(Variant(e.first));
	return out;
}

List Table::values() const
{
	List out;
	for (auto &e : m_impl->table)
		out.append(Variant(e.second));
	return out;
}

bool Table::operator==(const Table &o) const noexcept
{
	if (m_impl.get() == o.m_impl.get())
		return true;
	return table_structural_equals(&m_impl->header, &o.m_impl->header);
}

// ---------------------------------------------------------------------------
// Set
// ---------------------------------------------------------------------------

Set::Set() : m_impl(Handle<SetCell>::adopt(set_alloc())) {}

Set Set::from_value(Value v) noexcept
{
	PHON_ASSERT(v.is_cell() && v.as_cell()->class_id() == CID_SET);
	return Set(Handle<SetCell>(reinterpret_cast<SetCell *>(v.as_cell())));
}

SetCell *Set::detach()
{
	if (m_impl.unique())
		return m_impl.get();
	SetCell *clone = set_clone(m_impl.get());
	m_impl = Handle<SetCell>::adopt(clone);
	return clone;
}

bool Set::add(const Variant &v)
{
	SetCell *s = detach();
	Value k = v.value();
	auto r = s->table.insert(k);
	if (r.second)
		retain_value(k);
	return r.second;
}

bool Set::contains(const Variant &v) const
{
	return m_impl->table.contains(v.value());
}

bool Set::remove(const Variant &v)
{
	SetCell *s = detach();
	auto it = s->table.find(v.value());
	if (it == s->table.end())
		return false;
	Value stored = *it; // the actual stored cell (structural-equality keys)
	s->table.erase(v.value());
	release_value(stored);
	return true;
}

void Set::clear()
{
	if (m_impl->table.empty())
		return;
	SetCell *s = detach();
	for (const Value &k : s->table)
		release_value(k);
	s->table.clear();
}

List Set::to_list() const
{
	List out;
	for (const Value &k : m_impl->table)
		out.append(Variant(k));
	return out;
}

bool Set::operator==(const Set &o) const noexcept
{
	if (m_impl.get() == o.m_impl.get())
		return true;
	return set_structural_equals(&m_impl->header, &o.m_impl->header);
}

} // namespace phonometrica
