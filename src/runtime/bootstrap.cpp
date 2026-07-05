// Phonometrica engine — runtime bootstrap implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "runtime/bootstrap.hpp"

#include "object/class.hpp"

namespace phonometrica {

// Type modules install their own class hooks (finalize/hash/equals) here. Each
// is defined in its module and registers its builtin id. Wired in as the
void register_string_class();
void register_list_class();
void register_map_class();
void register_set_class();

namespace {

// Primitive (non-cell) classes: immediates, doubles, boxed integers, symbols.
// They carry no finalizer and are acyclic. They exist so is_a / dispatch have a
// class object for every Value (§6). instance_size 0: not heap-allocated.
Class g_object{CID_OBJECT, "Object", nullptr, CLASS_BUILTIN, 0};
Class g_null{CID_NULL, "Null", &g_object, CLASS_BUILTIN | CLASS_ACYCLIC, 0};
Class g_boolean{CID_BOOLEAN, "Boolean", &g_object, CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC, 0};
Class g_integer{CID_INTEGER, "Integer", &g_object, CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC, 0};
Class g_float{CID_FLOAT, "Float", &g_object, CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC, 0};
Class g_symbol{CID_SYMBOL, "Symbol", &g_object, CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC, 0};

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

	// Cell-headed value types install their own hooks.
	register_string_class();
	register_list_class();
	register_map_class();
	register_set_class();
}

} // namespace phonometrica
