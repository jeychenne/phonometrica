// Phonometrica engine — typed C++ registration front end tests (M8 §11.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Exercises rt.add_function(...) end-to-end: register an ordinary C++ callable, call
// it from a script, and check the boxed result. Covers scalar/String box-unbox, a
// capturing lambda (env cell), void return, the Isolate& pass-through, multi-arg, and
// type-based overload dispatch between two C++ registrations of one name.

// Embed through the public forwarding surface (phon/*.hpp), not engine internals.
#include <phon/runtime.hpp>
#include <phon/string.hpp>
#include <phon/array.hpp>
#include <phon/list.hpp>
#include <phon/error.hpp>
// Handle/Variant/Cell have no public forwarder yet — reach into the engine directly.
#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/handle.hpp>
#include <phon/engine/core/handle_cast.hpp>
#include <phon/engine/core/variant.hpp>
#include <phon/engine/types/atom.hpp> // intern (foreign-field duplicate guard)

#include "test_framework.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace phonometrica;

namespace {

std::string str_of(const Variant &v)
{
	if (!v.is_cell())
		return {};
	String s = String::from_value(v.value());
	return std::string(s.data(), static_cast<size_t>(s.size()));
}

// A registered C++ reference class. Cell-headed, with a `phon_class` slot and a
// heap-owning member (the vector) so the finalizer's ~EmbedPoint() has real work — a leak
// would show under ASan. `g_points_alive` tracks construction/destruction balance.
int g_points_alive = 0;

// A plain C++ class — no Cell, no static. The engine boxes it when exposed to scripts.
struct EmbedPoint
{
	double x, y;
	std::vector<double> trail; // gives ~EmbedPoint() something to free

	EmbedPoint(double x_, double y_) : x(x_), y(y_), trail{x_, y_} { ++g_points_alive; }
	~EmbedPoint() { --g_points_alive; }
};

// A registered C++ class that *holds a script value* (a Variant) and therefore can be part
// of a reference cycle. Defining `gc_trace` opts it into the cycle collector: the collector
// visits the captured cell, so a garbage cycle routed through the object is reclaimed
// instead of leaking. `g_nodes_alive` proves the finalizer actually runs.
int g_nodes_alive = 0;

struct GcNode
{
	Variant child; // may hold a List/another node -> can close a cycle

	GcNode() { ++g_nodes_alive; }
	~GcNode() { --g_nodes_alive; }

	void gc_trace(void (*visit)(Cell *)) const
	{
		Value v = child.value();
		if (v.owns_cell())
			visit(v.cell_ptr());
	}

	// Cyclic-free path: the destructor is bypassed there, so mirror its bookkeeping — but
	// do NOT touch `child` (the collector already balanced that edge via gc_trace).
	void gc_free() { --g_nodes_alive; }
};

} // namespace

TEST_CASE("embed: scalar box/unbox round-trips through a script call")
{
	Runtime rt;
	rt.add_function("dbl", [](double x) { return x * 2.0; });
	rt.add_function("inc", [](int64_t n) { return n + 1; });
	rt.add_function("negate", [](bool b) { return !b; });

	Variant d = rt.do_string("dbl(2.5)");
	CHECK(d.is_double());
	CHECK(d.as_double() == 5.0);

	// An integer literal satisfies a `double` parameter (Integer is-a Real) and coerces.
	Variant di = rt.do_string("dbl(3)");
	CHECK(di.is_double());
	CHECK(di.as_double() == 6.0);

	Variant i = rt.do_string("inc(41)");
	CHECK(i.is_int());
	CHECK(i.as_int() == 42);

	Variant b = rt.do_string("negate(true)");
	CHECK(b.is_bool());
	CHECK(b.as_bool() == false);
}

TEST_CASE("embed: String parameter and return")
{
	Runtime rt;
	rt.add_function("shout", [](const String &s) {
		String r = s;
		r.append("!");
		return r;
	});
	Variant v = rt.do_string("shout(\"hi\")");
	CHECK(str_of(v) == "hi!");
}

