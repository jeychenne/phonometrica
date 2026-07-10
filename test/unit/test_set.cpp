// Phonometrica engine — Set tests (CoW, membership, fuzz).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/types/set.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/string.hpp>
#include "test_framework.hpp"

#include <random>
#include <set>

using namespace phonometrica;

namespace {
Variant vi(int64_t n) { return Variant::from_int(n); }
Variant vs(const char *s) { return Variant(String(s).to_value()); }
} // namespace

TEST_CASE("Set basic add/contains/remove")
{
	Set s;
	CHECK(s.empty());
	CHECK(s.add(vi(1)));
	CHECK(!s.add(vi(1))); // already present
	CHECK(s.add(vi(2)));
	CHECK(s.size() == 2);
	CHECK(s.contains(vi(1)));
	CHECK(!s.contains(vi(3)));
	CHECK(s.remove(vi(1)));
	CHECK(!s.remove(vi(1)));
	CHECK(!s.contains(vi(1)));
}

TEST_CASE("Set uses structural equality for String members")
{
	Set s;
	s.add(vs("alpha"));
	CHECK(s.contains(vs("alpha"))); // distinct cell, equal bytes
	CHECK(!s.add(vs("alpha")));      // dedup
	CHECK(s.size() == 1);
}

TEST_CASE("Set to_list enumerates all members")
{
	Set s;
	int order[] = {7, 2, 9, 4};
	for (int k : order)
		s.add(vi(k));
	List l = s.to_list();
	CHECK(l.size() == 4);
	std::set<int> seen;
	for (intptr_t i = 1; i <= l.size(); ++i)
		seen.insert(static_cast<int>(l.get(i).as_int()));
	for (int k : order)
		CHECK(seen.count(k) == 1);
}

TEST_CASE("Set copy-on-write")
{
	Set a;
	a.add(vi(1));
	Set b = a;
	CHECK(a.use_count() == 2);
	b.add(vi(2));
	CHECK(a.size() == 1);
	CHECK(b.size() == 2);
	CHECK(!a.contains(vi(2)));
}

TEST_CASE("Set equality is order-independent")
{
	Set a;
	a.add(vi(1));
	a.add(vi(2));
	Set b;
	b.add(vi(2));
	b.add(vi(1));
	CHECK(a == b);
	b.add(vi(3));
	CHECK(a != b);
}

TEST_CASE("Set fuzz vs std::set")
{
	std::mt19937_64 rng(24680);
	Set mine;
	std::set<int64_t> ref;
	const int KEY_SPACE = 250;

	for (int it = 0; it < 100000; ++it)
	{
		int64_t key = static_cast<int64_t>(rng() % KEY_SPACE);
		int op = static_cast<int>(rng() % 3);
		if (op == 0)
		{
			bool m = mine.add(vi(key));
			bool r = ref.insert(key).second;
			CHECK(m == r);
		}
		else if (op == 1)
		{
			bool m = mine.remove(vi(key));
			bool r = ref.erase(key) != 0;
			CHECK(m == r);
		}
		else
		{
			CHECK(mine.contains(vi(key)) == (ref.count(key) != 0));
		}
		REQUIRE(mine.size() == static_cast<intptr_t>(ref.size()));
	}
	for (int64_t k : ref)
		CHECK(mine.contains(vi(k)));
}
