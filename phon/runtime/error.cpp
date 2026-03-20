/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 20/02/2019                                                                                                 *
 *                                                                                                                     *
 * purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/error.hpp>
#include <phon/string.hpp>

namespace phonometrica {

std::runtime_error error(const String &msg)
{
	return error(msg.data());
}

RuntimeError::RuntimeError(intptr_t line, const String &s) :
	std::runtime_error(s.data()), line(line)
{

}
} // namespace phonometrica
