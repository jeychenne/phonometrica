// Phonometrica engine — UAX #29 grapheme segmentation conformance test.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Runs the engine's grapheme iterator against the official Unicode 16.0
// GraphemeBreakTest.txt (vendored under tools/unicode/data/auxiliary/). Each line
// encodes a codepoint sequence with break (÷) / no-break (×) markers between
// codepoints; we verify our cluster boundaries match exactly.

#include <phon/engine/base/unicode.hpp>
#include "test_framework.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace phonometrica;

namespace {

// Parse one test line into codepoints and the set of "break before" flags
// (flags[i] == true means a cluster boundary precedes codepoint i). Returns
// false for blank/comment lines.
bool parse_line(const std::string &line, std::vector<char32_t> &cps, std::vector<bool> &break_before)
{
	std::string body = line.substr(0, line.find('#'));
	std::istringstream in(body);
	std::string tok;
	bool pending_break = false;
	bool have_pending = false;
	cps.clear();
	break_before.clear();
	while (in >> tok)
	{
		if (tok == "\xC3\xB7") // ÷ U+00F7 division sign = break
		{
			pending_break = true;
			have_pending = true;
		}
		else if (tok == "\xC3\x97") // × U+00D7 multiplication sign = no break
		{
			pending_break = false;
			have_pending = true;
		}
		else
		{
			// Hex codepoint.
			char32_t cp = static_cast<char32_t>(std::stoul(tok, nullptr, 16));
			cps.push_back(cp);
			break_before.push_back(pending_break);
			have_pending = false;
		}
	}
	(void) have_pending;
	return !cps.empty();
}

} // namespace

TEST_CASE("UAX #29 grapheme break conformance (Unicode 16.0)")
{
#ifndef PHON_UNICODE_DATA_DIR
	std::fprintf(stderr, "  (skipped: PHON_UNICODE_DATA_DIR not defined)\n");
	return;
#else
	const std::string path =
	    std::string(PHON_UNICODE_DATA_DIR) + "/auxiliary/GraphemeBreakTest.txt";
	std::ifstream f(path);
	if (!f)
	{
		std::fprintf(stderr, "  (skipped: cannot open %s)\n", path.c_str());
		return;
	}

	int cases_run = 0;
	int line_no = 0;
	std::string line;
	while (std::getline(f, line))
	{
		++line_no;
		std::vector<char32_t> cps;
		std::vector<bool> break_before;
		if (!parse_line(line, cps, break_before))
			continue;

		// Build the UTF-8 string, recording each codepoint's byte offset, and the
		// expected boundary offsets (a boundary precedes any codepoint flagged as
		// a break, plus the start and end of the string).
		std::string bytes;
		std::vector<size_t> expected;
		expected.push_back(0);
		char buf[4];
		for (size_t i = 0; i < cps.size(); ++i)
		{
			if (i > 0 && break_before[i])
				expected.push_back(bytes.size());
			size_t n = unicode::encode(cps[i], buf);
			if (n == 0) // surrogate/out-of-range: encode as replacement (test data avoids these)
				n = unicode::encode(unicode::REPLACEMENT, buf);
			bytes.append(buf, n);
		}
		expected.push_back(bytes.size());

		// Actual boundaries from the iterator.
		std::vector<size_t> actual;
		actual.push_back(0);
		unicode::GraphemeIter it;
		unicode::grapheme_init(it, bytes.data(), bytes.size());
		for (;;)
		{
			size_t n = unicode::grapheme_next(it);
			if (n == 0)
				break;
			actual.push_back(static_cast<size_t>(it.cur - bytes.data()));
		}

		if (actual != expected)
		{
			std::fprintf(stderr, "  line %d: boundary mismatch\n", line_no);
			REQUIRE(actual == expected);
		}
		++cases_run;
	}
	// The Unicode 16.0 suite has well over a thousand cases.
	CHECK(cases_run > 1000);
	std::fprintf(stderr, "  UAX #29 conformance: %d test sequences passed\n", cases_run);
#endif
}
