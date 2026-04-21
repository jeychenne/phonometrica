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
 * Purpose: apply a coding protocol (see phon/application/protocol.hpp) to a column of text values, splitting each     *
 * row into one output column per protocol field and optionally translating raw codes into human-readable labels.      *
 * This primitive is consumed by the protocol-builder dialog (for live preview), by the "Apply coding protocol..."     *
 * action on concordance and dataset columns, and by the scripting API. It operates on plain string arrays so that a   *
 * single implementation serves all call sites.                                                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PROTOCOL_APPLY_HPP
#define PHONOMETRICA_PROTOCOL_APPLY_HPP

#include <phon/array.hpp>
#include <phon/string.hpp>
#include <phon/application/protocol.hpp>

namespace phonometrica {

struct ProtocolApplyResult final
{
	// One header per protocol field, in field order. Size == protocol.field_count().
	Array<String> headers;

	// One output column per protocol field. columns[j] (1-based) holds the values of the j-th field,
	// with one entry per input row (so columns[j].size() == source.size() for every j).
	Array<Array<String>> columns;

	// 1-based indices of input rows that did not match the protocol. Those rows contain an empty
	// string in every output column.
	Array<intptr_t> failed_rows;

	// 1-based indices of input rows that matched the composite regex but contained at least one
	// field value that did not match any of the field's enumerated SearchValue patterns. Those
	// fields fall back to the raw capture (no data lost), but the user may want to know in order
	// to tighten the protocol. Only populated when `translate` is true; only counts rows where
	// the field actually had SearchValues defined (a field with no values is not a fall-through).
	Array<intptr_t> untranslated_rows;
};

// Apply a coding protocol to the given column of text values.
//
// The function compiles the protocol into a single anchored regex of the form
//     ^(P1)(?:sep)(P2)(?:sep)...(PN)$
// where each P_j is the match_all pattern of field j and (?:sep) is the escaped protocol separator
// (elided when the separator is empty). Each row is matched against this regex. On success, the
// j-th capture group is the raw value of field j; on failure, the row is recorded in failed_rows
// and emitted as empty cells across all output columns.
//
// When `translate` is true (default), each raw capture is then matched against the field's
// SearchValue patterns in order, and the first matching value's `text` is written to the output.
// If no SearchValue matches (i.e. the protocol's match_all is more permissive than its enumerated
// values), the raw capture is kept. When `translate` is false, raw captures are always kept.
//
// Throws std::runtime_error if the protocol is structurally invalid (e.g. the composite regex
// fails to compile because a field's match_all is malformed). Individual row parse failures are
// reported via failed_rows and do not throw.
//
// IMPORTANT: field `match_all` patterns must not contain capturing groups, because user-introduced
// capture groups would shift the capture numbering and break per-field extraction. Use (?:...) for
// non-capturing alternation inside a match_all.
ProtocolApplyResult apply_protocol(const Array<String> &source,
                                   const Protocol &protocol,
                                   bool translate = true);

} // namespace phonometrica

#endif // PHONOMETRICA_PROTOCOL_APPLY_HPP
