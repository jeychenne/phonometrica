// Phonometrica engine — Regex + Match implementation (PCRE2 with JIT).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/types/regex.hpp>
#include <phon/engine/base/definitions.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace phonometrica {

namespace {

constexpr uint32_t ERROR_BUFFER_SIZE = 256;

String pcre_error_message(int code)
{
	PCRE2_UCHAR buffer[ERROR_BUFFER_SIZE];
	int n = pcre2_get_error_message(code, buffer, ERROR_BUFFER_SIZE);
	if (n < 0)
		return String("unknown error");
	return String(reinterpret_cast<const char *>(buffer), n);
}

} // namespace

// --- Match --------------------------------------------------------------------

Match::Match(String subject, int total_groups, int rc, const PCRE2_SIZE *ovector)
    : m_subject(std::move(subject)), m_total_groups(total_groups)
{
	m_starts.reserve(static_cast<size_t>(total_groups));
	m_ends.reserve(static_cast<size_t>(total_groups));
	for (int i = 0; i < total_groups; ++i)
	{
		// A group beyond the highest participating one (i >= rc) has an undefined ovector
		// entry, so treat it as not participating; likewise an explicit PCRE2_UNSET pair.
		PCRE2_SIZE s = ovector[2 * i];
		PCRE2_SIZE e = ovector[2 * i + 1];
		if (i >= rc || s == PCRE2_UNSET)
		{
			s = PCRE2_UNSET;
			e = PCRE2_UNSET;
		}
		m_starts.push_back(s);
		m_ends.push_back(e);
	}
}

String Match::group(int n) const
{
	PHON_ASSERT(in_range(n) && group_is_set(n));
	PCRE2_SIZE s = m_starts[static_cast<size_t>(n)];
	PCRE2_SIZE e = m_ends[static_cast<size_t>(n)];
	return String(m_subject.data() + s, static_cast<intptr_t>(e - s));
}

intptr_t Match::group_start(int n) const
{
	PHON_ASSERT(in_range(n));
	PCRE2_SIZE s = m_starts[static_cast<size_t>(n)];
	if (s == PCRE2_UNSET)
		return 0;
	return m_subject.distance(m_subject.begin(), m_subject.begin() + s) + 1;
}

intptr_t Match::group_end(int n) const
{
	PHON_ASSERT(in_range(n));
	PCRE2_SIZE e = m_ends[static_cast<size_t>(n)];
	if (e == PCRE2_UNSET)
		return 0;
	return m_subject.distance(m_subject.begin(), m_subject.begin() + e) + 1;
}

// --- Regex --------------------------------------------------------------------

void Regex::compile()
{
	int error_code = 0;
	PCRE2_SIZE error_offset = 0;
	m_code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(m_pattern.data()),
	                       static_cast<PCRE2_SIZE>(m_pattern.size()),
	                       static_cast<uint32_t>(m_flags) | PCRE2_UTF, &error_code, &error_offset,
	                       nullptr);
	if (!m_code)
	{
		String msg = pcre_error_message(error_code);
		throw std::runtime_error("invalid regular expression at position " +
		                         std::to_string(error_offset + 1) + ": " +
		                         std::string(msg.data(), static_cast<size_t>(msg.size())));
	}
	int cc = 0;
	pcre2_pattern_info(m_code, PCRE2_INFO_CAPTURECOUNT, &cc);
	m_capture_count = cc;
	// JIT-compile for speed; matching falls back to the interpreter if this fails.
	m_jit = (pcre2_jit_compile(m_code, PCRE2_JIT_COMPLETE) == 0);
}

int Regex::parse_flags(const String &options)
{
	int flags = None;
	// The flags string is ASCII: split on '|' into tokens and OR in each recognised name.
	std::string_view text(options.data(), static_cast<size_t>(options.size()));
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t bar = text.find('|', pos);
		std::string_view s = text.substr(pos, (bar == std::string_view::npos ? text.size() : bar) - pos);
		if (s == "caseless")
			flags |= Caseless;
		else if (s == "multiline")
			flags |= Multiline;
		else if (s == "dotall")
			flags |= DotAll;
		else if (s == "extended")
			flags |= Extended;
		else if (s == "anchored")
			flags |= Anchored;
		else if (s == "dollar_endonly")
			flags |= DollarEndOnly;
		else if (s == "ungreedy")
			flags |= Ungreedy;
		if (bar == std::string_view::npos)
			break;
		pos = bar + 1;
	}
	return flags;
}

Regex::Regex(const String &pattern) : Regex(pattern, None) {}

Regex::Regex(const String &pattern, int flags) : m_pattern(pattern), m_flags(flags)
{
	compile();
}

Regex::Regex(const String &pattern, const String &flags)
    : Regex(pattern, parse_flags(flags))
{
}

Regex::Regex(const Regex &other) : m_pattern(other.m_pattern), m_flags(other.m_flags)
{
	// Recompile from the pattern that already compiled once — this cannot fail.
	compile();
}

Regex::Regex(Regex &&other) noexcept
    : m_code(other.m_code), m_pattern(std::move(other.m_pattern)), m_flags(other.m_flags),
      m_capture_count(other.m_capture_count), m_jit(other.m_jit)
{
	other.m_code = nullptr;
}

Regex::~Regex()
{
	if (m_code)
		pcre2_code_free(m_code);
}

Match Regex::match(const String &subject, intptr_t from, String *error) const
{
	pcre2_match_data *md = pcre2_match_data_create(static_cast<uint32_t>(m_capture_count + 1),
	                                               nullptr);
	// Each match owns its match data, so the same Regex matches concurrently from many
	// threads without shared mutable state.
	int rc;
	auto *sptr = reinterpret_cast<PCRE2_SPTR>(subject.data());
	auto len = static_cast<PCRE2_SIZE>(subject.size());
	auto start = static_cast<PCRE2_SIZE>(from);
	if (m_jit)
		rc = pcre2_jit_match(m_code, sptr, len, start, 0, md, nullptr);
	else
		rc = pcre2_match(m_code, sptr, len, start, 0, md, nullptr);

	Match result;
	if (rc > 0)
	{
		const PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(md);
		result = Match(subject, m_capture_count + 1, rc, ovector);
	}
	else if (rc < 0 && rc != PCRE2_ERROR_NOMATCH && rc != PCRE2_ERROR_PARTIAL)
	{
		// A genuine error (not simply "no match"): report it if the caller wants.
		if (error)
			*error = pcre_error_message(rc);
	}
	pcre2_match_data_free(md);
	return result;
}

} // namespace phonometrica
