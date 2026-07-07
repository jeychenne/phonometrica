// Phonometrica engine — scanner tests (M3 step 1).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// This file is UTF-8 encoded.

#include <phon/compile/scanner.hpp>
#include <phon/compile/source.hpp>
#include <phon/compile/diagnostic.hpp>

#include "test_framework.hpp"

#include <string>
#include <vector>

using namespace phonometrica;

namespace {

// Lex `code` to completion, returning every token up to and including Eot.
std::vector<Token> lex(const std::string &code)
{
	Source src = Source::from_string(code, "<test>");
	Scanner sc(src);
	std::vector<Token> toks;
	for (;;)
	{
		Token t = sc.next();
		bool eot = t.is_eot();
		toks.push_back(std::move(t));
		if (eot)
			break;
	}
	return toks;
}

// Lex, dropping the trailing Eot, returning only the lexeme kinds.
std::vector<Lexeme> kinds(const std::string &code)
{
	std::vector<Lexeme> out;
	for (const Token &t : lex(code))
		if (!t.is_eot())
			out.push_back(t.id);
	return out;
}

bool same(const std::vector<Lexeme> &a, std::initializer_list<Lexeme> b)
{
	if (a.size() != b.size())
		return false;
	auto it = b.begin();
	for (const auto &x : a)
		if (x != *it++)
			return false;
	return true;
}

} // namespace

TEST_CASE("scanner: empty and whitespace-only input")
{
	CHECK(lex("").size() == 1); // just Eot
	CHECK(lex("").front().is_eot());
	CHECK(lex("   \t  ").size() == 1);
	CHECK(lex("\n\n\n").size() == 1);   // leading/blank newlines suppressed
	CHECK(lex("   # a comment\n").size() == 1);
}

TEST_CASE("scanner: identifiers and keywords")
{
	auto t = lex("var snd = while_not");
	REQUIRE(t.size() == 5); // var snd = while_not Eot
	CHECK(t[0].id == Lexeme::Var);
	CHECK(t[1].id == Lexeme::Identifier);
	CHECK(t[1].spelling == "snd");
	CHECK(t[2].id == Lexeme::Assign);
	CHECK(t[3].id == Lexeme::Identifier); // "while_not" is not the keyword "while"
	CHECK(t[3].spelling == "while_not");

	// Every keyword is recognized as its own lexeme, not an identifier.
	CHECK(kinds("and as break cast catch class const continue div do")[0] == Lexeme::And);
	CHECK(lex("function")[0].id == Lexeme::Function);
	CHECK(lex("true false null")[0].id == Lexeme::True);
	CHECK(lex("true false null")[1].id == Lexeme::False);
	CHECK(lex("true false null")[2].id == Lexeme::Null);
	CHECK(lex("method field ref is cast")[0].id == Lexeme::Method);
}

TEST_CASE("scanner: unicode identifiers")
{
	auto t = lex("var café = 1");
	CHECK(t[1].id == Lexeme::Identifier);
	CHECK(t[1].spelling == "café");

	auto t2 = lex("漢字 = 2");
	CHECK(t2[0].id == Lexeme::Identifier);
	CHECK(t2[0].spelling == "漢字");
}

TEST_CASE("scanner: integer and float literals")
{
	CHECK(lex("42")[0].id == Lexeme::Integer);
	CHECK(lex("42")[0].spelling == "42");
	CHECK(lex("1_000_000")[0].id == Lexeme::Integer);
	CHECK(lex("1_000_000")[0].spelling == "1_000_000"); // separators kept in spelling
	CHECK(lex("1.0")[0].id == Lexeme::Float);
	CHECK(lex("3.14159")[0].id == Lexeme::Float);
	CHECK(lex("1e10")[0].id == Lexeme::Float);
	CHECK(lex("1.5e-3")[0].id == Lexeme::Float);
	CHECK(lex("2.3E+05")[0].id == Lexeme::Float);

	// `1.method` is Integer Dot Identifier, not a malformed float.
	CHECK(same(kinds("1.foo"), {Lexeme::Integer, Lexeme::Dot, Lexeme::Identifier}));

	// `1e` with no exponent digits: the 'e' is a separate identifier.
	CHECK(same(kinds("1e"), {Lexeme::Integer, Lexeme::Identifier}));
}

