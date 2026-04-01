/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 12/12/2023                                                                                                 *
 *                                                                                                                     *
 * Purpose: Integer builtin functions.                                                                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_INTEGER_HPP
#define PHONOMETRICA_FUNC_INTEGER_HPP


#include <phon/runtime.hpp>

namespace phonometrica {

static Variant integer_new(Runtime &, std::span<Variant> args)
{
	auto &s = cast<String>(args[0]);
	bool ok = false;
	intptr_t value = s.to_int(&ok);
	if (!ok) {
		throw error("[Type error] Cannot convert string \"%\" to Integer", s);
	}

	return Variant(value);
}

} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_INTEGER_HPP
