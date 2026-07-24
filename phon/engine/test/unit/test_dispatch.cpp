// Phonometrica engine — multiple-dispatch tests.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/object/generic.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/types/atom.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/vm/function.hpp> // UpvalueCell: build a reference box for a dispatch arg
#include "test_framework.hpp"

using namespace phonometrica;

namespace {

// Distinct opaque method identities.
void *tag(int n) { return reinterpret_cast<void *>(static_cast<uintptr_t>(n)); }
int untag(const Method *m) { return m ? static_cast<int>(reinterpret_cast<uintptr_t>(m->code)) : 0; }

SmallVector<Class *, 4> sig(std::initializer_list<Class *> cs)
{
	SmallVector<Class *, 4> s;
	for (Class *c : cs)
		s.push_back(c);
	return s;
}

Class *OBJ() { return get_class(CID_OBJECT); }
Class *INT() { return get_class(CID_INTEGER); }
Class *FLT() { return get_class(CID_FLOAT); }
Class *STR() { return get_class(CID_STRING); }
Class *LIST() { return get_class(CID_LIST); }

Method *call1(GenericFunction *g, Value a) { return resolve(g, &a, 1); }
Method *call2(GenericFunction *g, Value a, Value b)
{
	Value args[2] = {a, b};
	return resolve(g, args, 2);
}

// A variadic method: the last sig entry is the vararg element type.
AddMethod addv(GenericFunction *g, SmallVector<Class *, 4> s, void *code)
{
	return add_method(g, s, 0, /*is_vararg=*/true, code);
}
Method *callN(GenericFunction *g, std::initializer_list<Value> vs)
{
	SmallVector<Value, 8> a;
	for (Value v : vs)
		a.push_back(v);
	return resolve(g, a.data(), static_cast<int>(a.size()));
}
Value I(int n) { return Value::make_int(n); }
Value F() { return Value::make(1.5); } // a Float: is-a Object, not Integer (no cell lifetime)

} // namespace

TEST_CASE("dispatch selects the most specific applicable method")
{
	GenericFunction *g = get_or_create_generic(intern("describe1"));
	CHECK(add_method(g, sig({OBJ()}), 0, false, tag(1)) == AddMethod::Ok);
	CHECK(add_method(g, sig({INT()}), 0, false, tag(2)) == AddMethod::Ok);

	CHECK(untag(call1(g, Value::make_int(5))) == 2);   // Integer-specific
	CHECK(untag(call1(g, Value::make(1.5))) == 1);     // falls back to Object
	CHECK(untag(call1(g, String("x").to_value())) == 1);
}

TEST_CASE("dispatch returns null when nothing applies")
{
	GenericFunction *g = get_or_create_generic(intern("only_string"));
	add_method(g, sig({STR()}), 0, false, tag(1));
	CHECK(call1(g, String("hi").to_value()) != nullptr);
	CHECK(call1(g, Value::make_int(3)) == nullptr); // no Integer/Object method
}

TEST_CASE("two-argument dispatch")
{
	GenericFunction *g = get_or_create_generic(intern("combine2"));
	add_method(g, sig({OBJ(), OBJ()}), 0, false, tag(1));
	add_method(g, sig({INT(), INT()}), 0, false, tag(2));
	add_method(g, sig({INT(), STR()}), 0, false, tag(3));

	CHECK(untag(call2(g, Value::make_int(1), Value::make_int(2))) == 2);
	CHECK(untag(call2(g, Value::make_int(1), String("x").to_value())) == 3);
	CHECK(untag(call2(g, String("a").to_value(), Value::make_int(1))) == 1); // Object,Object
	CHECK(untag(call2(g, Value::make(1.0), Value::make(2.0))) == 1);
}

TEST_CASE("adding a method invalidates the memo (generic epoch)")
{
	GenericFunction *g = get_or_create_generic(intern("grow"));
	add_method(g, sig({OBJ()}), 0, false, tag(1));
	CHECK(untag(call1(g, Value::make_int(7))) == 1); // memoized as Object
	CHECK(untag(call1(g, Value::make_int(7))) == 1); // memo hit

	add_method(g, sig({INT()}), 0, false, tag(2));          // bumps generic epoch
	CHECK(untag(call1(g, Value::make_int(7))) == 2); // re-resolves to Integer
}

