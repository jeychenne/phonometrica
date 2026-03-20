/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 12/07/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime/meta.hpp>
#include <phon/runtime/class.hpp>

namespace phonometrica::meta {

namespace detail {

String get_class_name_helper(Class *klass)
{
	return klass->name();
}

} // namespace detail

bool is_base_of(const Class *base, const Class *derived)
{
	return derived->inherits(base);
}

} // namespace::meta::detail