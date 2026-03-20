/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 20/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: standard exceptions.                                                                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ERROR_HPP
#define PHONOMETRICA_ERROR_HPP

#include <cstdint>
#include <stdexcept>
#include <phon/utils/print.hpp>

namespace phonometrica {

// Forward declaration.
class String;

template<typename T, typename... Args>
std::runtime_error error(const char *fmt, const T &value, Args... args)
{
	auto msg = utils::format(fmt, value, args...);
	return std::runtime_error(msg);
}

static inline
std::runtime_error error(const std::string &msg)
{
	return std::runtime_error(msg);
}

static inline
std::runtime_error error(const char *msg)
{
	return std::runtime_error(msg);
}

std::runtime_error error(const String &msg);


//---------------------------------------------------------------------------------------------------------------------

// Error from the scripting engine
class RuntimeError : public std::runtime_error
{
public:

	template<typename T, typename... Args>
	RuntimeError(intptr_t line, const char *fmt, const T &value, Args... args) :
		std::runtime_error(utils::format(fmt, value, args...)), line(line)
	{

	}

	RuntimeError(intptr_t line, const std::string &s) :
		std::runtime_error(s), line(line)
	{

	}

	RuntimeError(intptr_t line, const char *s) :
		std::runtime_error(s), line(line)
	{

	}

	RuntimeError(intptr_t line, const String &s);

	intptr_t line_no() const { return line; }

private:

	intptr_t line;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ERROR_HPP