TEST_CASE("scanner: operators and punctuation")
{
	CHECK(same(kinds("+ - * / ^ &"),
	           {Lexeme::Plus, Lexeme::Minus, Lexeme::Star, Lexeme::Slash,
	            Lexeme::Caret, Lexeme::Concat}));
	CHECK(same(kinds("+= -= *= /= &="),
	           {Lexeme::PlusEq, Lexeme::MinusEq, Lexeme::StarEq, Lexeme::SlashEq,
	            Lexeme::ConcatEq}));
	CHECK(same(kinds("= == != < <= > >="),
	           {Lexeme::Assign, Lexeme::Eq, Lexeme::NotEq, Lexeme::Less,
	            Lexeme::LessEq, Lexeme::Greater, Lexeme::GreaterEq}));
	CHECK(same(kinds("( ) [ ] { } , : . ; ->"),
	           {Lexeme::LParen, Lexeme::RParen, Lexeme::LSquare, Lexeme::RSquare,
	            Lexeme::LBrace, Lexeme::RBrace, Lexeme::Comma, Lexeme::Colon,
	            Lexeme::Dot, Lexeme::Semicolon, Lexeme::Arrow}));
	// `->` must not be split into Minus Greater.
	CHECK(same(kinds("x -> x"), {Lexeme::Identifier, Lexeme::Arrow, Lexeme::Identifier}));
}

TEST_CASE("scanner: raw single-quoted strings")
{
	auto t = lex("'\\b[aeiou]+\\b'"); // a regex; backslashes are literal
	REQUIRE(t.size() == 2);
	CHECK(t[0].id == Lexeme::String);
	CHECK(t[0].spelling == "\\b[aeiou]+\\b");

	auto p = lex("'C:\\Users\\me'"); // a Windows path
	CHECK(p[0].id == Lexeme::String);
	CHECK(p[0].spelling == "C:\\Users\\me");
}

TEST_CASE("scanner: double-quoted strings with escapes")
{
	CHECK(lex("\"hello\"")[0].spelling == "hello");
	CHECK(lex("\"a\\tb\\n\"")[0].spelling == "a\tb\n");
	CHECK(lex("\"quote: \\\"x\\\"\"")[0].spelling == "quote: \"x\"");
	CHECK(lex("\"brace: \\{ \\}\"")[0].spelling == "brace: { }"); // escaped braces
	CHECK(lex("\"\"")[0].id == Lexeme::String);
	CHECK(lex("\"\"")[0].spelling == "");
}

TEST_CASE("scanner: string interpolation")
{
	// "Analyzing {path}: {n} intervals found"
	auto t = kinds("\"Analyzing {path}: {n} intervals found\"");
	CHECK(same(t, {Lexeme::InterpStart, Lexeme::Identifier, Lexeme::InterpMid,
	               Lexeme::Identifier, Lexeme::InterpEnd}));

	auto full = lex("\"a{x}b\"");
	CHECK(full[0].id == Lexeme::InterpStart);
	CHECK(full[0].spelling == "a");
	CHECK(full[1].id == Lexeme::Identifier);
	CHECK(full[1].spelling == "x");
	CHECK(full[2].id == Lexeme::InterpEnd);
	CHECK(full[2].spelling == "b");

	// A lone segment: InterpStart("") expr InterpEnd("").
	auto lone = lex("\"{x}\"");
	CHECK(lone[0].id == Lexeme::InterpStart);
	CHECK(lone[0].spelling == "");
	CHECK(lone[2].id == Lexeme::InterpEnd);
	CHECK(lone[2].spelling == "");

	// An embedded call, exercising parentheses inside the expression.
	CHECK(same(kinds("\"= {f(a, b)}\""),
	           {Lexeme::InterpStart, Lexeme::Identifier, Lexeme::LParen,
	            Lexeme::Identifier, Lexeme::Comma, Lexeme::Identifier,
	            Lexeme::RParen, Lexeme::InterpEnd}));
}

