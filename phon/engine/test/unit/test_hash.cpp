// Phonometrica engine — hash primitive tests (SipHash-1-3 + fmix64).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/core/hash.hpp>
#include "test_framework.hpp"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

using namespace phonometrica;

namespace {
uint64_t h(const std::string &s) { return hash_bytes(s.data(), static_cast<intptr_t>(s.size())); }
} // namespace

TEST_CASE("hash_bytes is deterministic")
{
	CHECK(h("phonetics") == h("phonetics"));
	CHECK(h("") == h(""));
	CHECK(h("a") != h("b"));
	CHECK(h("ab") != h("ba")); // order sensitive
}

TEST_CASE("hash_bytes regression vector (SipHash-1-3, default seed)")
{
	// Frozen output for the default (reference) key; guards against accidental
	// changes to the algorithm or seed. The SipHash round function is validated
	// against the canonical SipHash-2-4 vector (0xa129ca6149be45e5) separately.
	CHECK(h("The quick brown fox jumps over the lazy dog") == 0x9bd930430f05b1ceull);
}

TEST_CASE("hash_bytes is seed-sensitive")
{
	uint64_t saved0 = g_hash_seed0, saved1 = g_hash_seed1;
	uint64_t a = h("reproducible");
	set_hash_seed(0xdeadbeefcafef00dull, 0x0123456789abcdefull);
	uint64_t b = h("reproducible");
	set_hash_seed(saved0, saved1); // restore for other tests
	CHECK(a != b);
	CHECK(h("reproducible") == a); // restored
}

TEST_CASE("hash_bytes avalanche: one input bit flips ~half the output bits")
{
	// Average Hamming distance of the output over single-bit input flips should
	// be near 32 (out of 64). Use a fixed base buffer.
	std::string base = "avalanche-probe-0123456789";
	uint64_t h0 = h(base);
	long total = 0;
	int trials = 0;
	for (size_t byte = 0; byte < base.size(); ++byte)
	{
		for (int bit = 0; bit < 8; ++bit)
		{
			std::string x = base;
			x[byte] = static_cast<char>(x[byte] ^ (1 << bit));
			uint64_t hx = h(x);
			total += __builtin_popcountll(h0 ^ hx);
			++trials;
		}
	}
	double avg = static_cast<double>(total) / trials;
	CHECK(avg > 28.0);
	CHECK(avg < 36.0);
}

TEST_CASE("hash_bytes: no collisions and uniform low bits over a corpus")
{
	const int N = 100000;
	std::set<uint64_t> seen;
	int buckets[128] = {0}; // low 7 bits = the FlatHashMap control tag
	int collisions = 0;
	for (int i = 0; i < N; ++i)
	{
		std::string s = "identifier_" + std::to_string(i * 2654435761u);
		uint64_t hv = h(s);
		if (!seen.insert(hv).second)
			++collisions;
		buckets[hv & 127]++;
	}
	// A 64-bit hash over 100k distinct inputs should virtually never collide.
	CHECK(collisions == 0);
	// Low 7 bits roughly uniform: every bucket within 40% of the mean (781).
	int mean = N / 128;
	int lo = mean, hi = mean;
	for (int b : buckets)
	{
		if (b < lo)
			lo = b;
		if (b > hi)
			hi = b;
	}
	CHECK(lo > mean * 6 / 10);
	CHECK(hi < mean * 14 / 10);
}

TEST_CASE("hash_mix is a well-distributed bijection")
{
	// fmix64 is invertible, so distinct inputs give distinct outputs.
	std::set<uint64_t> seen;
	for (uint64_t i = 0; i < 100000; ++i)
		CHECK(seen.insert(hash_mix(i)).second);
	// Small sequential inputs must not stay clustered in the low bits.
	CHECK((hash_mix(0) & 127) != (hash_mix(1) & 127) || (hash_mix(1) & 127) != (hash_mix(2) & 127));
}
