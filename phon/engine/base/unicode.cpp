// Phonometrica engine — UTF-8 codec and UAX #29 segmentation implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Adapted from calao's src/runtime/utf8.c (Unicode 16.0). The property tables
// are in base/unicode_tables.cpp.

#include <phon/engine/base/unicode.hpp>

#include <phon/engine/base/unicode_tables.hpp>

namespace phonometrica {
namespace unicode {

// Pack masks — must agree with tools/unicode/generate_tables.py.
namespace {

constexpr uint8_t GCB_MASK = 0x0Fu;
constexpr uint8_t INCB_SHIFT = 4u;
constexpr uint8_t INCB_MASK = 0x03u;
constexpr uint8_t EXTPICT_MASK = 0x40u;

enum
{
	GCB_Other = 0,
	GCB_CR = 1,
	GCB_LF = 2,
	GCB_Control = 3,
	GCB_Extend = 4,
	GCB_Regional_Indicator = 5,
	GCB_Prepend = 6,
	GCB_SpacingMark = 7,
	GCB_L = 8,
	GCB_V = 9,
	GCB_T = 10,
	GCB_LV = 11,
	GCB_LVT = 12,
	GCB_ZWJ = 13,
};

enum
{
	InCB_None = 0,
	InCB_Consonant = 1,
	InCB_Extend = 2,
	InCB_Linker = 3,
};

uint8_t grapheme_props(char32_t cp)
{
	if (cp >= 0x110000u)
		return 0;
	uint32_t block = cp >> 8;
	uint32_t off = cp & 0xFFu;
	uint8_t idx = phon_uni_stage1[block];
	return phon_uni_stage2[(static_cast<size_t>(idx) << 8) + off];
}

} // namespace

// ---------------------------------------------------------------------------
// UTF-8 codec
// ---------------------------------------------------------------------------

size_t decode(const char *p, const char *end, char32_t *out_cp, bool *out_valid)
{
	if (p >= end)
	{
		*out_cp = 0;
		*out_valid = false;
		return 0;
	}
	uint8_t b0 = static_cast<uint8_t>(p[0]);

	if (b0 < 0x80u)
	{
		*out_cp = b0;
		*out_valid = true;
		return 1;
	}

	if ((b0 & 0xC0u) == 0x80u)
	{
		*out_cp = REPLACEMENT;
		*out_valid = false;
		return 1;
	}

	size_t want;
	uint32_t cp;
	uint32_t lo, hi;

	if ((b0 & 0xE0u) == 0xC0u)
	{
		want = 2;
		cp = b0 & 0x1Fu;
		lo = 0x80u;
		hi = 0xBFu;
		if (cp < 0x02u)
		{
			*out_cp = REPLACEMENT;
			*out_valid = false;
			return 1;
		}
	}
	else if ((b0 & 0xF0u) == 0xE0u)
	{
		want = 3;
		cp = b0 & 0x0Fu;
		if (b0 == 0xE0u)
		{
			lo = 0xA0u;
			hi = 0xBFu;
		}
		else if (b0 == 0xEDu)
		{
			lo = 0x80u;
			hi = 0x9Fu;
		}
		else
		{
			lo = 0x80u;
			hi = 0xBFu;
		}
	}
	else if ((b0 & 0xF8u) == 0xF0u)
	{
		want = 4;
		cp = b0 & 0x07u;
		if (b0 == 0xF0u)
		{
			lo = 0x90u;
			hi = 0xBFu;
		}
		else if (b0 == 0xF4u)
		{
			lo = 0x80u;
			hi = 0x8Fu;
		}
		else if (b0 > 0xF4u)
		{
			*out_cp = REPLACEMENT;
			*out_valid = false;
			return 1;
		}
		else
		{
			lo = 0x80u;
			hi = 0xBFu;
		}
	}
	else
	{
		*out_cp = REPLACEMENT;
		*out_valid = false;
		return 1;
	}

	if (static_cast<size_t>(end - p) < want)
	{
		*out_cp = REPLACEMENT;
		*out_valid = false;
		return 1;
	}

	uint8_t b1 = static_cast<uint8_t>(p[1]);
	if (b1 < lo || b1 > hi)
	{
		*out_cp = REPLACEMENT;
		*out_valid = false;
		return 1;
	}
	cp = (cp << 6) | (b1 & 0x3Fu);

	for (size_t i = 2; i < want; i++)
	{
		uint8_t bi = static_cast<uint8_t>(p[i]);
		if ((bi & 0xC0u) != 0x80u)
		{
			*out_cp = REPLACEMENT;
			*out_valid = false;
			return 1;
		}
		cp = (cp << 6) | (bi & 0x3Fu);
	}

	*out_cp = cp;
	*out_valid = true;
	return want;
}

size_t encode(char32_t cp, char out[4])
{
	if (cp < 0x80u)
	{
		out[0] = static_cast<char>(cp);
		return 1;
	}
	if (cp < 0x800u)
	{
		out[0] = static_cast<char>(0xC0u | (cp >> 6));
		out[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
		return 2;
	}
	if (cp < 0x10000u)
	{
		if (cp >= 0xD800u && cp <= 0xDFFFu)
			return 0;
		out[0] = static_cast<char>(0xE0u | (cp >> 12));
		out[1] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
		out[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
		return 3;
	}
	if (cp < 0x110000u)
	{
		out[0] = static_cast<char>(0xF0u | (cp >> 18));
		out[1] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
		out[2] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
		out[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
		return 4;
	}
	return 0;
}

size_t utf16_decode(const uint16_t *p, const uint16_t *end, char32_t *out_cp, bool *out_valid)
{
	if (p >= end)
		return 0;
	uint16_t u0 = p[0];
	if (u0 < 0xD800u || u0 > 0xDFFFu)
	{
		*out_cp = u0;
		*out_valid = true;
		return 1;
	}
	if (u0 >= 0xDC00u)
	{
		*out_cp = REPLACEMENT;
		*out_valid = false;
		return 1;
	}
	if (p + 1 >= end)
	{
		*out_cp = REPLACEMENT;
		*out_valid = false;
		return 1;
	}
	uint16_t u1 = p[1];
	if (u1 < 0xDC00u || u1 > 0xDFFFu)
	{
		*out_cp = REPLACEMENT;
		*out_valid = false;
		return 1;
	}
	*out_cp = 0x10000u + (static_cast<uint32_t>(u0 - 0xD800u) << 10) +
	          static_cast<uint32_t>(u1 - 0xDC00u);
	*out_valid = true;
	return 2;
}

size_t codepoint_size(const char *p, const char *end)
{
	char32_t cp;
	bool valid;
	return decode(p, end, &cp, &valid);
}

size_t codepoint_count(const char *bytes, size_t len)
{
	const char *p = bytes;
	const char *end = bytes + len;
	size_t n = 0;
	while (p < end)
	{
		char32_t cp;
		bool valid;
		p += decode(p, end, &cp, &valid);
		++n;
	}
	return n;
}

// ---------------------------------------------------------------------------
// UAX #29 grapheme cluster segmentation (Unicode 16.0)
// ---------------------------------------------------------------------------

void grapheme_init(GraphemeIter &it, const char *bytes, size_t len)
{
	it.cur = bytes;
	it.end = bytes + len;
	it.prev_props = 0;
	it.ri_count = 0;
	it.gb11_armed = false;
	it.incb_state = 0;
}

namespace {

// gb11 prefix lives in the high bit of incb_state to keep state compact.
inline uint8_t it_incb(const GraphemeIter &it) { return it.incb_state & 0x03u; }
inline void it_incb_set(GraphemeIter &it, uint8_t v)
{
	it.incb_state = static_cast<uint8_t>((it.incb_state & 0x80u) | (v & 0x03u));
}
inline bool it_prefix(const GraphemeIter &it) { return (it.incb_state & 0x80u) != 0; }
inline void it_prefix_set(GraphemeIter &it, bool b)
{
	it.incb_state = static_cast<uint8_t>((it.incb_state & 0x7Fu) | (b ? 0x80u : 0u));
}

void init_cluster_state(GraphemeIter &it, uint8_t props)
{
	uint8_t gcb = props & GCB_MASK;
	uint8_t incb = (props >> INCB_SHIFT) & INCB_MASK;
	bool ext = (props & EXTPICT_MASK) != 0;

	it.prev_props = props;
	it.ri_count = (gcb == GCB_Regional_Indicator) ? 1u : 0u;
	it.gb11_armed = false;
	it.incb_state = 0;
	it_incb_set(it, (incb == InCB_Consonant) ? 1u : 0u);
	it_prefix_set(it, ext);
}

bool should_break(const GraphemeIter &it, uint8_t rhs_props)
{
	uint8_t lhs_gcb = it.prev_props & GCB_MASK;
	uint8_t rhs_gcb = rhs_props & GCB_MASK;
	uint8_t rhs_incb = (rhs_props >> INCB_SHIFT) & INCB_MASK;
	bool rhs_ext = (rhs_props & EXTPICT_MASK) != 0;

	// GB3: CR x LF
	if (lhs_gcb == GCB_CR && rhs_gcb == GCB_LF)
		return false;
	// GB4: (Control|CR|LF) divide
	if (lhs_gcb == GCB_Control || lhs_gcb == GCB_CR || lhs_gcb == GCB_LF)
		return true;
	// GB5: divide (Control|CR|LF)
	if (rhs_gcb == GCB_Control || rhs_gcb == GCB_CR || rhs_gcb == GCB_LF)
		return true;
	// GB6..GB8: Hangul
	if (lhs_gcb == GCB_L &&
	    (rhs_gcb == GCB_L || rhs_gcb == GCB_V || rhs_gcb == GCB_LV || rhs_gcb == GCB_LVT))
		return false;
	if ((lhs_gcb == GCB_LV || lhs_gcb == GCB_V) && (rhs_gcb == GCB_V || rhs_gcb == GCB_T))
		return false;
	if ((lhs_gcb == GCB_LVT || lhs_gcb == GCB_T) && rhs_gcb == GCB_T)
		return false;
	// GB9: x (Extend | ZWJ)
	if (rhs_gcb == GCB_Extend || rhs_gcb == GCB_ZWJ)
		return false;
	// GB9a: x SpacingMark
	if (rhs_gcb == GCB_SpacingMark)
		return false;
	// GB9b: Prepend x
	if (lhs_gcb == GCB_Prepend)
		return false;
	// GB9c: InCB chain closing on a Consonant
	if (it_incb(it) == 2u && rhs_incb == InCB_Consonant)
		return false;
	// GB11: ExtPict Extend* ZWJ x ExtPict
	if (it.gb11_armed && rhs_ext)
		return false;
	// GB12/GB13: RI x RI with odd running RI count
	if (lhs_gcb == GCB_Regional_Indicator && rhs_gcb == GCB_Regional_Indicator &&
	    (it.ri_count & 1u) == 1u)
		return false;
	// GB999
	return true;
}

void join_state(GraphemeIter &it, uint8_t props)
{
	uint8_t gcb = props & GCB_MASK;
	uint8_t incb = (props >> INCB_SHIFT) & INCB_MASK;
	bool ext = (props & EXTPICT_MASK) != 0;

	if (gcb == GCB_Regional_Indicator)
		it.ri_count = static_cast<uint8_t>(it.ri_count + 1u);
	else
		it.ri_count = 0;

	bool prefix = it_prefix(it);
	if (gcb == GCB_ZWJ)
	{
		if (prefix)
			it.gb11_armed = true;
		prefix = false;
	}
	else if (ext)
	{
		it.gb11_armed = false;
		prefix = true;
	}
	else if (gcb == GCB_Extend)
	{
		// prefix sticks
	}
	else
	{
		prefix = false;
		it.gb11_armed = false;
	}
	it_prefix_set(it, prefix);

	uint8_t st = it_incb(it);
	if (incb == InCB_Linker)
	{
		if (st == 1u || st == 2u)
			st = 2u;
	}
	else if (incb == InCB_Extend)
	{
		// no change
	}
	else if (incb == InCB_Consonant)
	{
		st = 1u;
	}
	else
	{
		if (gcb != GCB_Extend && gcb != GCB_ZWJ)
			st = 0u;
	}
	it_incb_set(it, st);

	it.prev_props = props;
}

} // namespace

size_t grapheme_next(GraphemeIter &it)
{
	if (it.cur >= it.end)
		return 0;

	const char *cluster_start = it.cur;

	char32_t cp;
	bool valid;
	size_t n = decode(it.cur, it.end, &cp, &valid);
	init_cluster_state(it, grapheme_props(cp));
	it.cur += n;

	while (it.cur < it.end)
	{
		const char *cp_start = it.cur;
		char32_t cp2;
		bool v2;
		size_t n2 = decode(cp_start, it.end, &cp2, &v2);
		uint8_t props2 = grapheme_props(cp2);
		if (should_break(it, props2))
			return static_cast<size_t>(cp_start - cluster_start);
		join_state(it, props2);
		it.cur = cp_start + n2;
	}

	return static_cast<size_t>(it.cur - cluster_start);
}

size_t grapheme_count(const char *bytes, size_t len)
{
	GraphemeIter it;
	grapheme_init(it, bytes, len);
	size_t count = 0;
	while (grapheme_next(it) != 0)
		count++;
	return count;
}

size_t grapheme_byte_offset(const char *bytes, size_t len, size_t n)
{
	if (n == 0)
		return 0;
	GraphemeIter it;
	grapheme_init(it, bytes, len);
	for (size_t i = 0; i < n; i++)
	{
		if (grapheme_next(it) == 0)
			return static_cast<size_t>(-1);
	}
	return static_cast<size_t>(it.cur - bytes);
}

size_t byte_to_grapheme(const char *bytes, size_t len, size_t byte_offset)
{
	if (byte_offset >= len)
		return grapheme_count(bytes, len);
	GraphemeIter it;
	grapheme_init(it, bytes, len);
	size_t idx = 0;
	while (it.cur < it.end)
	{
		size_t cluster_start = static_cast<size_t>(it.cur - bytes);
		size_t n = grapheme_next(it);
		if (n == 0)
			break;
		if (byte_offset < cluster_start + n)
			return idx;
		idx++;
	}
	return idx;
}

// ---------------------------------------------------------------------------
// Case mapping
// ---------------------------------------------------------------------------

namespace {

uint32_t case_lookup(char32_t cp, const uint8_t *stage1, const uint32_t *stage2)
{
	if (cp >= 0x110000u)
		return 0;
	uint8_t idx = stage1[cp >> 8];
	return stage2[(static_cast<size_t>(idx) << 8) + (cp & 0xFFu)];
}

size_t encode_or_replace(char32_t cp, char *out)
{
	size_t n = encode(cp, out);
	if (n == 0)
		n = encode(REPLACEMENT, out);
	return n;
}

size_t apply_case(char32_t cp, char *out, const uint8_t *stage1, const uint32_t *stage2,
                  const uint32_t *side)
{
	uint32_t mapping = case_lookup(cp, stage1, stage2);
	if (mapping == 0)
		return encode_or_replace(cp, out);
	if ((mapping & 0xFFFF0000u) == 0xFFFF0000u)
	{
		size_t idx = mapping & 0xFFFFu;
		size_t off = idx * 4u;
		uint32_t cnt = side[off];
		size_t wrote = 0;
		for (uint32_t i = 0; i < cnt; i++)
			wrote += encode_or_replace(side[off + 1u + i], out + wrote);
		return wrote;
	}
	return encode_or_replace(mapping, out);
}

uint32_t case_simple(char32_t cp, const uint8_t *stage1, const uint32_t *stage2)
{
	uint32_t mapping = case_lookup(cp, stage1, stage2);
	if (mapping == 0)
		return cp;
	if ((mapping & 0xFFFF0000u) == 0xFFFF0000u)
		return cp;
	return mapping;
}

} // namespace

size_t to_lower_cp(char32_t cp, char *out)
{
	return apply_case(cp, out, phon_uni_lower_stage1, phon_uni_lower_stage2, phon_uni_lower_side);
}

size_t to_upper_cp(char32_t cp, char *out)
{
	return apply_case(cp, out, phon_uni_upper_stage1, phon_uni_upper_stage2, phon_uni_upper_side);
}

char32_t to_lower_simple(char32_t cp)
{
	return case_simple(cp, phon_uni_lower_stage1, phon_uni_lower_stage2);
}

char32_t to_upper_simple(char32_t cp)
{
	return case_simple(cp, phon_uni_upper_stage1, phon_uni_upper_stage2);
}

// ---------------------------------------------------------------------------
// White_Space and identifiers
// ---------------------------------------------------------------------------

bool is_white_space(char32_t cp)
{
	for (size_t i = 0; i < phon_uni_white_space_range_count; i++)
	{
		uint32_t lo = phon_uni_white_space_ranges[i * 2u];
		uint32_t hi = phon_uni_white_space_ranges[i * 2u + 1u];
		if (cp < lo)
			return false;
		if (cp <= hi)
			return true;
	}
	return false;
}

namespace {

uint8_t id_props(char32_t cp)
{
	if (cp >= 0x110000u)
		return 0;
	uint8_t idx = phon_uni_id_stage1[cp >> 8];
	return phon_uni_id_stage2[(static_cast<size_t>(idx) << 8) + (cp & 0xFFu)];
}

} // namespace

bool is_id_start(char32_t cp)
{
	if (cp == 0x5Fu)
		return true;
	return (id_props(cp) & 0x01u) != 0u;
}

bool is_id_continue(char32_t cp)
{
	return (id_props(cp) & 0x02u) != 0u;
}

} // namespace unicode
} // namespace phonometrica
