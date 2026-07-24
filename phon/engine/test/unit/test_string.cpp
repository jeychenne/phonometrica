// Phonometrica engine — String tests (CoW, graphemes, breadcrumbs, API subset).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// This file is UTF-8 encoded.

#include <phon/engine/types/string.hpp>
#include <phon/engine/types/regex.hpp>
#include <phon/engine/base/unicode.hpp>
#include "test_framework.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace phonometrica;

namespace {

std::string u8(char32_t cp)
{
	char buf[4];
	size_t n = unicode::encode(cp, buf);
	return std::string(buf, n);
}

} // namespace

TEST_CASE("String construction and basic access")
{
	String e;
	CHECK(e.empty());
	CHECK(e.size() == 0);
	CHECK(e == "");

	String s("hello");
	CHECK(s.size() == 5);
	CHECK(!s.empty());
	CHECK(s == "hello");
	CHECK(s.view() == "hello");
	CHECK(std::string(s.data()) == "hello");

	String s2("hello world", 5);
	CHECK(s2 == "hello");

	String rep(U'a', 4);
	CHECK(rep == "aaaa");
}

TEST_CASE("String copy is O(1) share; mutation triggers CoW")
{
	String a("phonetics");
	String b = a;
	CHECK(a.use_count() == 2);
	CHECK(!a.unique());
	CHECK(a.data() == b.data()); // shared buffer

	a.append("!");
	CHECK(a == "phonetics!");
	CHECK(b == "phonetics"); // untouched
	CHECK(b.use_count() == 1);
	CHECK(a.unique());
}

TEST_CASE("String append grows and preserves content")
{
	String s;
	std::string ref;
	for (int i = 0; i < 1000; ++i)
	{
		char c = static_cast<char>('a' + (i % 26));
		s.append(Substring(&c, 1));
		ref.push_back(c);
	}
	CHECK(s.size() == 1000);
	CHECK(s.view() == ref);
}

TEST_CASE("String self-append is safe (aliasing)")
{
	String s("abc");
	s.append(s.view()); // suffix aliases our own buffer
	CHECK(s == "abcabc");
	s.append(s.view());
	CHECK(s == "abcabcabcabc");
}

TEST_CASE("String prepend and insert")
{
	String s("world");
	s.prepend("hello ");
	CHECK(s == "hello world");
	s.insert(7, "big "); // before the 7th grapheme ('w')
	CHECK(s == "hello big world");
}

TEST_CASE("String grapheme_count on multibyte and IPA")
{
	// "café": 4 graphemes, 5 bytes.
	String cafe("café");
	CHECK(cafe.size() == 5);
	CHECK(cafe.grapheme_count() == 4);
	CHECK(cafe.length() == 4);

	// Decomposed é = e + combining acute: 1 grapheme, 2 codepoints.
	String decomp(("e" + u8(0x0301)).c_str());
	CHECK(decomp.grapheme_count() == 1);
	CHECK(String::utf8_length(decomp.view()) == 2);

	// IPA affricate t͡ʃ = t + tie bar (U+0361 Extend) + ʃ. Under UAX #29 the tie
	// bar attaches to the preceding 't', so this is 2 clusters: [t + tie] and [ʃ].
	std::string tesh = "t" + u8(0x0361) + u8(0x0283);
	String aff(tesh.c_str());
	CHECK(aff.grapheme_count() == 2);

	// Family emoji ZWJ sequence: 1 grapheme.
	std::string fam = u8(0x1F468) + u8(0x200D) + u8(0x1F469) + u8(0x200D) + u8(0x1F467);
	String family(fam.c_str());
	CHECK(family.grapheme_count() == 1);
}

TEST_CASE("String grapheme indexing (1-based, negative)")
{
	String s("héllo"); // h é l l o : 5 graphemes, é is 2 bytes
	CHECK(s.grapheme_count() == 5);
	CHECK(s.at(1) == "h");
	CHECK(s.at(2) == u8(0xE9)); // é
	CHECK(s.at(3) == "l");
	CHECK(s.at(5) == "o");
	CHECK(s.at(-1) == "o");
	CHECK(s.at(-4) == u8(0xE9));
	CHECK(s.first() == U'h');
	CHECK(s.last() == U'o');
}