TEST_CASE("embed: capturing lambda keeps its environment")
{
	Runtime rt;
	int offset = 10;
	rt.add_function("addoff", [offset](int64_t n) { return n + offset; });

	// A capture that owns a cell: verifies the env's destructor releases it (ASan).
	String prefix("pre-");
	rt.add_function("pfx", [prefix](const String &s) {
		String r = prefix;
		r.append(s);
		return r;
	});

	Variant a = rt.do_string("addoff(5)");
	CHECK(a.is_int());
	CHECK(a.as_int() == 15);
	CHECK(str_of(rt.do_string("pfx(\"fix\")")) == "pre-fix");
}

TEST_CASE("embed: void return yields null")
{
	Runtime rt;
	rt.add_function("noop", []() {});
	Variant v = rt.do_string("noop()");
	CHECK(v.is_null());
}

TEST_CASE("embed: a leading Isolate& is passed through, not dispatched")
{
	Runtime rt;
	// The Isolate& is not part of the signature; this is a one-argument generic.
	rt.add_function("triple", [](Isolate &iso, int64_t n) {
		(void) iso;
		return n * 3;
	});
	Variant v = rt.do_string("triple(14)");
	CHECK(v.is_int());
	CHECK(v.as_int() == 42);
}

TEST_CASE("embed: multi-argument callable")
{
	Runtime rt;
	rt.add_function("addp", [](double a, double b) { return a + b; });
	Variant v = rt.do_string("addp(1.5, 2.5)");
	CHECK(v.is_double());
	CHECK(v.as_double() == 4.0);
}

TEST_CASE("embed: two C++ registrations of one name dispatch on type")
{
	Runtime rt;
	rt.add_function("describe", [](int64_t) { return String("int"); });
	rt.add_function("describe", [](const String &) { return String("str"); });

	CHECK(str_of(rt.do_string("describe(7)")) == "int");
	CHECK(str_of(rt.do_string("describe(\"x\")")) == "str");
}

TEST_CASE("embed: a non-const T& parameter is a ref (scalar write-back)")
{
	Runtime rt;
	rt.add_function("double_it", [](int64_t &n) { n *= 2; });
	rt.add_function("halve", [](double &x) { x /= 2.0; });

	rt.do_string("var x = 21");
	rt.do_string("double_it(x)");
	Variant x = rt.do_string("x");
	CHECK(x.is_int());
	CHECK(x.as_int() == 42);

	rt.do_string("var y = 9.0");
	rt.do_string("halve(y)");
	Variant y = rt.do_string("y");
	CHECK(y.is_double());
	CHECK(y.as_double() == 4.5);
}

TEST_CASE("embed: a String& ref mutates the caller's variable")
{
	Runtime rt;
	rt.add_function("bang", [](String &s) { s.append("!"); });
	rt.do_string("var s = \"hi\"");
	rt.do_string("bang(s)");
	CHECK(str_of(rt.do_string("s")) == "hi!");
}

TEST_CASE("embed: a ref mutation preserves copy-on-write for aliases")
{
	Runtime rt;
	rt.add_function("bang", [](String &s) { s.append("!"); });
	// a and b share one String cell; mutating through the ref must copy, not clobber b.
	rt.do_string("var a = \"abc\"");
	rt.do_string("var b = a");
	rt.do_string("bang(a)");
	CHECK(str_of(rt.do_string("a")) == "abc!");
	CHECK(str_of(rt.do_string("b")) == "abc");
}

TEST_CASE("embed: a List& ref mutates in place")
{
	Runtime rt;
	rt.add_function("push", [](List &xs, int64_t v) { xs.append(Variant::from_int(v)); });
	rt.do_string("var xs = [10, 20]");
	rt.do_string("push(xs, 30)");
	Variant n = rt.do_string("len(xs)");
	CHECK(n.is_int());
	CHECK(n.as_int() == 3);
	Variant last = rt.do_string("xs[3]");
	CHECK(last.is_int());
	CHECK(last.as_int() == 30);
}

TEST_CASE("embed: a ref parameter coexists with a value return and Isolate&")
{
	Runtime rt;
	rt.add_function("bump", [](Isolate &iso, int64_t &n) {
		(void) iso;
		n += 1;
		return n * 10;
	});
	rt.do_string("var k = 4");
	Variant r = rt.do_string("bump(k)");
	CHECK(r.is_int());
	CHECK(r.as_int() == 50);
	Variant k = rt.do_string("k");
	CHECK(k.is_int());
	CHECK(k.as_int() == 5);
}

