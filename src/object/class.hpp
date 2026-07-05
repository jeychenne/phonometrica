// Phonometrica engine — Class descriptors, subtype intervals, and the registry.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The type system (design/architecture.md §6). Two id spaces, deliberately
// distinct:
//
//   * Stable id  — a class's registration index. It lives in every Cell header
//                  and in specialized VM opcodes, and NEVER changes. Builtin ids
//                  (CID_*) are compile-time constants.
//   * Interval   — a pre-order [lo, hi] range recomputed on every class addition.
//                  Subtyping is the interval containment test. Renumbering shifts
//                  only interval ids (never stable ids), so existing instances are
//                  never invalidated; a global type_epoch bump lets dispatch
//                  caches self-invalidate.
//
// Builtins register first and are sealed, so they occupy the lowest intervals and
// never shift; user classes append after them. Classes are also first-class
// values: `class_object(C)` yields a Value whose type is C's metaclass, which is
// what makes `cast(x, Float)` dispatch on the class object (§7).

#ifndef PHON_OBJECT_CLASS_HPP
#define PHON_OBJECT_CLASS_HPP

#include "base/definitions.hpp"
#include "core/value.hpp"

namespace phonometrica {

struct Cell;

// Per-class hooks. All may be null.
using FinalizeHook = void (*)(Cell *);
using CloneHook = void (*)(Cell *dst, const Cell *src);
using HashHook = uint64_t (*)(const Cell *);
using EqualsHook = bool (*)(const Cell *, const Cell *);
using TraceHook = void (*)(Cell *, void *visitor);

enum ClassFlags : uint16_t
{
	CLASS_VALUE = 1u << 0,
	CLASS_REF = 1u << 1,
	CLASS_ACYCLIC = 1u << 2,
	CLASS_BUILTIN = 1u << 3,
	CLASS_SEALED = 1u << 4,
	CLASS_META = 1u << 5, // a metaclass (its instances are class objects)
};

struct Class
{
	uint32_t id = 0;           // stable id (registry index; in cell headers)
	uint32_t interval_lo = 0;  // pre-order interval start (renumbered)
	uint32_t interval_hi = 0;  // pre-order interval end (max subclass)
	uint32_t meta_id = 0;      // stable id of this class's metaclass (0 = none yet)
	const char *name = nullptr;
	Class *base = nullptr;
	Class *first_child = nullptr; // intrusive child list (registration order)
	Class *last_child = nullptr;
	Class *next_sibling = nullptr;
	uint16_t flags = 0;
	intptr_t instance_size = 0;
	FinalizeHook finalize = nullptr;
	CloneHook clone = nullptr;
	HashHook hash = nullptr;
	EqualsHook equals = nullptr;
	TraceHook trace = nullptr;
	Cell *class_object = nullptr; // cached class-object cell (lazy)

	bool is_value() const noexcept { return flags & CLASS_VALUE; }
	bool is_ref() const noexcept { return flags & CLASS_REF; }
	bool is_acyclic() const noexcept { return flags & CLASS_ACYCLIC; }
	bool is_sealed() const noexcept { return flags & CLASS_SEALED; }
};

// Builtin stable class ids: first, stable, hard-codeable (§6).
enum BuiltinClassId : uint32_t
{
	CID_OBJECT = 0,
	CID_NULL,
	CID_BOOLEAN,
	CID_INTEGER,
	CID_FLOAT,
	CID_SYMBOL,
	CID_STRING,
	CID_LIST,
	CID_TABLE,
	CID_SET,
	CID_CLASS, // the root metaclass; per-class metaclasses derive from it
	CID_BUILTIN_COUNT
};

// --- registration ---

// Register a statically-allocated class at index `c->id` (builtins). Links it
// into its base's child list. Does not renumber — call renumber_types() once
// after a batch (bootstrap does).
uint32_t register_class(Class *c);

// Create and register a dynamically-owned class (user/library types, tests).
// Assigns the next stable id, links into base's children, and renumbers.
Class *add_class(const char *name, Class *base, uint16_t flags, intptr_t instance_size = 0);

Class *get_class(uint32_t id) noexcept;
bool has_class(uint32_t id) noexcept;
intptr_t class_count() noexcept;

// Recompute pre-order intervals from the class tree and bump type_epoch.
void renumber_types();
uint32_t type_epoch() noexcept;

// Free dynamically-owned descriptors and class-object cells. Runs automatically
// at process exit; exposed for tests/embedding teardown.
void registry_shutdown();

// --- subtyping ---

// Interval containment: is `sub` the same as or a subclass of `sup`?
PHON_FORCE_INLINE bool is_a(const Class *sub, const Class *sup) noexcept
{
	return sub->interval_lo >= sup->interval_lo && sub->interval_lo <= sup->interval_hi;
}

// Stable class id of any Value (immediates map to their builtin class).
uint32_t class_of(Value v) noexcept;

// Class descriptor of any Value.
Class *class_of_desc(Value v) noexcept;

// Is `v` an instance of `sup` (or a subclass)?
bool value_is_a(Value v, const Class *sup) noexcept;

// --- metaclasses and class objects ---

// The metaclass of `c` (lazily created; derives from the root metaclass Class).
Class *metaclass_of(Class *c);

// A Value denoting the class `c` (lazily created singleton). Its dispatch class
// is c's metaclass, so a method keyed on metaclass_of(c) matches this argument.
Value class_object(Class *c);

// The class a class-object Value denotes, or null if `v` is not a class object.
Class *class_denoted_by(Value v) noexcept;

} // namespace phonometrica

#endif // PHON_OBJECT_CLASS_HPP
