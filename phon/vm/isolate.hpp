// Phonometrica engine — Isolate: per-thread execution state (architecture §10.1).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// One Isolate per script thread owns the register stack, the call-frame vector,
// the open-upvalue list, the module namespace, and the current error. Compiled
// Protos are immutable and shared; everything mutable lives here (§10.4). M4 is
// single-threaded: a process-global "current" Isolate lets upvalue finalizers and
// native callbacks find their thread without threading a pointer everywhere; the
// real thread-local wiring lands with concurrency (M7).

#ifndef PHON_VM_ISOLATE_HPP
#define PHON_VM_ISOLATE_HPP

#include <phon/core/flat_hash_map.hpp>
#include <phon/core/value.hpp>
#include <phon/core/variant.hpp>
#include <phon/core/vector.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/function.hpp>
#include <phon/vm/opcode.hpp>
#include <phon/vm/proto.hpp>

#include <memory>

namespace phonometrica {

// One inline-cache slot for a CALLG site (architecture §10.4). Monomorphic in M4:
// caches the last argument-class tuple and the method it resolved to, guarded by
// the type/generic epochs so it self-invalidates when the hierarchy or method set
// changes. `key == kICEmpty` marks a cold slot.
struct ICEntry
{
	uint64_t key;
	void *callable; // resolved Callable cell (ClosureCell* / NativeCell*)
	uint32_t type_epoch;
	uint32_t generic_epoch;
};

inline constexpr uint64_t kICEmpty = ~uint64_t(0);

// A script error crossing the native/VM boundary (architecture §10.5). The full
// handler-stack unwinding and Error hierarchy arrive in M5; M4 propagates this
// C++ exception straight to the do_string boundary.
struct RuntimeError
{
	String message;
	int line;
};

// One activation record. `cl`/`base` drive execution of this frame; `ret_ip`/
// `ret_slot` say where the caller resumes and receives the result.
struct CallFrame
{
	ClosureCell *cl = nullptr;
	Value *base = nullptr;
	const Instruction *ret_ip = nullptr;
	Value *ret_slot = nullptr;
};

class Isolate final
{
public:
	Isolate();
	~Isolate();

	Isolate(const Isolate &) = delete;
	Isolate &operator=(const Isolate &) = delete;

	Value *stack() noexcept { return m_stack.get(); }
	intptr_t stack_capacity() const noexcept { return m_stack_cap; }

	Vector<CallFrame> frames;

	// Module namespace: slot-indexed bindings (design §11). Variant retains cells,
	// keeping module-level functions/values alive for the module's lifetime.
	Vector<Variant> module_slots;

	// --- open upvalues (shared while their register is live) ---

	UpvalueCell *find_or_make_open_upvalue(Value *slot);
	void close_upvalues(Value *from); // close every open upvalue at slot >= from
	void unlink_open_upvalue(UpvalueCell *uv) noexcept; // called by the finalizer

	// --- inline caches (per-Proto, isolate-local; chunks stay immutable §10.4) ---

	// The IC base offset for `p`, assigning a fresh block of p->num_ic slots on
	// first execution. CALLG at relative slot r uses ics[ic_base(p) + r].
	int ic_base(Proto *p);
	Vector<ICEntry> ics;

	// --- errors ---

	[[noreturn]] void raise(String message, int line);

private:
	std::unique_ptr<Value[]> m_stack;
	intptr_t m_stack_cap = 0;
	UpvalueCell *m_open = nullptr; // head of the open-upvalue list
	FlatHashMap<uint64_t, int> m_ic_base;
};

// The Isolate executing on this thread (M4: process-global). Set while a run is in
// progress; used by upvalue finalizers and native callbacks.
Isolate *current_isolate() noexcept;
void set_current_isolate(Isolate *iso) noexcept;

} // namespace phonometrica

#endif // PHON_VM_ISOLATE_HPP
