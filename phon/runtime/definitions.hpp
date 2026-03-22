/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: common definitions.                                                                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_DEFINITIONS_HPP
#define PHONOMETRICA_DEFINITIONS_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <functional>
#include <span>

#define PHON_UNUSED(x) (void)(x)

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

namespace meta {

static constexpr size_t pointer_size = sizeof(void*);
static constexpr bool is_arch32 = (pointer_size == 4);
static constexpr bool is_arch64 = (pointer_size == 8);

} // namespace phonometrica::meta


// Largest and smallest integers that can be safely stored in a double.
static constexpr double largest_integer = 9007199254740992;
static constexpr double smallest_integer = -9007199254740992;

// Forward declarations.
class Object;
class Collectable;

// Callback for the garbage collector.
using GCCallback = std::function<void(Collectable*)>;

} // namespace phonometrica


// If logging is enabled, messages are logged to a file named "phonometrica.log" in the user's home directory.
// Add "#include <wx/log.h>" to files that make use of this macro.
#if defined(PHON_ENABLE_LOGGING) || defined(PHON_DEBUG)
//#define PHON_LOG(...) wxLogDebug(__VA_ARGS__);
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


#define PHON_CTOR_STRING "new$"
#define PHON_INIT_STRING "initialize"
#define PHON_TOSTR_STRING "to_string"

#endif // PHONOMETRICA_DEFINITIONS_HPP
