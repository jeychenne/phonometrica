// Phonometrica engine — Set: a value-semantic set.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Mirrors Table (architecture §4): a stable cell wrapping FlatHashSet<Value>, keyed
// by structural hashing/equality, with copy-on-write value semantics. Iteration
// order is unspecified.

#ifndef PHON_TYPES_SET_HPP
#define PHON_TYPES_SET_HPP

#include <phon/core/cell.hpp>
#include <phon/core/flat_hash_set.hpp>
#include <phon/core/handle.hpp>
#include <phon/core/value.hpp>
#include <phon/core/variant.hpp>
#include <phon/object/value_ops.hpp>

#include <utility>

namespace phonometrica {

class List;

using SetTable = FlatHashSet<Value, ValueHash, ValueEqual>;

struct SetCell
{
	Cell header;
	SetTable table;
};

void register_set_class();

class Set final
{
public:
	Set();

	Set(const Set &) = default;
	Set(Set &&) noexcept = default;
	Set &operator=(const Set &) = default;
	Set &operator=(Set &&) noexcept = default;

	intptr_t size() const noexcept { return m_impl->table.size(); }
	bool empty() const noexcept { return m_impl->table.empty(); }
	uint32_t use_count() const noexcept { return m_impl.use_count(); }
	bool unique() const noexcept { return m_impl.unique(); }

	bool add(const Variant &v); // true if newly inserted
	bool contains(const Variant &v) const;
	bool remove(const Variant &v);
	void clear();

	// Elements as a List (iteration order unspecified).
	List to_list() const;

	bool operator==(const Set &o) const noexcept;
	bool operator!=(const Set &o) const noexcept { return !(*this == o); }

	Value to_value() const noexcept { return Value::make_cell(m_impl.cell()); }
	static Set from_value(Value v) noexcept;
	SetCell *cell() const noexcept { return m_impl.get(); }

private:
	explicit Set(Handle<SetCell> h) noexcept : m_impl(std::move(h)) {}
	SetCell *detach();

	Handle<SetCell> m_impl;
};

} // namespace phonometrica

#endif // PHON_TYPES_SET_HPP
