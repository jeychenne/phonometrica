// Phonometrica engine — UTF-8 codec and Unicode 16.0 algorithms.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Self-contained UTF-8 handling with prefetched Unicode property tables (no
// third-party dependency — satisfies the §0 invariant; no utf8proc). Adapted
// from calao's UTF-8 layer. The engine's String indexes by code point (design
// §8) and offers grapheme-cluster iteration (UAX #29) as library functions for
// IPA work with combining diacritics.
//
// The property tables live in base/unicode_tables.cpp (generated from Unicode
// 16.0 by tools/unicode/generate_tables.py).

#ifndef PHON_BASE_UNICODE_HPP
#define PHON_BASE_UNICODE_HPP

#include <cstddef>
#include <cstdint>

namespace phonometrica {
namespace unicode {

inline constexpr char32_t REPLACEMENT = 0xFFFD;

// Maximum UTF-8 bytes a single codepoint expands to under case mapping
// (3 codepoints * 4 bytes, e.g. some SpecialCasing rows).
inline constexpr size_t MAX_CASE_EXPANSION = 12;

// --- codec ---

// Decode one UTF-8 sequence at [p, end). On success writes the codepoint and
// returns bytes consumed (1..4) with *out_valid = true. On an ill-formed
// sequence, advances one byte, writes U+FFFD, and sets *out_valid = false
// (WHATWG substitution). Returns 0 only when p == end.
size_t decode(const char *p, const char *end, char32_t *out_cp, bool *out_valid);

// Encode a codepoint into 1..4 UTF-8 bytes. Returns bytes written, or 0 for
// surrogates / out-of-range codepoints.
size_t encode(char32_t cp, char out[4]);

// Decode one UTF-16 unit (or surrogate pair) at [p, end). Returns code units
// consumed (1 or 2); substitution policy as for decode().
size_t utf16_decode(const uint16_t *p, const uint16_t *end, char32_t *out_cp, bool *out_valid);

// Bytes in the codepoint beginning at p (bounded by end); 0 at end. Equivalent
// to decode()'s return value; use when only the length is needed.
size_t codepoint_size(const char *p, const char *end);

// Number of code points in a byte buffer (ill-formed bytes count as one each).
size_t codepoint_count(const char *bytes, size_t len);

// --- properties ---

// True iff cp has the White_Space property (used by trim).
bool is_white_space(char32_t cp);

// UAX #31 identifier classification (with `_` admitted as a start, as usual for
// programming languages).
bool is_id_start(char32_t cp);
bool is_id_continue(char32_t cp);

// --- case mapping ---

// Write the lower/upper-case UTF-8 form of cp into out (up to MAX_CASE_EXPANSION
// bytes). Returns bytes written. Unmapped codepoints round-trip unchanged.
size_t to_lower_cp(char32_t cp, char *out);
size_t to_upper_cp(char32_t cp, char *out);

// Simple (1:1) case mappings for case-insensitive comparison; returns cp
// unchanged when there is no mapping or only a multi-codepoint one.
char32_t to_lower_simple(char32_t cp);
char32_t to_upper_simple(char32_t cp);

// --- UAX #29 grapheme cluster segmentation ---

struct GraphemeIter
{
	const char *cur = nullptr;
	const char *end = nullptr;
	uint8_t prev_props = 0;
	uint8_t ri_count = 0;
	bool gb11_armed = false;
	uint8_t incb_state = 0;
};

void grapheme_init(GraphemeIter &it, const char *bytes, size_t len);

// Bytes in the next grapheme cluster, or 0 at end. it.cur advances past it.
size_t grapheme_next(GraphemeIter &it);

// Number of grapheme clusters in a byte buffer.
size_t grapheme_count(const char *bytes, size_t len);

// Byte offset of the start of the 0-based n-th cluster. n == count returns len;
// n > count returns SIZE_MAX.
size_t grapheme_byte_offset(const char *bytes, size_t len, size_t n);

// 0-based grapheme index of the cluster containing byte_offset (in [0, len]).
size_t byte_to_grapheme(const char *bytes, size_t len, size_t byte_offset);

} // namespace unicode
} // namespace phonometrica

#endif // PHON_BASE_UNICODE_HPP
