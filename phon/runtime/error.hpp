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
 * Created: 20/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: standard exceptions.                                                                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ERROR_HPP
#define PHONOMETRICA_ERROR_HPP

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <phon/utils/print.hpp>

namespace phonometrica {

// Forward declarations.
class String;
class Variant;

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


//---------------------------------------------------------------------------------------------------------------------

// Exception raised by the scripting language's `throw` statement. Carries an arbitrary
// scripted value alongside the error message, so that a user's `catch` clause can bind
// the original thrown value rather than only its string representation. When uncaught,
// it behaves like a regular RuntimeError (the message holds the value's string form).
//
// The carried value is held via a heap-allocated Variant in order to keep this header
// free of <variant.hpp>; all special members are defined out-of-line in error.cpp where
// Variant is complete.
class ScriptException : public RuntimeError
{
public:

	ScriptException(intptr_t line, const String &msg, Variant value);

	ScriptException(const ScriptException &other);

	ScriptException(ScriptException &&other) noexcept;

	~ScriptException() override;

	ScriptException &operator=(const ScriptException &) = delete;
	ScriptException &operator=(ScriptException &&) = delete;

	const Variant &value() const;

	Variant take_value();

private:

	std::unique_ptr<Variant> m_value;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ERROR_HPP
