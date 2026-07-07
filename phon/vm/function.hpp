// Phonometrica engine — callable cells: closures, native functions, upvalues.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A Proto (vm/proto.hpp) is inert; a *Closure* binds it to captured upvalues to
// become a callable Value. Three cell-headed types live here:
//
//   * ClosureCell   — Proto* + inline array of captured UpvalueCell*. The script
//                     value produced by CLOSURE and stored in variables/slots.
//   * NativeCell     — a C++ callback (Value(*)(Isolate&, Value*, int)); stdlib
//                     functions and operator fallbacks are natives.
//   * UpvalueCell    — an enclosing local captured by a closure. "Open" while the
//                     variable still lives on the stack (points at the register);
//                     "closed" once the frame returns (owns a copy). Sharing an
//                     open upvalue is what lets sibling closures see each other's
//                     writes (design §7 / test_upvalues).
//
// All three are reference classes (identity, no CoW). Closures/upvalues can form
// cycles; the cycle collector that reclaims them arrives in M5 (plain RC until
// then, so a self-capturing closure leaks — acceptable for M4).

#ifndef PHON_VM_FUNCTION_HPP
#define PHON_VM_FUNCTION_HPP

#include <phon/core/cell.hpp>
#include <phon/core/symbol.hpp>
#include <phon/core/value.hpp>
#include <phon/object/class.hpp>
#include <phon/vm/proto.hpp>

namespace phonometrica {

class Isolate;

// Native callback ABI (architecture §10.2): borrowed argument span, returns a
// value carrying one reference the caller adopts. Runtime errors are raised via
// Isolate::raise (which throws across the native boundary).
using NativeFn = Value (*)(Isolate &iso, Value *args, int argc);

// One cell type serves both upvalues and first-class reference boxes: an upvalue
// *is* a reference to a captured variable (design/references.md §3). "Open" means
// `slot` points at a live stack register (the caller's local, not yet moved); the
// box owns no value and the defining frame reads that register directly. "Closed"
// means `slot == &closed` and the box owns the value (a reference that outlived its
// source frame). Reading is uniformly `*slot`.
struct UpvalueCell
{
	Cell header;
	Value *slot;         // -> stack register (open) or -> closed (closed)
	Value closed;        // the captured value once closed
	UpvalueCell *next;   // intrusive open-upvalue list (Isolate)

	bool is_open() const noexcept { return slot != &closed; }
};

// The box an is_reference() Value points at.
PHON_FORCE_INLINE UpvalueCell *reference_box(Value v) noexcept
{
	return reinterpret_cast<UpvalueCell *>(v.as_reference_box());
}

// Read through a first-class reference to the value it currently stands for
// (design/references.md §5: PHP's ZVAL_DEREF). Non-references pass through — one
// predicted branch, an indirection only for actual references.
PHON_FORCE_INLINE Value deref(Value v) noexcept
{
	return v.is_reference() ? *reference_box(v)->slot : v;
}

struct ClosureCell
{
	Cell header;
	Proto *proto;
	int32_t nupvals;
	UpvalueCell *upvals[]; // inline
};

struct NativeCell
{
	Cell header;
	NativeFn fn;
	Symbol name;
	int32_t min_arity;
	int32_t max_arity; // -1 = variadic
};

// Register the Closure/Native/Upvalue classes (idempotent). Called by init_runtime().
void register_function_classes();

Class *closure_class() noexcept;
Class *native_class() noexcept;
Class *upvalue_class() noexcept;

PHON_FORCE_INLINE bool is_closure(Value v) noexcept
{
	return v.is_cell() && class_of(v) == closure_class()->id;
}
PHON_FORCE_INLINE bool is_native(Value v) noexcept
{
	return v.is_cell() && class_of(v) == native_class()->id;
}
PHON_FORCE_INLINE bool is_callable(Value v) noexcept { return is_closure(v) || is_native(v); }

// The uniform `ref` mask of a callable Value (0 if it is not a closure with `ref`
// parameters). Read at an *indirect* call to decide argument promotion at runtime
// (design/references.md §6.2). Natives currently never have `ref` parameters.
PHON_FORCE_INLINE uint64_t callable_ref_mask(Value v) noexcept
{
	return is_closure(v) ? reinterpret_cast<ClosureCell *>(v.as_cell())->proto->ref_mask : 0;
}

// --- construction (each returns a cell with refcount 1) ---

ClosureCell *make_closure(Proto *proto);
NativeCell *make_native(NativeFn fn, Symbol name, int min_arity, int max_arity);
UpvalueCell *make_upvalue(Value *slot);
// A *closed* reference box that owns `initial` (design/references.md §3): the
// reference has no write-back target (a non-lvalue passed to a `ref` parameter).
UpvalueCell *make_reference_box(Value initial);

} // namespace phonometrica

#endif // PHON_VM_FUNCTION_HPP
