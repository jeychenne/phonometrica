// Phonometrica engine — List: a growable value-semantic sequence of Variants.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// ListCell = Cell | size | capacity | Value data[] (inline payload, design §5.2).
// Value semantics via copy-on-write; growing a unique list may move its cell, so
// mutators rewrite the owning Handle slot. Indices are 1-based at this C++ API
// boundary (negative counts from the end), converted to 0-based internally.

#ifndef PHON_TYPES_LIST_HPP
#define PHON_TYPES_LIST_HPP

#include "core/cell.hpp"
#include "core/handle.hpp"
#include "core/value.hpp"
#include "core/variant.hpp"

#include <cstddef>
#include <initializer_list>

namespace phonometrica {

struct ListCell
{
	Cell header;
	intptr_t size;
	intptr_t capacity;
	Value data[]; // inline
};

void register_list_class();

class List final
{
public:
	List();
	explicit List(intptr_t size); // `size` nulls
	List(std::initializer_list<Variant> items);

	List(const List &) = default;
	List(List &&) noexcept = default;
	List &operator=(const List &) = default;
	List &operator=(List &&) noexcept = default;

	// --- capacity ---

	intptr_t size() const noexcept { return m_impl->size; }
	intptr_t capacity() const noexcept { return m_impl->capacity; }
	bool empty() const noexcept { return m_impl->size == 0; }
	uint32_t use_count() const noexcept { return m_impl.use_count(); }
	bool unique() const noexcept { return m_impl.unique(); }

	// --- element access (1-based, negative from end) ---

	Variant get(intptr_t i) const;         // copy
	void set(intptr_t i, const Variant &v); // CoW
	Variant &ref(intptr_t i);               // in-place slot view (unshares first)

	// --- modifiers (CoW) ---

	void append(const Variant &v);
	void prepend(const Variant &v);
	void insert(intptr_t i, const Variant &v); // before 1-based position i
	Variant pop();                              // remove and return the last
	void remove_at(intptr_t i);                 // remove 1-based position i
	void clear();
	void reserve(intptr_t n);

	// --- queries ---

	intptr_t index_of(const Variant &v) const; // 1-based, 0 if absent
	bool contains(const Variant &v) const { return index_of(v) != 0; }
	bool operator==(const List &o) const noexcept;
	bool operator!=(const List &o) const noexcept { return !(*this == o); }

	// --- engine interop ---

	Value to_value() const noexcept { return Value::make_cell(m_impl.cell()); }
	static List from_value(Value v) noexcept;
	ListCell *cell() const noexcept { return m_impl.get(); }

private:
	explicit List(Handle<ListCell> h) noexcept : m_impl(std::move(h)) {}

	// Convert a 1-based, possibly-negative index to a 0-based one, bounds-checked.
	intptr_t normalize(intptr_t i, bool allow_end = false) const;

	// Ensure unique with room for `need` slots; returns the (possibly moved) cell
	// and rewrites the owning slot.
	ListCell *detach_for_write(intptr_t need);

	Handle<ListCell> m_impl;
};

} // namespace phonometrica

#endif // PHON_TYPES_LIST_HPP
