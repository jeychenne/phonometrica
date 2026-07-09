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

#include <phon/base/definitions.hpp>
#include <phon/core/cell.hpp>
#include <new>
#include <utility>

namespace phonometrica {

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

	// Allocate and construct a new instance of a registered cell type (design §11.5:
	// replaces `rt.create<T>`). `T` must be a registered class (`T::phon_class`, bound
	// by add_class<T>); allocation is Isolate-independent (the FOREIGN cell path), so
	// this works on any thread, including with no Isolate. Returns an owning Handle.
	template<class... Args>
	static Handle make(Args &&...args)
	{
		Cell *c = cell_alloc(T::phon_class->id, static_cast<intptr_t>(sizeof(T)));
		// cell_alloc stamped the header + refcount; the constructor writes over T's
		// leading Cell member, so save and restore them around placement-construction.
		uint32_t hdr = c->hdr, rc = c->rc_bits;
		T *p = ::new (static_cast<void *>(c)) T(std::forward<Args>(args)...);
		c->hdr = hdr;
		c->rc_bits = rc;
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

private:
	static PHON_FORCE_INLINE Cell *cell_of(T *p) noexcept
	{
		// A cell-headed type has its Cell as the first member, so the object and
		// its header share an address.
		return reinterpret_cast<Cell *>(p);
	}

	T *m_ptr = nullptr;
};

} // namespace phonometrica

#endif // PHON_CORE_HANDLE_HPP
