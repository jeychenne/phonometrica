// Phonometrica engine — regular expressions (Regex + Match), built on PCRE2 with JIT.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A *new* design (not ported from Phonometrica): `Regex` is an immutable value type —
// a compiled pattern with no mutable match state — and matching produces a separate
// `Match` object. Because a Regex holds no per-match state, the same compiled pattern
// is safe to share and match from many threads at once (each match() allocates its own
// PCRE2 match data), which is what makes regex-heavy work parallelizable.
//
// Both are plain C++ classes (no engine machinery on them): lib/regex.cpp boxes them
// as script Value classes via add_class<Regex>/<Match> (ClassKind::Value, so a copy is
// a deep copy). The pattern language and flags are PCRE2's; positions handed to scripts
// are 1-based grapheme indices, consistent with String indexing.

#ifndef PHON_REGEX_REGEX_HPP
#define PHON_REGEX_REGEX_HPP

#include <phon/engine/types/string.hpp>

#include <vector>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace phonometrica {

// A successful match's captured groups plus a copy of the subject they index into.
// Group 0 is the whole match; groups 1.. follow the order of opening parentheses.
// Positions are 1-based grapheme indices into the subject. A non-participating group
// (inside an unmatched alternative, or an unmatched optional) is "unset": group() is
// null and its start/end are 0. An empty Match (default-constructed) means "no match".
class Match final
{
public:
	Match() = default;

	// Build from a completed PCRE2 match: `total_groups` = capture count + 1 (the whole
	// static group count, including group 0), `rc` = pcre2 return (one more than the
	// highest *participating* group), `ovector` the raw byte-offset pairs. A group index
	// >= rc, or whose pair is PCRE2_UNSET, is recorded as not participating.
	Match(String subject, int total_groups, int rc, const PCRE2_SIZE *ovector);

	bool has_match() const noexcept { return m_total_groups > 0; }

	// Number of groups including the implicit group 0 (so `(a)(b)(c)` yields 4).
	int group_count() const noexcept { return m_total_groups; }

	// True when `n` names a group of this match (0 <= n < group_count()).
	bool in_range(int64_t n) const noexcept { return n >= 0 && n < m_total_groups; }

	// True when group `n` participated in the match (precondition: in_range(n)).
	bool group_is_set(int n) const noexcept { return m_starts[n] != PCRE2_UNSET; }

	// The captured text of group `n` (precondition: group_is_set(n)).
	String group(int n) const;

	// 1-based grapheme start / (one-past) end of group `n` in the subject; 0 if unset.
	intptr_t group_start(int n) const;
	intptr_t group_end(int n) const;

	// 0-based byte offsets of group `n` in the subject (precondition: group_is_set(n)).
	// The byte-level layer under the grapheme positions above, for splicing (replace).
	intptr_t group_byte_start(int n) const;
	intptr_t group_byte_end(int n) const;

	// The subject this match indexes into (shares the buffer of the matched string).
	const String &subject() const noexcept { return m_subject; }

private:
	String m_subject;
	std::vector<PCRE2_SIZE> m_starts; // byte offsets; PCRE2_UNSET == not participating
	std::vector<PCRE2_SIZE> m_ends;
	int m_total_groups = 0;
};

// A compiled, immutable regular expression. Copying recompiles from the stored pattern
// (the CoW clone hook for the script Value class), so a copy is independent and equally
// usable. Matching never mutates the Regex.
class Regex final
{
public:
	// PCRE2 option bits, exposed under Phonometrica's names. Combined internally from a
	// flags string (parse_flags); scripts never see the raw bits.
	enum Option
	{
		None          = 0,
		Caseless      = PCRE2_CASELESS,
		Multiline     = PCRE2_MULTILINE,
		DotAll        = PCRE2_DOTALL,
		Extended      = PCRE2_EXTENDED,
		Anchored      = PCRE2_ANCHORED,
		DollarEndOnly = PCRE2_DOLLAR_ENDONLY,
		Ungreedy      = PCRE2_UNGREEDY,
	};

	// Compile `pattern` (PCRE2 with JIT is an internal detail). Throws std::runtime_error
	// on a syntactically invalid pattern, with a message naming the position. `flags` is
	// the combined Option bits, or a "caseless|multiline|…" string (parse_flags).
	explicit Regex(const String &pattern);
	Regex(const String &pattern, int flags);
	Regex(const String &pattern, const String &flags);

	// CoW clone: recompile from the stored pattern + flags (never fails — it compiled
	// once already).
	Regex(const Regex &other);
	Regex(Regex &&other) noexcept;
	Regex &operator=(const Regex &) = delete;
	Regex &operator=(Regex &&) = delete;
	~Regex();

	const String &pattern() const noexcept { return m_pattern; }
	int flags() const noexcept { return m_flags; }

	// Search `subject` for the first leftmost match, beginning at byte offset `from`.
	// Returns an empty Match (has_match() == false) if the pattern does not match. On a
	// genuine PCRE2 error, sets `*error` (if non-null) and returns an empty Match.
	Match match(const String &subject, intptr_t from = 0, String *error = nullptr) const;

	// Translate a "caseless|multiline|dotall|extended|anchored|dollar_endonly|ungreedy"
	// flags string into the combined Option bits (unknown tokens are ignored).
	static int parse_flags(const String &options);

private:
	// Compile m_pattern with m_flags (JIT enabled), filling m_code/m_capture_count/m_jit.
	// Throws std::runtime_error on a syntax error. The one place that touches PCRE2's
	// compiler — construction goes through here so pcre2 stays an implementation detail.
	void compile();

	pcre2_code *m_code = nullptr;
	String m_pattern;
	int m_flags = 0;
	int m_capture_count = 0;
	bool m_jit = false;
};

} // namespace phonometrica

#endif // PHON_REGEX_REGEX_HPP
