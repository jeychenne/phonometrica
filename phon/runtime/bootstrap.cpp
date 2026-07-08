// Phonometrica engine — runtime bootstrap implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/runtime/bootstrap.hpp>

#include <phon/object/class.hpp>
#include <phon/object/instance.hpp>
#include <phon/types/atom.hpp>

#include <mutex>

namespace phonometrica {

// Type modules install their own class hooks (finalize/hash/equals) and register
// their builtin id; wired in below.
void register_string_class();
void register_list_class();
void register_table_class();
void register_set_class();
void register_array_class();

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
// Abstract numeric bases (design §6): Number is the top of the numeric tower and Real
// its real-valued branch; both are abstract (no instances — a value's concrete class is
// always Integer or Float). Complex will later join as a sibling of Real under Number.
Class g_number{.id = CID_NUMBER, .name = "Number", .base = &g_object,
               .flags = CLASS_BUILTIN | CLASS_ACYCLIC | CLASS_SEALED};
Class g_real{.id = CID_REAL, .name = "Real", .base = &g_number,
             .flags = CLASS_BUILTIN | CLASS_ACYCLIC | CLASS_SEALED};
Class g_integer{.id = CID_INTEGER, .name = "Integer", .base = &g_real,
                .flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED};
Class g_float{.id = CID_FLOAT, .name = "Float", .base = &g_real,
              .flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED};
Class g_symbol{.id = CID_SYMBOL, .name = "Symbol", .base = &g_object,
               .flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED};

// The root metaclass. Per-class metaclasses derive from it (created lazily).
Class g_class{.id = CID_CLASS, .name = "Class", .base = &g_object,
              .flags = CLASS_BUILTIN | CLASS_REF | CLASS_META | CLASS_SEALED};

// The base Error class (design §12): a value class with `message` (slot 0) and
// `trace` (slot 1, the backtrace captured at first raise) fields, so its instances
// (and user subclasses) go through the ordinary instance machinery.
FieldInfo g_error_fields[2];
Class g_error{.id = CID_ERROR, .name = "Error", .base = &g_object,
              .flags = CLASS_BUILTIN | CLASS_VALUE};

std::once_flag g_boot_once;

void do_bootstrap()
{
	register_class(&g_object);
	register_class(&g_null);
	register_class(&g_boolean);
	// Numeric tower: Number and Real must register before Integer/Float (register_class
	// links each class into its base's child list immediately).
	register_class(&g_number);
	register_class(&g_real);
	register_class(&g_integer);
	register_class(&g_float);
	register_class(&g_symbol);

	// Cell-headed value types install their own hooks (stable ids 6..9).
	register_string_class();
	register_list_class();
	register_table_class();
	register_set_class();
	register_array_class(); // the numeric Array view + its double buffer (design §9)

	// The root metaclass registers after the concrete value types. (Subtype intervals
	// no longer equal the stable ids now that the abstract Number/Real bases are spliced
	// into the tree — intervals come from renumber_types() over the assembled hierarchy.)
	register_class(&g_class);

	// Error carries `message` and `trace` fields (Strings), via the instance machinery.
	g_error_fields[0] = FieldInfo{intern("message"), get_class(CID_STRING), nullptr, nullptr, false};
	g_error_fields[1] = FieldInfo{intern("trace"), get_class(CID_STRING), nullptr, nullptr, false};
	g_error.fields = g_error_fields;
	g_error.field_count = 2;
	g_error.instance_size =
	    static_cast<intptr_t>(sizeof(Cell)) + 2 * static_cast<intptr_t>(sizeof(Value));
	g_error.finalize = &instance_finalize;
	g_error.clone = &instance_clone_hook;
	g_error.trace = &instance_trace; // user subclasses may add cyclic fields (§8.2)
	register_class(&g_error);

	// Compute subtype intervals from the assembled hierarchy.
	renumber_types();
}

} // namespace

void bootstrap() { std::call_once(g_boot_once, do_bootstrap); }

} // namespace phonometrica