TEST_CASE("String breadcrumbs: random access across the stride boundary")
{
	// Build a long string of multibyte grapheme clusters so indexing crosses
	// the 32-grapheme breadcrumb stride many times. Each cluster is "é"
	// decomposed (e + U+0301) = 3 bytes, 1 grapheme.
	const int N = 500;
	std::string cluster = "e" + u8(0x0301);
	std::string built;
	for (int i = 0; i < N; ++i)
		built += cluster;
	String s(built.c_str());
	REQUIRE(s.grapheme_count() == N);

	// Every grapheme must decode back to the same cluster, via breadcrumb jumps.
	for (int i = 1; i <= N; ++i)
		REQUIRE(s.at(i) == cluster);
	// Negative indices too.
	for (int i = 1; i <= N; ++i)
		REQUIRE(s.at(-i) == cluster);

	// index_to_iter / distance / advance are consistent.
	auto it = s.index_to_iter(100);
	CHECK(s.distance(s.begin(), it) == 99);
	s.advance(it, 50);
	CHECK(s.distance(s.begin(), it) == 149);
	s.advance(it, -49);
	CHECK(s.distance(s.begin(), it) == 100);
}

TEST_CASE("String breadcrumbs: mixed cluster widths")
{
	// Alternate ASCII, 2-byte, 4-byte, and combining clusters.
	std::string built;
	std::vector<std::string> expect;
	for (int i = 0; i < 200; ++i)
	{
		std::string c;
		switch (i % 4)
		{
		case 0: c = "a"; break;
		case 1: c = u8(0xE9); break;             // é
		case 2: c = u8(0x1F600); break;          // 😀
		case 3: c = "o" + u8(0x0308); break;     // o + diaeresis
		}
		built += c;
		expect.push_back(c);
	}
	String s(built.c_str());
	REQUIRE(s.grapheme_count() == 200);
	for (int i = 0; i < 200; ++i)
		REQUIRE(s.at(i + 1) == expect[(size_t) i]);
}

TEST_CASE("String case mapping")
{
	CHECK(String("Hello").to_upper() == "HELLO");
	CHECK(String("Hello").to_lower() == "hello");
	CHECK(String("café").to_upper() == "CAFÉ");
	CHECK(String("straße").to_upper() == "STRASSE"); // ß -> SS
}

TEST_CASE("String reverse is grapheme-wise")
{
	CHECK(String("abc").reverse() == "cba");
	// é stays intact when reversed (bytes not split).
	String s(("ab" + u8(0xE9)).c_str());
	String r = s.reverse();
	CHECK(r == (u8(0xE9) + "ba"));
	// Combining mark stays attached to its base: "á b" (2 graphemes) reverses
	// to "b á", i.e. b, then a+combining-acute.
	String d(("a" + u8(0x0301) + "b").c_str());
	CHECK(d.grapheme_count() == 2);
	CHECK(d.reverse() == ("b" + std::string("a") + u8(0x0301)));
}

TEST_CASE("String search returns grapheme positions")
{
	String s("héllo wörld");
	CHECK(s.find("llo") == 3);   // grapheme position (é counts as 1)
	CHECK(s.find("wörld") == 7);
	CHECK(s.find("xyz") == 0);   // not found
	CHECK(s.contains("wör"));
	CHECK(!s.contains("xyz"));
	CHECK(s.starts_with("héllo"));
	CHECK(s.ends_with("wörld"));
	CHECK(String("aXbXcX").count("X") == 3);
	CHECK(String("aXbXcX").rfind("X") == 6);
}

TEST_CASE("String mid/left/right by graphemes")
{
	String s("héllo"); // 5 graphemes
	CHECK(s.left(2) == "hé");
	CHECK(s.right(2) == "lo");
	CHECK(String(s.mid(2, 2)) == "él");
	CHECK(String(s.mid(3)) == "llo");
}

TEST_CASE("String trim / replace / remove / chop")
{
	CHECK(String("  hi  ").trim() == "hi");
	CHECK(String("\thi\n").trim() == "hi");
	CHECK(String("a.b.c").replace(".", "-") == "a-b-c");
	CHECK(String("a.b.c").replace(".", "-", 1) == "a-b.c");
	CHECK(String("hello").remove("l") == "heo");

	String c("hello");
	c.chop(3);
	CHECK(c == "hel");
}

