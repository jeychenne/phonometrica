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
 * Created: 20/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: application-wide definitions. This is the application-domain remainder of the old engine's                 *
 * <phon/runtime/definitions.hpp>: the engine-core parts (cell/GC declarations, meta::) now come from the new          *
 * engine's <phon/engine/base/definitions.hpp>.                                                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DEFINITIONS_HPP
#define PHONOMETRICA_DEFINITIONS_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <phon/engine/base/definitions.hpp>

#ifdef __GNUC__
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#else
#define likely(x)       x
#define unlikely(x)     x
#endif

// File extension
#ifndef PHON_FILE_EXTENSION
#	define PHON_FILE_EXTENSION ".phon"
#endif


#ifdef PHON_EMBED_SCRIPTS
#define run_script(runtime, name) runtime.do_string(Settings::load_script(#name))
#else
#define run_script(rt, name) rt.do_file(Settings::get_std_script(#name))
#endif

namespace phonometrica {

// Largest and smallest integers that can be safely stored in a double.
static constexpr double largest_integer = 9007199254740992;
static constexpr double smallest_integer = -9007199254740992;

} // namespace phonometrica


// If logging is enabled, messages are logged to a file named "phonometrica.log" in the user's home directory.
#if defined(PHON_ENABLE_LOGGING) || defined(PHON_DEBUG)
#define PHON_LOG(...) fprintf(stderr, __VA_ARGS__);
#else
#define PHON_LOG(...);
#endif

#define PHON_MAX_FORMANTS 10
// Voicing thresholds:
//   - RAPT: -0.6 <= T <= 0.7 (default: 0.0)
//   - SWIPE: 0.2 <= T <=> 0.5 (default: 0.3)
//   - REAPER: -0.5 <= T <=> 1.6 (default: 0.9)
//   - Harvest: 0.0 <= T <= 0.2 (default: 0.01)

#define PHON_HARVEST_THRESHOLD_MIN 0.0
#define PHON_HARVEST_THRESHOLD_MAX 0.2
#define PHON_HARVEST_THESHOLD_STEP 0.01
#define PHON_HARVEST_THRESHOLD_DEFAULT 0.01
#define PHON_REAPER_THRESHOLD_MIN -0.5
#define PHON_REAPER_THRESHOLD_MAX 1.6
#define PHON_REAPER_THESHOLD_STEP 0.1
#define PHON_REAPER_THRESHOLD_DEFAULT 0.9
#define PHON_RAPT_THRESHOLD_MIN -0.6
#define PHON_RAPT_THRESHOLD_MAX 0.7
#define PHON_RAPT_THESHOLD_STEP 0.1
#define PHON_RAPT_THRESHOLD_DEFAULT 0.0
#define PHON_SWIPE_THRESHOLD_MIN 0.2
#define PHON_SWIPE_THRESHOLD_MAX 0.5
#define PHON_SWIPE_THESHOLD_STEP 0.05
#define PHON_SWIPE_THRESHOLD_DEFAULT 0.3

#endif // PHONOMETRICA_DEFINITIONS_HPP
