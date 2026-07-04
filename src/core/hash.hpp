// Phonometrica engine — hashing primitives and default hashers.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// FlatHashMap needs a well-mixed 64-bit hash: the low bits pick the slot and
// bits 0..6 form the control tag, so a weak hash (e.g. identity on integers)
// clusters badly. We use a MurmurHash3-style finalizer for integers and pointers
// and a small FNV-1a for byte spans.

#ifndef PHON_CORE_HASH_HPP
#define PHON_CORE_HASH_HPP

#include "base/definitions.hpp"
#include <cstring>
#include <type_traits>

namespace phonometrica {

// 64-bit avalanche mixer (MurmurHash3 fmix64).
PHON_FORCE_INLINE uint64_t hash_mix(uint64_t x) noexcept
{
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ull;
	x ^= x >> 33;
	return x;
}

// FNV-1a over a byte span, then avalanche so the low bits are usable.
PHON_FORCE_INLINE uint64_t hash_bytes(const void *data, intptr_t len) noexcept
{
	const auto *p = static_cast<const unsigned char *>(data);
	uint64_t h = 0xcbf29ce484222325ull;
	for (intptr_t i = 0; i < len; ++i)
	{
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return hash_mix(h);
}

// Default hasher. Specialized/overloaded for integral, enum, and pointer keys;
// user types provide their own Hash functor as the template argument.
template<typename K>
struct Hasher
{
	uint64_t operator()(const K &key) const noexcept
	{
		if constexpr (std::is_integral_v<K> || std::is_enum_v<K>)
			return hash_mix(static_cast<uint64_t>(key));
		else if constexpr (std::is_pointer_v<K>)
			return hash_mix(reinterpret_cast<uintptr_t>(key));
		else
			return key.hash();
	}
};

// Default equality. Uses operator== unless the key type overrides.
template<typename K>
struct KeyEqual
{
	bool operator()(const K &a, const K &b) const noexcept { return a == b; }
};

} // namespace phonometrica

#endif // PHON_CORE_HASH_HPP
