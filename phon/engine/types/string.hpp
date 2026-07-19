// Phonometrica engine — String: dynamic UTF-8 strings with CoW value semantics.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// API-compatible (in spirit) with Phonometrica's String so the application port
// is mechanical. Storage and semantics follow the new engine:
//
//   * Single allocation (design §5.1): the UTF-8 bytes live inline after the
//     header, so growing a unique string may move its cell — mutators write the
//     moved pointer back through the owning Handle slot.
//   * Value semantics via copy-on-write: mutation checks refcount == 1 and
//     clones if shared.
//   * Indexing is by GRAPHEME CLUSTER (user-perceived character, UAX #29), 1-based,
//     negative indices count from the end — matching Phonometrica's behavior.
//     Random access is O(1)-amortized via a lazily built breadcrumb index over
//     grapheme boundaries. A codepoint/byte-level API sits underneath.
//
// Positions returned/accepted by find/index_to_iter/advance/mid/left/right/length
// are grapheme positions. Iterators (begin/end) are over code units (char).

#ifndef PHON_TYPES_STRING_HPP
#define PHON_TYPES_STRING_HPP

#include <phon/engine/base/definitions.hpp>
#include <phon/engine/core/array.hpp>
#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/handle.hpp>
#include <phon/engine/core/value.hpp>

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

#ifdef PHON_WITH_QT
#include <QByteArray>
#include <QString>
#endif

namespace phonometrica {

class Regex; // types/regex.hpp; the Regex overload of replace lives in types/regex.cpp

using Substring = std::string_view;

// One encoded codepoint as UTF-8 bytes (NUL-terminated). Matches phon::Codepoint.
struct Codepoint
{
	char data[7] = {'\0'};
	uint8_t size = 0;

	operator Substring() const noexcept { return {data, size}; }
};

// The heap object. Payload bytes are inline after the header (flexible array).
struct StringCell
{
	Cell header;              // 8
	intptr_t byte_size;       // bytes, excluding the NUL terminator
	intptr_t capacity;        // bytes available in data[] (includes NUL slot)
	intptr_t grapheme_length; // cached grapheme count; -1 = not yet computed
	uint64_t hash;            // cached hash (valid iff SF_HASHED)
	intptr_t *crumbs;         // grapheme-boundary breadcrumbs (lazy); null = unbuilt
	uint32_t flags;
	uint32_t pad_;
	char data[]; // inline UTF-8, always NUL-terminated at data[byte_size]

	enum Flags : uint32_t
	{
		SF_ASCII = 1u << 0,  // all bytes < 0x80 (known)
		SF_HASHED = 1u << 1, // hash field is valid
	};
};

// Stride between grapheme breadcrumbs (power of two: shift, not divide).
inline constexpr intptr_t GRAPHEME_CRUMB_STRIDE = 32;
inline constexpr intptr_t GRAPHEME_CRUMB_SHIFT = 5;

// Registers the String class + hooks (called by bootstrap()).
void register_string_class();

class String final
{
public:
	using value_type = char;
	using iterator = char *;
	using const_iterator = const char *;

	enum class Option
	{
		Left = 1,
		Right = 2,
		Both = 3
	};

	// --- construction ---

	String();
	String(const char *str);
	String(const char *str, intptr_t len);
	String(Substring s);
	String(const std::string &s);
	String(char32_t codepoint, intptr_t count);
	// Pre-allocate `capacity` bytes for an empty string (append without regrow).
	explicit String(intptr_t capacity, bool exact = false);

	String(const String &) = default;
	String(String &&) noexcept = default;
	String &operator=(const String &) = default;
	String &operator=(String &&) noexcept = default;

#ifdef PHON_WITH_QT
	// Qt interop (opt-in): mirrors old Phonometrica's String exactly.
	String(const QString &str) : String(str.toUtf8()) {}
	explicit String(const QByteArray &utf8) : String(utf8.data(), intptr_t(utf8.size())) {}
	operator QString() const { return QString::fromUtf8(data(), int(size())); }
#endif

	// --- raw / byte access ---

	const char *data() const noexcept { return m_impl->data; }
	intptr_t size() const noexcept { return m_impl->byte_size; }
	intptr_t capacity() const noexcept { return m_impl->capacity - 1; }
	bool empty() const noexcept { return m_impl->byte_size == 0; }
	Substring view() const noexcept { return {data(), static_cast<size_t>(size())}; }
	operator Substring() const noexcept { return view(); }

