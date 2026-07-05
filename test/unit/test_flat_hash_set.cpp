// Phonometrica engine — FlatHashSet tests, incl. fuzz vs std::set.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/core/flat_hash_set.hpp>
#include "test_framework.hpp"

#include <algorithm>
#include <random>
#include <set>
#include <vector>

using namespace phonometrica;

TEST_CASE("FlatHashSet basic membership")
{
	FlatHashSet<int> s;
	CHECK(s.empty());
	CHECK(s.insert(5).second);
	CHECK(!s.insert(5).second); // already present
	CHECK(s.contains(5));
	CHECK(s.size() == 1);
	CHECK(s.erase(5) == 1);
	CHECK(s.erase(5) == 0);
	CHECK(!s.contains(5));
}

TEST_CASE("FlatHashSet iteration yields keys")
{
	FlatHashSet<int> s;
	for (int i = 0; i < 200; ++i)
		s.insert(i);
	std::vector<int> seen;
	for (int k : s)
		seen.push_back(k);
	CHECK(static_cast<int>(seen.size()) == 200);
	std::sort(seen.begin(), seen.end());
	for (int i = 0; i < 200; ++i)
		CHECK(seen[i] == i);
}

TEST_CASE("FlatHashSet fuzz vs std::set")
{
	std::mt19937_64 rng(7777);
	FlatHashSet<int> mine;
	std::set<int> ref;
	const int KEY_SPACE = 400;

	for (int it = 0; it < 150000; ++it)
	{
		int key = static_cast<int>(rng() % KEY_SPACE);
		int op = static_cast<int>(rng() % 3);
		if (op == 0)
		{
			bool m = mine.insert(key).second;
			bool r = ref.insert(key).second;
			CHECK(m == r);
		}
		else if (op == 1)
		{
			auto m = mine.erase(key);
			auto r = static_cast<intptr_t>(ref.erase(key));
			CHECK(m == r);
		}
		else
		{
			CHECK(mine.contains(key) == (ref.count(key) != 0));
		}
		REQUIRE(mine.size() == static_cast<intptr_t>(ref.size()));
	}

	for (int k : ref)
		CHECK(mine.contains(k));
	int count = 0;
	for (int k : mine)
	{
		++count;
		CHECK(ref.count(k) != 0);
	}
	CHECK(count == static_cast<int>(ref.size()));
}
