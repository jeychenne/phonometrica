// Phonometrica engine — modules & imports (design §11). Copyright (C) 2019-2026
// Julien Eychenne. GPLv3.
//
// A module is a `.phon` file; `import M` compiles+runs it once and makes its public
// functions callable bare (they join the global dispatch table). Fixtures live in
// test/scripts/modules/.

#include "test_framework.hpp"

#include <phon/engine/compile/diagnostic.hpp>
#include <phon/engine/runtime/runtime.hpp>
#include <phon/engine/types/string.hpp>

#include <string>

using namespace phonometrica;

namespace {

const std::string MODDIR = std::string(PHON_SCRIPTS_DIR) + "/modules";

TEST_CASE("modules: import M makes M's public functions callable bare")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	Variant r = rt.do_string("import greeting\ngreet(\"world\")");
	CHECK(String::from_value(r.value()) == "Hello, world!");
}

TEST_CASE("modules: a `local` function is not exported to importers")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try
	{
		rt.do_string("import greeting\nsecret()");
	}
	catch (const SyntaxError &)
	{
		threw = true; // `secret` is `local` -> invisible across the module boundary
	}
	CHECK(threw);
}

TEST_CASE("modules: multiple imports each contribute their functions")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	Variant r = rt.do_string("import greeting\nimport mathx\ntriple(7)");
	CHECK(r.value().as_int() == 21);
}

TEST_CASE("modules: an imported module's top-level runs before the main chunk")
{
	// mathx's triple is only callable because mathx's top-level (its DEFMETHOD) ran first.
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	Variant r = rt.do_string("import mathx\ntriple(triple(2))");
	CHECK(r.value().as_int() == 18); // triple(triple(2)) = triple(6) = 18
}

TEST_CASE("modules: importing an unknown module is a compile error")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try
	{
		rt.do_string("import no_such_module_xyz");
	}
	catch (const SyntaxError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("modules: qualified access M.x reads a module's public var/const")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import config\nconfig.MAX_ITEMS").value().as_int() == 100);
	CHECK(String::from_value(rt.do_string("import config\nconfig.version").value()) == "1.0");
}

TEST_CASE("modules: qualified access to a `local` member is a compile error")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try
	{
		rt.do_string("import config\nconfig.secret_key");
	}
	catch (const SyntaxError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("modules: qualified access to a non-member is a compile error")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try
	{
		rt.do_string("import config\nconfig.nope");
	}
	catch (const SyntaxError &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("modules: a cached module is not re-run on a second import")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	rt.do_string("import mathx\ntriple(1)");
	// A second chunk imports the same module; it must resolve without re-running or error.
	Variant r = rt.do_string("import mathx\ntriple(5)");
	CHECK(r.value().as_int() == 15);
}

// --- Stage 3: `import M as A` and chained `import M1, M2` --------------------

TEST_CASE("modules: import M as A binds the alias for qualified access")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import config as cfg\ncfg.MAX_ITEMS").value().as_int() == 100);
	// Functions remain flat-global regardless of the alias (design §11).
	CHECK(rt.do_string("import greeting as g\ngreet(\"x\")").value().is_cell());
}

TEST_CASE("modules: the original name is not bound when `as` renames it")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try { rt.do_string("import config as cfg\nconfig.MAX_ITEMS"); }
	catch (const SyntaxError &) { threw = true; }
	CHECK(threw);
}

TEST_CASE("modules: chained import M1, M2 loads both")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	Variant r = rt.do_string("import greeting, mathx\ntriple(7)");
	CHECK(r.value().as_int() == 21);
}

TEST_CASE("modules: chained import with mixed `as` and qualified access")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	Variant r = rt.do_string("import mathx, config as cfg\ntriple(cfg.MAX_ITEMS)");
	CHECK(r.value().as_int() == 300);
}

// --- Stage 4: `import M for X, Y` and `for *` -------------------------------

TEST_CASE("modules: `for X` brings a public const into scope bare")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import config for MAX_ITEMS\nMAX_ITEMS").value().as_int() == 100);
}

TEST_CASE("modules: `for X as Z` renames the imported binding")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import config for MAX_ITEMS as cap\ncap + 1").value().as_int() == 101);
	// The original name is not bound under a rename.
	bool threw = false;
	try { rt.do_string("import config for MAX_ITEMS as cap\nMAX_ITEMS"); }
	catch (const SyntaxError &) { threw = true; }
	CHECK(threw);
}

TEST_CASE("modules: `for X` on a private member is a compile error")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try { rt.do_string("import config for secret_key\nsecret_key"); }
	catch (const SyntaxError &) { threw = true; }
	CHECK(threw);
}

