// Phonometrica engine — user-class instances: layout, fields, inheritance, CoW.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/object/class.hpp>
#include <phon/engine/object/instance.hpp>
#include <phon/engine/types/atom.hpp>
#include <phon/engine/types/string.hpp>
#include "test_framework.hpp"

using namespace phonometrica;

namespace {

Symbol sym(const char *s) { return intern(s); }

} // namespace

TEST_CASE("instance: a user class lays out its fields in declaration order")
{
	FieldInfo fs[2] = {{sym("x"), get_class(CID_FLOAT), nullptr, nullptr},
	                   {sym("y"), get_class(CID_FLOAT), nullptr, nullptr}};
	Class *point = add_user_class("Point", get_class(CID_OBJECT), /*ref*/ false, /*open*/ false, fs, 2);

	CHECK(point->field_count == 2);
	CHECK(field_slot(point, sym("x")) == 0);
	CHECK(field_slot(point, sym("y")) == 1);
	CHECK(field_slot(point, sym("z")) == -1);
	CHECK(point->is_value());
	CHECK(!point->is_ref());
}

TEST_CASE("instance: a subclass inherits base fields, then appends its own")
{
	FieldInfo base_fs[1] = {{sym("a"), nullptr, nullptr, nullptr}};
	Class *base = add_user_class("Base", get_class(CID_OBJECT), false, false, base_fs, 1);
	FieldInfo derived_fs[2] = {{sym("b"), nullptr, nullptr, nullptr},
	                           {sym("c"), nullptr, nullptr, nullptr}};
	Class *derived = add_user_class("Derived", base, false, false, derived_fs, 2);

	CHECK(derived->field_count == 3);
	CHECK(field_slot(derived, sym("a")) == 0); // inherited
	CHECK(field_slot(derived, sym("b")) == 1);
	CHECK(field_slot(derived, sym("c")) == 2);
	CHECK(is_a(derived, base));
}

TEST_CASE("instance: make_instance null-fills, and fields hold Values")
{
	FieldInfo fs[2] = {{sym("name"), nullptr, nullptr, nullptr},
	                   {sym("count"), nullptr, nullptr, nullptr}};
	Class *rec = add_user_class("Rec", get_class(CID_OBJECT), false, false, fs, 2);

	Cell *inst = make_instance(rec);
	CHECK(inst->refcount() == 1);
	CHECK(class_of(Value::make_cell(inst)) == rec->id);
	Value *f = instance_fields(inst);
	CHECK(f[0].is_null());
	CHECK(f[1].is_null());

	// Store a heap value in a field; retain since the instance now owns it.
	String s("hi");
	f[0] = s.to_value();
	retain(f[0].as_cell());
	f[1] = Value::make_int(42);

	CHECK(class_of(f[0]) == CID_STRING);
	CHECK(f[1].as_int() == 42);

	release(inst); // runs instance_finalize -> releases the String; ASan-checked
}

TEST_CASE("instance: clone deep-copies fields and retains children")
{
	FieldInfo fs[1] = {{sym("label"), nullptr, nullptr, nullptr}};
	Class *box = add_user_class("Box", get_class(CID_OBJECT), false, false, fs, 1);

	Cell *a = make_instance(box);
	String s("shared");
	instance_fields(a)[0] = s.to_value();
	retain(instance_fields(a)[0].as_cell());

	Cell *b = instance_clone(a);
	CHECK(b->refcount() == 1);
	// Same String cell, now referenced by both instances.
	CHECK(instance_fields(a)[0].identical(instance_fields(b)[0]));
	CHECK(instance_fields(a)[0].as_cell()->refcount() >= 2);

	release(a);
	release(b); // both finalize; the String drops to zero exactly once
}
