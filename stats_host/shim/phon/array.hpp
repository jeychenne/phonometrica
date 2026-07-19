// Shim of phon/array.hpp for the headless statistics host (MIGRATION_NOTES step 4b):
// the engine's Array facade, plus phon/error.hpp — the old phon/base/array.hpp included
// it, and several analysis TUs get the formatted `error(...)` helper transitively that way.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#ifndef PHONOMETRICA_HEADLESS_ARRAY_INC_HPP
#define PHONOMETRICA_HEADLESS_ARRAY_INC_HPP

#include <phon/engine/core/array.hpp>
#include <phon/engine/types/array.hpp>
#include <phon/error.hpp>

#endif // PHONOMETRICA_HEADLESS_ARRAY_INC_HPP
