// Phonometrica engine — trivial-relocation trait and helpers.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A type is *trivially relocatable* if moving it to fresh storage and destroying
// the source is equivalent to memcpy-ing the bytes. This is true for all trivially
// copyable types and for engine value types whose moves only shuffle bytes
// (String, Handle, Value): those specialize the trait even though their copy
// constructors are non-trivial. Containers use it to grow with memcpy instead of
// element-wise move+destroy (design/architecture.md §4).

#ifndef PHON_CORE_RELOCATE_HPP
#define PHON_CORE_RELOCATE_HPP

#include <phon/engine/base/definitions.hpp>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace phonometrica {

template<typename T>
struct TriviallyRelocatable : std::bool_constant<std::is_trivially_copyable_v<T>>
{
};

template<typename T>
inline constexpr bool is_trivially_relocatable_v = TriviallyRelocatable<T>::value;

// Relocate n objects from src to dst. dst is raw (uninitialized) storage; on
// return src is raw storage (its objects have been destroyed/moved-from). The
// ranges must not overlap.
template<typename T>
void relocate_range(T *dst, T *src, intptr_t n) noexcept
{
	PHON_ASSERT(n >= 0);
	if constexpr (is_trivially_relocatable_v<T>)
	{
		if (n > 0)
			std::memcpy(dst, src, static_cast<size_t>(n) * sizeof(T));
	}
	else
	{
		for (intptr_t i = 0; i < n; ++i)
		{
			::new (static_cast<void *>(dst + i)) T(std::move(src[i]));
			src[i].~T();
		}
	}
}

// Destroy n objects in place (no-op for trivially destructible T).
template<typename T>
void destroy_range(T *first, intptr_t n) noexcept
{
	PHON_ASSERT(n >= 0);
	if constexpr (!std::is_trivially_destructible_v<T>)
	{
		for (intptr_t i = 0; i < n; ++i)
			first[i].~T();
	}
}

} // namespace phonometrica

#endif // PHON_CORE_RELOCATE_HPP
