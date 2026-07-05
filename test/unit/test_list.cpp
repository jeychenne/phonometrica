// Phonometrica engine — List tests (CoW, ownership, ops).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/types/list.hpp>
#include <phon/types/string.hpp>
#include "test_framework.hpp"

using namespace phonometrica;

TEST_CASE("List construction and access")
{
	List l;
	CHECK(l.empty());
	l.append(Variant::from_int(10));
	l.append(Variant::from_int(20));
	l.append(Variant::from_int(30));
	CHECK(l.size() == 3);
	CHECK(l.get(1).as_int() == 10);
	CHECK(l.get(3).as_int() == 30);
	CHECK(l.get(-1).as_int() == 30); // negative index
	CHECK(l.get(-3).as_int() == 10);
}

TEST_CASE("List preallocated with nulls")
{
	List l(3);
	CHECK(l.size() == 3);
	CHECK(l.get(1).is_null());
	l.set(2, Variant::from_double(2.5));
	CHECK(l.get(2).as_double() == 2.5);
	CHECK(l.get(1).is_null());
}

TEST_CASE("List initializer list")
{
	List l{Variant::from_int(1), Variant::from_double(2.0), Variant::from_bool(true)};
	CHECK(l.size() == 3);
	CHECK(l.get(1).as_int() == 1);
	CHECK(l.get(2).as_double() == 2.0);
	CHECK(l.get(3).as_bool());
}

TEST_CASE("List insert / remove / pop / prepend")
{
	List l{Variant::from_int(1), Variant::from_int(2), Variant::from_int(3)};
	l.insert(2, Variant::from_int(99)); // before position 2
	CHECK(l.size() == 4);
	CHECK(l.get(2).as_int() == 99);
	CHECK(l.get(3).as_int() == 2);
	l.prepend(Variant::from_int(0));
	CHECK(l.get(1).as_int() == 0);
	l.remove_at(1);
	CHECK(l.get(1).as_int() == 1);
	Variant last = l.pop();
	CHECK(last.as_int() == 3);
	CHECK(l.size() == 3);
}

TEST_CASE("List copy-on-write value semantics")
{
	List a{Variant::from_int(1), Variant::from_int(2)};
	List b = a;
	CHECK(a.use_count() == 2);
	CHECK(a.cell() == b.cell()); // shared

	b.append(Variant::from_int(3)); // triggers CoW
	CHECK(a.size() == 2);
	CHECK(b.size() == 3);
	CHECK(a.use_count() == 1);

	// Mutating an element also copies.
	List c = a;
	c.set(1, Variant::from_int(99));
	CHECK(a.get(1).as_int() == 1);
	CHECK(c.get(1).as_int() == 99);
}

TEST_CASE("List holds cell values with correct ownership")
{
	List l;
	{
		String s("shared string");
		l.append(Variant(s.to_value())); // list retains the string cell
		CHECK(s.use_count() == 2);       // s + list slot
	}
	// s destroyed; list still owns its reference.
	Variant got = l.get(1);
	String back = String::from_value(got.value());
	CHECK(back == "shared string");
}

TEST_CASE("List equality is structural")
{
	List a{Variant::from_int(1), Variant::from_int(2)};
	List b{Variant::from_int(1), Variant::from_int(2)};
	List c{Variant::from_int(1), Variant::from_int(3)};
	CHECK(a == b);
	CHECK(a != c);

	// Structural equality reaches into String elements.
	List sa;
	sa.append(Variant(String("x").to_value()));
	List sb;
	sb.append(Variant(String("x").to_value()));
	CHECK(sa == sb); // distinct String cells, equal content
}

TEST_CASE("List index_of / contains use value equality")
{
	List l{Variant::from_int(5), Variant::from_int(10)};
	l.append(Variant(String("hello").to_value()));
	CHECK(l.index_of(Variant::from_int(10)) == 2);
	CHECK(l.contains(Variant(String("hello").to_value()))); // equal by content
	CHECK(!l.contains(Variant::from_int(999)));
}

TEST_CASE("List ref() gives in-place mutable access")
{
	List l{Variant::from_int(1), Variant::from_int(2)};
	l.ref(1) = Variant::from_int(42);
	CHECK(l.get(1).as_int() == 42);
}

TEST_CASE("List clear releases elements")
{
	List l;
	String s("tracked");
	l.append(Variant(s.to_value()));
	CHECK(s.use_count() == 2);
	l.clear();
	CHECK(l.empty());
	CHECK(s.use_count() == 1); // list dropped its reference
}
