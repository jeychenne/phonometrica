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
#include <phon/core/small_vector.hpp>
#include <phon/core/value.hpp>
#include <phon/core/variant.hpp>
#include <phon/core/vector.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/function.hpp>
#include <phon/vm/opcode.hpp>
#include <phon/vm/proto.hpp>

#include <memory>

namespace phonometrica {

struct Class;
struct GenericFunction;

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

// A script error in flight (architecture §10.5). It carries the thrown `Error`
// value (an instance of Error or a subclass); `message`/`line` mirror the error's
// message and its origin line for the embedding boundary and diagnostics. The VM's
// per-Isolate handler stack catches it at a `try`; if none applies it propagates as
// this C++ exception to the `do_string` boundary. `error` holds one reference the
// catcher adopts.
struct RuntimeError
{
	String message;
	int line;
	Value error = Value::make_null();
};

// Create an Error instance carrying `message` (refcount 1). Used by `raise` and the
// embedding layer to build the thrown value for a builtin error.
Cell *make_error(const String &message);

// Capture the current backtrace into an Error's `trace` field (slot 1) if it has
// none yet, so a re-thrown error keeps its original origin (`top_line` is the
// raise/throw line).
void capture_error_trace(Isolate &iso, Cell *err, int top_line);

// One journaled generic-method registration (design §11 registration journal).
// The Isolate holds the +1 reference to the closure supplying the method's code;
// retracting the journal removes the method and releases the closure, restoring
// the process-global generics to their pre-run state. This is what keeps reloaded
// modules — and independent unit-test runs — from polluting each other now that
// named functions are generic methods rather than module bindings.
struct MethodRegistration
{
	GenericFunction *g = nullptr;
	SmallVector<Class *, 4> sig;
	uint64_t ref_mask = 0;
	Cell *closure = nullptr; // owned (+1)
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

	// Active `try` handlers (design §12 / architecture §10.5). `PUSHTRY` records where
	// to resume — the frame depth to unwind to, the frame base, the catch-dispatch ip,
	// and the register that receives the thrown error — and a raise walks these to
	// find the innermost applicable handler.
	struct Handler
	{
		intptr_t frame_depth;         // frames.size() when the try was entered
		Value *base;                  // the try frame's register base
		const Instruction *land_ip;   // catch-dispatch code
		int err_reg;                  // register that receives the error
	};
	Vector<Handler> handlers;

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

	// --- registration journal (design §11) ---

	// Record a method this run added to a generic. Takes ownership of the closure's
	// +1 reference (the caller must not release it).
	void record_method(GenericFunction *g, SmallVector<Class *, 4> sig, uint64_t ref_mask,
	                   Cell *closure);

	// Undo every journaled registration: remove the methods and release the
	// closures. Called by the destructor; exposed for the editor's reload surface.
	void retract_journal() noexcept;

	// Take ownership of a cell (its current +1) for the Isolate's lifetime; released
	// on teardown. Used for accessor closures held only by a Class's field layout.
	void keep_alive(Cell *c);

	// --- errors ---

	[[noreturn]] void raise(String message, int line);

	// A formatted backtrace of the current call stack (`top_line` is the active line
	// of the innermost frame). Captured into an Error's `trace` field at first raise.
	String backtrace(int top_line);

	// Release the live register span and drop all frames after an uncaught error, so
	// an aborted run leaves no leaked cells (a minimal stand-in until the full
	// handler-stack unwinding of arch §10.5). Idempotent.
	void unwind_on_error() noexcept;

private:
	std::unique_ptr<Value[]> m_stack;
	intptr_t m_stack_cap = 0;
	UpvalueCell *m_open = nullptr; // head of the open-upvalue list
	FlatHashMap<uint64_t, int> m_ic_base;
	Vector<MethodRegistration> m_journal;
	Vector<Cell *> m_kept; // cells owned for the Isolate's lifetime (accessor closures)
};

// The Isolate executing on this thread (M4: process-global). Set while a run is in
// progress; used by upvalue finalizers and native callbacks.
Isolate *current_isolate() noexcept;
void set_current_isolate(Isolate *iso) noexcept;

} // namespace phonometrica

#endif // PHON_VM_ISOLATE_HPP
