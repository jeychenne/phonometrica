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
 * Purpose: standard exceptions (A1 base-type swap). Provides the formatted `error(...)` helpers (a formatted          *
 * std::runtime_error via utils::format, '%' placeholders) on top of the NEW engine's types. The old engine's          *
 * RuntimeError/TraceEntry machinery is deliberately absent: script errors are now the engine's RuntimeError           *
 * (vm/isolate.hpp) — a TU that catches engine errors includes <phon/runtime.hpp> itself; this header stays light      *
 * because it is included from many TUs that only need the throw helper.                                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ERROR_HPP
#define PHONOMETRICA_ERROR_HPP

// Mirror the old header's standard includes — several TUs get <vector> and
// friends transitively from here.
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <phon/string.hpp>
#include <phon/utils/print.hpp>

namespace phonometrica {

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

static inline
std::runtime_error error(const String &msg)
{
	return std::runtime_error(std::string(msg.data(), static_cast<size_t>(msg.size())));
}

} // namespace phonometrica

#endif // PHONOMETRICA_ERROR_HPP
