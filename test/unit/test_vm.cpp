// Phonometrica engine — VM (M4) acceptance tests: compile + execute scripts.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "test_framework.hpp"

#include <phon/compile/diagnostic.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/types/string.hpp>

#include <string>

using namespace phonometrica;

namespace {

Variant run(const char *src) { return do_string(src); }

int64_t run_int(const char *src)
{
	Variant v = run(src);
	return v.is_int() ? v.as_int() : static_cast<int64_t>(v.to_double());
}

double run_double(const char *src) { return run(src).to_double(); }

std::string run_str(const char *src)
{
	Variant v = run(src);
	String s = String::from_value(v.value());
	return std::string(s.data(), static_cast<size_t>(s.size()));
}

bool run_bool(const char *src) { return run(src).as_bool(); }

} // namespace

// --- arithmetic ---------------------------------------------------------------

TEST_CASE("vm: integer arithmetic")
{
	CHECK(run_int("1 + 2") == 3);
	CHECK(run_int("10 - 4 * 2") == 2);
	CHECK(run_int("2 ^ 10") == 1024 || run_double("2 ^ 10") == 1024.0);
	CHECK(run_int("17 div 5") == 3);
	CHECK(run_int("17 mod 5") == 2);
	CHECK(run_int("-(3 + 4)") == -7);
}

TEST_CASE("vm: float arithmetic and division")
{
	CHECK(run_double("1 / 2") == 0.5);
	CHECK(run_double("3.0 * 2") == 6.0);
	CHECK(run_double("1.5 + 2.5") == 4.0);
}

TEST_CASE("vm: scientific notation")
{
	CHECK(run_double("1e3") == 1000.0);
	CHECK(run_double("2.5e-1") == 0.25);
}

TEST_CASE("vm: comparison and logic")
{
	CHECK(run_bool("1 < 2") == true);
	CHECK(run_bool("2 <= 2") == true);
	CHECK(run_bool("3 > 5") == false);
	CHECK(run_bool("2 == 2.0") == true);
	CHECK(run_bool("1 != 2") == true);
	CHECK(run_bool("true and false") == false);
	CHECK(run_bool("true or false") == true);
	CHECK(run_bool("not false") == true);
	CHECK(run_int("false or 7") == 7); // and/or yield operands, not just bools
	CHECK(run_int("3 and 4") == 4);
}

// --- strings ------------------------------------------------------------------

TEST_CASE("vm: concatenation stringifies")
{
	CHECK(run_str("\"n=\" & 5") == "n=5");
	CHECK(run_str("\"a\" & \"b\" & \"c\"") == "abc");
	CHECK(run_str("\"x=\" & (1 + 2) & \"!\"") == "x=3!");
}

TEST_CASE("vm: string interpolation")
{
	CHECK(run_str("var name = \"world\"\n\"hello {name}\"") == "hello world");
	CHECK(run_str("var n = 3\n\"n squared is {n * n}\"") == "n squared is 9");
}

// --- variables and scope ------------------------------------------------------

TEST_CASE("vm: module variables")
{
	CHECK(run_int("var x = 5\nx + 1") == 6);
	CHECK(run_int("var a = 10\nvar b = 20\na + b") == 30);
	CHECK(run_int("var x = 1\nx = x + 41\nx") == 42);
}

TEST_CASE("vm: compound assignment")
{
	CHECK(run_int("var x = 10\nx += 5\nx") == 15);
	CHECK(run_int("var x = 10\nx -= 3\nx") == 7);
	CHECK(run_int("var x = 4\nx *= 4\nx") == 16);
	CHECK(run_str("var s = \"a\"\ns &= \"b\"\ns &= \"c\"\ns") == "abc");
}

// --- control flow -------------------------------------------------------------

TEST_CASE("vm: if / elsif / else")
{
	const char *src = "var x = 7\n"
	                  "var r = 0\n"
	                  "if x < 5 then r = 1 elsif x < 10 then r = 2 else r = 3 end\n"
	                  "r";
	CHECK(run_int(src) == 2);
}

