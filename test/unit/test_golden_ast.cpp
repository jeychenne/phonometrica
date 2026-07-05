// Phonometrica engine — golden AST-dump tests (M3 step 4).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// For each `test/golden/ast/<name>.phon`, parse it, render the AST with
// dump_ast(), and compare to the checked-in `<name>.ast`. This locks the whole
// front end (scanner + parser + AST) against regressions: any change in how a
// construct parses shows up as a reviewable diff in the golden file.
//
// To regenerate the goldens after an intentional change, run the test binary
// with PHON_UPDATE_GOLDEN=1 in the environment; it overwrites the .ast files
// instead of comparing, and reports which ones changed.

#include <phon/compile/ast_printer.hpp>
#include <phon/compile/parser.hpp>
#include <phon/compile/source.hpp>
#include <phon/compile/diagnostic.hpp>

#include "test_framework.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace phonometrica;

namespace {

// Every corpus base name under test/golden/ast (one <name>.phon + <name>.ast).
const char *CORPUS[] = {
    "literals", "expressions", "declarations", "controlflow",
    "functions", "classes", "handling",
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

void write_file(const std::string &path, const std::string &content)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << content;
}

// First line index where two texts differ, for a readable failure message.
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
		{
			return "line " + std::to_string(line) + ":\n  expected: " + (eok ? el : "<eof>")
			       + "\n  actual:   " + (aok ? al : "<eof>");
		}
		++line;
	}
}

} // namespace

TEST_CASE("golden: AST dumps match the checked-in corpus")
{
	const std::string dir = PHON_GOLDEN_AST_DIR;
	const bool update = std::getenv("PHON_UPDATE_GOLDEN") != nullptr;

	for (const char *name : CORPUS)
	{
		const std::string phon = dir + "/" + name + ".phon";
		const std::string gold = dir + "/" + name + ".ast";

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
			Source src = Source::from_string(code, std::string(name) + ".phon");
			Parser parser(src);
			actual = dump_ast(parser.parse().get());
		}
		catch (const SyntaxError &e)
		{
			CHECK_MESSAGE(false, (std::string("corpus '") + name + "' failed to parse:\n" + e.what()).c_str());
			continue;
		}

		if (update)
		{
			bool had = false;
			std::string prev = read_file(gold, had);
			write_file(gold, actual);
			if (!had || prev != actual)
				std::fprintf(stderr, "  [updated] %s.ast\n", name);
			continue;
		}

		bool gold_ok = false;
		std::string expected = read_file(gold, gold_ok);
		if (!gold_ok)
		{
			CHECK_MESSAGE(false, (std::string("missing golden file: ") + gold
			                      + " (run with PHON_UPDATE_GOLDEN=1 to create it)")
			                         .c_str());
			continue;
		}

		bool matches = (actual == expected);
		CHECK_MESSAGE(matches, (std::string("AST dump mismatch for '") + name + "':\n"
		                        + first_diff(expected, actual)
		                        + "\n(run with PHON_UPDATE_GOLDEN=1 to accept)")
		                           .c_str());
	}
}
