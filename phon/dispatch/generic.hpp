// Phonometrica engine — generic functions and multiple dispatch.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// All behaviour lives in generic functions; defining a function with an existing
// name and a new signature adds a method (design §6). Dispatch pipeline (§7),
// fast to slow — the inline cache (per call site) arrives with the VM in M4:
//
//   1. Memo table on the generic: arity 1/2 keyed exactly by the argument
//      class-id tuple (with ref-ness folded in); higher arities resolve fully.
//   2. Full resolution: filter applicable methods (interval subtype test +
//      exact ref-mask match), then pick the most specific pointwise.
//
// Ambiguity is detected at method-definition time (add_method), not at dispatch:
// under single inheritance two incomparable-but-overlapping signatures require a
// disambiguating method, else the definition is rejected.

#ifndef PHON_DISPATCH_GENERIC_HPP
#define PHON_DISPATCH_GENERIC_HPP

#include <phon/core/flat_hash_map.hpp>
#include <phon/core/small_vector.hpp>
#include <phon/core/symbol.hpp>
#include <phon/core/value.hpp>
#include <phon/core/vector.hpp>
#include <phon/object/class.hpp>

namespace phonometrica {

struct Method
{
	SmallVector<Class *, 4> sig; // declared parameter classes
	uint64_t ref_mask = 0;       // bit i set => parameter i is `ref`
	void *code = nullptr;        // opaque callable identity (native fn / routine, M4+)

	int arity() const noexcept { return static_cast<int>(sig.size()); }
};

struct GenericFunction
{
	Symbol name;
	Vector<Method> methods;
	uint8_t min_arity = 255;
	uint8_t max_arity = 0;
	bool sealed = false;

	// Memo: encoded arg-tuple -> method index (-1 = no applicable method).
	FlatHashMap<uint64_t, int32_t> memo;
	uint32_t generic_epoch = 0; // bumped when the method set changes
	uint32_t memo_generic_epoch = 0;
	uint32_t memo_type_epoch = 0;
};

enum class AddMethod
{
	Ok,        // added (or redefined an identical signature)
	Ambiguous, // rejected: overlaps an existing method with no disambiguator
};

// Global generic registry (keyed by name). get_or_create returns a stable
// pointer owned by the registry.
GenericFunction *get_or_create_generic(Symbol name);
GenericFunction *find_generic(Symbol name) noexcept;

// Add a method. `sig` are the declared parameter classes; `ref_mask` marks `ref`
// parameters. An identical (sig, ref_mask) redefines in place. Bumps the epoch.
AddMethod add_method(GenericFunction *g, const SmallVector<Class *, 4> &sig, uint64_t ref_mask,
                     void *code);

// Remove the method with exactly (sig, ref_mask) from `g` (the retraction half of
// add_method, used by the registration journal on module unload/reload, design
// §11). Bumps the epoch and clears the memo so inline caches self-invalidate. A
// no-op if no such method exists. The generic itself is never destroyed; an
// emptied non-builtin generic is treated as undefined at name resolution.
void remove_method(GenericFunction *g, const SmallVector<Class *, 4> &sig, uint64_t ref_mask);

// Resolve the method for a call. Returns the selected Method (nullptr if none is
// applicable). `args` may contain REF values for `ref` parameters.
Method *resolve(GenericFunction *g, const Value *args, int argc);

// Free the generic registry (runs at process exit; exposed for teardown/tests).
void generic_registry_shutdown();

} // namespace phonometrica

#endif // PHON_DISPATCH_GENERIC_HPP