TEST_CASE("embed: a mixed ref + value parameter list")
{
	Runtime rt;
	rt.add_function("scale_add", [](double &acc, double factor) { acc = acc * factor + 1.0; });
	rt.do_string("var t = 3.0");
	rt.do_string("scale_add(t, 2.0)");
	Variant t = rt.do_string("t");
	CHECK(t.is_double());
	CHECK(t.as_double() == 7.0);
}

TEST_CASE("embed: a C++ registration coexists with a script method of the same name")
{
	Runtime rt;
	rt.add_function("area", [](double r) { return 3.0 * r * r; }); // Real overload
	rt.do_string("function area(w as Integer, h as Integer) return w * h end");

	Variant f = rt.do_string("area(2.0)");
	CHECK(f.is_double());
	CHECK(f.as_double() == 12.0);

	Variant g = rt.do_string("area(3, 4)");
	CHECK(g.is_int());
	CHECK(g.as_int() == 12);
}

TEST_CASE("embed: register a C++ reference class, construct and dispatch on it")
{
	g_points_alive = 0;
	{
		Runtime rt;
		// Registration is process-global; guard against a re-run rebinding the class.
		if (!class_of<EmbedPoint>())
			rt.add_class<EmbedPoint>("EmbedPoint", rt.get_class("Object"));
		CHECK(rt.get_class("EmbedPoint") == class_of<EmbedPoint>());

		// A factory (not named "EmbedPoint", which would be read as construction of a builtin).
		rt.add_function("make_point",
		                [](double x, double y) { return Handle<EmbedPoint>::make(x, y); });
		rt.add_function("px", [](Handle<EmbedPoint> p) { return p->x; });
		rt.add_function("norm",
		                [](Handle<EmbedPoint> p) { return std::sqrt(p->x * p->x + p->y * p->y); });
		// A method that takes another registered instance (two Handle<T> args).
		rt.add_function("same", [](Handle<EmbedPoint> a, Handle<EmbedPoint> b) { return a.get() == b.get(); });

		rt.do_string("var p = make_point(3.0, 4.0)");
		CHECK(g_points_alive == 1);

		Variant x = rt.do_string("px(p)");
		CHECK(x.is_double());
		CHECK(x.as_double() == 3.0);

		Variant n = rt.do_string("norm(p)");
		CHECK(n.is_double());
		CHECK(n.as_double() == 5.0);

		// The instance's class participates in the type system.
		CHECK(rt.do_string("p is EmbedPoint").as_bool() == true);
		CHECK(rt.do_string("p is Object").as_bool() == true);

		// Identity is preserved across the boundary (reference semantics).
		CHECK(rt.do_string("same(p, p)").as_bool() == true);
		rt.do_string("var q = make_point(3.0, 4.0)");
		CHECK(rt.do_string("same(p, q)").as_bool() == false);
		CHECK(g_points_alive == 2);
	}
	// The Runtime released its module slots on teardown; both finalizers ran.
	CHECK(g_points_alive == 0);
}

TEST_CASE("embed: a registered class passed by plain reference (const T& / T&)")
{
	Runtime rt;
	if (!class_of<EmbedPoint>())
		rt.add_class<EmbedPoint>("EmbedPoint", rt.get_class("Object"));
	rt.add_function("make_point", [](double x, double y) { return Handle<EmbedPoint>::make(x, y); });

	// const T& — read-only access to the shared object, no copy.
	rt.add_function("gx", [](const EmbedPoint &p) { return p.x; });
	rt.add_function("gy", [](const EmbedPoint &p) { return p.y; });
	// T& — mutate the shared object in place (identity semantics, not write-back).
	rt.add_function("shift_x", [](EmbedPoint &p, double dx) { p.x += dx; });

	rt.do_string("var p = make_point(3.0, 4.0)");
	CHECK(rt.do_string("gx(p)").as_double() == 3.0);
	CHECK(rt.do_string("gy(p)").as_double() == 4.0);

	// Mutating through a T& is visible on the same object afterwards.
	rt.do_string("shift_x(p, 10.0)");
	CHECK(rt.do_string("gx(p)").as_double() == 13.0);

	// An alias sees the mutation too (it is the *same* object, not a copy).
	rt.do_string("var q = p");
	rt.do_string("shift_x(q, 100.0)");
	CHECK(rt.do_string("gx(p)").as_double() == 113.0);
}

