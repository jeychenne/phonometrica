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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <utility>
#include <phon/application/protocol_apply.hpp>
#include <phon/regex.hpp>

namespace phonometrica {

// Escape PCRE2 metacharacters in a literal string so that the result matches the input verbatim.
// Byte-level iteration is safe: every PCRE2 metacharacter is a single-byte ASCII character, so
// multi-byte UTF-8 sequences (in the separator, for instance) pass through unchanged and PCRE2
// interprets them correctly.
static String regex_escape(const String &s)
{
	String result;
	for (auto it = s.begin(); it != s.end(); ++it)
	{
		char c = *it;
		switch (c)
		{
			case '\\': case '^': case '$': case '.': case '|':
			case '?':  case '*': case '+': case '(': case ')':
			case '[':  case ']': case '{': case '}':
				result.push_back('\\');
				break;
			default:
				break;
		}
		result.push_back(c);
	}
	return result;
}

// Build an anchored composite pattern with one capture group per protocol field:
//     ^(P1)(?:escaped_sep)(P2)(?:escaped_sep)...(PN)$
// When the separator is empty the inter-field (?:sep) groups are omitted. Capture group j
// corresponds to the j-th field (1-based), which is why field match_all patterns must not
// contain user capture groups (documented in the header).
static String build_composite_pattern(const Protocol &protocol)
{
	const auto &fields = protocol.fields();
	const String sep = protocol.separator();
	const bool has_sep = !sep.empty();
	String escaped_sep;
	if (has_sep) {
		escaped_sep = regex_escape(sep);
	}

	String pattern("^");
	for (intptr_t i = 0; i < fields.size(); i++)
	{
		if (i > 0 && has_sep)
		{
			pattern.append("(?:");
			pattern.append(escaped_sep);
			pattern.append(")");
		}
		pattern.append("(");
		pattern.append(fields[i].match_all);
		pattern.append(")");
	}
	pattern.append("$");
	return pattern;
}

// Match the raw captured value against each SearchValue in the field, in order, and return the
// first matching value's human-readable label. If no value pattern matches (i.e. the field's
// match_all is intentionally more permissive than the enumerated values), the raw capture is
// returned unchanged so that output cells are never silently dropped.
//
// The out-parameter `found` distinguishes two fall-through cases: when the field has no values
// defined, `found` is set to true (nothing to translate to, not a mismatch); when the field has
// values but none matched, `found` is set to false so the caller can count it as a fall-through.
static String translate_field_value(const SearchField &field, const String &raw, bool case_sensitive,
                                    bool &found)
{
	if (field.values.empty()) {
		found = true;
		return raw;
	}
	const int flags = case_sensitive ? Regex::None : Regex::Caseless;
	for (intptr_t i = 0; i < field.values.size(); i++)
	{
		const auto &v = field.values[i];
		String p("^(?:");
		p.append(v.match);
		p.append(")$");
		Regex re(p, flags);
		if (re.match(raw)) {
			found = true;
			return v.text;
		}
	}
	found = false;
	return raw;
}

ProtocolApplyResult apply_protocol(const Array<String> &source,
                                   const Protocol &protocol,
                                   bool translate)
{
	ProtocolApplyResult result;
	const auto &fields = protocol.fields();
	const intptr_t n_fields = fields.size();
	const intptr_t n_rows = source.size();
	const int composite_flags = protocol.case_sensitive() ? Regex::None : Regex::Caseless;

	// Field-name headers, one per output column.
	for (intptr_t j = 0; j < n_fields; j++) {
		result.headers.append(fields[j].name);
	}

	// Pre-allocate one output column per field with capacity for n_rows entries. Array<String>(n)
	// is capacity-only (size 0); values are appended inside the main loop below.
	for (intptr_t j = 0; j < n_fields; j++) {
		result.columns.append(Array<String>(n_rows));
	}

	// Degenerate case: protocol with no fields. Nothing to split; return empty columns and don't
	// flag any rows, since "no fields" is a trivially satisfied pattern.
	if (n_fields == 0) {
		return result;
	}

	// Compile the composite regex once. A malformed match_all propagates a compile error here,
	// before any row is processed.
	Regex composite(build_composite_pattern(protocol), composite_flags);

	for (intptr_t i = 0; i < n_rows; i++)
	{
		const String &text = source[i];

		auto m = composite.match(text);
		if (m)
		{
			// A row is counted as "untranslated" if any of its fields fell through to the raw
			// value despite having SearchValues defined. We append to untranslated_rows at most
			// once per row (after the per-field loop).
			bool row_had_untranslated = false;
			for (intptr_t j = 0; j < n_fields; j++)
			{
				// Capture groups are numbered from 1 (group 0 is the whole match).
				String raw = m.capture(j + 1);
				String cell;
				if (translate) {
					bool found = false;
					cell = translate_field_value(fields[j], raw, protocol.case_sensitive(), found);
					if (!found) row_had_untranslated = true;
				}
				else {
					cell = std::move(raw);
				}
				result.columns[j].append(std::move(cell));
			}
			if (row_had_untranslated) {
				result.untranslated_rows.append(i);
			}
		}
		else
		{
			for (intptr_t j = 0; j < n_fields; j++) {
				result.columns[j].append(String());
			}
			result.failed_rows.append(i);
		}
	}

	return result;
}

} // namespace phonometrica
