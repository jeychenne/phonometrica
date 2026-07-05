// Phonometrica engine — Cell, Handle, retain/release tests.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "core/cell.hpp"
#include "core/handle.hpp"
#include "core/value.hpp"
#include "object/class.hpp"
#include "test_framework.hpp"

using namespace phonometrica;

namespace {

// A minimal cell-headed type for exercising the lifecycle machinery. Its Cell is
// the first member, as Handle<T> requires.
struct TestCell
{
	Cell header;
	int payload;
	int *sentinel;
};

int g_finalized = 0;

void test_finalize(Cell *c)
{
	auto *t = reinterpret_cast<TestCell *>(c);
	delete t->sentinel; // finalizer releases owned C++ resource (deterministic dtor)
	++g_finalized;
}

// Use a non-builtin id so we don't collide with bootstrap's primitives.
constexpr uint32_t TEST_CID = 1000;
Class g_test_class{.id = TEST_CID,
                   .name = "TestCell",
                   .base = nullptr,
                   .flags = CLASS_VALUE,
                   .instance_size = intptr_t(sizeof(TestCell)),
                   .finalize = &test_finalize};

Handle<TestCell> make_test(int payload)
{
	Cell *c = cell_alloc(TEST_CID, sizeof(TestCell));
	auto *t = reinterpret_cast<TestCell *>(c);
	t->payload = payload;
	t->sentinel = new int(payload);
	return Handle<TestCell>::adopt(t);
}

struct Fixture
{
	Fixture() { register_class(&g_test_class); }
};
Fixture g_fixture;

} // namespace

TEST_CASE("Cell header packs class id and size class")
{
	Cell c;
	c.set_header(CID_STRING, Cell::SC_FOREIGN);
	c.rc_bits = 1;
	CHECK(c.class_id() == CID_STRING);
	CHECK(c.size_class() == Cell::SC_FOREIGN);
	CHECK(c.refcount() == 1);
	CHECK(c.is_unique());
	CHECK(!c.is_shared());
	CHECK(!c.is_frozen());

	// Max class id fits in 24 bits.
	c.set_header(0x00FFFFFFu, Cell::SC_LARGE);
	CHECK(c.class_id() == 0x00FFFFFFu);
	CHECK(c.size_class() == Cell::SC_LARGE);
}

TEST_CASE("retain/release drives the refcount and finalizes at zero")
{
	g_finalized = 0;
	Cell *c = cell_alloc(TEST_CID, sizeof(TestCell));
	reinterpret_cast<TestCell *>(c)->sentinel = new int(0);
	CHECK(c->refcount() == 1);
	retain(c);
	CHECK(c->refcount() == 2);
	retain(c);
	CHECK(c->refcount() == 3);
	release(c);
	CHECK(c->refcount() == 2);
	release(c);
	CHECK(c->refcount() == 1);
	CHECK(g_finalized == 0);
	release(c); // last reference -> dispose
	CHECK(g_finalized == 1);
}

TEST_CASE("Handle manages references (copy retains, destroy releases)")
{
	g_finalized = 0;
	{
		Handle<TestCell> a = make_test(11);
		CHECK(a.use_count() == 1);
		CHECK(a.unique());
		CHECK(a->payload == 11);
		{
			Handle<TestCell> b = a; // copy retains
			CHECK(a.use_count() == 2);
			CHECK(!a.unique());
			CHECK(b->payload == 11);
		}
		CHECK(a.use_count() == 1); // b's destructor released
		CHECK(g_finalized == 0);
	}
	CHECK(g_finalized == 1); // a's destructor disposed
}

TEST_CASE("Handle move transfers ownership without touching refcount")
{
	g_finalized = 0;
	Handle<TestCell> a = make_test(7);
	CHECK(a.use_count() == 1);
	Handle<TestCell> b = std::move(a);
	CHECK(!a); // moved-from is null
	CHECK(b.use_count() == 1); // no spurious retain/release
	CHECK(b->payload == 7);
	b.reset();
	CHECK(g_finalized == 1);
}

TEST_CASE("Handle and Value interoperate through the cell")
{
	g_finalized = 0;
	{
		Handle<TestCell> h = make_test(99);
		Value v = Value::make_cell(h.cell());
		CHECK(v.is_cell());
		CHECK(v.as_cell() == h.cell());
		CHECK(reinterpret_cast<TestCell *>(v.as_cell())->payload == 99);
		// The Value is a borrowed view; Handle still owns the single reference.
		CHECK(h.use_count() == 1);
	}
	CHECK(g_finalized == 1);
}

TEST_CASE("Handle assignment balances references")
{
	g_finalized = 0;
	Handle<TestCell> a = make_test(1);
	Handle<TestCell> b = make_test(2);
	CHECK(g_finalized == 0);
	a = b; // a's old cell disposed, b's cell retained
	CHECK(g_finalized == 1);
	CHECK(a->payload == 2);
	CHECK(b.use_count() == 2);
	a.reset();
	b.reset();
	CHECK(g_finalized == 2);
}
