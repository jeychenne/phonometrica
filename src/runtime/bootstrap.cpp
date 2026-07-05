// Phonometrica engine — runtime bootstrap implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "runtime/bootstrap.hpp"

#include "object/class.hpp"

namespace phonometrica {

// Type modules install their own class hooks (finalize/hash/equals) and register
// their builtin id; wired in below.
void register_string_class();
void register_list_class();
void register_map_class();
void register_set_class();

namespace {

// Primitive (non-cell) classes: immediates, doubles, boxed integers, symbols.
// They carry no finalizer and are acyclic; they exist so is_a / dispatch have a
// class for every Value (§6). Designated initializers keep these robust against
// changes to the Class field order.
Class g_object{.id = CID_OBJECT, .name = "Object", .base = nullptr,
               .flags = CLASS_BUILTIN | CLASS_SEALED};
Class g_null{.id = CID_NULL, .name = "Null", .base = &g_object,
             .flags = CLASS_BUILTIN | CLASS_ACYCLIC | CLASS_SEALED};
Class g_boolean{.id = CID_BOOLEAN, .name = "Boolean", .base = &g_object,
                .flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED};
Class g_integer{.id = CID_INTEGER, .name = "Integer", .base = &g_object,
                .flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED};
Class g_float{.id = CID_FLOAT, .name = "Float", .base = &g_object,
              .flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED};
Class g_symbol{.id = CID_SYMBOL, .name = "Symbol", .base = &g_object,
               .flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED};

// The root metaclass. Per-class metaclasses derive from it (created lazily).
Class g_class{.id = CID_CLASS, .name = "Class", .base = &g_object,
              .flags = CLASS_BUILTIN | CLASS_REF | CLASS_META | CLASS_SEALED};

bool g_done = false;

} // namespace

void bootstrap()
{
	if (g_done)
		return;
	g_done = true;

	register_class(&g_object);
	register_class(&g_null);
	register_class(&g_boolean);
	register_class(&g_integer);
	register_class(&g_float);
	register_class(&g_symbol);

	// Cell-headed value types install their own hooks (stable ids 6..9).
	register_string_class();
	register_list_class();
	register_map_class();
	register_set_class();

	// The root metaclass is registered last so builtin intervals equal their
	// stable ids (0..10).
	register_class(&g_class);

	// Compute subtype intervals from the assembled hierarchy.
	renumber_types();
}

} // namespace phonometrica