	const_iterator begin() const noexcept { return m_impl->data; }
	const_iterator end() const noexcept { return m_impl->data + m_impl->byte_size; }
	const_iterator cbegin() const noexcept { return begin(); }
	const_iterator cend() const noexcept { return end(); }

	// --- ownership / cache ---

	uint32_t use_count() const noexcept { return m_impl.use_count(); }
	bool unique() const noexcept { return m_impl.unique(); }
	bool shared() const noexcept { return !m_impl.unique(); }
	void unshare();       // force a unique copy
	void shrink_to_fit(); // trim capacity to byte_size + 1

	// Freeze this string: materialize its lazy caches and mark the cell frozen +
	// shared-buffer, so it becomes immutable and can be shared zero-copy across threads
	// (freeze() builtin, §8.3). Idempotent. Subsequent mutations copy-on-write.
	void make_frozen();
	void swap(String &o) noexcept { m_impl.swap(o.m_impl); }

	bool is_ascii() const;
	size_t hash() const;

	// --- length in graphemes (the user-facing "character" unit) ---

	intptr_t grapheme_count() const;
	intptr_t length() const { return grapheme_count(); } // alias

	// Number of code points in a byte range (a lower-level count).
	static intptr_t utf8_length(Substring s);

	// --- grapheme indexing (1-based, negative from the end) ---

	// The grapheme cluster at 1-based position i, as a view into this string.
	Substring at(intptr_t i) const;
	Substring next_grapheme(intptr_t i) const { return at(i); }

	// Iterator (code-unit pointer) at the start of the 1-based grapheme i.
	const_iterator index_to_iter(intptr_t i) const;

	// Grapheme distance between two code-unit iterators (from <= to).
	intptr_t distance(const_iterator from, const_iterator to) const;

	// Advance `it` by `count` graphemes (negative iterates backward).
	void advance(const_iterator &it, intptr_t count) const;

	char32_t first() const;
	char32_t last() const;

	// --- codepoint / grapheme iteration primitives ---

	static Codepoint encode(char32_t c);
	char32_t next_codepoint(const_iterator &it) const;
	Substring next_grapheme(const_iterator &it) const;
	void next_grapheme(const_iterator &it, intptr_t &len) const;

	// --- comparison ---

	int compare(Substring other) const noexcept;
	// String compares via its implicit conversion to Substring, so a single
	// Substring overload covers String/Substring/std::string operands; a const
	// char* overload is added so string literals match exactly (no conversion).
	bool operator==(Substring o) const noexcept { return equals(o); }
	bool operator!=(Substring o) const noexcept { return !equals(o); }
	bool operator==(const char *o) const noexcept { return equals(Substring(o)); }
	bool operator!=(const char *o) const noexcept { return !equals(Substring(o)); }
	bool operator<(const String &o) const noexcept { return compare(o.view()) < 0; }
	bool operator>(const String &o) const noexcept { return compare(o.view()) > 0; }
	bool operator<=(const String &o) const noexcept { return compare(o.view()) <= 0; }
	bool operator>=(const String &o) const noexcept { return compare(o.view()) >= 0; }

	// --- mutation (CoW) ---

	String &append(Substring suffix);
	String &append(char32_t c);
	String &prepend(Substring prefix);
	String &prepend(char32_t c);
	void push_back(char c);
	String &insert(intptr_t grapheme_pos, Substring infix);

	String &operator+=(Substring s) { return append(s); }
	String operator+(Substring s) const;

	void clear();
	void reserve(intptr_t bytes);
	void chop(intptr_t new_byte_size);

	String &replace(Substring before, Substring after, intptr_t ntimes = -1);
	// Replace the first match of `pattern`, substituting `%%` (the whole match) and
	// `%1`..`%9` (capture groups) inside `after` first — old Phonometrica semantics.
	// `ntimes` bounds those placeholder substitutions within `after` (-1 = all).
	// Defined in types/regex.cpp.
	String &replace(const Regex &pattern, String after, intptr_t ntimes = -1);
	String &remove(Substring what, intptr_t ntimes = -1);