TEST_CASE("vm: if-expression")
{
	CHECK(run_int("var r = if 1 < 2 then 10 else 20 end\nr") == 10);
	CHECK(run_int("var n = 0 - 5\nvar m = if n > 0 then n else 0 - n end\nm") == 5);
}

TEST_CASE("vm: while loop")
{
	CHECK(run_int("var i = 0\nvar s = 0\nwhile i < 5 do s += i\ni += 1 end\ns") == 10);
}

TEST_CASE("vm: repeat loop")
{
	CHECK(run_int("var i = 0\nrepeat i += 1 until i >= 3\ni") == 3);
}

TEST_CASE("vm: numeric for")
{
	CHECK(run_int("var t = 0\nfor i = 1 to 10 do t += i end\nt") == 55);
	CHECK(run_int("var t = 0\nfor i = 1 to 10 step 2 do t += i end\nt") == 25);
	CHECK(run_int("var t = 0\nfor i = 5 to 1 step -1 do t += i end\nt") == 15);
}

TEST_CASE("vm: break and continue")
{
	CHECK(run_int("var s = 0\nfor i = 1 to 100 do if i > 5 then break end\ns += i end\ns") == 15);
	CHECK(run_int("var s = 0\nfor i = 1 to 10 do if i mod 2 == 0 then continue end\ns += i end\ns") ==
	      25);
}

// --- functions ----------------------------------------------------------------

TEST_CASE("vm: functions and recursion")
{
	CHECK(run_int("function sq(x) return x * x end\nsq(6)") == 36);
	const char *fib = "function fib(n)\n"
	                  "  if n < 2 then return n end\n"
	                  "  return fib(n - 1) + fib(n - 2)\n"
	                  "end\n"
	                  "fib(10)";
	CHECK(run_int(fib) == 55);
}

TEST_CASE("vm: mutual recursion via hoisting")
{
	const char *src = "function is_even(n) if n == 0 then return true end\nreturn is_odd(n - 1) end\n"
	                  "function is_odd(n) if n == 0 then return false end\nreturn is_even(n - 1) end\n"
	                  "is_even(10)";
	CHECK(run_bool(src) == true);
}

TEST_CASE("vm: anonymous functions and lambdas")
{
	CHECK(run_int("var f = function(x) return x + 1 end\nf(41)") == 42);
	CHECK(run_int("var g = x -> x * 2\ng(21)") == 42);
}

// --- closures and upvalues ----------------------------------------------------

TEST_CASE("vm: closure counter")
{
	const char *src = "function make_counter()\n"
	                  "  var count = 0\n"
	                  "  function inc()\n"
	                  "    count += 1\n"
	                  "    return count\n"
	                  "  end\n"
	                  "  return inc\n"
	                  "end\n"
	                  "var c = make_counter()\n"
	                  "c()\n"
	                  "c()\n"
	                  "c()";
	CHECK(run_int(src) == 3);
}

TEST_CASE("vm: shared upvalue between closures")
{
	const char *src = "function make()\n"
	                  "  var n = 0\n"
	                  "  function get() return n end\n"
	                  "  function set(v) n = v end\n"
	                  "  return [get, set]\n"
	                  "end\n"
	                  "var pair = make()\n"
	                  "var get = pair[1]\n"
	                  "var set = pair[2]\n"
	                  "set(42)\n"
	                  "get()";
	CHECK(run_int(src) == 42);
}

// --- aggregates and indexing --------------------------------------------------

TEST_CASE("vm: list literal and indexing")
{
	CHECK(run_int("var a = [10, 20, 30]\na[2]") == 20);
	CHECK(run_int("var a = [10, 20, 30]\na[-1]") == 30);
	CHECK(run_int("var a = [1, 2, 3]\na[1] += 100\na[1]") == 101);
	CHECK(run_int("len([1, 2, 3, 4])") == 4);
}

TEST_CASE("vm: table literal and indexing")
{
	CHECK(run_int("var t = {\"a\": 1, \"b\": 2}\nt[\"b\"]") == 2);
	CHECK(run_int("var t = {}\nt[\"k\"] = 9\nt[\"k\"]") == 9);
}