TEST_CASE("String conversions")
{
	CHECK(String::convert(true) == "true");
	CHECK(String::convert(intptr_t(42)) == "42");
	CHECK(String::convert(intptr_t(-7)) == "-7");
	CHECK(String::convert(3.5) == "3.5");

	bool ok = false;
	CHECK(String("42").to_int(&ok) == 42);
	CHECK(ok);
	String("4x").to_int(&ok);
	CHECK(!ok);
	CHECK(String("3.14").to_float(&ok) == 3.14);
	CHECK(ok);
	CHECK(String("true").to_bool());
	CHECK(!String("false").to_bool());

	CHECK(String::format("%s=%d", "x", 10) == "x=10");
}

TEST_CASE("String UTF-16 / UTF-32 round-trips")
{
	String s(("café" + u8(0x1F600)).c_str());
	CHECK(String::from_utf16(s.to_utf16()) == s);
	CHECK(String::from_utf32(s.to_utf32()) == s);
}

TEST_CASE("String comparison and ordering")
{
	CHECK(String("abc") == String("abc"));
	CHECK(String("abc") != String("abd"));
	CHECK(String("abc") < String("abd"));
	CHECK(String("ab") < String("abc"));
	CHECK(String("abc").compare("abc") == 0);
	CHECK(String("a").compare("b") < 0);
}

TEST_CASE("String hash and equality are consistent")
{
	String a("phonetics");
	String b("phonetics");
	String c("phonology");
	CHECK(a.hash() == b.hash());
	CHECK(a == b);
	CHECK(a.hash() != c.hash() || a != c); // (allow rare collisions but not equal)
	CHECK(a != c);
}

TEST_CASE("String reserve / shrink_to_fit / is_ascii")
{
	String s("hi");
	s.reserve(100);
	CHECK(s.capacity() >= 100);
	CHECK(s == "hi");
	s.shrink_to_fit();
	CHECK(s.capacity() >= 2);
	CHECK(s == "hi");
	CHECK(s.is_ascii());
	CHECK(!String("café").is_ascii());
}

TEST_CASE("String to_value / from_value round-trip")
{
	String s("round-trip");
	Value v = s.to_value();
	CHECK(v.is_cell());
	String back = String::from_value(v);
	CHECK(back == "round-trip");
	CHECK(back.use_count() == 2); // s and back share the cell
}

// ---------------------------------------------------------------------------
// Embedding-surface additions (split/join, %N arg, Regex replace, static init)
// ---------------------------------------------------------------------------

// Constructed before main() — and before any explicit bootstrap() call — to pin
// the static-initializer guarantee: String construction self-bootstraps the class
// registry (see string_create), so embedders may keep file-scope Strings.
static const String g_static_init_string("static-init δῶρον");

TEST_CASE("String construction is safe in a static initializer (pre-bootstrap)")
{
	CHECK(g_static_init_string == "static-init δῶρον");
	CHECK(g_static_init_string.length() == 17);
	String copy = g_static_init_string;
	copy.append("!");
	CHECK(copy.ends_with("!"));
	CHECK(g_static_init_string.ends_with("δῶρον"));
}

