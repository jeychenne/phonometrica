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
 * Purpose: THE conversion boundary between script indices and native indices. The scripting language is 1-based and   *
 * accepts negative indices (counting from the end); native containers (Array, List storage) are strictly 0-based.     *
 * These two functions are the only place where the conversion may happen: every app native exposed to the scripting  *
 * engine must route incoming script indices through index_from_script() and outgoing native positions through         *
 * index_to_script(). Native C++ code must never add or subtract 1 to convert an index itself.                         *
 *                                                                                                                     *
 * The engine applies the equivalent conversion to its own builtins; this header is the app-side converter, used at    *
 * the binding boundary of the natives registered by the application layer (roadmap A3).                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INDEX_CONVERSION_HPP
#define PHONOMETRICA_INDEX_CONVERSION_HPP

#include <cstdint>
#include <phon/error.hpp>

namespace phonometrica {

// Inbound choke point: convert a 1-based, possibly negative script index to a checked 0-based native
// index into a sequence of length `len`. When `allow_end` is true, positions past the end are clamped
// to `len` (one past the last element), which preserves the historical script semantics of insertion
// ("inserting" past the end appends).
inline intptr_t index_from_script(intptr_t i, intptr_t len, bool allow_end = false)
{
	if (i > 0)
	{
		if (i <= len) {
			return i - 1;
		}
		if (allow_end) {
			return len;
		}
	}
	else if (i >= -len && i < 0)
	{
		return len + i;
	}

	throw error("Index % out of range in array containing % items", i, len);
}

// Same conversion as index_from_script(), for one axis of a matrix; only the error wording differs
// (it reports a dimension length rather than an item count, as the historical messages did).
inline intptr_t dim_index_from_script(intptr_t i, intptr_t len)
{
	if (i > 0 && i <= len) {
		return i - 1;
	}
	if (i >= -len && i < 0) {
		return len + i;
	}

	throw error("Index % out of range in array dimension with length %", i, len);
}

// Outbound choke point: convert a 0-based native position to a 1-based script index. A negative
// native position (e.g. Array::npos, "not found") maps to 0, the script-level "not found" value.
inline intptr_t index_to_script(intptr_t i)
{
	return (i < 0) ? 0 : i + 1;
}

} // namespace phonometrica

#endif // PHONOMETRICA_INDEX_CONVERSION_HPP
