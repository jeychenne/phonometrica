/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 13/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: regular expressions (Regex + Match), built on top of PCRE2. Ported from the new engine's design: a Regex   *
 * is an immutable compiled pattern with no per-match mutable state, and matching returns a separate Match object      *
 * holding the captured groups. Because the pattern object never changes after compilation, the same Regex can be      *
 * shared and matched from many threads at once (each match allocates its own PCRE2 match data), which is what makes   *
 * regex-heavy work such as concurrent queries parallelizable. The Match accessors keep the names and semantics of     *
 * the old stateful Regex accessors (capture, capture_start/end with 1-based code point indices, capture iterators)    *
 * so that call sites only need to thread a Match through instead of reading results off the Regex.                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_BASE_REGEX_HPP
#define PHONOMETRICA_BASE_REGEX_HPP

#include <vector>
#include <phon/string.hpp>
#include <pcre2.h>

namespace phonometrica {

// The result of matching a Regex against a subject: a copy of the subject (cheap, copy-on-write)
// plus the byte offsets of the captured groups. Group 0 is the whole match; groups 1 to count()
// follow the order of opening parentheses. A default-constructed Match means "no match".
class Match final
{
public:

	Match() = default;

	Match(const Match &) = default;
	Match(Match &&) noexcept = default;
	Match &operator=(const Match &) = default;
	Match &operator=(Match &&) noexcept = default;

	bool has_match() const { return m_rc > 0; }

	explicit operator bool() const { return has_match(); }

	// Number of capture groups (excluding group 0, the whole match).
	intptr_t count() const { return m_rc > 0 ? intptr_t(m_rc) - 1 : 0; }

	// The text captured by group `nth`, with 0 <= nth <= count().
	String capture(intptr_t nth) const;

	// Get the beginning and end indices of a capture. If utf8 is true, the returned index should be
	// interpreted as a 1-based code point index. Otherwise, it is a 1-based code unit index.
	intptr_t capture_start(intptr_t nth, bool utf8 = true) const;
	intptr_t capture_end(intptr_t nth, bool utf8 = true) const;

	// Byte iterators into subject() delimiting a capture.
	String::const_iterator capture_start_iter(intptr_t nth) const;
	String::const_iterator capture_end_iter(intptr_t nth) const;

	String subject() const { return m_subject; }

private:

	friend class Regex;

	Match(String subject, int rc, uint32_t group_count, const PCRE2_SIZE *ovector);

	void check_capture(intptr_t nth) const;

	String m_subject;

	// Byte offsets into the subject; PCRE2_UNSET for a group that did not participate in the match.
	std::vector<PCRE2_SIZE> m_starts;
	std::vector<PCRE2_SIZE> m_ends;

	int m_rc = 0;
};


// A compiled, immutable regular expression. Copying recompiles from the stored pattern, so a copy
// is independent and equally usable. Matching never mutates the Regex: all per-match state lives in
// the returned Match.
class Regex final
{
public:

	enum Option {
		None           = 0,
		Caseless       = PCRE2_CASELESS,
		Multiline      = PCRE2_MULTILINE,
		DotAll         = PCRE2_DOTALL,
		Extended       = PCRE2_EXTENDED,
		Anchored       = PCRE2_ANCHORED,
		DollarEndOnly  = PCRE2_DOLLAR_ENDONLY,
		Ungreedy       = PCRE2_UNGREEDY,
	};

	// An empty Regex, which cannot be matched against (empty() returns true).
	Regex() = default;

	explicit Regex(const String &pattern);

	Regex(const String &pattern, int flags);

	Regex(const String &pattern, const String &flags);

	// Recompile from the stored pattern (which compiled once already, so this cannot fail).
	Regex(const Regex &other);

	Regex(Regex &&other) noexcept;

	Regex &operator=(const Regex &) = delete;

	Regex &operator=(Regex &&other) noexcept;

	~Regex();

	String pattern() const { return m_pattern; }

	int flags() const { return m_flags; }

	bool empty() const { return m_regex == nullptr; }

	// Search `subject` for the first leftmost match, starting at the beginning or at a given
	// position. Returns an empty Match (has_match() == false) if the pattern does not match; throws
	// on a genuine PCRE2 error. An empty subject never matches.
	Match match(const String &subject) const;
	Match match(const String &subject, String::const_iterator from) const;

	static int parse_flags(const String &options);

private:

	static String error_message(int error);

	// Compile m_pattern with m_flags (JIT enabled when available). This is the only place that
	// touches PCRE2's compiler; it throws on an invalid pattern.
	void compile();

	pcre2_code *m_regex = nullptr;

	String m_pattern;

	int m_flags = 0;

	uint32_t m_capture_count = 0;

	bool m_jit = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_BASE_REGEX_HPP