	// Replace positional arguments %1 to %9, Qt-style (each `arg` is substituted in
	// order, so a placeholder occurring in an earlier argument's text is rewritten by
	// the later ones — exactly the old Phonometrica behavior).
	String &arg(Substring a1);
	String &arg(Substring a1, Substring a2);
	String &arg(Substring a1, Substring a2, Substring a3);
	String &arg(Substring a1, Substring a2, Substring a3, Substring a4);
	String &arg(Substring a1, Substring a2, Substring a3, Substring a4, Substring a5);
	String &arg(Substring a1, Substring a2, Substring a3, Substring a4, Substring a5, Substring a6);
	String &arg(Substring a1, Substring a2, Substring a3, Substring a4, Substring a5, Substring a6,
	            Substring a7);
	String &arg(Substring a1, Substring a2, Substring a3, Substring a4, Substring a5, Substring a6,
	            Substring a7, Substring a8);
	String &arg(Substring a1, Substring a2, Substring a3, Substring a4, Substring a5, Substring a6,
	            Substring a7, Substring a8, Substring a9);

	String &trim();
	String &ltrim();
	String &rtrim();

	// --- non-mutating derivations (return a new String) ---

	String to_upper() const;
	String to_lower() const;
	String reverse() const;
	String repeat(intptr_t count) const;
	String left(intptr_t count) const;  // first `count` graphemes
	String right(intptr_t count) const; // last `count` graphemes

	// Grapheme-range substring view: `count` graphemes from 1-based `from`.
	Substring mid(intptr_t from, intptr_t count = -1) const;

	// --- search (grapheme positions; 0 = not found) ---

	intptr_t find(Substring needle, intptr_t from = 1) const;
	intptr_t rfind(Substring needle) const;

	// Split on a byte-wise separator (old Phonometrica semantics: an empty separator
	// throws; a separator no shorter than the string yields the whole string as the
	// single element; leading/trailing separators yield empty chunks).
	Array<String> split(Substring separator) const;

	// Join `strings` with `separator` between consecutive elements.
	static String join(const Array<String> &strings, Substring separator);
	bool contains(Substring needle) const;
	bool contains(char32_t c) const;
	bool starts_with(Substring prefix) const;
	bool ends_with(Substring suffix) const;
	intptr_t count(Substring needle) const;

	// --- conversion ---

	static String convert(bool b);
	static String convert(intptr_t n);
	static String convert(double x);
	double to_float(bool *ok = nullptr) const;
	intptr_t to_int(bool *ok = nullptr) const;
	bool to_bool(bool strict = false) const;
	static String format(const char *fmt, ...);

	// True if `codepoint` is a letter (old-Phonometrica String API parity, used by
	// tokenizers for non-ASCII identifier characters). Approximated by the Unicode
	// ID_Start property minus '_' — ID_Start ≈ the letter categories plus Nl, which
	// is the right notion for identifier-shaped names.
	static bool is_letter(char32_t codepoint);

	std::u16string to_utf16() const;
	std::u32string to_utf32() const;
	static String from_utf16(const std::u16string &s);
	static String from_utf32(const std::u32string &s);

	// --- engine interop ---

	// The owning cell, as a Value (borrowed view; the Handle keeps the reference).
	Value to_value() const noexcept { return Value::make_cell(m_impl.cell()); }

	// Wrap an existing StringCell reference (adopts, does not retain).
	static String adopt(StringCell *cell) noexcept { return String(Handle<StringCell>::adopt(cell)); }
	static String from_value(Value v) noexcept;

	StringCell *cell() const noexcept { return m_impl.get(); }

private:
	explicit String(Handle<StringCell> h) noexcept : m_impl(std::move(h)) {}

	bool equals(Substring o) const noexcept;

	// Ensure the cell is unique and has room for `need_bytes` bytes (incl. NUL);
	// returns the (possibly moved) cell and rewrites the owning slot. Callers
	// then write bytes and call invalidate().
	StringCell *detach_for_write(intptr_t need_bytes);

	// Drop cached grapheme_length / breadcrumbs / hash / ascii after a mutation.
	void invalidate() noexcept;

	Handle<StringCell> m_impl;
};

inline std::ostream &operator<<(std::ostream &os, const String &s)
{
	os.write(s.data(), static_cast<std::streamsize>(s.size()));
	return os;
}

} // namespace phonometrica

// std::hash specialization so String works as a key in std containers (tests).
template<>
struct std::hash<phonometrica::String>
{
	size_t operator()(const phonometrica::String &s) const noexcept { return s.hash(); }
};

#endif // PHON_TYPES_STRING_HPP
