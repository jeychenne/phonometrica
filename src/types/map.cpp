// Phonometrica engine — Map and Set implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Both wrap the engine's one hash table (FlatHashMap/FlatHashSet) in a stable,
// copy-on-write cell. The wrapper drives retain/release on the contained cells;
// the table drives the hashing. Structural-equality keys mean the *stored* cell
// may differ from a query cell, so mutators release the stored value found via
// the table, never the caller's argument.

#include "types/map.hpp"
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

// --- Map cell hooks ---

void map_finalize(Cell *c)
{
	auto *m = reinterpret_cast<MapCell *>(c);
	for (auto &e : m->table)
	{
		release_value(e.first);
		release_value(e.second);
	}
	m->table.~MapTable();
}

MapCell *map_alloc()
{
	Cell *c = cell_alloc(CID_MAP, static_cast<intptr_t>(sizeof(MapCell)));
	auto *m = reinterpret_cast<MapCell *>(c);
	::new (&m->table) MapTable();
	return m;
}

MapCell *map_clone(MapCell *s)
{
	Cell *c = cell_alloc(CID_MAP, static_cast<intptr_t>(sizeof(MapCell)));
	auto *m = reinterpret_cast<MapCell *>(c);
	::new (&m->table) MapTable(s->table); // copies Value bits (no refcount)
	for (auto &e : m->table)               // now claim references on the copies
	{
		retain_value(e.first);
		retain_value(e.second);
	}
	return m;
}

bool map_structural_equals(const Cell *a, const Cell *b)
{
	auto *ma = reinterpret_cast<MapCell *>(const_cast<Cell *>(a));
	auto *mb = reinterpret_cast<MapCell *>(const_cast<Cell *>(b));
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

Class g_map_class;
Class g_set_class;

} // namespace

void register_map_class()
{
	g_map_class.id = CID_MAP;
	g_map_class.name = "Map";
	g_map_class.base = get_class(CID_OBJECT);
	g_map_class.flags = CLASS_BUILTIN | CLASS_VALUE; // may contain itself -> cyclic
	g_map_class.instance_size = static_cast<intptr_t>(sizeof(MapCell));
	g_map_class.finalize = &map_finalize;
	g_map_class.equals = &map_structural_equals;
	register_class(&g_map_class);
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
// Map
// ---------------------------------------------------------------------------

Map::Map() : m_impl(Handle<MapCell>::adopt(map_alloc())) {}

Map Map::from_value(Value v) noexcept
{
	PHON_ASSERT(v.is_cell() && v.as_cell()->class_id() == CID_MAP);
	return Map(Handle<MapCell>(reinterpret_cast<MapCell *>(v.as_cell())));
}

MapCell *Map::detach()
{
	if (m_impl.unique())
		return m_impl.get();
	MapCell *clone = map_clone(m_impl.get());
	m_impl = Handle<MapCell>::adopt(clone);
	return clone;
}

void Map::set(const Variant &key, const Variant &value)
{
	MapCell *m = detach();
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

Variant Map::get(const Variant &key) const
{
	MapCell *m = m_impl.get();
	auto it = m->table.find(key.value());
	if (it == m->table.end())
		return Variant();
	return Variant(it->second);
}

bool Map::contains(const Variant &key) const
{
	return m_impl->table.contains(key.value());
}

bool Map::remove(const Variant &key)
{
	MapCell *m = detach();
	auto it = m->table.find(key.value());
	if (it == m->table.end())
		return false;
	release_value(it->first); // stored key (may differ from the query cell)
	release_value(it->second);
	m->table.erase(key.value());
	return true;
}

void Map::clear()
{
	if (m_impl->table.empty())
		return;
	MapCell *m = detach();
	for (auto &e : m->table)
	{
		release_value(e.first);
		release_value(e.second);
	}
	m->table.clear();
}

List Map::keys() const
{
	List out;
	for (auto &e : m_impl->table)
		out.append(Variant(e.first));
	return out;
}

List Map::values() const
{
	List out;
	for (auto &e : m_impl->table)
		out.append(Variant(e.second));
	return out;
}

bool Map::operator==(const Map &o) const noexcept
{
	if (m_impl.get() == o.m_impl.get())
		return true;
	return map_structural_equals(&m_impl->header, &o.m_impl->header);
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