TEST_CASE("modules: `for *` brings every public name into scope bare")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import config for *\nMAX_ITEMS").value().as_int() == 100);
	CHECK(String::from_value(rt.do_string("import config for *\nversion").value()) == "1.0");
}

// --- Stage 5: imported classes in `is` and type annotations -----------------

TEST_CASE("modules: `x is M.C` type-tests against an imported class")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import geometry\norigin() is geometry.Point").value().as_bool());
}

TEST_CASE("modules: `for C` makes the bare class name usable in `is`")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import geometry for Point, origin\norigin() is Point").value().as_bool());
}

TEST_CASE("modules: an imported class works as a parameter type annotation")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	const char *code =
	    "import geometry for Point, make_point\n"
	    "function tag(p as Point) as Float\n"
	    "    return p.x\n"
	    "end\n"
	    "tag(make_point(3.0, 4.0))";
	CHECK(rt.do_string(code).value().as_double() == 3.0);
}

TEST_CASE("modules: a qualified class annotation `as M.C` resolves")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	const char *code =
	    "import geometry\n"
	    "function tag(p as geometry.Point) as Float\n"
	    "    return p.y\n"
	    "end\n"
	    "tag(make_point(3.0, 4.0))";
	CHECK(rt.do_string(code).value().as_double() == 4.0);
}

TEST_CASE("modules: an imported class can be constructed bare (via `for`)")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	// Point has no field defaults; constructing it and running its (inherited) init path
	// works across the module boundary now that construction needs no local ClassDecl.
	const char *code =
	    "import geometry for Point\n"
	    "var p = Point()\n"
	    "p.x = 3.0\n"
	    "p.y = 4.0\n"
	    "p.x + p.y";
	CHECK(rt.do_string(code).value().as_double() == 7.0);
}

TEST_CASE("modules: an imported class can be constructed qualified (`M.C()`)")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import geometry\nvar p = geometry.Point()\np.x = 5.0\np.x").value().as_double() ==
	      5.0);
}

TEST_CASE("modules: an imported class's field defaults resolve in its own module")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	// Widget's defaults name a module-private const (LABEL) and a module function
	// (base_scale) invisible to this importer — so a correct build resolves them in
	// geometry, not here. A regression would either miscompile or resolve to null.
	const char *code =
	    "import geometry for Widget\n"
	    "var w = Widget()\n"
	    "w.name & \"/\" & w.scale & \"/\" & w.count";
	CHECK(rt.do_string(code).to<String>() == String("geo/2/1"));
}

TEST_CASE("modules: inherited field defaults chain base→derived across a module boundary")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	// LabeledWidget is Widget: constructing it from here must apply Widget's defaults
	// (base) then LabeledWidget's own — every initializer resolved in geometry's scope.
	const char *code =
	    "import geometry for LabeledWidget\n"
	    "var w = LabeledWidget()\n"
	    "w.name & \"/\" & w.count & \"/\" & w.tag";
	CHECK(rt.do_string(code).to<String>() == String("geo/1/geo-tag"));
}

TEST_CASE("modules: defaults of a base in ANOTHER module resolve in that base's module")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	// BigWidget (module shapes) is Widget (module geometry). Constructing it here must run
	// Widget's thunk (its `name` default names geometry's private LABEL) AND BigWidget's
	// thunk (its `kind` default names shapes' private KIND) — each in its home module. This
	// is the case a construction-site-inlined default could never get right.
	const char *code =
	    "import shapes for BigWidget\n"
	    "var b = BigWidget()\n"
	    "b.name & \"/\" & b.count & \"/\" & b.kind & \"/\" & b.size";
	CHECK(rt.do_string(code).to<String>() == String("geo/1/big/100"));
}

TEST_CASE("modules: `for *` brings an imported class into scope for `is`")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import geometry for *\norigin() is Point").value().as_bool());
}

TEST_CASE("modules: naming a public function in `for` is accepted (it is already bare)")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	CHECK(rt.do_string("import geometry for origin, Point\norigin() is Point").value().as_bool());
}

TEST_CASE("modules: `for` on a name that does not exist is a compile error")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try { rt.do_string("import config for nope"); }
	catch (const SyntaxError &) { threw = true; }
	CHECK(threw);
}

TEST_CASE("modules: `import` nested inside a function is a compile error")
{
	Runtime rt;
	rt.add_import_path(String(MODDIR.c_str()));
	bool threw = false;
	try { rt.do_string("function f()\n  import config\nend"); }
	catch (const SyntaxError &) { threw = true; }
	CHECK(threw);
}

} // namespace
