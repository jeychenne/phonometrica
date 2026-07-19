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
 * Created: 19/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: shim of phon/error.hpp for the headless statistics host (MIGRATION_NOTES step 4b). Provides the old        *
 * formatted `error(...)` helpers (a formatted std::runtime_error via utils::format, '%' placeholders) on top of the   *
 * NEW engine's types, plus the engine's own embedder-visible error types. The old engine's RuntimeError/TraceEntry    *
 * machinery is deliberately absent — the analysis layer only throws error(...).                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_HEADLESS_ERROR_HPP
#define PHONOMETRICA_HEADLESS_ERROR_HPP

// Mirror the old header's standard includes — several analysis TUs get <vector>
// and friends transitively from here.
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Deliberately NOT including the engine's isolate/diagnostic headers here (the real
// engine facade does): this shim serves the analysis layer, which only needs the
// formatted throw helper — a host TU that catches engine errors includes
// <phon/runtime.hpp> itself.
#include <phon/string.hpp>
#include <phon/utils/print.hpp>

namespace phonometrica {

template<typename T, typename... Args>
std::runtime_error error(const char *fmt, const T &value, Args... args)
{
	auto msg = utils::format(fmt, value, args...);
	return std::runtime_error(msg);
}

static inline std::runtime_error error(const std::string &msg)
{
	return std::runtime_error(msg);
}

static inline std::runtime_error error(const char *msg)
{
	return std::runtime_error(msg);
}

static inline std::runtime_error error(const String &msg)
{
	return std::runtime_error(std::string(msg.data(), static_cast<size_t>(msg.size())));
}

// The OLD engine's GC trait (phon/runtime/traits.hpp): analysis headers specialize it
// for Model/PriorSpec. Under the new engine it is meaningless — declaring the primary
// template here lets those specializations compile as dead metadata. The
// specializations (and this stub) disappear with the old engine at final cutover.
namespace traits {
template<typename T>
struct maybe_cyclic : std::true_type
{
};
} // namespace traits

} // namespace phonometrica

#endif // PHONOMETRICA_HEADLESS_ERROR_HPP
