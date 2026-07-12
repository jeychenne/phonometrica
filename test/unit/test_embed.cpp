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
#include <phon/engine/core/variant.hpp>

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

TEST_CASE("embed: Array view from C++ (size/dim/data)")
{
	Runtime rt;
	Array a = rt.do_string("@[1.0, 2.0, 3.0]").to<Array>();
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
