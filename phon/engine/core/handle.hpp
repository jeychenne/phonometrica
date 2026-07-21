// Phonometrica engine — Handle<T>: intrusive refcounted smart pointer over cells.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The C++-side owner type for cell-headed objects (design/architecture.md §3.4),
// mirroring the current engine's Handle<T>. `Value` is the script-side
// representation; conversions both ways are explicit and cheap. Non-atomic RC
// (thread-confined heaps). T must be a cell-headed type: its first member is a
// `Cell header` (checked below).

#ifndef PHON_CORE_HANDLE_HPP
#define PHON_CORE_HANDLE_HPP

#include <phon/engine/base/definitions.hpp>
#include <phon/engine/core/cell.hpp>
#include <new>
#include <type_traits>
#include <utility>

namespace phonometrica {

struct Class;

// The Class an application type `T` is registered under (set by add_class<T>). A plain
// class carries *nothing* on itself — this template variable is the whole T→Class map,
// so `Sound`/`File`/`Regex` stay ordinary C++ classes. Null until registered.
template<class T>
inline Class *g_registered_class = nullptr;

template<class T>
Class *class_of() noexcept
{
	return g_registered_class<T>;
}

template<typename T>
class Handle
{
public:
	// Tag for adopting an already-owned reference (e.g. a freshly allocated cell
	// with refcount 1) without an extra retain.
	struct Adopt
	{
	};

	Handle() = default;

	// Null handle (old Phonometrica parity: handles are compared to and reset with
	// nullptr all over the application).
	Handle(std::nullptr_t) noexcept {}

	// Retaining constructor: takes a borrowed pointer and adds a reference.
	explicit Handle(T *p) noexcept : m_ptr(p)
	{
		if (m_ptr)
			retain(cell_of(m_ptr));
	}

	// Adopting constructor: takes ownership of an existing reference.
	Handle(T *p, Adopt) noexcept : m_ptr(p) {}

	Handle(const Handle &o) noexcept : m_ptr(o.m_ptr)
	{
		if (m_ptr)
			retain(cell_of(m_ptr));
	}

	Handle(Handle &&o) noexcept : m_ptr(o.m_ptr) { o.m_ptr = nullptr; }

	// Upcasting constructor: build a Handle<T> from a Handle<U> where U derives from T
	// under single, non-virtual inheritance (the constraint every add_class hierarchy
	// satisfies — MIGRATION_NOTES step 5). The boxed base subobject shares the payload
	// address (offset 0) and box_value_offset is alignment-only, so it is identical for
	// T and U; the cell is therefore the same box, and this just retains it. Implicit,
	// so a Handle<Derived> flows into a Handle<Base> slot or an Array<Handle<Base>>
	// element. Mirrors the old poly-box seam's `operator Handle<Base>`
	// (phon/runtime/typed_object.hpp) that roadmap A0 relied on; the checked inverse is
	// `handle_cast<Derived>` (core/handle_cast.hpp).
	template<class U, class = std::enable_if_t<!std::is_same_v<T, U> && std::is_base_of_v<T, U>>>
	Handle(const Handle<U> &o) noexcept : m_ptr(o.get() ? static_cast<T *>(o.get()) : nullptr)
	{
		static_assert(box_value_offset<T>() == box_value_offset<U>(),
		              "Handle upcast requires an identical box offset (equal alignment); the "
		              "hierarchy must use single, non-virtual inheritance");
		if (m_ptr)
			retain(cell_of(m_ptr));
	}

	Handle &operator=(const Handle &o) noexcept
	{
		if (m_ptr != o.m_ptr)
		{
			if (o.m_ptr)
				retain(cell_of(o.m_ptr));
			if (m_ptr)
				release(cell_of(m_ptr));
			m_ptr = o.m_ptr;
		}
		return *this;
	}

