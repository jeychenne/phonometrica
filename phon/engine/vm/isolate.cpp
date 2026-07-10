// Phonometrica engine — Isolate implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/vm/isolate.hpp>

#include <phon/engine/object/generic.hpp>
#include <phon/engine/object/instance.hpp>
#include <phon/engine/types/atom.hpp>

#include <utility>

namespace phonometrica {

Cell *make_error(const String &message)
{
	Cell *e = make_instance(error_class());
	Value mv = message.to_value();
	if (mv.is_cell())
		retain(mv.as_cell()); // the instance's field owns this reference
	instance_fields(e)[0] = mv; // slot 0 = message
	return e;
}

namespace {

// M4 fixed register stack: 64K Values (512 KiB). Deep recursion overflows with a
// clean error rather than corrupting memory. Geometric growth + base fixup
// (architecture §10.2) is deferred; recorded in DEVIATIONS.
constexpr intptr_t STACK_CAPACITY = 1 << 16;

// Thread-local: every script thread (the main one and any spawned Isolate) has its
// own "current Isolate" so native callbacks and upvalue finalizers find the Isolate
// running on *their* thread (architecture §10.1).
thread_local Isolate *g_current = nullptr;

PHON_FORCE_INLINE void retain_value(Value v) noexcept
{
	if (v.owns_cell())
		retain(v.cell_ptr());
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
	// The teardown releases below must feed *this* Isolate's collector, whichever
	// Isolate was current (mirrors do_string's current-isolate save/restore).
	CycleCollector *prev = current_collector();
	set_current_collector(&m_collector);

	// Join every spawned worker first, while this Isolate's roots — the channels the
	// workers share, the callee closures the journal still holds — are all still alive.
	// Releasing a thread handle runs its finalizer, which joins the OS thread (structured
	// concurrency, architecture §13).
	for (intptr_t i = 0; i < m_threads.size(); ++i)
		release(m_threads[i]);
	m_threads.clear();

	// Release every root the Isolate still owns BEFORE the final collection, so any
	// garbage cycle they anchored becomes reclaimable. Ordering matters: the module
	// slots hold the module-level values, which are the usual cycle anchors.
	retract_journal();
	for (intptr_t i = 0; i < m_kept.size(); ++i)
		release(m_kept[i]);
	m_kept.clear();
	unwind_on_error();     // drop any live registers/frames from an aborted run
	module_slots.clear();  // release module-level values

	// Reap the cycles those releases orphaned.
	m_collector.collect_until_stable();
	set_current_collector(prev == &m_collector ? nullptr : prev);
}

void Isolate::keep_alive(Cell *c) { m_kept.push_back(c); }

void Isolate::adopt_thread(Cell *handle) { m_threads.push_back(handle); }

void Isolate::record_method(GenericFunction *g, SmallVector<Class *, 4> sig, bool is_vararg,
                            Cell *closure)
{
	m_journal.push_back(MethodRegistration{g, std::move(sig), is_vararg, closure});
}

void Isolate::retract_journal() noexcept
{
	// Undo in reverse (a redefinition re-adds the same signature; unwinding LIFO
	// removes the most recent binding first, mirroring how they were layered on).
	for (intptr_t i = m_journal.size() - 1; i >= 0; --i)
	{
		MethodRegistration &r = m_journal[i];
		remove_method(r.g, r.sig, r.is_vararg);
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
		if (p->owns_cell())
			release(p->cell_ptr());
		*p = Value::make_null();
	}
	frames.clear();
	handlers.clear();
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
		ics.push_back(ICEntry{kICEmpty, nullptr, 0, 0, nullptr});
	m_ic_base.insert(k, base);
	return base;
}

String Isolate::backtrace(int top_line)
{
	String out;
	intptr_t n = frames.size();
	for (intptr_t i = n - 1; i >= 0; --i)
	{
		Proto *p = frames[i].cl->proto;
		int line = top_line;
		if (i != n - 1)
		{
			// A non-innermost frame's active line is where it called the frame above.
			intptr_t idx = (frames[i + 1].ret_ip - p->code.data()) - 1;
			line = static_cast<int>(p->line_at(idx));
		}
		out.append("  at ");
		out.append(p->name == NO_SYMBOL ? String("<module>") : String(symbol_name(p->name)));
		out.append(" (line ");
		out.append(String::convert(static_cast<intptr_t>(line)));
		out.append(")\n");
	}
	return out;
}

void capture_error_trace(Isolate &iso, Cell *err, int top_line)
{
	Value *f = instance_fields(err);
	if (!f[1].is_null())
		return; // already captured at first raise
	String tr = iso.backtrace(top_line); // keep alive while we take ownership
	Value tv = tr.to_value();
	if (tv.is_cell())
		retain(tv.as_cell()); // the instance's field owns this reference
	f[1] = tv;
}

void Isolate::safepoint(int line)
{
	uint32_t poll = m_poll.load(std::memory_order_relaxed);
	if (PHON_UNLIKELY(poll & POLL_INTERRUPT))
	{
		// Consume the request so a caught interrupt does not immediately re-fire, then
		// raise. Uncaught, this unwinds to the do_string boundary as a RuntimeError.
		m_poll.fetch_and(~POLL_INTERRUPT, std::memory_order_relaxed);
		raise(String("[Interrupt] script interrupted"), line);
	}
	m_collector.collect_if_needed();
}

void Isolate::raise(String message, int line)
{
	// Builtin errors are thrown as Error instances so scripts can `catch` them.
	Cell *e = make_error(message);
	capture_error_trace(*this, e, line);
	throw RuntimeError{std::move(message), line, Value::make_cell(e)};
}

} // namespace phonometrica
