// Phonometrica engine — SmallVector<T, N> tests, incl. fuzz vs std::vector.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/core/small_vector.hpp>
#include "test_framework.hpp"

#include <random>
#include <vector>

using namespace phonometrica;

namespace {
int g_live = 0;

struct Tracked
{
	int value;
	int *sentinel;

	explicit Tracked(int v = 0) : value(v), sentinel(new int(v)) { ++g_live; }
	Tracked(const Tracked &o) : value(o.value), sentinel(new int(o.value)) { ++g_live; }
	Tracked(Tracked &&o) noexcept : value(o.value), sentinel(o.sentinel)
	{
		o.sentinel = nullptr;
		++g_live;
	}
	Tracked &operator=(const Tracked &o)
	{
		value = o.value;
		return *this;
	}
	Tracked &operator=(Tracked &&o) noexcept
	{
		value = o.value;
		std::swap(sentinel, o.sentinel);
		return *this;
	}
	~Tracked()
	{
		delete sentinel;
		--g_live;
	}
};
} // namespace

TEST_CASE("SmallVector stays inline below N then spills")
{
	SmallVector<int, 4> v;
	CHECK(v.is_inline());
	CHECK(v.capacity() == 4);
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.push_back(4);
	CHECK(v.is_inline()); // exactly N still inline
	v.push_back(5);
	CHECK(!v.is_inline()); // spilled to heap
	CHECK(v.size() == 5);
	for (int i = 0; i < 5; ++i)
		CHECK(v[i] == i + 1);
}

TEST_CASE("SmallVector move steals heap buffer, copies inline")
{
	SmallVector<int, 2> heapish;
	for (int i = 0; i < 10; ++i)
		heapish.push_back(i);
	CHECK(!heapish.is_inline());
	int *buf = heapish.data();
	SmallVector<int, 2> moved = std::move(heapish);
	CHECK(moved.data() == buf); // stole the heap buffer
	CHECK(moved.size() == 10);
	CHECK(heapish.is_inline()); // source reset to inline

	SmallVector<int, 8> tiny;
	tiny.push_back(7);
	tiny.push_back(8);
	SmallVector<int, 8> moved2 = std::move(tiny);
	CHECK(moved2.size() == 2);
	CHECK(moved2[0] == 7);
	CHECK(moved2.is_inline());
}

TEST_CASE("SmallVector destroys elements across inline/heap boundary")
{
	g_live = 0;
	{
		SmallVector<Tracked, 3> v;
		for (int i = 0; i < 20; ++i)
			v.emplace_back(i); // crosses the inline->heap spill
		CHECK(g_live == 20);
		SmallVector<Tracked, 3> copy = v;
		CHECK(g_live == 40);
		v.clear();
		CHECK(g_live == 20);
	}
	CHECK(g_live == 0);
}

TEST_CASE("SmallVector fuzz vs std::vector")
{
	std::mt19937_64 rng(999);
	SmallVector<int, 4> mine;
	std::vector<int> ref;

	auto check_equal = [&]() -> bool {
		if (mine.size() != static_cast<intptr_t>(ref.size()))
			return false;
		for (intptr_t i = 0; i < mine.size(); ++i)
			if (mine[i] != ref[i])
				return false;
		return true;
	};

	for (int it = 0; it < 20000; ++it)
	{
		int op = static_cast<int>(rng() % 5);
		switch (op)
		{
		case 0:
		case 1:
		{
			int x = static_cast<int>(rng());
			mine.push_back(x);
			ref.push_back(x);
			break;
		}
		case 2:
			if (!ref.empty())
			{
				mine.pop_back();
				ref.pop_back();
			}
			break;
		case 3:
			if (!ref.empty())
			{
				auto pos = static_cast<intptr_t>(rng() % ref.size());
				mine.erase(pos);
				ref.erase(ref.begin() + pos);
			}
			break;
		case 4:
		{
			auto n = static_cast<intptr_t>(rng() % 32);
			mine.resize(n);
			ref.resize(n);
			break;
		}
		}
		REQUIRE(check_equal());
	}
}
