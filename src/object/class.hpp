// Phonometrica engine — Class descriptor and the process-global registry.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// M1 seed of the type system (design/architecture.md §6). The full version (M2)
// adds subtype intervals, renumbering, epochs, and metaclasses; this M1 subset is
// exactly what retain/release, CoW, and hashing need: per-class finalize / clone /
// hash / equals hooks and a flat id->Class table. Builtin ids are compile-time
// constants placed first and kept stable, as the invariant in §6 requires.

#ifndef PHON_OBJECT_CLASS_HPP
#define PHON_OBJECT_CLASS_HPP

#include "base/definitions.hpp"

namespace phonometrica {

struct Cell;

// Per-class hooks. All may be null.
using FinalizeHook = void (*)(Cell *);                       // release child resources
using CloneHook = void (*)(Cell *dst, const Cell *src);      // CoW copy (value classes)
using HashHook = uint64_t (*)(const Cell *);                 // structural hash
using EqualsHook = bool (*)(const Cell *, const Cell *);     // structural equality
using TraceHook = void (*)(Cell *, void *visitor);           // cycle collector (M5)

enum ClassFlags : uint16_t
{
	CLASS_VALUE = 1u << 0,   // value semantics (CoW)
	CLASS_REF = 1u << 1,     // reference semantics (identity)
	CLASS_ACYCLIC = 1u << 2, // instances can never be part of a cycle
	CLASS_BUILTIN = 1u << 3,
	CLASS_SEALED = 1u << 4,
};

struct Class
{
	uint32_t id = 0;
	const char *name = nullptr;
	Class *base = nullptr;
	uint16_t flags = 0;
	intptr_t instance_size = 0; // fixed payload size, or -1 for variable-size
	FinalizeHook finalize = nullptr;
	CloneHook clone = nullptr;
	HashHook hash = nullptr;
	EqualsHook equals = nullptr;
	TraceHook trace = nullptr;

	bool is_value() const noexcept { return flags & CLASS_VALUE; }
	bool is_ref() const noexcept { return flags & CLASS_REF; }
	bool is_acyclic() const noexcept { return flags & CLASS_ACYCLIC; }
};

// Builtin class ids: placed first, stable across (future) renumbering (§6).
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
	CID_MAP,
	CID_SET,
	CID_BUILTIN_COUNT
};

// Register a class at index `c->id`, growing the table as needed. Re-registering
// the same id overwrites (used when a type module installs its hooks). Returns id.
uint32_t register_class(Class *c);

// Look up a class by id. The id must be registered.
Class *get_class(uint32_t id) noexcept;

// True if `id` currently has a registered class.
bool has_class(uint32_t id) noexcept;

// Number of registered class slots (max id + 1).
intptr_t class_count() noexcept;

} // namespace phonometrica

#endif // PHON_OBJECT_CLASS_HPP
