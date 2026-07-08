// Phonometrica engine — modules & imports (design §11). Copyright (C) 2019-2026
// Julien Eychenne. GPLv3.
//
// A module is a `.phon` file; `import M` compiles+runs it once and makes its public
// functions callable bare (they join the global dispatch table). Fixtures live in
// test/scripts/modules/.

#include "test_framework.hpp"

#include <phon/compile/diagnostic.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/types/string.hpp>

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

} // namespace
