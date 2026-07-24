// Phonometrica engine — bit and alignment utilities.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Every representation detail that these helpers touch is funnelled through a
// single inline choke point (design/architecture.md §16.1).

#ifndef PHON_BASE_BITS_HPP
#define PHON_BASE_BITS_HPP

#include <phon/engine/base/definitions.hpp>
#include <bit>
#include <cstring>
#include <type_traits>

namespace phonometrica {

// bit_cast: reinterpret the object representation of one type as another of the
// same size. Wraps std::bit_cast so the whole engine has one spelling.
template<typename To, typename From>
PHON_FORCE_INLINE To bit_cast(const From &src) noexcept
{
	static_assert(sizeof(To) == sizeof(From), "bit_cast requires equal sizes");
	static_assert(std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>,
	              "bit_cast requires trivially copyable types");
	return std::bit_cast<To>(src);
}

// is_power_of_two: true for 1, 2, 4, 8, ...  (false for 0).
PHON_FORCE_INLINE constexpr bool is_power_of_two(uintptr_t n) noexcept
{
	return n != 0 && (n & (n - 1)) == 0;
}

// align_up: round n up to the next multiple of alignment a (a must be a power
// of two). Works for both intptr_t and uintptr_t sizes.
PHON_FORCE_INLINE constexpr intptr_t align_up(intptr_t n, intptr_t a) noexcept
{
	PHON_ASSERT(a > 0 && is_power_of_two(static_cast<uintptr_t>(a)));
	return (n + (a - 1)) & ~(a - 1);
}

// align_down: round n down to the previous multiple of alignment a.
PHON_FORCE_INLINE constexpr intptr_t align_down(intptr_t n, intptr_t a) noexcept
{
	PHON_ASSERT(a > 0 && is_power_of_two(static_cast<uintptr_t>(a)));
	return n & ~(a - 1);
}

PHON_FORCE_INLINE bool is_aligned(const void *p, uintptr_t a) noexcept
{
	return (reinterpret_cast<uintptr_t>(p) & (a - 1)) == 0;
}

// next_power_of_two: smallest power of two >= n (n >= 1). n == 0 yields 1.
PHON_FORCE_INLINE uintptr_t next_power_of_two(uintptr_t n) noexcept
{
	if (n <= 1)
		return 1;
	return std::bit_ceil(n);
}

// count_trailing_zeros / count_leading_zeros: undefined for 0 (assert in debug).
PHON_FORCE_INLINE int count_trailing_zeros(uint64_t n) noexcept
{
	PHON_ASSERT(n != 0);
	return std::countr_zero(n);
}

PHON_FORCE_INLINE int count_trailing_zeros(uint32_t n) noexcept
{
	PHON_ASSERT(n != 0);
	return std::countr_zero(n);
}

PHON_FORCE_INLINE int count_leading_zeros(uint64_t n) noexcept
{
	PHON_ASSERT(n != 0);
	return std::countl_zero(n);
}

PHON_FORCE_INLINE int popcount(uint64_t n) noexcept
{
	return std::popcount(n);
}

// floor_log2: index of the highest set bit (n >= 1).
PHON_FORCE_INLINE int floor_log2(uint64_t n) noexcept
{
	PHON_ASSERT(n != 0);
	return 63 - std::countl_zero(n);
}

} // namespace phonometrica

#endif // PHON_BASE_BITS_HPP