TEST_CASE("String::split: separators, boundaries, errors")
{
	auto parts = String("a,b,c").split(",");
	REQUIRE(parts.size() == 3);
	CHECK(parts[0] == "a");
	CHECK(parts[1] == "b");
	CHECK(parts[2] == "c");

	// Leading/trailing separators yield empty chunks.
	parts = String(",a,b,").split(",");
	REQUIRE(parts.size() == 4);
	CHECK(parts[0] == "");
	CHECK(parts[1] == "a");
	CHECK(parts[2] == "b");
	CHECK(parts[3] == "");

	// A string equal to (or shorter than) the separator is a single chunk.
	parts = String("ab").split("ab");
	REQUIRE(parts.size() == 1);
	CHECK(parts[0] == "ab");
	parts = String("a").split("--");
	REQUIRE(parts.size() == 1);
	CHECK(parts[0] == "a");
	parts = String().split(",");
	REQUIRE(parts.size() == 1);
	CHECK(parts[0] == "");

	// Adjacent separators.
	parts = String("aaa").split("a");
	REQUIRE(parts.size() == 4);
	for (intptr_t i = 0; i < 4; ++i)
		CHECK(parts[i] == "");

	// Multi-byte (UTF-8) separator.
	parts = String("un→deux→trois").split("→");
	REQUIRE(parts.size() == 3);
	CHECK(parts[1] == "deux");

	// Multi-char separator with partial-overlap text.
	parts = String("xx-y-xx--z").split("--");
	REQUIRE(parts.size() == 2);
	CHECK(parts[0] == "xx-y-xx");
	CHECK(parts[1] == "z");

	// Empty separator throws.
	bool threw = false;
	try
	{
		String("abc").split("");
	}
	catch (std::runtime_error &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("String::join and split/join round-trip")
{
	Array<String> items{String("a"), String("b"), String("c")};
	CHECK(String::join(items, ", ") == "a, b, c");
	CHECK(String::join(items, "") == "abc");

	Array<String> empty;
	CHECK(String::join(empty, ",") == "");

	Array<String> one{String("solo")};
	CHECK(String::join(one, ",") == "solo");

	String src("π,e,,φ");
	CHECK(String::join(src.split(","), ",") == src);
}

TEST_CASE("String::arg replaces positional %1..%9")
{
	String t("%1 + %2 = %3");
	t.arg("1", "2", "3");
	CHECK(t == "1 + 2 = 3");

	// Every occurrence of a placeholder is replaced.
	String r("%1%1");
	r.arg("x");
	CHECK(r == "xx");

	// Qt-style sequential substitution: a placeholder inside an earlier argument's
	// text is rewritten by the later ones (old Phonometrica behavior, pinned).
	String s("%1 and %2");
	s.arg("A%2", "B");
	CHECK(s == "AB and B");

	String nine("%1%2%3%4%5%6%7%8%9");
	nine.arg("a", "b", "c", "d", "e", "f", "g", "h", "i");
	CHECK(nine == "abcdefghi");

	// Chaining single-argument calls fills successive placeholders only if the
	// text names them; %2 stays put until substituted.
	String chain("%2-%1");
	chain.arg("first");
	CHECK(chain == "%2-first");
	chain.arg("second"); // no %1 left; %2 untouched by arg(a1)
	CHECK(chain == "%2-first");
}

TEST_CASE("String::replace(Regex): first match, %% and %1..%9 substitution")
{
	// Captures substitute into the replacement.
	String s("hello world");
	s.replace(Regex("wor(l)d"), "W%1D");
	CHECK(s == "hello WlD");

	// %% is the whole match (Perl's $&).
	String t("abc");
	t.replace(Regex("b+"), "[%%]");
	CHECK(t == "a[b]c");

	// Only the first match is replaced.
	String u("aaa");
	u.replace(Regex("a"), "b");
	CHECK(u == "baa");

	// No match: unchanged.
	String v("abc");
	v.replace(Regex("z"), "!");
	CHECK(v == "abc");

	// A non-participating group substitutes the empty string.
	String w("b");
	w.replace(Regex("(a)|(b)"), "[%1|%2]");
	CHECK(w == "[|b]");

	// ntimes bounds the placeholder substitutions inside the replacement text
	// (old Phonometrica semantics, pinned).
	String x("ab");
	x.replace(Regex("(a)"), "%1%1", 1);
	CHECK(x == "a%1b");

	// Multiple captures, non-ASCII subject.
	String y("café au lait");
	y.replace(Regex("(caf.) au (lait)"), "%2 et %1");
	CHECK(y == "lait et café");

	// Anchored replacement at the very start and end.
	String z("prefix-rest");
	z.replace(Regex("^prefix"), "P");
	CHECK(z == "P-rest");
	z.replace(Regex("rest$"), "R");
	CHECK(z == "P-R");
}

// Wide-string interop (roadmap A1 stage 1): old Phonometrica parity for the app's wide
// OS-path code. On this host wchar_t is 4 bytes (UTF-32); the Windows 2-byte (UTF-16)
// branch is compile-selected and not exercised here.
TEST_CASE("String: to_wide / from_wide round-trip and the std::wstring ctor")
{
	// "Aé€🎵" — ASCII, 2-byte, 3-byte, and an astral (4-byte) code point.
	String s("Aé€\U0001F3B5");
	std::wstring w = s.to_wide();
	if constexpr (sizeof(wchar_t) == 4)
		CHECK(w.size() == 4); // one wchar_t per code point in UTF-32
	CHECK(String::from_wide(w) == s);          // round-trip through wstring
	CHECK(String(w) == s);                     // the std::wstring ctor
	CHECK(String::to_wide(std::string_view("hi")).size() == 2); // static overload
	CHECK(String::from_wide(w.data(), static_cast<intptr_t>(w.size())) == s);

	// Empty and pure-ASCII edge cases.
	CHECK(String("").to_wide().empty());
	CHECK(String::from_wide(std::wstring()) == String(""));
}