TEST_CASE("adding a class invalidates the memo (type epoch)")
{
	GenericFunction *g = get_or_create_generic(intern("type_epoch_probe"));
	add_method(g, sig({OBJ()}), 0, false, tag(1));
	CHECK(untag(call1(g, Value::make_int(1))) == 1); // memoized
	uint32_t before = type_epoch();
	add_class("EpochProbe", OBJ(), 0); // bumps type epoch
	CHECK(type_epoch() > before);
	// Still correct after the memo self-invalidates.
	CHECK(untag(call1(g, Value::make_int(1))) == 1);
}

TEST_CASE("ref-mask is uniform per generic")
{
	// Ref-ness is a uniform property of the generic, not a dispatch dimension
	// (design/references.md §4): all overloads must agree, and a reference argument
	// dispatches on its referent's type.
	GenericFunction *g = get_or_create_generic(intern("uniform_ref"));
	CHECK(add_method(g, sig({LIST()}), 0b1, false, tag(1)) == AddMethod::Ok); // fixes mask: param 0 is ref
	CHECK(add_method(g, sig({INT()}), 0b1, false, tag(2)) == AddMethod::Ok);  // agrees -> ok
	CHECK(add_method(g, sig({STR()}), 0b0, false, tag(3)) == AddMethod::RefMaskConflict); // disagrees

	List l;
	Value lv = l.to_value();
	// An open reference box standing in for the List (resolve only reads through it).
	UpvalueCell box;
	box.slot = &lv;
	Value ref = Value::make_reference(reinterpret_cast<Cell *>(&box));
	CHECK(untag(call1(g, ref)) == 1); // ref-to-List dispatches on List
	CHECK(untag(call1(g, lv)) == 1);  // a plain List selects the same (uniform) method
}

TEST_CASE("metaclass dispatch: cast(x, Float) pattern")
{
	GenericFunction *g = get_or_create_generic(intern("cast_like"));
	Class *float_meta = metaclass_of(FLT());
	Class *int_meta = metaclass_of(INT());
	add_method(g, sig({OBJ(), float_meta}), 0, false, tag(100)); // cast(x, Float)
	add_method(g, sig({OBJ(), int_meta}), 0, false, tag(200));   // cast(x, Integer)

	Value float_obj = class_object(FLT());
	Value int_obj = class_object(INT());
	CHECK(untag(call2(g, Value::make_int(3), float_obj)) == 100);
	CHECK(untag(call2(g, String("x").to_value(), int_obj)) == 200);
	// A class object we have no cast method for does not resolve.
	CHECK(call2(g, Value::make_int(3), class_object(STR())) == nullptr);
}

TEST_CASE("ambiguity is detected at method-definition time")
{
	Class *obj = OBJ();
	Class *a = add_class("AmbA", obj, 0);
	Class *b = add_class("AmbB", obj, 0);

	GenericFunction *g = get_or_create_generic(intern("ambig1"));
	CHECK(add_method(g, sig({a, obj}), 0, false, tag(1)) == AddMethod::Ok);
	// (Object, B) overlaps (A, Object) at args (A, B) and is incomparable -> ambiguous.
	CHECK(add_method(g, sig({obj, b}), 0, false, tag(2)) == AddMethod::Ambiguous);

	// Disjoint signatures never conflict (siblings at a position -> no overlap).
	GenericFunction *g2 = get_or_create_generic(intern("ambig_disjoint"));
	CHECK(add_method(g2, sig({a}), 0, false, tag(1)) == AddMethod::Ok);
	CHECK(add_method(g2, sig({b}), 0, false, tag(2)) == AddMethod::Ok); // A, B are siblings
}

