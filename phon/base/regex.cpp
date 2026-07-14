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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/regex.hpp>
#include <phon/error.hpp>
#include <phon/third_party/utf8/utf8.h>

namespace phonometrica {

static const size_t ERROR_BUFFER_SIZE = 512;


//----------------------------------------------------------------------------------------------------------------------
// Match

Match::Match(String subject, int rc, uint32_t group_count, const PCRE2_SIZE *ovector) :
	m_subject(std::move(subject)), m_rc(rc)
{
	m_starts.reserve(group_count);
	m_ends.reserve(group_count);

	for (uint32_t i = 0; i < group_count; i++)
	{
		// A group beyond the highest participating one has an undefined ovector entry, so record it
		// as not participating; likewise an explicit PCRE2_UNSET pair.
		PCRE2_SIZE start = ovector[2 * i];
		PCRE2_SIZE end = ovector[2 * i + 1];

		if (int(i) >= rc || start == PCRE2_UNSET)
		{
			start = PCRE2_UNSET;
			end = PCRE2_UNSET;
		}
		m_starts.push_back(start);
		m_ends.push_back(end);
	}
}

void Match::check_capture(intptr_t nth) const
{
	if (!has_match()) {
		throw error("Cannot access capture % (no match)", nth);
	}
	if (nth < 0 || nth > count()) {
		throw error("Invalid capture index % (regex has % captures)", nth, count());
	}
	if (m_starts[size_t(nth)] == PCRE2_UNSET) {
		throw error("Capture % did not participate in the match", nth);
	}
}

String Match::capture(intptr_t nth) const
{
	check_capture(nth);
	auto start = m_starts[size_t(nth)];
	auto len = intptr_t(m_ends[size_t(nth)] - start);

	return String(m_subject.data() + start, len);
}

intptr_t Match::capture_start(intptr_t nth, bool utf8) const
{
	check_capture(nth);
	auto pos = intptr_t(m_starts[size_t(nth)]);

	if (utf8) {
		pos = utf8::unchecked::distance(m_subject.begin(), m_subject.begin() + pos);
	}

	return pos + 1; // base 1
}

intptr_t Match::capture_end(intptr_t nth, bool utf8) const
{
	check_capture(nth);
	auto pos = intptr_t(m_ends[size_t(nth)]);

	if (utf8) {
		pos = utf8::unchecked::distance(m_subject.begin(), m_subject.begin() + pos);
	}

	return pos + 1; // base 1
}

String::const_iterator Match::capture_start_iter(intptr_t nth) const
{
	check_capture(nth);
	return m_subject.begin() + intptr_t(m_starts[size_t(nth)]);
}

String::const_iterator Match::capture_end_iter(intptr_t nth) const
{
	check_capture(nth);
	return m_subject.begin() + intptr_t(m_ends[size_t(nth)]);
}


//----------------------------------------------------------------------------------------------------------------------
// Regex

Regex::Regex(const String &pattern) :
	Regex(pattern, None)
{

}

Regex::Regex(const String &pattern, int flags) :
	m_pattern(pattern), m_flags(flags)
{
	compile();
}

Regex::Regex(const String &pattern, const String &flags) :
	Regex(pattern, parse_flags(flags))
{

}

Regex::Regex(const Regex &other) :
	m_pattern(other.m_pattern), m_flags(other.m_flags)
{
	if (!other.empty()) {
		compile(); // recompile from a pattern that already compiled once: this cannot fail.
	}
}

Regex::Regex(Regex &&other) noexcept :
	m_regex(other.m_regex), m_pattern(std::move(other.m_pattern)), m_flags(other.m_flags),
	m_capture_count(other.m_capture_count), m_jit(other.m_jit)
{
	other.m_regex = nullptr;
}

Regex &Regex::operator=(Regex &&other) noexcept
{
	if (this != &other)
	{
		if (m_regex) {
			pcre2_code_free(m_regex);
		}
		m_regex = other.m_regex;
		m_pattern = std::move(other.m_pattern);
		m_flags = other.m_flags;
		m_capture_count = other.m_capture_count;
		m_jit = other.m_jit;
		other.m_regex = nullptr;
	}

	return *this;
}

Regex::~Regex()
{
	if (m_regex) {
		pcre2_code_free(m_regex);
	}
}

void Regex::compile()
{
	int error_code = 0;
	PCRE2_SIZE error_offset = 0;

	m_regex = pcre2_compile((PCRE2_SPTR) m_pattern.data(), m_pattern.size(),
	                        (uint32_t) m_flags|PCRE2_UTF, &error_code, &error_offset, nullptr);

	if (m_regex == nullptr) {
		throw error("compilation of regular expression failed at position %: %",
		            error_offset + 1, error_message(error_code));
	}

	uint32_t cc = 0;
	pcre2_pattern_info(m_regex, PCRE2_INFO_CAPTURECOUNT, &cc);
	m_capture_count = cc;
	// JIT-compile for speed; matching falls back to the interpreter if this fails.
	m_jit = (pcre2_jit_compile(m_regex, PCRE2_JIT_COMPLETE) == 0);
}

String Regex::error_message(int error)
{
	PCRE2_UCHAR buffer[ERROR_BUFFER_SIZE];
	pcre2_get_error_message(error, buffer, ERROR_BUFFER_SIZE);

	return String::format("[Regex error] %s", reinterpret_cast<const char*>(buffer));
}

Match Regex::match(const String &subject) const
{
	return match(subject, subject.begin());
}

Match Regex::match(const String &subject, String::const_iterator from) const
{
	assert(!empty());
	if (subject.empty()) {
		return Match();
	}
	size_t index = from - subject.begin();

	// Each match owns its match data, so the same Regex can be matched concurrently from several
	// threads without any shared mutable state.
	pcre2_match_data *match_data = pcre2_match_data_create(m_capture_count + 1, nullptr);
	int rc;

	if (m_jit) {
		rc = pcre2_jit_match(m_regex, (PCRE2_SPTR) subject.data(), subject.size(), index, 0, match_data, nullptr);
	}
	else {
		rc = pcre2_match(m_regex, (PCRE2_SPTR) subject.data(), subject.size(), index, 0, match_data, nullptr);
	}

	// -1 is used to indicate there is no match, so we don't want to trigger an error for that.
	if (rc < -1)
	{
		pcre2_match_data_free(match_data);
		throw error(error_message(rc));
	}

	Match result;

	if (rc > 0)
	{
		const PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
		result = Match(subject, rc, m_capture_count + 1, ovector);
	}
	pcre2_match_data_free(match_data);

	return result;
}

int Regex::parse_flags(const String &options)
{
	int flags = None;

	for (auto &s : options.split("|"))
	{
		if (s == "caseless") {
			flags |= Caseless;
		}
		else if (s == "multiline") {
			flags |= Multiline;
		}
		else if (s == "dotall") {
			flags |= DotAll;
		}
		else if (s == "extended") {
			flags |= Extended;
		}
		else if (s == "anchored") {
			flags |= Anchored;
		}
		else if (s == "dollar_endonly") {
			flags |= DollarEndOnly;
		}
		else if (s == "ungreedy") {
			flags |= Ungreedy;
		}
	}

	return flags;
}

} // namespace phonometrica