TEST_CASE("vm: string indexing by grapheme")
{
	CHECK(run_str("var s = \"abc\"\ns[1]") == "a");
	CHECK(run_str("\"hello\"[5]") == "o");
	CHECK(run_int("len(\"hello\")") == 5);
}

// --- type tests ---------------------------------------------------------------

TEST_CASE("vm: is operator")
{
	CHECK(run_bool("5 is Integer") == true);
	CHECK(run_bool("5 is Float") == false);
	CHECK(run_bool("1.5 is Float") == true);
	CHECK(run_bool("\"x\" is String") == true);
	CHECK(run_bool("[1] is List") == true);
}

// --- errors -------------------------------------------------------------------

TEST_CASE("vm: runtime errors are raised")
{
	bool threw = false;
	try
	{
		run("1 div 0");
	}
	catch (const RuntimeError &)
	{
		threw = true;
	}
	CHECK(threw);
}

// --- persistent sessions (REPL / editor surface) ------------------------------

TEST_CASE("vm: Runtime persists module bindings across do_string")
{
	Runtime rt;
	rt.do_string("var x = 5");
	rt.do_string("var y = 10");
	CHECK(rt.do_string("x + y").as_int() == 15);

	// Assignment persists and resolves to the existing binding.
	rt.do_string("x = 100");
	CHECK(rt.do_string("x").as_int() == 100);

	// A function defined in one chunk is callable in a later one.
	rt.do_string("function inc(n) return n + 1 end");
	CHECK(rt.do_string("inc(41)").as_int() == 42);
}

TEST_CASE("vm: closures stored in module slots survive across chunks")
{
	// The Proto backing `counter` was compiled in an earlier chunk; the Runtime
	// keeps it alive, so the closure keeps working call after call.
	Runtime rt;
	rt.do_string("function make() var c = 0\nfunction f() c += 1\nreturn c end\nreturn f end");
	rt.do_string("var counter = make()");
	rt.do_string("counter()");
	rt.do_string("counter()");
	CHECK(rt.do_string("counter()").as_int() == 3);
}

TEST_CASE("vm: separate Runtimes have independent namespaces")
{
	Runtime a;
	Runtime b;
	a.do_string("var shared = 1");
	b.do_string("var shared = 2");
	CHECK(a.do_string("shared").as_int() == 1);
	CHECK(b.do_string("shared").as_int() == 2);
}

// --- named functions are generic methods (design §6) --------------------------

TEST_CASE("vm: top-level functions overload by arity")
{
	CHECK(run_int("function f() return 0 end\nfunction f(x) return x end\nf() + f(7)") == 7);
}

TEST_CASE("vm: top-level functions dispatch on argument type")
{
	const char *src = "function kind(x as Integer) return 1 end\n"
	                  "function kind(x as String) return 2 end\n"
	                  "kind(3) + kind(\"a\") * 10";
	CHECK(run_int(src) == 21);
}

TEST_CASE("vm: a type overload extends a builtin generic, then retracts")
{
	{
		// `len` is a builtin generic; a more-specific Boolean method overloads it.
		Runtime rt;
		CHECK(rt.do_string("function len(x as Boolean) return 99 end\nlen(true)").as_int() == 99);
	}
	// The overload was journaled to that Runtime's Isolate and retracted on
	// teardown, so a fresh run sees the builtin `len` unchanged.
	CHECK(run_int("len([1, 2, 3])") == 3);
}

TEST_CASE("vm: `local function` stays a module-private binding")
{
	CHECK(run_int("local function twice(x) return x * 2 end\ntwice(21)") == 42);
}

TEST_CASE("vm: a run's generic methods do not leak into a later run")
{
	{
		Runtime rt;
		CHECK(rt.do_string("function only_here() return 1 end\nonly_here()").as_int() == 1);
	}
	// After the Runtime is gone its journal is retracted, emptying the generic, so
	// the name resolves as undeclared again — a compile error, not a stale call.
	bool threw = false;
	try
	{
		run("only_here()");
	}
	catch (const SyntaxError &)
	{
		threw = true;
	}
	CHECK(threw);
}

// --- user-defined classes (design §5.6/§6) ------------------------------------

