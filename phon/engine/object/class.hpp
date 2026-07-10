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

#include <phon/engine/base/definitions.hpp>
#include <phon/engine/core/symbol.hpp>
#include <phon/engine/core/value.hpp>

namespace phonometrica {

struct Cell;
struct Class;

// One field of a user-defined class (design §5.6). Fields are laid out in the
// instance in declaration order, a subclass's own fields following its base's, so
// a field's index in `Class::fields` is its slot in the instance payload. `getter`
// and `setter` are the accessor closures (design "Field accessors"); both null for
// a plain storage field.
struct FieldInfo
{
	Symbol name;
	Class *type = nullptr;   // declared type (null => Object/untyped; advisory)
	Cell *getter = nullptr;  // `get` accessor closure, or null
	Cell *setter = nullptr;  // `set` accessor closure, or null
	bool is_private = false; // `local field`: reachable only through `this`
};

// Per-class hooks. All may be null.
using FinalizeHook = void (*)(Cell *);
using CloneHook = void (*)(Cell *dst, const Cell *src);
using HashHook = uint64_t (*)(const Cell *);
using EqualsHook = bool (*)(const Cell *, const Cell *);
// Enumerate a cell's child *cells* for the cycle collector (§8.2): call `visit`
// once per contained cell reference (immediates are skipped). Null for leaf types.
using TraceHook = void (*)(Cell *self, void (*visit)(Cell *child));
// Free a white cell's *auxiliary* storage (e.g. a Table's hash buffer) WITHOUT
// releasing its child Values — the collector has already accounted for the child
// edges (§8.2 collect_white). Null when the payload is inline (List, instances).
using GcFreeHook = void (*)(Cell *);

enum ClassFlags : uint16_t
{
	CLASS_VALUE = 1u << 0,
	CLASS_REF = 1u << 1,
	CLASS_ACYCLIC = 1u << 2,
	CLASS_BUILTIN = 1u << 3,
	CLASS_SEALED = 1u << 4,
	CLASS_META = 1u << 5,    // a metaclass (its instances are class objects)
	CLASS_FOREIGN = 1u << 6, // a registered C++ class, boxed as { Cell; T } (add_class<T>)
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
	GcFreeHook gc_free = nullptr;
	Cell *class_object = nullptr; // cached class-object cell (lazy)

	// User-class instance layout (design §5.6): a heap array of `field_count`
	// descriptors (owned; base fields first, then own), null for builtins.
	FieldInfo *fields = nullptr;
	int32_t field_count = 0;

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
	CID_ERROR, // base of the thrown-error hierarchy (design §12)
	// Abstract numeric bases (design §6): Object -> Number -> Real -> {Integer, Float}.
	// Real is the common base of Integer/Float; Number leaves room for Complex later.
	// Stable ids need not reflect the hierarchy — subtype intervals are computed from
	// the tree at renumber time — so these append here while keeping CID_INTEGER/FLOAT.
	CID_NUMBER,
	CID_REAL,
	CID_ARRAY,       // numeric Array view (design §9 / architecture §5.3)
	CID_ARRAYBUFFER, // the Array's separately-refcounted double buffer (never script-visible)
	CID_BUILTIN_COUNT
};

// The base Error class (design §12). Its instances carry a `message` field at slot
// 0; user error types derive from it.
Class *error_class() noexcept;

// --- registration ---

// Register a statically-allocated class at index `c->id` (builtins). Links it
// into its base's child list. Does not renumber — call renumber_types() once
// after a batch (bootstrap does).
uint32_t register_class(Class *c);

// Create and register a dynamically-owned class (user/library types, tests).
// Assigns the next stable id, links into base's children, and renumbers.
Class *add_class(const char *name, Class *base, uint16_t flags, intptr_t instance_size = 0);

// Register an instantiable user class (design §5.6): inherit `base`'s fields, append
// `own` (n_own), build the instance layout, wire the instance hooks (finalize/clone),
// and set `instance_size`. Renumbers. The returned Class owns a copy of the field
// array. `is_ref` selects reference (identity) vs value (copy-on-write) semantics.
Class *add_user_class(const char *name, Class *base, bool is_ref, bool is_open,
                      const FieldInfo *own, int32_t n_own);

// The slot of field `name` in `c` (searching declared then inherited fields), or -1.
int32_t field_slot(const Class *c, Symbol name) noexcept;

// Field descriptor at `slot` (0-based), or null if out of range.
const FieldInfo *field_at(const Class *c, int32_t slot) noexcept;

Class *get_class(uint32_t id) noexcept;
bool has_class(uint32_t id) noexcept;
intptr_t class_count() noexcept;

// Look up a class by name (the embedding API's `Runtime::get_class(name)`). Returns the
// first non-metaclass whose name matches, or null. Used to resolve a base class for C++
// class registration (design §11.2).
Class *find_class(const char *name) noexcept;

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
