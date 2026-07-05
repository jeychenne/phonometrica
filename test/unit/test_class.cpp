// Phonometrica engine — class system tests (intervals, renumbering, metaclasses).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/object/class.hpp>
#include <phon/types/string.hpp>
#include "test_framework.hpp"

using namespace phonometrica;

TEST_CASE("builtin subtype intervals")
{
	Class *obj = get_class(CID_OBJECT);
	Class *integer = get_class(CID_INTEGER);
	Class *flt = get_class(CID_FLOAT);
	Class *str = get_class(CID_STRING);

	CHECK(is_a(integer, obj));
	CHECK(is_a(str, obj));
	CHECK(is_a(obj, obj));
	CHECK(!is_a(obj, integer)); // Object is not a subtype of Integer
	CHECK(!is_a(integer, flt)); // sibling primitives
	CHECK(!is_a(integer, str));
}

TEST_CASE("class_of maps every Value to its class")
{
	CHECK(class_of(Value::make_int(5)) == CID_INTEGER);
	CHECK(class_of(Value::make(1.5)) == CID_FLOAT);
	CHECK(class_of(Value::make_bool(true)) == CID_BOOLEAN);
	CHECK(class_of(Value::make_null()) == CID_NULL);
	CHECK(class_of(Value::make_symbol(Symbol{7})) == CID_SYMBOL);

	String s("hi");
	CHECK(class_of(s.to_value()) == CID_STRING);
}

TEST_CASE("value_is_a uses the interval test")
{
	Class *obj = get_class(CID_OBJECT);
	Class *integer = get_class(CID_INTEGER);
	Class *str = get_class(CID_STRING);

	CHECK(value_is_a(Value::make_int(5), integer));
	CHECK(value_is_a(Value::make_int(5), obj));
	CHECK(!value_is_a(Value::make_int(5), str));

	String s("hi");
	CHECK(value_is_a(s.to_value(), str));
	CHECK(value_is_a(s.to_value(), obj));
	CHECK(!value_is_a(s.to_value(), integer));
}

TEST_CASE("add_class builds a subtype hierarchy")
{
	Class *obj = get_class(CID_OBJECT);
	uint32_t epoch0 = type_epoch();

	Class *animal = add_class("Animal", obj, 0);
	CHECK(type_epoch() > epoch0); // renumbering bumped the epoch
	Class *dog = add_class("Dog", animal, 0);
	Class *cat = add_class("Cat", animal, 0);

	CHECK(is_a(dog, animal));
	CHECK(is_a(dog, obj));
	CHECK(is_a(cat, animal));
	CHECK(!is_a(dog, cat)); // siblings
	CHECK(!is_a(animal, dog));
	CHECK(!is_a(dog, get_class(CID_INTEGER)));

	// Builtins are unaffected by user-class additions.
	CHECK(is_a(get_class(CID_INTEGER), obj));
	CHECK(class_of(Value::make_int(1)) == CID_INTEGER);
}

TEST_CASE("renumbering after a mid-tree insert keeps subtyping correct")
{
	Class *obj = get_class(CID_OBJECT);
	Class *base = add_class("Base", obj, 0);
	Class *a = add_class("A", base, 0);
	Class *b = add_class("B", base, 0);
	CHECK(is_a(a, base));
	CHECK(is_a(b, base));

	uint32_t epoch_before = type_epoch();
	// Insert a subclass of A after B already exists — intervals shift, is_a holds.
	Class *a_child = add_class("AChild", a, 0);
	CHECK(type_epoch() > epoch_before);
	CHECK(is_a(a_child, a));
	CHECK(is_a(a_child, base));
	CHECK(is_a(a_child, obj));
	CHECK(!is_a(a_child, b));
	CHECK(!is_a(b, a_child));
	// Previously-created relationships still correct after the shift.
	CHECK(is_a(a, base));
	CHECK(is_a(b, base));
	CHECK(!is_a(a, b));
}

TEST_CASE("metaclasses and class objects")
{
	Class *flt = get_class(CID_FLOAT);
	Class *integer = get_class(CID_INTEGER);

	Value float_obj = class_object(flt);
	CHECK(float_obj.is_cell());
	// Cached: same class object each time.
	CHECK(class_object(flt).identical(float_obj));
	// The class object denotes Float.
	CHECK(class_denoted_by(float_obj) == flt);
	CHECK(class_denoted_by(Value::make_int(3)) == nullptr);

	// Distinct classes have distinct metaclasses, so their class objects have
	// different dispatch classes — the basis for cast(x, Float) vs cast(x, Integer).
	Value int_obj = class_object(integer);
	CHECK(class_of(float_obj) != class_of(int_obj));
	CHECK(class_of(float_obj) == metaclass_of(flt)->id);
	// The metaclass is a subtype of the root metaclass Class.
	CHECK(is_a(metaclass_of(flt), get_class(CID_CLASS)));
}
