// Phonometrica engine — FlatHashMap tests, incl. fuzz vs std::unordered_map.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/core/flat_hash_map.hpp>
#include "test_framework.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

using namespace phonometrica;

TEST_CASE("FlatHashMap basic insert/find/erase")
{
	FlatHashMap<int, int> m;
	CHECK(m.empty());
	CHECK(!m.contains(1));

	auto r = m.insert(1, 100);
	CHECK(r.second);
	CHECK(m.size() == 1);
	CHECK(m.contains(1));

	// Duplicate insert does not overwrite.
	auto r2 = m.insert(1, 999);
	CHECK(!r2.second);
	CHECK(m.find(1)->second == 100);

	// operator[] inserts a default then we assign.
	m[2] = 200;
	CHECK(m.size() == 2);
	CHECK(m[2] == 200);

	// insert_or_assign overwrites.
	m.insert_or_assign(1, 111);
	CHECK(m.find(1)->second == 111);

	CHECK(m.erase(1) == 1);
	CHECK(m.erase(1) == 0);
	CHECK(!m.contains(1));
	CHECK(m.size() == 1);
}

TEST_CASE("FlatHashMap grows and keeps all keys findable")
{
	FlatHashMap<int64_t, int64_t> m;
	const int N = 5000;
	for (int i = 0; i < N; ++i)
		m.insert(i, i * 3);
	CHECK(m.size() == N);
	for (int i = 0; i < N; ++i)
	{
		auto it = m.find(i);
		REQUIRE(it != m.end());
		CHECK(it->second == i * 3);
	}
	CHECK(!m.contains(N));
	CHECK(!m.contains(-1));
}

TEST_CASE("FlatHashMap reserve avoids rehash churn")
{
	FlatHashMap<int, int> m;
	m.reserve(1000);
	intptr_t cap = m.capacity();
	CHECK(cap >= 1000);
	for (int i = 0; i < 800; ++i)
		m.insert(i, i);
	CHECK(m.capacity() == cap); // no rehash occurred
}

TEST_CASE("FlatHashMap iteration visits every entry once")
{
	FlatHashMap<int, int> m;
	for (int i = 0; i < 300; ++i)
		m.insert(i, i * 2);
	std::vector<int> seen;
	for (const auto &e : m)
	{
		CHECK(e.second == e.first * 2);
		seen.push_back(e.first);
	}
	CHECK(static_cast<int>(seen.size()) == 300);
	std::sort(seen.begin(), seen.end());
	for (int i = 0; i < 300; ++i)
		CHECK(seen[i] == i);
}

// A deliberately terrible hash to force heavy collisions and long probe chains,
// stressing group probing and tombstone reuse.
namespace {
struct BadHash
{
	uint64_t operator()(int k) const noexcept { return static_cast<uint64_t>(k % 8); }
};
} // namespace

TEST_CASE("FlatHashMap survives pathological collisions")
{
	FlatHashMap<int, int, BadHash> m;
	for (int i = 0; i < 2000; ++i)
		m.insert(i, i);
	CHECK(m.size() == 2000);
	for (int i = 0; i < 2000; ++i)
	{
		auto it = m.find(i);
		REQUIRE(it != m.end());
		CHECK(it->second == i);
	}
}

TEST_CASE("FlatHashMap tombstone reuse under churn")
{
	FlatHashMap<int, int, BadHash> m; // bad hash maximizes tombstone pressure
	for (int i = 0; i < 100; ++i)
		m.insert(i, i);
	// Repeatedly erase and reinsert the same keys many times: exercises tombstone
	// creation and reuse without unbounded capacity growth.
	intptr_t cap_before = m.capacity();
	for (int round = 0; round < 500; ++round)
	{
		for (int i = 0; i < 100; ++i)
			CHECK(m.erase(i) == 1);
		CHECK(m.empty());
		for (int i = 0; i < 100; ++i)
			m.insert(i, i + round);
		CHECK(m.size() == 100);
	}
	// Capacity must stay bounded despite 50k erase/insert cycles.
	CHECK(m.capacity() <= cap_before * 4);
	for (int i = 0; i < 100; ++i)
		CHECK(m.find(i)->second == i + 499);
}

TEST_CASE("FlatHashMap copy and move")
{
	FlatHashMap<int, int> a;
	for (int i = 0; i < 50; ++i)
		a.insert(i, i);
	FlatHashMap<int, int> b = a; // copy
	b.insert(999, 1);
	CHECK(a.size() == 50);
	CHECK(b.size() == 51);
	CHECK(!a.contains(999));

	FlatHashMap<int, int> c = std::move(a);
	CHECK(c.size() == 50);
	CHECK(a.size() == 0);
	CHECK(c.contains(25));
}

TEST_CASE("FlatHashMap fuzz vs std::unordered_map")
{
	std::mt19937_64 rng(2024);
	FlatHashMap<int64_t, int64_t> mine;
	std::unordered_map<int64_t, int64_t> ref;

	// Small key space => frequent collisions, updates, and erases.
	const int KEY_SPACE = 500;

	for (int it = 0; it < 200000; ++it)
	{
		int64_t key = static_cast<int64_t>(rng() % KEY_SPACE);
		int op = static_cast<int>(rng() % 4);
		switch (op)
		{
		case 0:
		{
			int64_t val = static_cast<int64_t>(rng());
			auto mr = mine.insert(key, val);
			auto rr = ref.insert({key, val});
			CHECK(mr.second == rr.second);
			break;
		}
		case 1:
		{
			int64_t val = static_cast<int64_t>(rng());
			mine.insert_or_assign(key, val);
			ref[key] = val;
			break;
		}
		case 2:
		{
			auto mc = mine.erase(key);
			auto rc = static_cast<intptr_t>(ref.erase(key));
			CHECK(mc == rc);
			break;
		}
		case 3:
		{
			auto it2 = mine.find(key);
			auto rit = ref.find(key);
			if (rit == ref.end())
			{
				CHECK(it2 == mine.end());
			}
			else
			{
				REQUIRE(it2 != mine.end());
				CHECK(it2->second == rit->second);
			}
			break;
		}
		}
		REQUIRE(mine.size() == static_cast<intptr_t>(ref.size()));
	}

	// Full cross-check of contents.
	for (const auto &kv : ref)
	{
		auto it = mine.find(kv.first);
		REQUIRE(it != mine.end());
		CHECK(it->second == kv.second);
	}
	int64_t count = 0;
	for (const auto &e : mine)
	{
		++count;
		CHECK(ref.at(e.first) == e.second);
	}
	CHECK(count == static_cast<int64_t>(ref.size()));
}