TEST_CASE("vm: construct an instance and read its fields")
{
	const char *src = "class P\n field x as Float = 0.0\n field y as Float = 0.0\n"
	                  " method init(x as Float, y as Float)\n this.x = x\n this.y = y\n end\n"
	                  "end\n var p = P(2.0, 5.0)\n p.x + p.y";
	CHECK(run_double(src) == 7.0);
}

TEST_CASE("vm: field defaults apply via the default constructor")
{
	CHECK(run_int("class B\n field n as Integer = 42\nend\nB().n") == 42);
	// No initializer defaults to null.
	CHECK(run("class B\n field label\nend\nB().label").is_null());
}

TEST_CASE("vm: a value class detaches on mutation (copy-on-write)")
{
	// q aliases p; mutating p must not change q.
	const char *src = "class P\n field x as Integer = 0\nend\n"
	                  "var p = P()\n var q = p\n p.x = 9\n q.x";
	CHECK(run_int(src) == 0);
}

TEST_CASE("vm: a ref class has identity semantics (no detach)")
{
	const char *src = "ref class C\n field n as Integer = 0\nend\n"
	                  "var a = C()\n var b = a\n a.n = 5\n b.n";
	CHECK(run_int(src) == 5);
}

TEST_CASE("vm: methods and free functions dispatch on user classes")
{
	const char *src = "class Dog\nend\nclass Cat\nend\n"
	                  "function speak(d as Dog) return 1 end\n"
	                  "function speak(c as Cat) return 2 end\n"
	                  "speak(Dog()) + speak(Cat()) * 10";
	CHECK(run_int(src) == 21);
}

TEST_CASE("vm: `is` and `cast` work on user classes")
{
	CHECK(run_bool("class P\nend\nP() is P"));
	CHECK(run_bool("class P\nend\nclass Q\nend\nnot (Q() is P)"));
	CHECK(run_bool("class P\nend\nvar p = P()\n(cast p as P) is P"));
}

TEST_CASE("vm: a subclass inherits base fields and is-a its base")
{
	const char *src = "class A\n field a as Integer = 0\nend\n"
	                  "class B is A\n field b as Integer = 0\nend\n"
	                  "var x = B()\n x.a = 3\n x.b = 4\n x.a + x.b";
	CHECK(run_int(src) == 7);
	CHECK(run_bool("class A\nend\nclass B is A\nend\nB() is A"));
}

TEST_CASE("vm: inherited field defaults chain at construction")
{
	const char *src = "class Base\n field a as Integer = 5\nend\n"
	                  "class Derived is Base\n field b as Integer = 9\nend\n"
	                  "var d = Derived()\n d.a + d.b";
	CHECK(run_int(src) == 14);
}

TEST_CASE("vm: constructor inheritance applies the full field layout")
{
	// Sub has no init, so Base's init runs; Sub's own default must still apply.
	const char *src = "class Base\n field a as Integer = 0\n"
	                  " method init(x as Integer)\n this.a = x\n end\nend\n"
	                  "class Sub is Base\n field b as Integer = 20\nend\n"
	                  "var s = Sub(7)\n s.a + s.b";
	CHECK(run_int(src) == 27);
}

TEST_CASE("vm: user to_string is dispatched by & and interpolation")
{
	const char *src = "class F\n field n as Integer = 0\n"
	                  " method init(n as Integer)\n this.n = n\n end\n"
	                  " method to_string() as String\n return \"F\" & this.n\n end\nend\n"
	                  "var f = F(3)\n \"<\" & f & \">\"";
	CHECK(run_str(src) == "<F3>");
	CHECK(run_str("class F\n method to_string() as String\n return \"hi\"\n end\nend\n"
	              "\"{F()}!\"") == "hi!");
}

// --- field accessors (design "Field accessors") -------------------------------

TEST_CASE("vm: a getter computes on read")
{
	const char *src = "class C\n field r as Integer = 3\n"
	                  " field sq as Integer\n get\n return this.r * this.r\n end\n end\nend\n"
	                  "C().sq";
	CHECK(run_int(src) == 9);
}

