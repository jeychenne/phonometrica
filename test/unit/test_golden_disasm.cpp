// Phonometrica engine — golden disassembly tests (M4).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// For each `test/golden/disasm/<name>.phon`, compile it to bytecode, disassemble,
// and compare to the checked-in `<name>.dis`. This locks codegen against
// regressions: any change in emitted opcodes/registers surfaces as a reviewable
// diff. Regenerate with PHON_UPDATE_GOLDEN=1 (as with the AST goldens).

#include <phon/engine/compile/diagnostic.hpp>
#include <phon/engine/runtime/runtime.hpp>

#include "test_framework.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace phonometrica;

namespace {

const char *CORPUS[] = {"arithmetic", "controlflow", "closures", "slices",
                        "variadics",  "options",     "splat",   "arrays"};

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

void write_file(const std::string &path, const std::string &content)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << content;
}

std::string first_diff(const std::string &expected, const std::string &actual)
{
	std::istringstream e(expected), a(actual);
	std::string el, al;
	int line = 1;
	while (true)
	{
		bool eok = static_cast<bool>(std::getline(e, el));
		bool aok = static_cast<bool>(std::getline(a, al));
		if (!eok && !aok)
			return "no line-level difference (trailing bytes differ)";
		if (eok != aok || el != al)
			return "line " + std::to_string(line) + ":\n  expected: " + (eok ? el : "<eof>") +
			       "\n  actual:   " + (aok ? al : "<eof>");
		++line;
	}
}

} // namespace

TEST_CASE("golden: disassembly matches the checked-in corpus")
{
	const std::string dir = PHON_GOLDEN_DISASM_DIR;
	const bool update = std::getenv("PHON_UPDATE_GOLDEN") != nullptr;

	for (const char *name : CORPUS)
	{
		const std::string phon = dir + "/" + name + ".phon";
		const std::string gold = dir + "/" + name + ".dis";

		bool ok = false;
		std::string code = read_file(phon, ok);
		if (!ok)
		{
			CHECK_MESSAGE(false, (std::string("missing corpus source: ") + phon).c_str());
			continue;
		}

		std::string actual;
		try
		{
			actual = disassemble_source(code);
		}
		catch (const SyntaxError &e)
		{
			CHECK_MESSAGE(false,
			              (std::string("corpus '") + name + "' failed to compile:\n" + e.what()).c_str());
			continue;
		}

		if (update)
		{
			bool had = false;
			std::string prev = read_file(gold, had);
			write_file(gold, actual);
			if (!had || prev != actual)
				std::fprintf(stderr, "  [updated] %s.dis\n", name);
			continue;
		}

		bool gold_ok = false;
		std::string expected = read_file(gold, gold_ok);
		if (!gold_ok)
		{
			CHECK_MESSAGE(false, (std::string("missing golden file: ") + gold +
			                      " (run with PHON_UPDATE_GOLDEN=1 to create it)")
			                         .c_str());
			continue;
		}

		CHECK_MESSAGE(actual == expected, (std::string("disassembly mismatch for '") + name + "':\n" +
		                                   first_diff(expected, actual) +
		                                   "\n(run with PHON_UPDATE_GOLDEN=1 to accept)")
		                                      .c_str());
	}
}
