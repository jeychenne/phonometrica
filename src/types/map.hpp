// Phonometrica engine — Map: a value-semantic dictionary.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The script Map is a stable cell wrapping the engine's one hash table
// (FlatHashMap), keyed by Value with structural hashing/equality (architecture
// §4). The cell provides copy-on-write value semantics and drives retain/release
// on the contained key/value cells; the FlatHashMap provides the algorithm. The
// cell never moves on growth (only the table's own buffer does), so no slot
// rewriting is needed (§5.0). Iteration order is unspecified.

#ifndef PHON_TYPES_MAP_HPP
#define PHON_TYPES_MAP_HPP

#include "core/cell.hpp"
#include "core/flat_hash_map.hpp"
#include "core/handle.hpp"
#include "core/value.hpp"
#include "core/variant.hpp"
#include "object/value_ops.hpp"

#include <utility>

namespace phonometrica {

class List;

using MapTable = FlatHashMap<Value, Value, ValueHash, ValueEqual>;

struct MapCell
{
	Cell header;
	MapTable table;
};

void register_map_class();

class Map final
{
public:
	Map();

	Map(const Map &) = default;
	Map(Map &&) noexcept = default;
	Map &operator=(const Map &) = default;
	Map &operator=(Map &&) noexcept = default;

	intptr_t size() const noexcept { return m_impl->table.size(); }
	bool empty() const noexcept { return m_impl->table.empty(); }
	uint32_t use_count() const noexcept { return m_impl.use_count(); }
	bool unique() const noexcept { return m_impl.unique(); }

	void set(const Variant &key, const Variant &value);
	Variant get(const Variant &key) const; // null if absent
	bool contains(const Variant &key) const;
	bool remove(const Variant &key);
	void clear();

	// Keys / values as Lists (iteration order unspecified).
	List keys() const;
	List values() const;

	bool operator==(const Map &o) const noexcept;
	bool operator!=(const Map &o) const noexcept { return !(*this == o); }

	Value to_value() const noexcept { return Value::make_cell(m_impl.cell()); }
	static Map from_value(Value v) noexcept;
	MapCell *cell() const noexcept { return m_impl.get(); }

private:
	explicit Map(Handle<MapCell> h) noexcept : m_impl(std::move(h)) {}
	MapCell *detach();

	Handle<MapCell> m_impl;
};

} // namespace phonometrica

#endif // PHON_TYPES_MAP_HPP
