/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
