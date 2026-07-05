// Phonometrica engine — atom table (Symbol interning) tests.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "types/atom.hpp"
#include "core/value.hpp"
#include "test_framework.hpp"

#include <string>
#include <vector>

using namespace phonometrica;

TEST_CASE("interning is stable and deduplicated")
{
	Symbol a = intern("pitch");
	Symbol b = intern("pitch");
	Symbol c = intern("intensity");
	CHECK(a == b);
	CHECK(a != c);
	CHECK(a.id != 0); // never NO_SYMBOL
	CHECK(symbol_name(a) == "pitch");
	CHECK(symbol_name(c) == "intensity");
}

TEST_CASE("NO_SYMBOL maps to empty")
{
	CHECK(symbol_name(NO_SYMBOL).empty());
}

TEST_CASE("interning handles embedded NUL and binary")
{
	std::string with_nul("a\0b", 3);
	Symbol s = intern(std::string_view(with_nul.data(), 3));
	CHECK(symbol_name(s).size() == 3);
	CHECK(symbol_name(s) == std::string_view("a\0b", 3));
	// A different length with the same prefix is a distinct atom.
	Symbol s2 = intern("a");
	CHECK(s != s2);
}

TEST_CASE("many distinct atoms are all retrievable")
{
	const int N = 2000;
	std::vector<Symbol> syms;
	for (int i = 0; i < N; ++i)
		syms.push_back(intern("id_" + std::to_string(i)));
	for (int i = 0; i < N; ++i)
	{
		CHECK(symbol_name(syms[(size_t) i]) == ("id_" + std::to_string(i)));
		// Re-interning returns the same symbol.
		CHECK(intern("id_" + std::to_string(i)) == syms[(size_t) i]);
	}
	CHECK(symbol_count() >= N);
}

TEST_CASE("Symbol survives boxing in a Value")
{
	Symbol s = intern("duration");
	Value v = Value::make_symbol(s);
	CHECK(v.is_symbol());
	CHECK(v.as_symbol() == s);
	CHECK(symbol_name(v.as_symbol()) == "duration");
}
