// Phonometrica engine — Table tests (CoW, ordering, String keys, fuzz).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/types/table.hpp>
#include <phon/types/list.hpp>
#include <phon/types/string.hpp>
#include "test_framework.hpp"

#include <random>
#include <set>
#include <unordered_map>

using namespace phonometrica;

namespace {
Variant vi(int64_t n) { return Variant::from_int(n); }
Variant vs(const char *s) { return Variant(String(s).to_value()); }
} // namespace

TEST_CASE("Table basic set/get/contains/remove")
{
	Table m;
	CHECK(m.empty());
	m.set(vi(1), vs("one"));
	m.set(vi(2), vs("two"));
	CHECK(m.size() == 2);
	CHECK(m.contains(vi(1)));
	CHECK(!m.contains(vi(3)));
	CHECK(String::from_value(m.get(vi(1)).value()) == "one");
	CHECK(m.get(vi(3)).is_null());

	m.set(vi(1), vs("uno")); // overwrite
	CHECK(m.size() == 2);
	CHECK(String::from_value(m.get(vi(1)).value()) == "uno");

	CHECK(m.remove(vi(1)));
	CHECK(!m.remove(vi(1)));
	CHECK(m.size() == 1);
	CHECK(!m.contains(vi(1)));
}

TEST_CASE("Table keys() and values() enumerate all entries")
{
	Table m;
	int order[] = {5, 3, 9, 1, 7, 2};
	for (int k : order)
		m.set(vi(k), vi(k * 10));
	List ks = m.keys();
	List vals = m.values();
	CHECK(ks.size() == 6);
	CHECK(vals.size() == 6);
	// Every key present and mapped to key*10 (iteration order is unspecified).
	std::set<int> seen;
	for (intptr_t i = 1; i <= ks.size(); ++i)
	{
		int k = static_cast<int>(ks.get(i).as_int());
		seen.insert(k);
		CHECK(m.get(vi(k)).as_int() == k * 10);
	}
	for (int k : order)
		CHECK(seen.count(k) == 1);
	// Overwriting a key updates its value; size unchanged.
	m.set(vi(9), vi(999));
	CHECK(m.size() == 6);
	CHECK(m.get(vi(9)).as_int() == 999);
}

TEST_CASE("Table String keys use structural equality")
{
	Table m;
	m.set(vs("pitch"), vi(1));
	// A distinct String cell with the same bytes is the same key.
	CHECK(m.contains(vs("pitch")));
	CHECK(m.get(vs("pitch")).as_int() == 1);
	m.set(vs("pitch"), vi(2));
	CHECK(m.size() == 1);
	CHECK(m.get(vs("pitch")).as_int() == 2);
}

TEST_CASE("Table copy-on-write value semantics")
{
	Table a;
	a.set(vi(1), vi(10));
	Table b = a;
	CHECK(a.use_count() == 2);
	CHECK(a.cell() == b.cell());
	b.set(vi(2), vi(20)); // CoW
	CHECK(a.size() == 1);
	CHECK(b.size() == 2);
	CHECK(a.use_count() == 1);
	CHECK(!a.contains(vi(2)));
}

TEST_CASE("Table removal keeps remaining entries findable")
{
	Table m;
	for (int i = 1; i <= 6; ++i)
		m.set(vi(i), vi(i));
	m.remove(vi(3));
	m.remove(vi(1));
	CHECK(m.size() == 4);
	CHECK(!m.contains(vi(3)));
	CHECK(!m.contains(vi(1)));
	for (int k : {2, 4, 5, 6})
		CHECK(m.get(vi(k)).as_int() == k);
}

TEST_CASE("Table structural equality is order-independent")
{
	Table a;
	a.set(vi(1), vi(1));
	a.set(vi(2), vi(2));
	Table b;
	b.set(vi(2), vi(2));
	b.set(vi(1), vi(1));
	CHECK(a == b); // same entries, different insertion order
	b.set(vi(3), vi(3));
	CHECK(a != b);
}

TEST_CASE("Table clear releases values")
{
	Table m;
	String s("v");
	m.set(vi(1), Variant(s.to_value()));
	CHECK(s.use_count() == 2);
	m.clear();
	CHECK(m.empty());
	CHECK(s.use_count() == 1);
}

TEST_CASE("Table fuzz vs std::unordered_map")
{
	std::mt19937_64 rng(31337);
	Table mine;
	std::unordered_map<int64_t, int64_t> ref;
	const int KEY_SPACE = 300;

	for (int it = 0; it < 100000; ++it)
	{
		int64_t key = static_cast<int64_t>(rng() % KEY_SPACE);
		int op = static_cast<int>(rng() % 3);
		if (op == 0)
		{
			int64_t val = static_cast<int64_t>(rng() % 1000000);
			mine.set(vi(key), vi(val));
			ref[key] = val;
		}
		else if (op == 1)
		{
			bool m = mine.remove(vi(key));
			bool r = ref.erase(key) != 0;
			CHECK(m == r);
		}
		else
		{
			Variant got = mine.get(vi(key));
			auto rit = ref.find(key);
			if (rit == ref.end())
				CHECK(got.is_null());
			else
				CHECK(got.as_int() == rit->second);
		}
		REQUIRE(mine.size() == static_cast<intptr_t>(ref.size()));
	}
	for (const auto &kv : ref)
		CHECK(mine.get(vi(kv.first)).as_int() == kv.second);
}
