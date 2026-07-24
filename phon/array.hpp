// Forwarding header — the engine's arrays: the generic growable CoW container
// Array<T> and the numeric strided NumArray (the script-visible class named
// "Array"). phon/error.hpp is included because many TUs get the formatted
// `error(...)` helper transitively that way.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#ifndef PHONSCRIPT_ARRAY_INC_HPP
#define PHONSCRIPT_ARRAY_INC_HPP

#include <phon/engine/core/array.hpp>
#include <phon/engine/types/array.hpp>
#include <phon/error.hpp>

#endif
