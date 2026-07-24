// Phonometrica engine — UTF-8 / Unicode 16.0 tests.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// This file is UTF-8 encoded; narrow string literals below carry UTF-8 bytes.

#include <phon/engine/base/unicode.hpp>
#include "test_framework.hpp"

#include <cstdint>
#include <cstring>
#include <string>

using namespace phonometrica;

namespace {

std::string cp_to_utf8(char32_t cp)
{
	char buf[4];
	size_t n = unicode::encode(cp, buf);
	return std::string(buf, n);
}

} // namespace

TEST_CASE("UTF-8 encode/decode round-trip across planes")
{
	char32_t cps[] = {
	    0x41,      // 'A' 1 byte
	    0xE9,      // 'é' 2 bytes
	    0x3B1,     // 'α' 2 bytes
	    0x20AC,    // '€' 3 bytes
	    0x1D11E,   // '𝄞' 4 bytes (musical G clef)
	    0x1F600,   // '😀' 4 bytes
	    0x2C7,     // 'ˇ' modifier
	    0x0301,    // combining acute
	};
	for (char32_t cp : cps)
	{
		char buf[4];
		size_t n = unicode::encode(cp, buf);
		REQUIRE(n >= 1 && n <= 4);
		char32_t back;
		bool valid;
		size_t m = unicode::decode(buf, buf + n, &back, &valid);
		CHECK(valid);
		CHECK(m == n);
		CHECK(back == cp);
	}
}

TEST_CASE("UTF-8 byte lengths are correct")
{
	char buf[4];
	CHECK(unicode::encode(0x41, buf) == 1);
	CHECK(unicode::encode(0xE9, buf) == 2);
	CHECK(unicode::encode(0x20AC, buf) == 3);
	CHECK(unicode::encode(0x1F600, buf) == 4);
	// Surrogates are unencodable.
	CHECK(unicode::encode(0xD800, buf) == 0);
}

TEST_CASE("UTF-8 decode substitutes ill-formed sequences and advances")
{
	// Lone continuation byte.
	{
		const char s[] = {(char) 0x80, 'A', 0};
		char32_t cp;
		bool valid;
		size_t n = unicode::decode(s, s + 2, &cp, &valid);
		CHECK(n == 1);
		CHECK(!valid);
		CHECK(cp == unicode::REPLACEMENT);
	}
	// Overlong 2-byte encoding of '/'.
	{
		const char s[] = {(char) 0xC0, (char) 0xAF, 0};
		char32_t cp;
		bool valid;
		size_t n = unicode::decode(s, s + 2, &cp, &valid);
		CHECK(n == 1);
		CHECK(!valid);
	}
	// Truncated 3-byte sequence.
	{
		const char s[] = {(char) 0xE2, (char) 0x82, 0};
		char32_t cp;
		bool valid;
		size_t n = unicode::decode(s, s + 2, &cp, &valid);
		CHECK(n == 1);
		CHECK(!valid);
	}
	// Surrogate encoded in UTF-8 (ED A0 80 = U+D800) is rejected.
	{
		const char s[] = {(char) 0xED, (char) 0xA0, (char) 0x80, 0};
		char32_t cp;
		bool valid;
		size_t n = unicode::decode(s, s + 3, &cp, &valid);
		CHECK(!valid);
		CHECK(n == 1);
	}
}

TEST_CASE("codepoint_count")
{
	CHECK(unicode::codepoint_count("hello", 5) == 5);
	std::string s = "café"; // c a f + é(2 bytes) = 5 bytes, 4 codepoints
	CHECK(s.size() == 5);
	CHECK(unicode::codepoint_count(s.data(), s.size()) == 4);
	std::string g = "𝄞😀"; // two 4-byte codepoints
	CHECK(g.size() == 8);
	CHECK(unicode::codepoint_count(g.data(), g.size()) == 2);
}

TEST_CASE("grapheme segmentation: combining marks")
{
	// 'e' + U+0301 combining acute = one grapheme, two codepoints.
	std::string s = cp_to_utf8(0x65) + cp_to_utf8(0x0301);
	CHECK(unicode::codepoint_count(s.data(), s.size()) == 2);
	CHECK(unicode::grapheme_count(s.data(), s.size()) == 1);
}

TEST_CASE("grapheme segmentation: IPA with diacritics")
{
	// A phonetician's string: aspirated p, nasalized/long vowels, tone marks.
	// p + ʰ(U+02B0), a + ̃(U+0303 combining tilde), o + ː(U+02D0 length mark
	// is a spacing modifier -> its own cluster), plus e + ́(U+0301).
	std::string s;
	s += "p";
	s += cp_to_utf8(0x02B0); // ʰ modifier letter (spacing) -> separate cluster
	s += "a";
	s += cp_to_utf8(0x0303); // combining tilde -> joins 'a'
	s += "e";
	s += cp_to_utf8(0x0301); // combining acute -> joins 'e'
	// Clusters: [p][ʰ][a+tilde][e+acute] = 4 graphemes, 6 codepoints.
	CHECK(unicode::codepoint_count(s.data(), s.size()) == 6);
	CHECK(unicode::grapheme_count(s.data(), s.size()) == 4);
}

