// Phonometrica engine — String implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "types/string.hpp"

#include "base/alloc.hpp"
#include "base/unicode.hpp"
#include "core/hash.hpp"
#include "object/class.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace phonometrica {

namespace {

constexpr intptr_t STRING_HEADER = offsetof(StringCell, data);
constexpr intptr_t CRUMB_MASK = GRAPHEME_CRUMB_STRIDE - 1;

// Capacity schedule (bytes, incl. NUL): min 16, double to 64, then 1.5x.
intptr_t str_capacity_for(intptr_t need)
{
	intptr_t cap = 16;
	while (cap < need)
		cap = (cap < 64) ? (cap << 1) : (cap + (cap >> 1));
	return cap;
}

StringCell *string_create(const char *str, intptr_t len, intptr_t cap_hint)
{
	PHON_ASSERT(len >= 0);
	intptr_t need = len + 1;
	intptr_t capacity = str_capacity_for(cap_hint > need ? cap_hint : need);
	Cell *c = cell_alloc(CID_STRING, STRING_HEADER + capacity);
	auto *s = reinterpret_cast<StringCell *>(c);
	s->byte_size = len;
	s->capacity = capacity;
	s->grapheme_length = -1;
	s->hash = 0;
	s->crumbs = nullptr;
	s->flags = 0;
	s->pad_ = 0;
	if (str && len > 0)
		std::memcpy(s->data, str, static_cast<size_t>(len));
	s->data[len] = '\0';
	return s;
}

void free_crumbs(StringCell *s) noexcept
{
	if (s->crumbs)
	{
		raw_free(s->crumbs, static_cast<intptr_t>(alignof(intptr_t)));
		s->crumbs = nullptr;
	}
}

void string_finalize(Cell *c)
{
	free_crumbs(reinterpret_cast<StringCell *>(c));
}

uint64_t string_hash_hook(const Cell *c)
{
	auto *s = reinterpret_cast<StringCell *>(const_cast<Cell *>(c));
	if (!(s->flags & StringCell::SF_HASHED))
	{
		s->hash = hash_bytes(s->data, s->byte_size);
		s->flags |= StringCell::SF_HASHED;
	}
	return s->hash;
}

bool string_equals_hook(const Cell *a, const Cell *b)
{
	auto *sa = reinterpret_cast<const StringCell *>(a);
	auto *sb = reinterpret_cast<const StringCell *>(b);
	if (sa->byte_size != sb->byte_size)
		return false;
	return std::memcmp(sa->data, sb->data, static_cast<size_t>(sa->byte_size)) == 0;
}

Class g_string_class;

// Compute grapheme_length (cached) for a cell.
intptr_t ensure_grapheme_length(StringCell *s)
{
	if (s->grapheme_length < 0)
		s->grapheme_length =
		    static_cast<intptr_t>(unicode::grapheme_count(s->data, static_cast<size_t>(s->byte_size)));
	return s->grapheme_length;
}

// Build the grapheme breadcrumb index: crumbs[k] = byte offset of grapheme
// (k * STRIDE). Requires grapheme_length already computed.
void build_crumbs(StringCell *s)
{
	intptr_t gl = s->grapheme_length;
	PHON_ASSERT(gl >= 0);
	intptr_t n = (gl <= 0) ? 1 : (((gl - 1) >> GRAPHEME_CRUMB_SHIFT) + 1);
	auto *cr = static_cast<intptr_t *>(
	    raw_alloc(n * static_cast<intptr_t>(sizeof(intptr_t)), static_cast<intptr_t>(alignof(intptr_t))));
	unicode::GraphemeIter it;
	unicode::grapheme_init(it, s->data, static_cast<size_t>(s->byte_size));
	const char *base = s->data;
	intptr_t gi = 0;
	while (it.cur < it.end)
	{
		if ((gi & CRUMB_MASK) == 0)
			cr[gi >> GRAPHEME_CRUMB_SHIFT] = static_cast<intptr_t>(it.cur - base);
		if (unicode::grapheme_next(it) == 0)
			break;
		++gi;
	}
	s->crumbs = cr;
}

// Byte pointer at the start of 0-based grapheme `gi` (0 <= gi <= grapheme_length).
const char *grapheme_ptr(StringCell *s, intptr_t gi)
{
	intptr_t gl = ensure_grapheme_length(s);
	if (gi <= 0)
		return s->data;
	if (gi >= gl)
		return s->data + s->byte_size;
	if (!s->crumbs)
		build_crumbs(s);
	intptr_t k = gi >> GRAPHEME_CRUMB_SHIFT;
	const char *p = s->data + s->crumbs[k];
	intptr_t remaining = gi - (k << GRAPHEME_CRUMB_SHIFT);
	unicode::GraphemeIter it;
	unicode::grapheme_init(it, p, static_cast<size_t>(s->data + s->byte_size - p));
	while (remaining-- > 0 && it.cur < it.end)
		unicode::grapheme_next(it);
	return it.cur;
}

} // namespace

