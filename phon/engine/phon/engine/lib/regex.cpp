// Phonometrica engine — regular-expression standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Exposes the immutable `Regex` and its `Match` results to scripts. Both are registered
// as Value classes (add_class<…>(…, ClassKind::Value)); a Regex is built through the
// `regex(pattern[, flags])` factory (a foreign class is not script-constructible), and
// matching produces a fresh `Match` — so a Regex carries no mutable state and can be
// shared across threads. `match` returns `null` on no match; `group` returns `null` for
// a non-participating group. Group positions are 1-based grapheme indices.

#include <phon/engine/lib/lib.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/types/regex.hpp>
#include <phon/engine/runtime/native_traits.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/vm/isolate.hpp>

#include <exception>
#include <utility>

namespace phonometrica {

namespace {

// Build a Regex, raising a clean script error if the pattern is invalid. The Regex is
// constructed on the stack first (its constructor compiles and throws on a bad pattern)
// and then moved into its box — so a compile failure never leaves an orphaned cell.
Handle<Regex> make_regex(Isolate &iso, const String &pattern, int flags)
{
	try
	{
		Regex re(pattern, flags);
		return Handle<Regex>::make(std::move(re));
	}
	catch (const std::exception &e)
	{
		iso.raise(String("[Regex error] ") + e.what(), 0);
	}
}

// Convert a 1-based grapheme position into a byte offset into `s`, clamped to the string.
intptr_t grapheme_to_byte(const String &s, int64_t from)
{
	if (from <= 1)
		return 0;
	if (from > s.length())
		return s.size();
	return s.index_to_iter(from) - s.begin();
}

// Run `re` over `subject` from byte offset `from`, returning a Match handle (null when
// the pattern does not match), and raising on a genuine PCRE2 error.
Handle<Match> do_match(Isolate &iso, const Regex &re, const String &subject, intptr_t from)
{
	String error;
	Match m = re.match(subject, from, &error);
	if (!error.empty())
		iso.raise(String("[Regex error] ") + error.view(), 0);
	if (!m.has_match())
		return Handle<Match>();
	return Handle<Match>::make(std::move(m));
}

// Validate a group index against a match, raising an Index error otherwise.
void check_group(Isolate &iso, const Match &m, int64_t n)
{
	if (!m.in_range(n))
		iso.raise(String::format("[Index error] invalid group index %d (match has %d groups)",
		                         static_cast<int>(n), m.group_count()),
		          0);
}

} // namespace

void register_regex_lib()
{
	// Register the value classes (once). Must precede the function registrations, whose
	// Regex&/Match& parameters dispatch on class_of<Regex>()/class_of<Match>().
	if (!class_of<Regex>())
		add_class<Regex>("Regex", get_class(CID_OBJECT), ClassKind::Value);
	if (!class_of<Match>())
		add_class<Match>("Match", get_class(CID_OBJECT), ClassKind::Value);

	// --- construction (factory: the class is not script-constructible) ---
	// regex(pattern): default options. regex(pattern, flags): a PCRE2 flags string such
	// as "caseless|multiline|dotall|extended|anchored|dollar_endonly|ungreedy".
	register_function("regex", [](Isolate &iso, const String &pattern) {
		return make_regex(iso, pattern, Regex::None);
	});
	register_function("regex", [](Isolate &iso, const String &pattern, const String &flags) {
		return make_regex(iso, pattern, Regex::parse_flags(flags));
	});

	// --- matching ---
	// match(r, s) -> Match?  ; match(r, s, from) -> Match?  (from = 1-based grapheme pos)
	register_function("match", [](Isolate &iso, const Regex &re, const String &subject) {
		return do_match(iso, re, subject, 0);
	});
	register_function("match",
	                  [](Isolate &iso, const Regex &re, const String &subject, int64_t from) {
		                  return do_match(iso, re, subject, grapheme_to_byte(subject, from));
	                  });

	// --- Match accessors ---
	// group(m, n) -> String?  (null for a non-participating group; group 0 = whole match)
	register_function("group", [](Isolate &iso, const Match &m, int64_t n) -> Variant {
		check_group(iso, m, n);
		if (!m.group_is_set(static_cast<int>(n)))
			return Variant::null();
		return Variant::make(m.group(static_cast<int>(n)));
	});
	// group_count(m): number of groups including group 0 (so `(a)(b)(c)` yields 4).
	register_function("group_count", [](const Match &m) {
		return static_cast<int64_t>(m.group_count());
	});
	// group_start / group_end: 1-based grapheme bounds of group `n` (0 if unset).
	register_function("group_start", [](Isolate &iso, const Match &m, int64_t n) {
		check_group(iso, m, n);
		return static_cast<int64_t>(m.group_start(static_cast<int>(n)));
	});
	register_function("group_end", [](Isolate &iso, const Match &m, int64_t n) {
		check_group(iso, m, n);
		return static_cast<int64_t>(m.group_end(static_cast<int>(n)));
	});
	// groups(m): the captures as a List — [group(m,1), …]; a non-participating group
	// contributes null. (Replaces the old engine's iteration over the regex object.)
	register_function("groups", [](const Match &m) {
		List out;
		for (int i = 1; i < m.group_count(); ++i)
		{
			if (m.group_is_set(i))
				out.append(Variant::make(m.group(i)));
			else
				out.append(Variant::null());
		}
		return out;
	});

	// --- Regex accessors ---
	register_function("pattern", [](const Regex &re) { return re.pattern(); });
}

} // namespace phonometrica
