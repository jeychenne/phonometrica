// Phonometrica engine — instances of user-defined classes (design §5.6).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// An instance is a Cell header followed by one Value slot per field (fixed size,
// the field count and name→slot map live in the Class). A value `class` has
// copy-on-write value semantics: sharing bumps the refcount and a mutation of a
// shared instance detaches a private copy (`instance_detach`); a `ref class` has
// identity and skips CoW. The finalize/clone hooks are wired onto the Class by
// add_user_class, so retain/release and the eventual cycle collector work uniformly.

#ifndef PHON_OBJECT_INSTANCE_HPP
#define PHON_OBJECT_INSTANCE_HPP

#include <phon/core/cell.hpp>
#include <phon/core/value.hpp>
#include <phon/object/class.hpp>

namespace phonometrica {

struct InstanceCell
{
	Cell header;
	Value fields[]; // field_count(class) slots, inline
};

// Allocate an instance of user class `c`, all fields null, refcount 1.
Cell *make_instance(Class *c);

// Field storage of an instance cell.
PHON_FORCE_INLINE Value *instance_fields(Cell *c) noexcept
{
	return reinterpret_cast<InstanceCell *>(c)->fields;
}

// Deep-copy an instance (fresh cell, refcount 1, children retained). Used for CoW.
Cell *instance_clone(const Cell *src);

// Class hooks (wired by add_user_class).
void instance_finalize(Cell *c);
void instance_clone_hook(Cell *dst, const Cell *src);
void instance_trace(Cell *c, void (*visit)(Cell *child));

} // namespace phonometrica

#endif // PHON_OBJECT_INSTANCE_HPP