TEST_CASE("vm: a setter runs on write and can update another field")
{
	const char *src = "class C\n field r as Integer = 0\n"
	                  " field twice as Integer\n"
	                  "  get\n return this.r * 2\n end\n"
	                  "  set(v as Integer)\n this.r = v / 2\n end\n"
	                  " end\nend\n"
	                  "var c = C()\n c.twice = 10\n c.r";
	CHECK(run_int(src) == 5);
}

TEST_CASE("vm: a validated setter reaches the raw slot without recursing")
{
	const char *src = "class C\n field n as Integer = 0\n"
	                  "  set(v as Integer)\n this.n = v\n end\n end\nend\n"
	                  "var c = C()\n c.n = 42\n c.n";
	CHECK(run_int(src) == 42);
}

TEST_CASE("vm: writing a getter-only field is a runtime error")
{
	bool threw = false;
	try
	{
		run("class C\n field r as Integer = 1\n"
		    " field ro as Integer\n get\n return this.r\n end\n end\nend\n"
		    "var c = C()\n c.ro = 5");
	}
	catch (const RuntimeError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("vm: a subclass inherits its base's accessors")
{
	const char *src = "class Base\n field r as Integer = 4\n"
	                  " field dbl as Integer\n get\n return this.r * 2\n end\n end\nend\n"
	                  "class Sub is Base\nend\n"
	                  "Sub().dbl";
	CHECK(run_int(src) == 8);
}

// --- private (local) fields ---------------------------------------------------

TEST_CASE("vm: a private field is reachable through this")
{
	const char *src = "class A\n local field n as Integer = 0\n"
	                  " method init(x as Integer)\n this.n = x\n end\n"
	                  " method get() as Integer\n return this.n\n end\nend\n"
	                  "get(A(9))";
	CHECK(run_int(src) == 9);
}

TEST_CASE("vm: external access to a private field is an error")
{
	auto denied = [](const char *src) {
		bool threw = false;
		try
		{
			run(src);
		}
		catch (const RuntimeError &)
		{
			threw = true;
		}
		return threw;
	};
	CHECK(denied("class A\n local field s as Integer = 1\nend\n A().s"));
	CHECK(denied("class A\n local field s as Integer = 1\nend\n var a = A()\n a.s = 2"));
}

TEST_CASE("vm: a subclass reaches a base private field through this (protected)")
{
	const char *src = "class Base\n local field n as Integer = 5\nend\n"
	                  "class Sub is Base\n method get() as Integer\n return this.n\n end\nend\n"
	                  "get(Sub())";
	CHECK(run_int(src) == 5);
}

TEST_CASE("vm: a private field with accessors is a syntax error")
{
	bool threw = false;
	try
	{
		run("class A\n local field x as Integer\n get\n return 1\n end\n end\nend");
	}
	catch (const SyntaxError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("vm: user classes do not leak across runs")
{
	{
		Runtime rt;
		CHECK(rt.do_string("class Widget\n field v as Integer = 1\nend\nWidget().v").as_int() == 1);
	}
	// The class was module-scoped; a fresh run does not see it.
	bool threw = false;
	try
	{
		run("Widget()");
	}
	catch (const SyntaxError &)
	{
		threw = true;
	}
	CHECK(threw);
}

// --- errors: throw / try / catch / finally (design §12) -----------------------

TEST_CASE("vm: throw is caught by a matching catch clause")
{
	const char *src = "class IOError is Error\nend\n"
	                  "var r = \"\"\n"
	                  "try\n throw IOError(\"disk\")\n"
	                  "catch e as IOError\n r = e.message\n"
	                  "catch e as Error\n r = \"other\"\n end\n r";
	CHECK(run_str(src) == "disk");
}

TEST_CASE("vm: the first matching catch wins; base Error matches subclasses")
{
	const char *src = "class AErr is Error\nend\n"
	                  "var r = 0\n"
	                  "try\n throw AErr(\"x\")\n"
	                  "catch e as Error\n r = 1\n"
	                  "catch e as AErr\n r = 2\n end\n r";
	CHECK(run_int(src) == 1);
}

TEST_CASE("vm: a builtin error is catchable as Error")
{
	CHECK(run_int("var r = 0\n try\n var x = 1 div 0\n catch e as Error\n r = 1\n end\n r") == 1);
}

TEST_CASE("vm: finally runs on the normal, caught, and propagating paths")
{
	// normal + caught
	CHECK(run_str("var s = \"\"\n try\n s = s & \"a\"\n finally\n s = s & \"f\"\n end\n"
	              "try\n throw Error(\"x\")\n catch e as Error\n s = s & \"c\"\n"
	              "finally\n s = s & \"F\"\n end\n s") == "afcF");
}

TEST_CASE("vm: an unmatched inner catch runs finally then propagates")
{
	const char *src = "class A1 is Error\nend\nclass B1 is Error\nend\n"
	                  "var t = \"\"\n"
	                  "try\n"
	                  "  try\n throw B1(\"b\")\n catch e as A1\n t = t & \"A\"\n"
	                  "  finally\n t = t & \"f\"\n end\n"
	                  "catch e as Error\n t = t & \"o\"\n end\n t";
	CHECK(run_str(src) == "fo");
}

TEST_CASE("vm: an error thrown across a call frame is caught in the caller")
{
	const char *src = "function boom()\n throw Error(\"deep\")\nend\n"
	                  "var m = \"\"\n"
	                  "try\n boom()\n catch e as Error\n m = e.message\n end\n m";
	CHECK(run_str(src) == "deep");
}

TEST_CASE("vm: throw re-raises a caught error to an outer handler")
{
	const char *src = "var g = \"\"\n"
	                  "try\n"
	                  "  try\n throw Error(\"boom\")\n catch e as Error\n throw e\n end\n"
	                  "catch e as Error\n g = e.message\n end\n g";
	CHECK(run_str(src) == "boom");
}

TEST_CASE("vm: an uncaught throw reaches the embedding boundary")
{
	bool threw = false;
	try
	{
		run("throw Error(\"nope\")");
	}
	catch (const RuntimeError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("vm: finally runs when a try body returns")
{
	const char *src = "var log = \"\"\n"
	                  "function f() as Integer\n"
	                  "  try\n log = log & \"t\"\n return 7\n finally\n log = log & \"F\"\n end\n"
	                  "end\n"
	                  "var r = f()\n log & r";
	CHECK(run_str(src) == "tF7");
}

TEST_CASE("vm: nested-try return runs both finallys, innermost first")
{
	const char *src = "var log = \"\"\n"
	                  "function f() as Integer\n"
	                  "  try\n try\n return 1\n finally\n log = log & \"i\"\n end\n"
	                  "  finally\n log = log & \"o\"\n end\n"
	                  "end\n f()\n log";
	CHECK(run_str(src) == "io");
}

TEST_CASE("vm: break inside a try runs its finally and leaves no stale handler")
{
	const char *src = "var log = \"\"\n"
	                  "for i = 1 to 3 do\n"
	                  "  try\n if i == 2 then break end\n log = log & \"b\"\n"
	                  "  finally\n log = log & \"f\"\n end\n end\n"
	                  "log";
	CHECK(run_str(src) == "bff");
}

TEST_CASE("vm: throwing a non-Error is a type error")
{
	bool threw = false;
	try
	{
		run("throw 42");
	}
	catch (const RuntimeError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("vm: an error carries a backtrace captured at first raise")
{
	const char *src = "function inner()\n throw Error(\"x\")\nend\n"
	                  "function outer()\n inner()\nend\n"
	                  "var t = \"\"\n"
	                  "try\n outer()\n catch e as Error\n t = e.trace\n end\n t";
	CHECK(run_str(src) == "  at inner (line 2)\n  at outer (line 5)\n  at <module> (line 9)\n");
}

TEST_CASE("vm: re-throwing preserves the original backtrace")
{
	const char *src = "function inner()\n throw Error(\"x\")\nend\n"
	                  "var t = \"\"\n"
	                  "try\n"
	                  "  try\n inner()\n catch e as Error\n throw e\n end\n"
	                  "catch e as Error\n t = e.trace\n end\n t";
	// Origin is `inner` at line 2 and the outer call at line 7 — not the re-throw site.
	CHECK(run_str(src) == "  at inner (line 2)\n  at <module> (line 7)\n");
}

// --- iteration protocol: for … in (design §12) --------------------------------

TEST_CASE("vm: for x in list sums its elements")
{
	CHECK(run_int("var s = 0\n for x in [10, 20, 30] do s += x end\n s") == 60);
}

TEST_CASE("vm: for i, v in list binds index and value")
{
	CHECK(run_str("var r = \"\"\n for i, v in [7, 8, 9] do r = r & i & \":\" & v & \" \" end\n r") ==
	      "1:7 2:8 3:9 ");
}

TEST_CASE("vm: for k, v in table iterates pairs")
{
	// Order is unspecified, so check the aggregate.
	CHECK(run_int("var s = 0\n for k, v in {\"a\": 1, \"b\": 2, \"c\": 3} do s += v end\n s") == 6);
}

TEST_CASE("vm: for x in set iterates the members")
{
	CHECK(run_int("var s = 0\n for x in {2, 4, 6, 8} do s += x end\n s") == 20);
}

TEST_CASE("vm: break and continue work inside for … in")
{
	const char *src = "var s = 0\n"
	                  "for x in [1, 2, 3, 4, 5, 6] do\n"
	                  "  if x == 2 then continue end\n if x == 5 then break end\n s += x\n end\n s";
	CHECK(run_int(src) == 1 + 3 + 4);
}

TEST_CASE("vm: iterating a non-collection is a type error")
{
	bool threw = false;
	try
	{
		run("for x in 42 do end");
	}
	catch (const RuntimeError &)
	{
		threw = true;
	}
	CHECK(threw);
}

// --- ref parameters (design §7) -----------------------------------------------

TEST_CASE("vm: a ref parameter mutates the caller's local")
{
	const char *src = "function inc(ref x) x = x + 1 end\n"
	                  "function run() as Integer\n var n = 5\n inc(n)\n inc(n)\n return n\nend\n"
	                  "run()";
	CHECK(run_int(src) == 7);
}

TEST_CASE("vm: compound assignment through a ref parameter")
{
	const char *src = "function scale(ref x, k) x *= k end\n"
	                  "function run() as Integer\n var v = 10\n scale(v, 3)\n return v\nend\n"
	                  "run()";
	CHECK(run_int(src) == 30);
}

TEST_CASE("vm: mutating a list element through a ref detaches an alias (CoW)")
{
	const char *src = "function put(ref a, i, val) a[i] = val end\n"
	                  "function run() as Integer\n"
	                  "  var xs = [1, 2, 3]\n var ys = xs\n put(xs, 1, 99)\n"
	                  "  return xs[1] * 100 + ys[1]\nend\n"
	                  "run()";
	// xs[1] became 99; ys stayed 1 (detached).
	CHECK(run_int(src) == 99 * 100 + 1);
}

TEST_CASE("vm: a ref is forwarded through a call chain")
{
	const char *src = "function bump(ref x) x = x + 100 end\n"
	                  "function via(ref y) bump(y) end\n"
	                  "function run() as Integer\n var z = 1\n via(z)\n return z\nend\n"
	                  "run()";
	CHECK(run_int(src) == 101);
}

TEST_CASE("vm: ref-ness is uniform per generic")
{
	// Ref-ness is uniform per generic (design/references.md §4): it is not a dispatch
	// dimension, and all overloads of a name must agree on their `ref` positions.
	const char *src = "function bump(ref x as Integer) x = x + 100 end\n"
	                  "function bump(ref x as String) x = x & \"!\" end\n"
	                  "function run() as Integer\n var n = 1\n bump(n)\n return n\nend\n"
	                  "run()";
	CHECK(run_int(src) == 101);

	// Two overloads that disagree on ref-ness are rejected at definition (load time).
	bool threw = false;
	try
	{
		run("function tag(ref x) return 1 end\nfunction tag(x) return 2 end");
	}
	catch (const RuntimeError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("vm: compile errors for undeclared names")
{
	bool threw = false;
	try
	{
		run("y = 5");
	}
	catch (const SyntaxError &)
	{
		threw = true;
	}
	CHECK(threw);
}