TEST_CASE("scanner: triple-quoted strings")
{
	// Double triple-quoted: spans lines, processes escapes, single token.
	auto t = lex("\"\"\"line one\nline two\"\"\"");
	REQUIRE(t.size() == 2);
	CHECK(t[0].id == Lexeme::String);
	CHECK(t[0].spelling == "line one\nline two");

	// Embedded single and double quotes are literal content inside a triple.
	CHECK(lex("\"\"\"a \"b\" c\"\"\"")[0].spelling == "a \"b\" c");

	// Raw triple-quoted: spans lines, no escape processing.
	auto r = lex("'''a\\nb\nc'''");
	CHECK(r[0].id == Lexeme::String);
	CHECK(r[0].spelling == "a\\nb\nc"); // backslash-n stays literal, real newline kept

	// Interpolation works inside a triple-quoted double string, across lines.
	CHECK(same(kinds("\"\"\"x = {\n  a + b\n}!\"\"\""),
	           {Lexeme::InterpStart, Lexeme::Identifier, Lexeme::Plus,
	            Lexeme::Identifier, Lexeme::InterpEnd}));

	// Empty strings, both families.
	CHECK(lex("\"\"")[0].id == Lexeme::String);
	CHECK(lex("\"\"")[0].spelling == "");
	CHECK(lex("''")[0].id == Lexeme::String);
	CHECK(lex("''")[0].spelling == "");
	CHECK(lex("\"\"\"\"\"\"")[0].spelling == ""); // empty triple-quoted
}

TEST_CASE("scanner: ellipsis for variadics and splat")
{
	// Variadic parameter: `values as Object...`
	CHECK(same(kinds("values as Object..."),
	           {Lexeme::Identifier, Lexeme::As, Lexeme::Identifier, Lexeme::Ellipsis}));
	// Call-site splat: `f(xs...)`
	CHECK(same(kinds("f(xs...)"),
	           {Lexeme::Identifier, Lexeme::LParen, Lexeme::Identifier,
	            Lexeme::Ellipsis, Lexeme::RParen}));
	// A single dot is still Dot; `a.b` is field access.
	CHECK(same(kinds("a.b"), {Lexeme::Identifier, Lexeme::Dot, Lexeme::Identifier}));
	// `..` (reserved, unspent) is a scan error.
	try
	{
		lex("a..b");
		CHECK_MESSAGE(false, "expected a SyntaxError for '..'");
	}
	catch (const SyntaxError &e)
	{
		CHECK(e.line == 1);
		CHECK(e.column == 1); // anchored at the first '.'
	}
}

TEST_CASE("scanner: nested interpolation and braces")
{
	// A table literal inside an interpolation: the inner { } must not close it.
	CHECK(same(kinds("\"{ {a: 1} }\""),
	           {Lexeme::InterpStart, Lexeme::LBrace, Lexeme::Identifier,
	            Lexeme::Colon, Lexeme::Integer, Lexeme::RBrace, Lexeme::InterpEnd}));

	// A string inside an interpolation inside a string.
	CHECK(same(kinds("\"a{ f(\"b{y}c\") }d\""),
	           {Lexeme::InterpStart,                       // "a
	            Lexeme::Identifier, Lexeme::LParen,         // f(
	            Lexeme::InterpStart,                        //   "b
	            Lexeme::Identifier,                         //   y
	            Lexeme::InterpEnd,                          //   c"
	            Lexeme::RParen,                             // )
	            Lexeme::InterpEnd}));                       // d"
}

TEST_CASE("scanner: newline as statement separator")
{
	CHECK(same(kinds("a\nb"), {Lexeme::Identifier, Lexeme::Newline, Lexeme::Identifier}));
	// Blank lines collapse to a single separator; leading newlines are
	// suppressed. A newline following a complete statement is significant, so
	// the trailing newline after `b` survives (the parser skips it).
	CHECK(same(kinds("\n\na\n\n\nb\n\n"),
	           {Lexeme::Identifier, Lexeme::Newline, Lexeme::Identifier, Lexeme::Newline}));
	// Semicolon is a separator too.
	CHECK(same(kinds("a; b"), {Lexeme::Identifier, Lexeme::Semicolon, Lexeme::Identifier}));
}

