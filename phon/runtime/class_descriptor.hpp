/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 04/06/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: manages classes known at compile time.                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CLASS_DESCRIPTOR_HPP
#define PHONOMETRICA_CLASS_DESCRIPTOR_HPP

#include <type_traits>
#include <phon/runtime/definitions.hpp>

namespace phonometrica {
class Class;
}

namespace phonometrica::detail {


// A template to keep track of classes known at compile time. This should not be accessed directly: use
// Class::get<T>() instead.
template<typename T>
struct ClassDescriptor
{
	static Class *get()
	{
		// Class is null while we are bootstrapping the class system. The runtime will check that we have a valid pointer for Class.
		assert(isa || (std::is_same_v<T, Class>));
		return isa;
	}

	static void set(Class *cls)
	{
		assert(isa == nullptr);
		isa = cls;
	}

private:

	static Class *isa;
};

template<class T>
Class *ClassDescriptor<T>::isa = nullptr;

} // namespace phonometrica::detail

#endif // PHONOMETRICA_CLASS_DESCRIPTOR_HPP
