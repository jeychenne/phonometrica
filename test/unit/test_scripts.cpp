// Phonometrica engine — .phon acceptance-script runner (M4).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Each `test/scripts/<name>.phon` self-checks with assert(); running it to
// completion without a RuntimeError is the pass condition. This ports the spirit
// of Phonometrica's test/engine/run_all.phon suite to the new grammar (the old
// scripts used `let`/`assert`/bare `print`, all changed in the new language).

#include <phon/compile/diagnostic.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/types/string.hpp>

#include "test_framework.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace phonometrica;

namespace {

const char *SCRIPTS[] = {
    "test_scientific_notation", "test_string_interpolation", "test_compound_assignment",
    "test_upvalues",            "test_functions",           "test_generics",
    "test_classes",             "test_errors",             "test_iteration",
    "test_refs",                "test_gc",           "test_slices",
    "test_variadics",           "test_options",           "test_splat",
    "test_numeric",             "test_array",              "test_freeze",
    "test_channel",             "test_spawn",
    "test_stdlib",
};

std::string read_file(const std::string &path, bool &ok)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		ok = false;
		return {};
	}
	ok = true;
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} // namespace

TEST_CASE("scripts: .phon acceptance suite runs clean")
{
	const std::string dir = PHON_SCRIPTS_DIR;
	for (const char *name : SCRIPTS)
	{
		bool ok = false;
		std::string code = read_file(dir + "/" + name + ".phon", ok);
		if (!ok)
		{
			CHECK_MESSAGE(false, (std::string("missing script: ") + name).c_str());
			continue;
		}
		std::string failure;
		try
		{
			do_string(code);
		}
		catch (const RuntimeError &e)
		{
			String m = e.message;
			failure = std::string(name) + ": " + std::string(m.data(), static_cast<size_t>(m.size())) +
			          " (line " + std::to_string(e.line) + ")";
		}
		catch (const SyntaxError &e)
		{
			failure = std::string(name) + ": " + e.what();
		}
		CHECK_MESSAGE(failure.empty(), failure.c_str());
	}
}