TEST_CASE("grapheme segmentation: emoji ZWJ and flags")
{
	// Family emoji: 👨‍👩‍👧 = man ZWJ woman ZWJ girl = 1 grapheme, 5 codepoints.
	std::string family = cp_to_utf8(0x1F468) + cp_to_utf8(0x200D) + cp_to_utf8(0x1F469) +
	                     cp_to_utf8(0x200D) + cp_to_utf8(0x1F467);
	CHECK(unicode::codepoint_count(family.data(), family.size()) == 5);
	CHECK(unicode::grapheme_count(family.data(), family.size()) == 1);

	// Regional indicator pair = one flag = 1 grapheme, 2 codepoints.
	std::string flag = cp_to_utf8(0x1F1EB) + cp_to_utf8(0x1F1F7); // FR
	CHECK(unicode::grapheme_count(flag.data(), flag.size()) == 1);
	// Three RIs = flag + lone RI = 2 graphemes.
	std::string three = flag + cp_to_utf8(0x1F1E9);
	CHECK(unicode::grapheme_count(three.data(), three.size()) == 2);
}

TEST_CASE("grapheme byte offsets and inverse")
{
	// "a" + é(2B) + 😀(4B): clusters at byte 0, 1, 3; total len 7.
	std::string s = "a" + cp_to_utf8(0xE9) + cp_to_utf8(0x1F600);
	CHECK(s.size() == 7);
	CHECK(unicode::grapheme_count(s.data(), s.size()) == 3);
	CHECK(unicode::grapheme_byte_offset(s.data(), s.size(), 0) == 0);
	CHECK(unicode::grapheme_byte_offset(s.data(), s.size(), 1) == 1);
	CHECK(unicode::grapheme_byte_offset(s.data(), s.size(), 2) == 3);
	CHECK(unicode::grapheme_byte_offset(s.data(), s.size(), 3) == 7); // == len
	CHECK(unicode::grapheme_byte_offset(s.data(), s.size(), 4) == (size_t) -1);
	// Inverse: a byte inside the third cluster maps back to index 2.
	CHECK(unicode::byte_to_grapheme(s.data(), s.size(), 0) == 0);
	CHECK(unicode::byte_to_grapheme(s.data(), s.size(), 1) == 1);
	CHECK(unicode::byte_to_grapheme(s.data(), s.size(), 4) == 2); // mid 😀
	CHECK(unicode::byte_to_grapheme(s.data(), s.size(), 7) == 3);
}

TEST_CASE("case mapping")
{
	char buf[unicode::MAX_CASE_EXPANSION];
	// ASCII.
	CHECK(unicode::to_upper_simple('a') == 'A');
	CHECK(unicode::to_lower_simple('Z') == 'z');
	// é <-> É.
	CHECK(unicode::to_upper_simple(0xE9) == 0xC9);
	CHECK(unicode::to_lower_simple(0xC9) == 0xE9);
	// ß uppercases to "SS" (multi-codepoint) via the full mapping.
	size_t n = unicode::to_upper_cp(0xDF, buf);
	CHECK(n == 2);
	CHECK(std::string(buf, n) == "SS");
	// Simple mapping leaves ß unchanged (no 1:1 mapping).
	CHECK(unicode::to_lower_simple('A') == 'a');
	CHECK(unicode::to_upper_simple(0xDF) == 0xDF);
	// Codepoints with no case round-trip.
	n = unicode::to_lower_cp(0x1F600, buf);
	CHECK(std::string(buf, n) == cp_to_utf8(0x1F600));
}

TEST_CASE("white space and identifier classification")
{
	CHECK(unicode::is_white_space(' '));
	CHECK(unicode::is_white_space('\t'));
	CHECK(unicode::is_white_space('\n'));
	CHECK(unicode::is_white_space(0x00A0)); // no-break space
	CHECK(!unicode::is_white_space('a'));

	CHECK(unicode::is_id_start('a'));
	CHECK(unicode::is_id_start('_'));
	CHECK(unicode::is_id_start(0x3B1)); // Greek alpha
	CHECK(!unicode::is_id_start('1'));
	CHECK(unicode::is_id_continue('1'));
	CHECK(unicode::is_id_continue('a'));
	CHECK(!unicode::is_id_continue(' '));
}

TEST_CASE("UTF-16 decode incl. surrogate pairs")
{
	// U+1F600 as surrogate pair D83D DE00.
	uint16_t units[] = {0xD83D, 0xDE00};
	char32_t cp;
	bool valid;
	size_t n = unicode::utf16_decode(units, units + 2, &cp, &valid);
	CHECK(valid);
	CHECK(n == 2);
	CHECK(cp == 0x1F600);
	// Lone high surrogate.
	uint16_t lone[] = {0xD83D, 0x0041};
	n = unicode::utf16_decode(lone, lone + 2, &cp, &valid);
	CHECK(!valid);
	CHECK(n == 1);
}