void register_string_class()
{
	g_string_class.id = CID_STRING;
	g_string_class.name = "String";
	g_string_class.base = get_class(CID_OBJECT);
	g_string_class.flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC;
	g_string_class.instance_size = -1; // variable
	g_string_class.finalize = &string_finalize;
	g_string_class.hash = &string_hash_hook;
	g_string_class.equals = &string_equals_hook;
	register_class(&g_string_class);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

String::String() : m_impl(Handle<StringCell>::adopt(string_create(nullptr, 0, 0))) {}

String::String(const char *str, intptr_t len)
    : m_impl(Handle<StringCell>::adopt(string_create(str, len, 0)))
{
}

String::String(const char *str) : String(str, str ? static_cast<intptr_t>(std::strlen(str)) : 0) {}

String::String(Substring s) : String(s.data(), static_cast<intptr_t>(s.size())) {}

String::String(const std::string &s) : String(s.data(), static_cast<intptr_t>(s.size())) {}

String::String(intptr_t capacity, bool exact)
    : m_impl(Handle<StringCell>::adopt(string_create(nullptr, 0, exact ? capacity + 1 : capacity)))
{
}

String::String(char32_t codepoint, intptr_t count)
{
	char buf[4];
	size_t n = unicode::encode(codepoint, buf);
	if (n == 0)
	{
		n = unicode::encode(unicode::REPLACEMENT, buf);
	}
	intptr_t total = static_cast<intptr_t>(n) * (count > 0 ? count : 0);
	StringCell *s = string_create(nullptr, 0, total + 1);
	char *out = s->data;
	for (intptr_t i = 0; i < count; ++i)
	{
		std::memcpy(out, buf, n);
		out += n;
	}
	s->byte_size = total;
	s->data[total] = '\0';
	m_impl = Handle<StringCell>::adopt(s);
}

String String::from_value(Value v) noexcept
{
	PHON_ASSERT(v.is_cell() && v.as_cell()->class_id() == CID_STRING);
	return String(Handle<StringCell>(reinterpret_cast<StringCell *>(v.as_cell())));
}

// ---------------------------------------------------------------------------
// CoW plumbing
// ---------------------------------------------------------------------------

StringCell *String::detach_for_write(intptr_t need_bytes)
{
	StringCell *s = m_impl.get();
	bool shared = !m_impl.unique();
	bool too_small = s->capacity < need_bytes;

	if (!shared && !too_small)
		return s;

	if (!shared)
	{
		intptr_t target = need_bytes > s->capacity ? need_bytes : s->capacity;
		intptr_t newcap = str_capacity_for(target);
		free_crumbs(s); // stale after the coming mutation; also avoids dangling across realloc
		Cell *moved = cell_realloc(&s->header, STRING_HEADER + newcap);
		auto *ns = reinterpret_cast<StringCell *>(moved);
		ns->capacity = newcap;
		m_impl.reset_reallocated(ns);
		return ns;
	}

	// Shared: clone into a fresh unique cell.
	intptr_t newcap = str_capacity_for(need_bytes > s->byte_size + 1 ? need_bytes : s->byte_size + 1);
	StringCell *clone = string_create(s->data, s->byte_size, newcap);
	m_impl = Handle<StringCell>::adopt(clone);
	return clone;
}

void String::invalidate() noexcept
{
	StringCell *s = m_impl.get();
	free_crumbs(s);
	s->grapheme_length = -1;
	s->flags &= ~StringCell::SF_HASHED;
}

void String::unshare()
{
	if (!m_impl.unique())
		detach_for_write(m_impl->byte_size + 1);
}

void String::shrink_to_fit()
{
	StringCell *s = m_impl.get();
	if (m_impl.unique() && s->capacity > s->byte_size + 1)
	{
		intptr_t newcap = s->byte_size + 1;
		free_crumbs(s);
		Cell *moved = cell_realloc(&s->header, STRING_HEADER + newcap);
		auto *ns = reinterpret_cast<StringCell *>(moved);
		ns->capacity = newcap;
		m_impl.reset_reallocated(ns);
	}
}

// ---------------------------------------------------------------------------
// Length, indexing, iteration
// ---------------------------------------------------------------------------

bool String::is_ascii() const
{
	StringCell *s = m_impl.get();
	for (intptr_t i = 0; i < s->byte_size; ++i)
		if (static_cast<unsigned char>(s->data[i]) >= 0x80u)
			return false;
	return true;
}

size_t String::hash() const
{
	return static_cast<size_t>(string_hash_hook(&m_impl->header));
}

intptr_t String::grapheme_count() const
{
	return ensure_grapheme_length(m_impl.get());
}

intptr_t String::utf8_length(Substring s)
{
	return static_cast<intptr_t>(unicode::codepoint_count(s.data(), s.size()));
}

String::const_iterator String::index_to_iter(intptr_t i) const
{
	StringCell *s = m_impl.get();
	intptr_t gl = ensure_grapheme_length(s);
	if (i > 0 && i <= gl)
		return grapheme_ptr(s, i - 1);
	if (i < 0 && i >= -gl)
		return grapheme_ptr(s, gl + i);
	PHON_CHECK(false, "[Index error] String index out of range");
	return end();
}

Substring String::at(intptr_t i) const
{
	const_iterator p = index_to_iter(i);
	unicode::GraphemeIter it;
	unicode::grapheme_init(it, p, static_cast<size_t>(end() - p));
	size_t n = unicode::grapheme_next(it);
	return Substring(p, n);
}

intptr_t String::distance(const_iterator from, const_iterator to) const
{
	PHON_ASSERT(from <= to);
	return static_cast<intptr_t>(unicode::grapheme_count(from, static_cast<size_t>(to - from)));
}

void String::advance(const_iterator &it, intptr_t count) const
{
	if (count == 0)
		return;
	StringCell *s = m_impl.get();
	if (count > 0)
	{
		unicode::GraphemeIter g;
		unicode::grapheme_init(g, it, static_cast<size_t>(end() - it));
		while (count-- > 0 && g.cur < g.end)
			unicode::grapheme_next(g);
		it = g.cur;
	}
	else
	{
		intptr_t gi = static_cast<intptr_t>(
		    unicode::byte_to_grapheme(s->data, static_cast<size_t>(s->byte_size),
		                              static_cast<size_t>(it - s->data)));
		intptr_t target = gi + count;
		if (target < 0)
			target = 0;
		it = grapheme_ptr(s, target);
	}
}

char32_t String::first() const
{
	PHON_ASSERT(!empty());
	const_iterator it = begin();
	return next_codepoint(it);
}

char32_t String::last() const
{
	PHON_ASSERT(!empty());
	const_iterator it = index_to_iter(-1);
	return next_codepoint(it);
}

Codepoint String::encode(char32_t c)
{
	Codepoint cp;
	size_t n = unicode::encode(c, cp.data);
	if (n == 0)
		n = unicode::encode(unicode::REPLACEMENT, cp.data);
	cp.size = static_cast<uint8_t>(n);
	cp.data[n] = '\0';
	return cp;
}

char32_t String::next_codepoint(const_iterator &it) const
{
	char32_t cp;
	bool valid;
	it += unicode::decode(it, end(), &cp, &valid);
	return cp;
}

Substring String::next_grapheme(const_iterator &it) const
{
	const char *start = it;
	unicode::GraphemeIter g;
	unicode::grapheme_init(g, it, static_cast<size_t>(end() - it));
	size_t n = unicode::grapheme_next(g);
	it += n;
	return Substring(start, n);
}

void String::next_grapheme(const_iterator &it, intptr_t &len) const
{
	Substring s = next_grapheme(it);
	len = static_cast<intptr_t>(s.size());
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool String::equals(Substring o) const noexcept
{
	StringCell *s = m_impl.get();
	if (s->byte_size != static_cast<intptr_t>(o.size()))
		return false;
	return std::memcmp(s->data, o.data(), o.size()) == 0;
}

int String::compare(Substring other) const noexcept
{
	// Byte-lexicographic order == codepoint order for well-formed UTF-8.
	StringCell *s = m_impl.get();
	size_t n = static_cast<size_t>(s->byte_size) < other.size()
	               ? static_cast<size_t>(s->byte_size)
	               : other.size();
	int r = std::memcmp(s->data, other.data(), n);
	if (r != 0)
		return r < 0 ? -1 : 1;
	if (static_cast<size_t>(s->byte_size) == other.size())
		return 0;
	return static_cast<size_t>(s->byte_size) < other.size() ? -1 : 1;
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

String &String::append(Substring suffix)
{
	if (suffix.empty())
		return *this;
	intptr_t len = static_cast<intptr_t>(suffix.size());
	StringCell *before = m_impl.get();
	// Guard against self-append aliasing (suffix pointing into our own buffer):
	// snapshot to a temporary if it overlaps, since detach may realloc-move.
	bool aliases = suffix.data() >= before->data && suffix.data() < before->data + before->capacity;
	char stackbuf[256];
	char *snap = nullptr;
	if (aliases)
	{
		snap = len <= static_cast<intptr_t>(sizeof(stackbuf)) ? stackbuf
		                                                      : static_cast<char *>(sys_alloc(len));
		std::memcpy(snap, suffix.data(), static_cast<size_t>(len));
	}
	const char *src = aliases ? snap : suffix.data();

	StringCell *s = detach_for_write(before->byte_size + len + 1);
	// byte_size may have been reset by clone? No: clone copies byte_size. Use s.
	std::memcpy(s->data + s->byte_size, src, static_cast<size_t>(len));
	s->byte_size += len;
	s->data[s->byte_size] = '\0';
	invalidate();

	if (snap && snap != stackbuf)
		sys_free(snap);
	return *this;
}

String &String::append(char32_t c)
{
	Codepoint cp = encode(c);
	return append(Substring(cp.data, cp.size));
}

String &String::prepend(Substring prefix)
{
	return insert(1, prefix);
}

String &String::prepend(char32_t c)
{
	Codepoint cp = encode(c);
	return insert(1, Substring(cp.data, cp.size));
}

void String::push_back(char c)
{
	append(Substring(&c, 1));
}

String &String::insert(intptr_t grapheme_pos, Substring infix)
{
	if (infix.empty())
		return *this;
	StringCell *s0 = m_impl.get();
	intptr_t gl = ensure_grapheme_length(s0);
	// 1-based; grapheme_pos == gl+1 means append at end.
	intptr_t gi = grapheme_pos - 1;
	if (grapheme_pos < 0)
		gi = gl + grapheme_pos + 1;
	PHON_CHECK(gi >= 0 && gi <= gl, "[Index error] String insert position out of range");
	intptr_t byte_off = static_cast<intptr_t>(grapheme_ptr(s0, gi) - s0->data);

	intptr_t len = static_cast<intptr_t>(infix.size());
	// Snapshot infix if it aliases our buffer.
	char stackbuf[256];
	char *snap = nullptr;
	bool aliases = infix.data() >= s0->data && infix.data() < s0->data + s0->capacity;
	if (aliases)
	{
		snap = len <= static_cast<intptr_t>(sizeof(stackbuf)) ? stackbuf
		                                                      : static_cast<char *>(sys_alloc(len));
		std::memcpy(snap, infix.data(), static_cast<size_t>(len));
	}
	const char *src = aliases ? snap : infix.data();

	StringCell *s = detach_for_write(s0->byte_size + len + 1);
	std::memmove(s->data + byte_off + len, s->data + byte_off,
	             static_cast<size_t>(s->byte_size - byte_off));
	std::memcpy(s->data + byte_off, src, static_cast<size_t>(len));
	s->byte_size += len;
	s->data[s->byte_size] = '\0';
	invalidate();

	if (snap && snap != stackbuf)
		sys_free(snap);
	return *this;
}

String String::operator+(Substring s) const
{
	String r(size() + static_cast<intptr_t>(s.size()) + 1, true);
	r.append(view());
	r.append(s);
	return r;
}

void String::clear()
{
	StringCell *s = detach_for_write(1);
	s->byte_size = 0;
	s->data[0] = '\0';
	invalidate();
}

void String::reserve(intptr_t bytes)
{
	if (bytes + 1 > m_impl->capacity || !m_impl.unique())
		detach_for_write(bytes + 1);
}

void String::chop(intptr_t new_byte_size)
{
	if (new_byte_size < 0)
		new_byte_size = 0;
	if (new_byte_size >= m_impl->byte_size)
		return;
	StringCell *s = detach_for_write(m_impl->byte_size + 1);
	s->byte_size = new_byte_size;
	s->data[new_byte_size] = '\0';
	invalidate();
}

String &String::replace(Substring before, Substring after, intptr_t ntimes)
{
	if (before.empty())
		return *this;
	// Build the result in a fresh buffer (simple, avoids in-place overlap woes).
	std::string out;
	Substring hay = view();
	size_t pos = 0;
	intptr_t done = 0;
	for (;;)
	{
		size_t hit = hay.find(before, pos);
		if (hit == Substring::npos || (ntimes >= 0 && done >= ntimes))
		{
			out.append(hay.data() + pos, hay.size() - pos);
			break;
		}
		out.append(hay.data() + pos, hit - pos);
		out.append(after.data(), after.size());
		pos = hit + before.size();
		++done;
	}
	*this = String(out);
	return *this;
}

String &String::remove(Substring what, intptr_t ntimes)
{
	return replace(what, Substring(), ntimes);
}

namespace {

// First byte offset that is not leading whitespace.
intptr_t ltrim_offset(const char *data, intptr_t len)
{
	const char *p = data;
	const char *end = data + len;
	while (p < end)
	{
		const char *q = p;
		char32_t cp;
		bool valid;
		size_t n = unicode::decode(q, end, &cp, &valid);
		if (!unicode::is_white_space(cp))
			break;
		p += n;
	}
	return static_cast<intptr_t>(p - data);
}

// Byte offset one past the last non-whitespace codepoint.
intptr_t rtrim_offset(const char *data, intptr_t len)
{
	// Walk forward tracking the end of the last non-space codepoint.
	const char *p = data;
	const char *end = data + len;
	const char *last_end = data;
	while (p < end)
	{
		char32_t cp;
		bool valid;
		size_t n = unicode::decode(p, end, &cp, &valid);
		if (!unicode::is_white_space(cp))
			last_end = p + n;
		p += n;
	}
	return static_cast<intptr_t>(last_end - data);
}

} // namespace

String &String::ltrim()
{
	StringCell *s0 = m_impl.get();
	intptr_t off = ltrim_offset(s0->data, s0->byte_size);
	if (off == 0)
		return *this;
	intptr_t new_len = s0->byte_size - off;
	StringCell *s = detach_for_write(s0->byte_size + 1);
	std::memmove(s->data, s->data + off, static_cast<size_t>(new_len));
	s->byte_size = new_len;
	s->data[new_len] = '\0';
	invalidate();
	return *this;
}

String &String::rtrim()
{
	StringCell *s0 = m_impl.get();
	intptr_t new_len = rtrim_offset(s0->data, s0->byte_size);
	if (new_len == s0->byte_size)
		return *this;
	StringCell *s = detach_for_write(s0->byte_size + 1);
	s->byte_size = new_len;
	s->data[new_len] = '\0';
	invalidate();
	return *this;
}

String &String::trim()
{
	rtrim();
	ltrim();
	return *this;
}

// ---------------------------------------------------------------------------
// Derivations
// ---------------------------------------------------------------------------

namespace {

String map_case(Substring in, bool upper)
{
	std::string out;
	out.reserve(in.size());
	const char *p = in.data();
	const char *end = p + in.size();
	char buf[unicode::MAX_CASE_EXPANSION];
	while (p < end)
	{
		char32_t cp;
		bool valid;
		p += unicode::decode(p, end, &cp, &valid);
		size_t n = upper ? unicode::to_upper_cp(cp, buf) : unicode::to_lower_cp(cp, buf);
		out.append(buf, n);
	}
	return String(out);
}

} // namespace

String String::to_upper() const { return map_case(view(), true); }
String String::to_lower() const { return map_case(view(), false); }

String String::reverse() const
{
	// Reverse grapheme clusters (keeps combining marks attached to their base).
	StringCell *s = m_impl.get();
	String out(s->byte_size + 1, true);
	std::string tmp;
	tmp.resize(static_cast<size_t>(s->byte_size));
	unicode::GraphemeIter it;
	unicode::grapheme_init(it, s->data, static_cast<size_t>(s->byte_size));
	intptr_t write_end = s->byte_size;
	for (;;)
	{
		const char *cluster = it.cur;
		size_t n = unicode::grapheme_next(it);
		if (n == 0)
			break;
		std::memcpy(&tmp[static_cast<size_t>(write_end) - n], cluster, n);
		write_end -= static_cast<intptr_t>(n);
	}
	return String(tmp);
}

String String::repeat(intptr_t count) const
{
	if (count <= 0)
		return String();
	StringCell *s = m_impl.get();
	String out(s->byte_size * count + 1, true);
	for (intptr_t i = 0; i < count; ++i)
		out.append(view());
	return out;
}

String String::left(intptr_t count) const
{
	return String(mid(1, count));
}

String String::right(intptr_t count) const
{
	intptr_t gl = grapheme_count();
	if (count >= gl)
		return *this;
	if (count <= 0)
		return String();
	return String(mid(gl - count + 1, count));
}

Substring String::mid(intptr_t from, intptr_t count) const
{
	StringCell *s = m_impl.get();
	intptr_t gl = ensure_grapheme_length(s);
	intptr_t gi = from - 1;
	if (from < 0)
		gi = gl + from;
	if (gi < 0)
		gi = 0;
	if (gi > gl)
		gi = gl;
	const char *start = grapheme_ptr(s, gi);
	const char *stop;
	if (count < 0)
		stop = s->data + s->byte_size;
	else
	{
		intptr_t end_gi = gi + count;
		if (end_gi > gl)
			end_gi = gl;
		stop = grapheme_ptr(s, end_gi);
	}
	return Substring(start, static_cast<size_t>(stop - start));
}

// ---------------------------------------------------------------------------
// Search (grapheme positions)
// ---------------------------------------------------------------------------

intptr_t String::find(Substring needle, intptr_t from) const
{
	StringCell *s = m_impl.get();
	intptr_t start_byte = 0;
	if (from > 1)
		start_byte = static_cast<intptr_t>(index_to_iter(from) - s->data);
	Substring hay(s->data + start_byte, static_cast<size_t>(s->byte_size - start_byte));
	size_t pos = hay.find(needle);
	if (pos == Substring::npos)
		return 0;
	intptr_t byte_off = start_byte + static_cast<intptr_t>(pos);
	return static_cast<intptr_t>(
	           unicode::byte_to_grapheme(s->data, static_cast<size_t>(s->byte_size),
	                                     static_cast<size_t>(byte_off))) +
	       1;
}

intptr_t String::rfind(Substring needle) const
{
	StringCell *s = m_impl.get();
	size_t pos = view().rfind(needle);
	if (pos == Substring::npos)
		return 0;
	return static_cast<intptr_t>(
	           unicode::byte_to_grapheme(s->data, static_cast<size_t>(s->byte_size), pos)) +
	       1;
}

bool String::contains(Substring needle) const { return view().find(needle) != Substring::npos; }

bool String::contains(char32_t c) const
{
	Codepoint cp = encode(c);
	return contains(Substring(cp.data, cp.size));
}

bool String::starts_with(Substring prefix) const
{
	return size() >= static_cast<intptr_t>(prefix.size()) &&
	       std::memcmp(data(), prefix.data(), prefix.size()) == 0;
}

bool String::ends_with(Substring suffix) const
{
	return size() >= static_cast<intptr_t>(suffix.size()) &&
	       std::memcmp(data() + size() - static_cast<intptr_t>(suffix.size()), suffix.data(),
	                   suffix.size()) == 0;
}

intptr_t String::count(Substring needle) const
{
	if (needle.empty())
		return 0;
	Substring hay = view();
	intptr_t n = 0;
	size_t pos = 0;
	for (;;)
	{
		size_t hit = hay.find(needle, pos);
		if (hit == Substring::npos)
			break;
		++n;
		pos = hit + needle.size();
	}
	return n;
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

String String::convert(bool b) { return String(b ? "true" : "false"); }

String String::convert(intptr_t n)
{
	char buf[32];
	int len = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
	return String(buf, len);
}

String String::convert(double x)
{
	char buf[32];
	// Shortest representation that round-trips cleanly for typical values.
	int len = std::snprintf(buf, sizeof(buf), "%.15g", x);
	return String(buf, len);
}

double String::to_float(bool *ok) const
{
	// NUL-terminated (single-allocation guarantee), so strtod is safe.
	char *endp = nullptr;
	double v = std::strtod(data(), &endp);
	if (ok)
		*ok = (endp == data() + size());
	return v;
}

intptr_t String::to_int(bool *ok) const
{
	char *endp = nullptr;
	long long v = std::strtoll(data(), &endp, 10);
	if (ok)
		*ok = (endp == data() + size());
	return static_cast<intptr_t>(v);
}

bool String::to_bool(bool strict) const
{
	if (view() == "true")
		return true;
	if (view() == "false")
		return false;
	if (strict)
		return false;
	return !empty();
}

String String::format(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int need = std::vsnprintf(nullptr, 0, fmt, ap);
	va_end(ap);
	String out(need + 1, true);
	StringCell *s = out.detach_for_write(need + 1);
	std::vsnprintf(s->data, static_cast<size_t>(need) + 1, fmt, ap2);
	va_end(ap2);
	s->byte_size = need;
	s->data[need] = '\0';
	out.invalidate();
	return out;
}

std::u16string String::to_utf16() const
{
	std::u16string out;
	const char *p = data();
	const char *e = end();
	while (p < e)
	{
		char32_t cp;
		bool valid;
		p += unicode::decode(p, e, &cp, &valid);
		if (cp < 0x10000u)
		{
			out.push_back(static_cast<char16_t>(cp));
		}
		else
		{
			cp -= 0x10000u;
			out.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
		}
	}
	return out;
}

std::u32string String::to_utf32() const
{
	std::u32string out;
	const char *p = data();
	const char *e = end();
	while (p < e)
	{
		char32_t cp;
		bool valid;
		p += unicode::decode(p, e, &cp, &valid);
		out.push_back(cp);
	}
	return out;
}

String String::from_utf16(const std::u16string &s)
{
	std::string out;
	const uint16_t *p = reinterpret_cast<const uint16_t *>(s.data());
	const uint16_t *e = p + s.size();
	char buf[4];
	while (p < e)
	{
		char32_t cp;
		bool valid;
		p += unicode::utf16_decode(p, e, &cp, &valid);
		size_t n = unicode::encode(cp, buf);
		if (n == 0)
			n = unicode::encode(unicode::REPLACEMENT, buf);
		out.append(buf, n);
	}
	return String(out);
}

String String::from_utf32(const std::u32string &s)
{
	std::string out;
	char buf[4];
	for (char32_t cp : s)
	{
		size_t n = unicode::encode(cp, buf);
		if (n == 0)
			n = unicode::encode(unicode::REPLACEMENT, buf);
		out.append(buf, n);
	}
	return String(out);
}

} // namespace phonometrica