TEST_CASE("embed: Variant::to<T> extracts typed C++ values")
{
	Runtime rt;
	CHECK(rt.do_string("2.5").to<double>() == 2.5);
	CHECK(rt.do_string("7").to<int64_t>() == 7);
	CHECK(rt.do_string("7").to<double>() == 7.0); // an Integer widens to double
	CHECK(rt.do_string("true").to<bool>() == true);

	String s = rt.do_string("\"hi\"").to<String>();
	CHECK(std::string(s.data(), static_cast<size_t>(s.size())) == "hi");
}

TEST_CASE("embed: Variant::to<T> throws on a type mismatch")
{
	Runtime rt;
	Variant s = rt.do_string("\"nope\"");
	bool threw = false;
	try
	{
		(void) s.to<int64_t>();
	}
	catch (const std::runtime_error &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("embed: Variant::make round-trips through to<T>")
{
	CHECK(Variant::make(2.5).to<double>() == 2.5);
	CHECK(Variant::make<int64_t>(42).to<int64_t>() == 42);
	CHECK(Variant::make(true).to<bool>() == true);
	Variant v = Variant::make(String("xyz"));
	String s = v.to<String>();
	CHECK(std::string(s.data(), static_cast<size_t>(s.size())) == "xyz");
}

TEST_CASE("embed: NumArray view from C++ (size/dim/data)")
{
	Runtime rt;
	NumArray a = rt.do_string("@[1.0, 2.0, 3.0]").to<NumArray>();
	CHECK(a.size() == 3);
	CHECK(a.dim(0) == 3);
	const double *d = a.data();
	CHECK(d[0] == 1.0);
	CHECK(d[2] == 3.0);
}

TEST_CASE("embed: a foreign class with gc_trace lets the collector reclaim a cycle")
{
	g_nodes_alive = 0;
	{
		Runtime rt;
		// Registration is process-global; guard against a re-run rebinding the class.
		if (!class_of<GcNode>())
			rt.add_class<GcNode>("GcNode", rt.get_class("Object"));
		rt.add_function("make_node", [] { return Handle<GcNode>::make(); });
		rt.add_function("set_child", [](Handle<GcNode> n, Variant v) { n->child = v; });

		// Close a cycle: the node holds the list, the list holds the node.
		rt.do_string("var n = make_node()\n"
		             "var lst = [n]\n"
		             "set_child(n, lst)");
		CHECK(g_nodes_alive == 1);

		// Drop both external references; the node and list now reference only each other.
		rt.do_string("n = null\nlst = null");
		// The cycle is unreachable. Reclaiming it requires the collector to see the node's
		// captured cell through gc_trace — without the trace hook this leaks.
		rt.do_string("collect_garbage()");
		CHECK(g_nodes_alive == 0);
	}
	CHECK(g_nodes_alive == 0);
}

TEST_CASE("embed: a foreign class without gc_trace is a GC leaf (acyclic)")
{
	Runtime rt;
	if (!class_of<EmbedPoint>())
		rt.add_class<EmbedPoint>("EmbedPoint", rt.get_class("Object"));
	// EmbedPoint declares no gc_trace, so its class is marked acyclic (born GREEN): it can
	// never be buffered as a cycle candidate. The traceable GcNode must NOT be acyclic.
	CHECK((class_of<EmbedPoint>()->flags & CLASS_ACYCLIC) != 0);
	if (!class_of<GcNode>())
		rt.add_class<GcNode>("GcNode", rt.get_class("Object"));
	CHECK((class_of<GcNode>()->flags & CLASS_ACYCLIC) == 0);
	CHECK(class_of<GcNode>()->trace != nullptr);
}

// A foreign subclass for the field-inheritance check (add_field, Phonometrica step 4b):
// fields registered on the base resolve on a derived instance via the base-chain lookup.
struct EmbedPoint3 : EmbedPoint
{
	double z;
	EmbedPoint3(double x_, double y_, double z_) : EmbedPoint(x_, y_), z(z_) {}
};

TEST_CASE("embed: read-only fields on a registered class route through native getters")
{
	Runtime rt;
	if (!class_of<EmbedPoint>())
		rt.add_class<EmbedPoint>("EmbedPoint", rt.get_class("Object"));
	rt.add_function("make_point",
	                [](double x, double y) { return Handle<EmbedPoint>::make(x, y); });
	rt.add_field<EmbedPoint>("x", [](const EmbedPoint &p) { return p.x; });
	rt.add_field<EmbedPoint>("y", [](const EmbedPoint &p) { return p.y; });
	// A computed field (no stored member), and one built from a Handle parameter.
	rt.add_field<EmbedPoint>("norm2", [](const EmbedPoint &p) { return p.x * p.x + p.y * p.y; });
	rt.add_field<EmbedPoint>("tag", [](Handle<EmbedPoint>) { return String("pt"); });

	rt.do_string("var p = make_point(3.0, 4.0)");
	CHECK(rt.do_string("p.x").as_double() == 3.0);
	CHECK(rt.do_string("p.y").as_double() == 4.0);
	CHECK(rt.do_string("p.norm2").as_double() == 25.0);
	CHECK(rt.do_string("p.tag").to<String>() == String("pt"));
	// Field reads compose in expressions and through aliases.
	CHECK(rt.do_string("var q = p\nq.x + q.norm2").as_double() == 28.0);

	// An unknown field raises a catchable Name error.
	Variant e1 = rt.do_string("var r1 = \"\"\n"
	                          "try\n var v = p.nope\ncatch e as Error\n r1 = e.message\nend\n"
	                          "r1");
	CHECK(e1.to<String>().find("has no field") != 0);
	// Writing a foreign field raises read-only, catchably.
	Variant e2 = rt.do_string("var r2 = \"\"\n"
	                          "try\n p.x = 9.0\ncatch e as Error\n r2 = e.message\nend\n"
	                          "r2");
	CHECK(e2.to<String>().find("read-only") != 0);
	// The failed write left the object untouched.
	CHECK(rt.do_string("p.x").as_double() == 3.0);

	// A duplicate field registration is an embedding error, surfaced eagerly.
	bool threw = false;
	try
	{
		rt.add_field<EmbedPoint>("x", [](const EmbedPoint &p) { return p.x; });
	}
	catch (std::exception &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("embed: a foreign subclass inherits its base's fields by chain lookup")
{
	Runtime rt;
	if (!class_of<EmbedPoint>())
		rt.add_class<EmbedPoint>("EmbedPoint", rt.get_class("Object"));
	if (!class_of<EmbedPoint3>())
		rt.add_class<EmbedPoint3>("EmbedPoint3", rt.get_class("EmbedPoint"));
	rt.add_function("make_point3", [](double x, double y, double z) {
		return Handle<EmbedPoint3>::make(x, y, z);
	});
	// `x`/`y` may already be registered on the base by the previous case (process-global
	// registration); make sure they exist without tripping the duplicate check.
	if (!find_foreign_field(class_of<EmbedPoint>(), intern("x")))
		rt.add_field<EmbedPoint>("x", [](const EmbedPoint &p) { return p.x; });
	rt.add_field<EmbedPoint3>("z", [](const EmbedPoint3 &p) { return p.z; });

	rt.do_string("var p3 = make_point3(1.0, 2.0, 7.0)");
	// Own field, and a base field resolved through the chain (the base getter receives
	// the derived cell; the boxed payload upcasts under single non-virtual inheritance).
	CHECK(rt.do_string("p3.z").as_double() == 7.0);
	CHECK(rt.do_string("p3.x").as_double() == 1.0);
	// The derived class is still a subtype for dispatch/`is`.
	CHECK(rt.do_string("p3 is EmbedPoint").as_bool() == true);
}

// E4 / gap G5: explicit Handle<Derived> -> Handle<Base> upcast (implicit converting
// ctor) and the checked handle_cast<Derived>(Handle<Base>) downcast, plus a
// container of base handles holding derived instances — the primitives roadmap A2
// swaps the old unchecked recast<T> onto.
TEST_CASE("embed: polymorphic handle upcast, checked downcast, and base-handle containers")
{
	Runtime rt;
	if (!class_of<EmbedPoint>())
		rt.add_class<EmbedPoint>("EmbedPoint", rt.get_class("Object"));
	if (!class_of<EmbedPoint3>())
		rt.add_class<EmbedPoint3>("EmbedPoint3", rt.get_class("EmbedPoint"));

	int before = g_points_alive;
	{
		// Implicit upcast: a Handle<EmbedPoint3> flows into a Handle<EmbedPoint> and
		// keeps pointing at the same box (same cell, one shared reference).
		Handle<EmbedPoint3> d = Handle<EmbedPoint3>::make(1.0, 2.0, 7.0);
		CHECK(d.use_count() == 1);
		Handle<EmbedPoint> b = d; // converting ctor
		CHECK(b.cell() == d.cell());
		CHECK(b.use_count() == 2);
		CHECK(b->x == 1.0);              // base subobject reachable through the base handle
		CHECK(b.cell()->class_id() == class_of<EmbedPoint3>()->id); // dynamic class preserved

		// handle_cast upcast agrees with the converting ctor.
		Handle<EmbedPoint> b2 = handle_cast<EmbedPoint>(d);
		CHECK(b2.cell() == d.cell());

		// Checked downcast succeeds: the box really is an EmbedPoint3.
		Handle<EmbedPoint3> back = handle_cast<EmbedPoint3>(b);
		CHECK(back.get() != nullptr);
		CHECK(back.cell() == d.cell());
		CHECK(back->z == 7.0);

		// Checked downcast fails on a box whose dynamic class is only the base: an
		// empty Handle, not a mistyped one (the old recast<T> would have lied).
		Handle<EmbedPoint> plain = Handle<EmbedPoint>::make(5.0, 6.0);
		Handle<EmbedPoint3> bad = handle_cast<EmbedPoint3>(plain);
		CHECK(bad.get() == nullptr);

		// A null handle casts to a null handle both ways.
		Handle<EmbedPoint3> nd;
		CHECK(handle_cast<EmbedPoint>(nd).get() == nullptr);
		Handle<EmbedPoint> nb;
		CHECK(handle_cast<EmbedPoint3>(nb).get() == nullptr);

		// A container typed on the base holds derived instances by upcast; each
		// element still reports its dynamic class, and a base-typed C++ view reads
		// the shared payload.
		Array<Handle<EmbedPoint>> elems;
		elems.append(Handle<EmbedPoint3>::make(10.0, 0.0, 1.0));
		elems.append(plain);
		CHECK(elems.size() == 2);
		CHECK(elems[0].cell()->class_id() == class_of<EmbedPoint3>()->id);
		CHECK(elems[1].cell()->class_id() == class_of<EmbedPoint>()->id);
		CHECK(elems[0]->x == 10.0);
		// Only the first element downcasts to EmbedPoint3.
		CHECK(handle_cast<EmbedPoint3>(elems[0]).get() != nullptr);
		CHECK(handle_cast<EmbedPoint3>(elems[1]).get() == nullptr);
	}
	// Every retained reference was balanced — no leak from the extra handles.
	CHECK(g_points_alive == before);
}

// E1 / gap G2: call a script-defined function from C++ (Project::emit_signal's shape).
TEST_CASE("embed: Runtime::call invokes a script function from C++")
{
	Runtime rt;
	if (!class_of<EmbedPoint>())
		rt.add_class<EmbedPoint>("EmbedPoint", rt.get_class("Object"));
	if (!find_foreign_field(class_of<EmbedPoint>(), intern("x")))
		rt.add_field<EmbedPoint>("x", [](const EmbedPoint &p) { return p.x; });

	// A named script function is a first-class value: fetch it and call it from C++.
	rt.do_string("function twice(n)\n return n * 2\nend");
	Variant twice = rt.get_function("twice");
	CHECK(!twice.is_null());
	Variant a0 = Variant::from_int(21);
	CHECK(rt.call(twice, &a0, 1).as_int() == 42);

	// The gate case: pass a boxed foreign-class instance across the boundary — the script
	// reads a registered field off the C++ object we hand it.
	rt.do_string("function px_plus(p, d)\n return p.x + d\nend");
	Handle<EmbedPoint> p = Handle<EmbedPoint>::make(3.0, 4.0);
	Variant args[2] = {Variant::make(p), Variant::from_double(1.5)};
	CHECK(rt.call(rt.get_function("px_plus"), args, 2).as_double() == 4.5);

	// get_function on an undefined name is null; calling a non-callable throws.
	CHECK(rt.get_function("no_such_fn").is_null());
	bool threw = false;
	try
	{
		Variant n = Variant::from_int(5);
		rt.call(n, &n, 1);
	}
	catch (RuntimeError &)
	{
		threw = true;
	}
	CHECK(threw);

	// An uncaught script error propagates as RuntimeError and leaves the session usable.
	rt.do_string("function boom()\n return 1 div 0\nend");
	bool threw2 = false;
	try
	{
		rt.call(rt.get_function("boom"));
	}
	catch (RuntimeError &)
	{
		threw2 = true;
	}
	CHECK(threw2);
	CHECK(rt.do_string("1 + 1").as_int() == 2);

	// A re-entrant call: a native, invoked mid-run, calls back into a script function
	// while a frame is live (the vm_call path inside call_from_host).
	rt.add_function("reenter", [&rt](int64_t n) {
		Variant arg = Variant::from_int(n);
		return rt.call(rt.get_function("twice"), &arg, 1).as_int();
	});
	CHECK(rt.do_string("reenter(20) + 1").as_int() == 41);

	// get_global is the inverse of add_global (and reads null for an absent name).
	rt.add_global("inj", Variant::from_int(99));
	CHECK(rt.get_global("inj").as_int() == 99);
	CHECK(rt.get_global("nope").is_null());
}

// E3 / gap G4: redirectable print / error / clear-output hooks.
TEST_CASE("embed: output hooks redirect print, error output, and clear")
{
	Runtime rt;
	std::string out;
	rt.set_output_hook([&](std::string_view s) { out.append(s); });

	rt.do_string("print(\"hello\")");
	CHECK(out == "hello\n");
	out.clear();
	rt.do_string("print(1, 2, 3)"); // print joins with a space and adds a newline
	CHECK(out == "1 2 3\n");

	// Host-side output funnels through the same sink.
	out.clear();
	rt.print("direct");
	CHECK(out == "direct");

	// Error output and clear route to their own hooks.
	std::string err;
	bool cleared = false;
	rt.set_error_output_hook([&](std::string_view s) { err.append(s); });
	rt.set_clear_output_hook([&] { cleared = true; });
	rt.print_error("oops");
	rt.clear_output();
	CHECK(err == "oops");
	CHECK(cleared);

	// Clearing a hook restores the default (no capture): the next print does not reach
	// our buffer (it goes to stdout).
	rt.set_output_hook(nullptr);
	out.clear();
	rt.print("to stdout");
	CHECK(out.empty());
}

// E2 / gap G3: Table dot-sugar — the `phon` namespace shape (callable members +
// an assignable field), with dot reading/writing string keys and a missing member
// raising [Key error] while indexing stays lenient.
TEST_CASE("embed: Table dot-sugar reads and writes string keys")
{
	Runtime rt;
	CHECK(rt.do_string("var t = {\"a\": 1}\nt.a").as_int() == 1);          // dot read
	CHECK(rt.do_string("var t = {}\nt.b = 42\nt[\"b\"]").as_int() == 42);  // dot write == index set
	CHECK(rt.do_string("var t = {\"f\": function() return 7 end}\nt.f()").as_int() == 7); // member call
	CHECK(rt.do_string("var n = {\"p\": {\"x\": 5}}\nn.p.x").as_int() == 5);              // nested read

	// A missing member: dot raises [Key error]; indexing the same key returns null.
	CHECK(rt.do_string("var t = {\"a\": 1}\nt[\"nope\"] == null").as_bool());
	bool threw = false;
	try
	{
		rt.do_string("var t = {\"a\": 1}\nt.nope");
	}
	catch (RuntimeError &e)
	{
		threw = true;
		CHECK(std::string(e.what()).find("Key error") != std::string::npos);
	}
	CHECK(threw);

	// The injected-namespace shape: a global table with a callable member and an
	// assignable field (top-level field writes on a global table persist).
	rt.add_global("phon",
	              rt.do_string("{\"get_version\": function() return \"1.5\" end, \"settings\": {}}"));
	CHECK(rt.do_string("phon.get_version()").to<String>() == String("1.5"));
	rt.do_string("phon.settings = {\"x\": 3}");
	CHECK(rt.do_string("phon.settings[\"x\"]").as_int() == 3);
}

// E5 / G6c: an uncaught script error is catchable through a std::exception handler
// (RuntimeError now derives from std::exception), so an embedder's catch-all works.
TEST_CASE("embed: an uncaught script error is catchable as std::exception")
{
	Runtime rt;
	bool caught = false;
	try
	{
		rt.do_string("1 div 0"); // raises [Math error]
	}
	catch (const std::exception &e)
	{
		caught = true;
		CHECK(std::string(e.what()).find("Math error") != std::string::npos);
	}
	CHECK(caught);
}

TEST_CASE("embed: a caught RuntimeError carries the structured backtrace")
{
	Runtime rt;
	bool caught = false;
	try
	{
		// Two script frames above the module chunk, so the trace has a shape
		// worth asserting: inner (throw site) -> outer -> <module>.
		rt.do_string("function inner()\n"
		             "    throw Error(\"boom\")\n"
		             "end\n"
		             "function outer()\n"
		             "    inner()\n"
		             "end\n"
		             "outer()\n");
	}
	catch (const RuntimeError &e)
	{
		caught = true;
		CHECK(std::string(e.what()).find("boom") != std::string::npos);
		REQUIRE(e.frames.size() == 3);
		// Innermost first; the throw site is line 2.
		CHECK(e.frames[0].function == "inner");
		CHECK(e.frames[0].line == 2);
		CHECK(e.frames[1].function == "outer");
		CHECK(e.frames[1].line == 5);
		CHECK(e.frames[2].function == "<module>");
		CHECK(e.frames[2].line == 7);
		// do_string chunks have no source file.
		CHECK(e.frames[0].file.empty());
		// The engine dropped the in-flight error value at the boundary.
		CHECK(e.error.is_null());
	}
	CHECK(caught);
}

TEST_CASE("embed: do_string with an associated source path")
{
	Runtime rt;
	// get_script_path() reports the attributed file, not "<string>".
	auto v = rt.do_string(String("get_script_path()"), String("/tmp/phon_embed/buffer.phon"));
	CHECK(v.to<String>() == "/tmp/phon_embed/buffer.phon");

	// Error backtraces carry the file too.
	bool caught = false;
	try
	{
		rt.do_string(String("function ef_boom()\n    throw Error(\"efb\")\nend\nef_boom()\n"),
		             String("/tmp/phon_embed/buffer.phon"));
	}
	catch (const RuntimeError &e)
	{
		caught = true;
		REQUIRE(e.frames.size() >= 1);
		CHECK(e.frames[0].function == "ef_boom");
		CHECK(e.frames[0].file == "/tmp/phon_embed/buffer.phon");
	}
	CHECK(caught);

	// An empty path degrades to plain do_string.
	auto w = rt.do_string(String("1 + 1"), String());
	CHECK(w.to<int64_t>() == 2);
}

TEST_CASE("embed: Runtime::disassemble compiles against the session without running")
{
	Runtime rt;
	rt.add_global("dis_x", Variant::make<int64_t>(5));

	// Compiles because dis_x is a session global; nothing runs.
	auto listing = rt.disassemble(String("var dis_probe = dis_x + 1\nprint(dis_probe)\n"));
	CHECK(listing.find("proto #0") != std::string::npos);
	CHECK(listing.find("ADD") != std::string::npos);

	// The chunk did NOT run: dis_probe's slot exists (compile-time binding)
	// but holds null — had the chunk executed it would be 6.
	auto probe = rt.do_string(String("dis_probe"));
	CHECK(probe.is_null());

	// The session is still usable.
	auto v = rt.do_string(String("dis_x + 2"));
	CHECK(v.to<int64_t>() == 7);
}

TEST_CASE("embed: host-side release of a buffered cycle candidate parks it")
{
	{
		Runtime rt;
		// A Table global whose cell becomes a cycle candidate: the in-script
		// copy's release decrements the refcount with the collector current,
		// buffering the cell as a possible root.
		rt.add_global("cc_cand", rt.do_string(String("{ \"xs\": [1, 2, 3] }")));
		rt.do_string(String("var a = cc_cand\na = null"));
		// Host-side overwrite drops the LAST reference with no collector current
		// (add_global runs outside any engine run). The old fallback freed the
		// cell here while the collector's candidate buffer still pointed at it —
		// a use-after-free in the final collection at ~Runtime. The fix parks it.
		rt.add_global("cc_cand", Variant::make(Table()));
		rt.do_string(String("assert(len(cc_cand) == 0)")); // session still sane
	}
	// ~Runtime ran its final collection over the parked cell; surviving to this
	// line (cleanly under ASan) is the regression check.
	CHECK(true);
}
