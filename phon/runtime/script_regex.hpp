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
 * Purpose: the scripting language's Regex object. The language keeps its historical stateful API (match(re, s) then   *
 * group(re, i)), but the C++ Regex class is now immutable and reentrant, so the last match is stored here — in the    *
 * boxed script object owned by the old engine's bridge — and never in the Regex itself. This wrapper is retired with  *
 * the old engine: the new engine exposes Match objects to scripts directly.                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCRIPT_REGEX_HPP
#define PHONOMETRICA_SCRIPT_REGEX_HPP

#include <phon/regex.hpp>

namespace phonometrica {

struct ScriptRegex final
{
	explicit ScriptRegex(const String &pattern) : re(pattern) { }

	ScriptRegex(const String &pattern, const String &flags) : re(pattern, flags) { }

	// The compiled pattern (immutable, no per-match state).
	Regex re;

	// The result of the most recent match(re, ...) call from a script.
	Match last;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SCRIPT_REGEX_HPP