TEST_CASE("a disambiguator resolves an otherwise ambiguous overload set")
{
	Class *obj = OBJ();
	Class *a = add_class("DisA", obj, 0);
	Class *b = add_class("DisB", obj, 0);

	GenericFunction *g = get_or_create_generic(intern("disambig"));
	// Register the meet (A, B) first: now (A, Object) and (Object, B) are safe.
	CHECK(add_method(g, sig({a, b}), 0, false, tag(3)) == AddMethod::Ok);
	CHECK(add_method(g, sig({a, obj}), 0, false, tag(1)) == AddMethod::Ok);
	CHECK(add_method(g, sig({obj, b}), 0, false, tag(2)) == AddMethod::Ok);
}

TEST_CASE("redefining an identical signature replaces in place")
{
	GenericFunction *g = get_or_create_generic(intern("redef"));
	add_method(g, sig({INT()}), 0, false, tag(1));
	CHECK(untag(call1(g, Value::make_int(1))) == 1);
	CHECK(add_method(g, sig({INT()}), 0, false, tag(9)) == AddMethod::Ok); // same sig
	CHECK(untag(call1(g, Value::make_int(1))) == 9);
	CHECK(g->methods.size() == 1); // replaced, not appended
}

TEST_CASE("variadic applicability and fixed-beats-variadic")
{
	GenericFunction *g = get_or_create_generic(intern("varapp"));
	CHECK(add_method(g, sig({INT()}), 0, false, tag(1)) == AddMethod::Ok); // f(x:Int)
	CHECK(addv(g, sig({INT()}), tag(2)) == AddMethod::Ok);                 // f(xs:Int...)

	CHECK(untag(callN(g, {I(5)})) == 1);        // argc 1: fixed beats variadic
	CHECK(untag(callN(g, {})) == 2);            // argc 0: only the variadic applies
	CHECK(untag(callN(g, {I(1), I(2)})) == 2);  // argc 2: variadic
	CHECK(untag(callN(g, {I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9), I(10)})) == 2);
	CHECK(callN(g, {F()}) == nullptr);          // a Float matches neither Int overload
}

TEST_CASE("among variadics, more fixed parameters wins")
{
	GenericFunction *g = get_or_create_generic(intern("varfixed"));
	CHECK(addv(g, sig({INT()}), tag(1)) == AddMethod::Ok);         // f(xs:Int...)      fixed 0
	CHECK(addv(g, sig({INT(), INT()}), tag(2)) == AddMethod::Ok);  // f(x:Int, ys:Int...) fixed 1

	CHECK(untag(callN(g, {})) == 1);            // only the fixed-0 variadic applies
	CHECK(untag(callN(g, {I(1)})) == 2);        // both apply -> more fixed wins
	CHECK(untag(callN(g, {I(1), I(2), I(3)})) == 2);
}

TEST_CASE("vararg element type participates in specificity")
{
	// Two variadics differing only in element type (Integer is-a Object): the more
	// specific element wins, and a non-Integer Object selects the Object variadic.
	GenericFunction *g = get_or_create_generic(intern("varelem"));
	CHECK(addv(g, sig({OBJ()}), tag(1)) == AddMethod::Ok); // f(xs:Object...)
	CHECK(addv(g, sig({INT()}), tag(2)) == AddMethod::Ok); // f(xs:Int...)

	CHECK(untag(callN(g, {I(1), I(2)})) == 2); // all Integers -> the Integer variadic
	CHECK(untag(callN(g, {F()})) == 1);        // a Float is Object but not Integer
	CHECK(untag(callN(g, {I(1), F()})) == 1);  // mixed -> falls back to the Object variadic
}

TEST_CASE("variadic ambiguity is detected at definition time")
{
	Class *obj = OBJ();
	Class *a = add_class("VAmbA", obj, 0);
	Class *b = add_class("VAmbB", obj, 0);
	// f(a, xs:Object...) and f(xs:A...) both apply to (A, A, …) and are incomparable
	// (A ⊄ Object at the tail vs Object ⊄ A at the head) -> needs a disambiguator.
	GenericFunction *g = get_or_create_generic(intern("varambig"));
	CHECK(addv(g, sig({a, obj}), tag(1)) == AddMethod::Ok);      // f(x:A, ys:Object...)
	CHECK(addv(g, sig({obj, a}), tag(2)) == AddMethod::Ambiguous); // f(x:Object, ys:A...)

	(void) b;
}
