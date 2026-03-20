/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: native callback.                                                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CALLBACK_HPP
#define PHONOMETRICA_CALLBACK_HPP

#include <bitset>
#include <functional>
#include <phon/runtime/variant.hpp>

namespace phonometrica {

class Runtime;

// Flags used to distinguish references and values in function signatures.
static constexpr size_t PARAM_BITSET_SIZE = 64;
using ParamBitset = std::bitset<PARAM_BITSET_SIZE>;


// A native C++ callback.
using NativeCallback = std::function<Variant(Runtime &rt, std::span<Variant> args)>;

} // namespace phonometrica

#endif // PHONOMETRICA_CALLBACK_HPP