	Handle &operator=(Handle &&o) noexcept
	{
		if (this != &o)
		{
			if (m_ptr)
				release(cell_of(m_ptr));
			m_ptr = o.m_ptr;
			o.m_ptr = nullptr;
		}
		return *this;
	}

	~Handle()
	{
		if (m_ptr)
			release(cell_of(m_ptr));
	}

	// Adopt a raw pointer that already carries the reference we should own.
	static Handle adopt(T *p) noexcept { return Handle(p, Adopt{}); }

	// Retaining handle to the payload of an existing cell (script → C++). For a
	// cell-headed type the cell *is* the payload; for a plain (boxed) class the payload
	// sits after the header. Null cell → empty handle.
	static Handle from_cell(Cell *c) noexcept
	{
		if (!c)
			return Handle();
		if constexpr (is_cell_headed_v<T>)
			return Handle(reinterpret_cast<T *>(c));
		else
			return Handle(box_value<T>(c));
	}

	// Allocate and construct a new instance of a registered application class (design
	// §11.5: replaces `rt.create<T>`). `T` is a plain class registered by add_class<T>;
	// it is boxed as `{ Cell header; T value }`, so the constructor writes the value
	// *after* the header (no clobber). Allocation is Isolate-independent (the FOREIGN
	// cell path), so this works on any thread, including with no Isolate.
	template<class... Args>
	static Handle make(Args &&...args)
	{
		static_assert(!is_cell_headed_v<T>,
		              "cell-headed engine types are created by their own factory, not make()");
		Cell *c = cell_alloc(class_of<T>()->id, box_total_size<T>());
		T *p = ::new (static_cast<void *>(box_value<T>(c))) T(std::forward<Args>(args)...);
		return Handle(p, Adopt{});
	}

	// --- access ---

	T *get() const noexcept { return m_ptr; }
	T *operator->() const noexcept { return m_ptr; }
	T &operator*() const noexcept { return *m_ptr; }
	explicit operator bool() const noexcept { return m_ptr != nullptr; }

	Cell *cell() const noexcept { return m_ptr ? cell_of(m_ptr) : nullptr; }

	bool unique() const noexcept { return m_ptr && cell_of(m_ptr)->is_unique(); }
	uint32_t use_count() const noexcept { return m_ptr ? cell_of(m_ptr)->refcount() : 0; }

	// Release ownership without decrementing (transfers the reference to caller).
	T *release_ownership() noexcept
	{
		T *p = m_ptr;
		m_ptr = nullptr;
		return p;
	}

	// Adopt the new address of a cell that was reallocated in place (single-
	// allocation growth, §5.0). The reference travels with the object, so this
	// neither retains nor releases — it only rewrites the owning slot.
	void reset_reallocated(T *moved) noexcept { m_ptr = moved; }

	void reset() noexcept
	{
		if (m_ptr)
			release(cell_of(m_ptr));
		m_ptr = nullptr;
	}

	void swap(Handle &o) noexcept { std::swap(m_ptr, o.m_ptr); }

	bool operator==(const Handle &o) const noexcept { return m_ptr == o.m_ptr; }
	bool operator!=(const Handle &o) const noexcept { return m_ptr != o.m_ptr; }
	bool operator==(std::nullptr_t) const noexcept { return m_ptr == nullptr; }
	bool operator!=(std::nullptr_t) const noexcept { return m_ptr != nullptr; }

private:
	static PHON_FORCE_INLINE Cell *cell_of(T *p) noexcept
	{
		// A cell-headed type has its Cell as the first member, so the object and its
		// header share an address. A plain (boxed) class's payload sits after the header,
		// so back up by the box offset to reach the cell.
		if constexpr (is_cell_headed_v<T>)
			return reinterpret_cast<Cell *>(p);
		else
			return reinterpret_cast<Cell *>(reinterpret_cast<char *>(p) - box_value_offset<T>());
	}

	T *m_ptr = nullptr;
};

} // namespace phonometrica

#endif // PHON_CORE_HANDLE_HPP