TEST_CASE("scanner: line continuation rule")
{
	// A line ending in a binary operator continues.
	CHECK(same(kinds("a +\nb"), {Lexeme::Identifier, Lexeme::Plus, Lexeme::Identifier}));
	CHECK(same(kinds("a and\nb"), {Lexeme::Identifier, Lexeme::And, Lexeme::Identifier}));
	// A trailing comma continues.
	CHECK(same(kinds("f(a,\nb)"),
	           {Lexeme::Identifier, Lexeme::LParen, Lexeme::Identifier,
	            Lexeme::Comma, Lexeme::Identifier, Lexeme::RParen}));
	// Inside brackets, newlines are insignificant.
	CHECK(same(kinds("[\n1,\n2\n]"),
	           {Lexeme::LSquare, Lexeme::Integer, Lexeme::Comma, Lexeme::Integer,
	            Lexeme::RSquare}));
	// But a complete expression is separated.
	CHECK(same(kinds("a\n+b"),
	           {Lexeme::Identifier, Lexeme::Newline, Lexeme::Plus, Lexeme::Identifier}));
}

TEST_CASE("scanner: comments")
{
	CHECK(same(kinds("a # trailing\nb"),
	           {Lexeme::Identifier, Lexeme::Newline, Lexeme::Identifier}));
	CHECK(same(kinds("#!/usr/bin/env phon\nx"),
	           {Lexeme::Identifier})); // shebang line is a comment; its newline is leading
	CHECK(same(kinds("a #* block\nspanning\nlines *# b"),
	           {Lexeme::Identifier, Lexeme::Identifier}));
	CHECK(same(kinds("a #* inline *# + b"),
	           {Lexeme::Identifier, Lexeme::Plus, Lexeme::Identifier}));
}

TEST_CASE("scanner: token positions")
{
	// line 1: "var x = 1"
	// line 2: "  y = 2"
	auto t = lex("var x = 1\n  y = 2");
	CHECK(t[0].line == 1);
	CHECK(t[0].column == 0); // 'var'
	CHECK(t[1].column == 4); // 'x'
	CHECK(t[3].column == 8); // '1'
	// after the newline token, 'y' is on line 2 at column 2
	Token *y = nullptr;
	for (auto &tok : t)
		if (tok.spelling == "y")
			y = &tok;
	REQUIRE(y != nullptr);
	CHECK(y->line == 2);
	CHECK(y->column == 2);
}

TEST_CASE("scanner: error positions")
{
	auto check_err = [](const std::string &code, intptr_t line, intptr_t col) {
		try
		{
			lex(code);
			CHECK_MESSAGE(false, "expected a SyntaxError");
		}
		catch (const SyntaxError &e)
		{
			CHECK(e.line == line);
			CHECK(e.column == col);
		}
	};

	check_err("\"unterminated", 1, 0);        // string starts at column 0
	check_err("x = $", 1, 4);                 // stray '$' (not a token; '@' now starts an array literal)
	check_err("'raw", 1, 0);                  // unterminated raw string
	check_err("\"bad\\qesc\"", 1, 4);         // invalid escape (anchored at the '\')
	check_err("a = 1\n  \"oops", 2, 2);       // error on line 2
}

TEST_CASE("scanner: fuzz — arbitrary bytes never crash")
{
	// Deterministic pseudo-random byte streams. The scanner must either tokenize
	// or throw SyntaxError; it must never crash, hang, or read out of bounds.
	uint64_t state = 0x9E3779B97F4A7C15ull;
	auto rng = [&]() -> uint8_t {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		return static_cast<uint8_t>(state >> 24);
	};

	for (int iter = 0; iter < 4000; ++iter)
	{
		int len = rng() % 64;
		std::string code;
		for (int i = 0; i < len; ++i)
			code.push_back(static_cast<char>(rng()));

		try
		{
			Source src = Source::from_string(code, "<fuzz>");
			Scanner sc(src);
			int guard = 0;
			for (;;)
			{
				Token t = sc.next();
				if (t.is_eot())
					break;
				if (++guard > 100000)
				{
					CHECK_MESSAGE(false, "scanner failed to make progress");
					break;
				}
			}
		}
		catch (const SyntaxError &)
		{
			// Acceptable outcome.
		}
	}
	CHECK(true); // reached here without crashing
}
