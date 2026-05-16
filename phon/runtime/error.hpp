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
#include <string>
#include <utility>
#include <vector>
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

// One frame of the call-stack trace attached to a RuntimeError.
//
// The trace is accumulated by `Runtime::interpret()` at each catch handler
// invocation as the exception propagates: the innermost frame (the throw
// site) is appended first, then each enclosing frame in turn. Each entry
// captures:
//   - file:    the path of the script being interpreted when this frame
//              caught (`Runtime::script_path()`); empty for in-memory
//              chunks or unknown sources.
//   - line:    1-based source line in that file. For the innermost frame
//              this is the throw site; for enclosing frames it is the
//              call-site line that led deeper.
//   - routine: name of the routine running in that frame; "<chunk>" for
//              top-level code with no enclosing function.
//
// std::string is used (rather than phonometrica::String) so this header
// stays free of the heavy String / QString chain — error.hpp is included
// from many TUs and we want to keep its dependencies minimal.
struct TraceEntry
{
	std::string file;
	intptr_t    line = 0;
	std::string routine;
};


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

	// Append a frame to the call-stack trace. `Runtime::interpret()` calls
	// this from inside its catch handler at each level the exception passes
	// through, so the resulting vector reads innermost-first: trace().front()
	// is the throw site, trace().back() is the outermost frame the error
	// reached (typically the call site in the catching frame, or the chunk
	// frame for an uncaught error). See TraceEntry for the per-entry fields.
	void push_trace(std::string file, intptr_t line_no, std::string routine)
	{
		trace_.push_back({ std::move(file), line_no, std::move(routine) });
	}

	// Frames the error passed through, innermost first. Empty for a freshly
	// constructed RuntimeError that has not been propagated through any
	// `interpret()` frame yet.
	const std::vector<TraceEntry> &trace() const { return trace_; }

private:

	intptr_t line;

	// Accumulated at unwind sites. See push_trace() above.
	std::vector<TraceEntry> trace_;
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
