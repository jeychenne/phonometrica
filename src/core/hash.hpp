// Phonometrica engine — hashing primitives and default hashers.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// FlatHashMap needs a well-mixed 64-bit hash: the low bits pick the slot and
// bits 0..6 form the control tag, so a weak hash clusters badly. Two primitives:
//
//   * hash_mix  — MurmurHash3 fmix64 finalizer, for integer and pointer keys.
//   * hash_bytes — SipHash-1-3 over a byte span (word-at-a-time, seeded), for
//                  strings/atoms/Value keys. SipHash gives good distribution and,
//                  with a randomized seed, resistance to hash-flooding — the same
//                  choice as calao, Rust's std, and Python.
//
// The seed defaults to fixed constants so hashing is deterministic (reproducible
// runs and golden tests). An embedder that wants hash-flood resistance calls
// set_hash_seed() once at startup, before any value is hashed (String, atom, and
// Map/Set caches assume a stable seed for the process lifetime).

#ifndef PHON_CORE_HASH_HPP
#define PHON_CORE_HASH_HPP

#include "base/definitions.hpp"
#include <cstring>
#include <type_traits>

namespace phonometrica {

// 64-bit avalanche mixer (MurmurHash3 fmix64). A bijection with excellent
// diffusion; ideal for finalizing integer/pointer keys.
PHON_FORCE_INLINE uint64_t hash_mix(uint64_t x) noexcept
{
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ull;
	x ^= x >> 33;
	return x;
}

// Process-global SipHash key. Defaults to the reference test key (fixed, so runs
// are reproducible). ODR-safe inline variables.
inline uint64_t g_hash_seed0 = 0x0706050403020100ull;
inline uint64_t g_hash_seed1 = 0x0f0e0d0c0b0a0908ull;

// Override the SipHash key. Call once at startup before hashing anything.
inline void set_hash_seed(uint64_t k0, uint64_t k1) noexcept
{
	g_hash_seed0 = k0;
	g_hash_seed1 = k1;
}

namespace detail {

PHON_FORCE_INLINE uint64_t rotl64(uint64_t x, int b) noexcept
{
	return (x << b) | (x >> (64 - b));
}

#define PHON_SIPROUND()                                                                        \
	do                                                                                         \
	{                                                                                          \
		v0 += v1;                                                                              \
		v1 = ::phonometrica::detail::rotl64(v1, 13);                                           \
		v1 ^= v0;                                                                              \
		v0 = ::phonometrica::detail::rotl64(v0, 32);                                           \
		v2 += v3;                                                                              \
		v3 = ::phonometrica::detail::rotl64(v3, 16);                                           \
		v3 ^= v2;                                                                              \
		v0 += v3;                                                                              \
		v3 = ::phonometrica::detail::rotl64(v3, 21);                                           \
		v3 ^= v0;                                                                              \
		v2 += v1;                                                                              \
		v1 = ::phonometrica::detail::rotl64(v1, 17);                                           \
		v1 ^= v2;                                                                              \
		v2 = ::phonometrica::detail::rotl64(v2, 32);                                           \
	} while (0)

} // namespace detail

// SipHash-1-3 over a byte span, keyed by the process seed.
inline uint64_t hash_bytes(const void *data, intptr_t len) noexcept
{
	PHON_ASSERT(len >= 0);
	const uint64_t k0 = g_hash_seed0;
	const uint64_t k1 = g_hash_seed1;

	uint64_t v0 = 0x736f6d6570736575ull ^ k0;
	uint64_t v1 = 0x646f72616e646f6dull ^ k1;
	uint64_t v2 = 0x6c7967656e657261ull ^ k0;
	uint64_t v3 = 0x7465646279746573ull ^ k1;

	const auto *p = static_cast<const unsigned char *>(data);
	const auto n = static_cast<size_t>(len);
	const unsigned char *end = p + (n - (n & 7));

	for (; p != end; p += 8)
	{
		uint64_t m;
		std::memcpy(&m, p, 8);
		v3 ^= m;
		PHON_SIPROUND(); // c = 1 compression round
		v0 ^= m;
	}

	uint64_t b = static_cast<uint64_t>(n) << 56;
	switch (n & 7)
	{
	case 7: b |= static_cast<uint64_t>(p[6]) << 48; [[fallthrough]];
	case 6: b |= static_cast<uint64_t>(p[5]) << 40; [[fallthrough]];
	case 5: b |= static_cast<uint64_t>(p[4]) << 32; [[fallthrough]];
	case 4: b |= static_cast<uint64_t>(p[3]) << 24; [[fallthrough]];
	case 3: b |= static_cast<uint64_t>(p[2]) << 16; [[fallthrough]];
	case 2: b |= static_cast<uint64_t>(p[1]) << 8; [[fallthrough]];
	case 1: b |= static_cast<uint64_t>(p[0]); break;
	case 0: break;
	}

	v3 ^= b;
	PHON_SIPROUND();
	v0 ^= b;

	v2 ^= 0xff;
	PHON_SIPROUND(); // d = 3 finalization rounds
	PHON_SIPROUND();
	PHON_SIPROUND();

	return v0 ^ v1 ^ v2 ^ v3;
}

#undef PHON_SIPROUND

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
