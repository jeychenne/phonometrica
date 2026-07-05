// Phonometrica engine — Isolate implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/vm/isolate.hpp>

#include <phon/dispatch/generic.hpp>

#include <utility>

namespace phonometrica {

namespace {

// M4 fixed register stack: 64K Values (512 KiB). Deep recursion overflows with a
// clean error rather than corrupting memory. Geometric growth + base fixup
// (architecture §10.2) is deferred; recorded in DEVIATIONS.
constexpr intptr_t STACK_CAPACITY = 1 << 16;

Isolate *g_current = nullptr;

PHON_FORCE_INLINE void retain_value(Value v) noexcept
{
	if (v.is_cell())
		retain(v.as_cell());
}

} // namespace

Isolate *current_isolate() noexcept { return g_current; }
void set_current_isolate(Isolate *iso) noexcept { g_current = iso; }

Isolate::Isolate()
{
	m_stack_cap = STACK_CAPACITY;
	m_stack = std::make_unique<Value[]>(static_cast<size_t>(m_stack_cap));
	Value null = Value::make_null();
	for (intptr_t i = 0; i < m_stack_cap; ++i)
		m_stack[i] = null;
}

Isolate::~Isolate()
{
	retract_journal();
	for (intptr_t i = 0; i < m_kept.size(); ++i)
		release(m_kept[i]);
}

void Isolate::keep_alive(Cell *c) { m_kept.push_back(c); }

void Isolate::record_method(GenericFunction *g, SmallVector<Class *, 4> sig, uint64_t ref_mask,
                            Cell *closure)
{
	m_journal.push_back(MethodRegistration{g, std::move(sig), ref_mask, closure});
}

void Isolate::retract_journal() noexcept
{
	// Undo in reverse (a redefinition re-adds the same signature; unwinding LIFO
	// removes the most recent binding first, mirroring how they were layered on).
	for (intptr_t i = m_journal.size() - 1; i >= 0; --i)
	{
		MethodRegistration &r = m_journal[i];
		remove_method(r.g, r.sig, r.ref_mask);
		if (r.closure)
			release(r.closure);
	}
	m_journal.clear();
}

void Isolate::unwind_on_error() noexcept
{
	if (frames.empty())
		return;
	// The live registers form one contiguous span from the root frame's base to the
	// innermost frame's end (overlapping call windows are covered exactly once).
	Value *lo = frames.front().base;
	Value *hi = frames.back().base + frames.back().cl->proto->num_regs;
	close_upvalues(lo); // detach open upvalues from the dying stack
	for (Value *p = lo; p < hi; ++p)
	{
		if (p->is_cell())
			release(p->as_cell());
		*p = Value::make_null();
	}
	frames.clear();
}

UpvalueCell *Isolate::find_or_make_open_upvalue(Value *slot)
{
	// Open list is unordered; a linear scan is fine (few open upvalues at once).
	for (UpvalueCell *uv = m_open; uv; uv = uv->next)
		if (uv->slot == slot)
			return uv;
	UpvalueCell *uv = make_upvalue(slot); // rc == 1: this list reference
	uv->next = m_open;
	m_open = uv;
	return uv;
}

void Isolate::close_upvalues(Value *from)
{
	UpvalueCell **link = &m_open;
	while (UpvalueCell *uv = *link)
	{
		if (uv->slot >= from)
		{
			// Close: take ownership of a copy, redirect the slot, unlink, and drop
			// the list's reference (closures keep it alive if any still hold it).
			uv->closed = *uv->slot;
			retain_value(uv->closed);
			uv->slot = &uv->closed;
			*link = uv->next;
			uv->next = nullptr;
			release(reinterpret_cast<Cell *>(uv));
		}
		else
		{
			link = &uv->next;
		}
	}
}

void Isolate::unlink_open_upvalue(UpvalueCell *uv) noexcept
{
	for (UpvalueCell **link = &m_open; *link; link = &(*link)->next)
	{
		if (*link == uv)
		{
			*link = uv->next;
			uv->next = nullptr;
			return;
		}
	}
}

int Isolate::ic_base(Proto *p)
{
	uint64_t k = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
	auto it = m_ic_base.find(k);
	if (it != m_ic_base.end())
		return it->second;
	int base = static_cast<int>(ics.size());
	for (int i = 0; i < p->num_ic; ++i)
		ics.push_back(ICEntry{kICEmpty, nullptr, 0, 0});
	m_ic_base.insert(k, base);
	return base;
}

void Isolate::raise(String message, int line) { throw RuntimeError{std::move(message), line}; }

} // namespace phonometrica
