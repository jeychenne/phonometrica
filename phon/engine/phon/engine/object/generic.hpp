// Phonometrica engine — generic functions and multiple dispatch.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// All behaviour lives in generic functions; defining a function with an existing
// name and a new signature adds a method (design §6). Dispatch pipeline (§7),
// fast to slow — the inline cache (per call site) arrives with the VM in M4:
//
//   1. Memo table on the generic: arity 1/2 keyed exactly by the argument
//      class-id tuple; higher arities resolve fully.
//   2. Full resolution: filter applicable methods (interval subtype test), then
//      pick the most specific pointwise. Ref-ness is uniform per generic (§4), so
//      it plays no part in selection.
//
// Ambiguity is detected at method-definition time (add_method), not at dispatch:
// under single inheritance two incomparable-but-overlapping signatures require a
// disambiguating method, else the definition is rejected.

#ifndef PHON_DISPATCH_GENERIC_HPP
#define PHON_DISPATCH_GENERIC_HPP

#include <phon/engine/core/flat_hash_map.hpp>
#include <phon/engine/core/small_vector.hpp>
#include <phon/engine/core/symbol.hpp>
#include <phon/engine/core/value.hpp>
#include <phon/engine/core/vector.hpp>
#include <phon/engine/object/class.hpp>

namespace phonometrica {

struct Method
{
	SmallVector<Class *, 4> sig; // declared parameter classes
	void *code = nullptr;        // opaque callable identity (native fn / routine, M4+)
	bool is_vararg = false;      // trailing variadic: sig.back() is the element type

	int arity() const noexcept { return static_cast<int>(sig.size()); }
	// Number of fixed positional parameters (dispatched exactly). For a variadic
	// method the last sig entry is the vararg element type, not a fixed parameter.
	int fixed_count() const noexcept { return is_vararg ? arity() - 1 : arity(); }
	// The vararg element type (valid only when is_vararg).
	Class *vararg_type() const noexcept { return sig[sig.size() - 1]; }
};

struct GenericFunction
{
	Symbol name;
	Vector<Method> methods;
	// Ref-ness is uniform per generic (design/references.md §4): every overload
	// agrees on which parameter positions are `ref`, so the mask lives here (not on
	// each Method) and is not a dispatch dimension. Set by the first `add_method`;
	// a later overload whose mask disagrees is rejected (AddMethod::RefMaskConflict).
	uint64_t ref_mask = 0;
	uint8_t min_arity = 255; // smallest fixed-parameter count across overloads
	uint8_t max_arity = 0;   // largest fixed arity (ignored for the upper bound if has_vararg)
	bool has_vararg = false; // any overload is variadic → no upper arity bound
	bool sealed = false;

	// Memo: encoded arg-tuple -> method index (-1 = no applicable method).
	FlatHashMap<uint64_t, int32_t> memo;
	uint32_t generic_epoch = 0; // bumped when the method set changes
	uint32_t memo_generic_epoch = 0;
	uint32_t memo_type_epoch = 0;
};

enum class AddMethod
{
	Ok,             // added (or redefined an identical signature)
	Ambiguous,      // rejected: overlaps an existing method with no disambiguator
	RefMaskConflict, // rejected: `ref` parameters disagree with the generic's other overloads
};

// Global generic registry (keyed by name). get_or_create returns a stable
// pointer owned by the registry.
GenericFunction *get_or_create_generic(Symbol name);
GenericFunction *find_generic(Symbol name) noexcept;

// Add a method. `sig` are the declared parameter classes; `ref_mask` marks `ref`
// parameters and must agree with the generic's established (uniform) mask — the
// first method sets it, a later disagreement is rejected (RefMaskConflict). An
// identical `sig` redefines in place. Bumps the epoch.
AddMethod add_method(GenericFunction *g, const SmallVector<Class *, 4> &sig, uint64_t ref_mask,
                     bool is_vararg, void *code);

// Remove the method with exactly `sig` from `g` (the retraction half of add_method,
// used by the registration journal on module unload/reload, design §11). Bumps the
// epoch and clears the memo so inline caches self-invalidate. A no-op if no such
// method exists. The generic itself is never destroyed; an emptied non-builtin
// generic is treated as undefined at name resolution.
void remove_method(GenericFunction *g, const SmallVector<Class *, 4> &sig, bool is_vararg);

// Resolve the method for a call. Returns the selected Method (nullptr if none is
// applicable). Selection is by argument *types* only; a reference argument
// dispatches on its referent (ref-ness is uniform, not a dispatch dimension).
Method *resolve(GenericFunction *g, const Value *args, int argc);

// A generic function as a first-class script value (design §6): the singleton
// Function cell for `name`, created on first use. The cell is a native trampoline
// that resolves the generic BY NAME at each call, so the value tracks later-added
// (or journal-retracted) overloads, and two references to the same function
// compare equal. Defined in vm/interpreter.cpp — a declaration seam like the cc_*
// collector hooks: dispatch sits below the VM, but the value must invoke through it.
Cell *generic_function_value(Symbol name);

// The per-generic memo cache in `resolve` is shared mutable state, so it is used only
// while dispatch is single-threaded. A spawned worker brackets its run with these
// (concurrency §13): each worker increments on entry — so a worker always observes a
// non-zero count and skips the memo — and decrements on exit. Only the main thread,
// and only when no worker is live, ever touches the memo, making that access exclusive
// by construction. `full_resolve` (read-only over the stable method set) is used by
// everyone else.
void dispatch_enter_thread() noexcept;
void dispatch_exit_thread() noexcept;

// Enumerate every registered generic (in registration order). Used by the freeze walk
// that makes script-method constants cross-thread-safe before parallel_map fans out
// (concurrency §13). `fn` is called with each generic and the opaque `ctx`.
void for_each_generic(void (*fn)(GenericFunction *g, void *ctx), void *ctx);

// Global value constants: bare-name builtin bindings (e.g. `PI`, `E`) that the
// compiler resolves to a compile-time constant load. Registered once at init_runtime,
// then read-only during compilation. Shadowable by any local/module binding of the
// same name. A cell-valued constant is retained by the table (released at shutdown).
void register_constant(Symbol name, Value v);
bool find_constant(Symbol name, Value &out) noexcept;

// Free the generic registry (runs at process exit; exposed for teardown/tests).
void generic_registry_shutdown();

} // namespace phonometrica

#endif // PHON_DISPATCH_GENERIC_HPP
