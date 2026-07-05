// Phonometrica engine — Vector<T> tests, incl. fuzz vs std::vector.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "core/vector.hpp"
#include "test_framework.hpp"

#include <random>
#include <vector>

using namespace phonometrica;

// A non-trivially-copyable, non-trivially-relocatable element that tracks live
// instances, so we can assert constructor/destructor balance.
namespace {
int g_live = 0;

struct Tracked
{
	int value;
	int *sentinel;

	Tracked() : value(0), sentinel(new int(0xABCD)) { ++g_live; }
	explicit Tracked(int v) : value(v), sentinel(new int(0xABCD)) { ++g_live; }
	Tracked(const Tracked &o) : value(o.value), sentinel(new int(0xABCD)) { ++g_live; }
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

TEST_CASE("Vector basic push/pop/index")
{
	Vector<int> v;
	CHECK(v.empty());
	for (int i = 0; i < 10; ++i)
		v.push_back(i * i);
	CHECK(v.size() == 10);
	CHECK(v.capacity() >= 10);
	CHECK(v[0] == 0);
	CHECK(v[9] == 81);
	CHECK(v.front() == 0);
	CHECK(v.back() == 81);
	v.pop_back();
	CHECK(v.size() == 9);
	CHECK(v.back() == 64);
}

TEST_CASE("Vector growth starts at 8 then 1.5x")
{
	Vector<int> v;
	v.push_back(1);
	CHECK(v.capacity() == Vector<int>::INITIAL_CAPACITY);
	for (int i = 0; i < 8; ++i)
		v.push_back(i);
	CHECK(v.capacity() == 12); // 8 + 8/2
}

TEST_CASE("Vector insert/erase stable ordering")
{
	Vector<int> v{0, 1, 2, 3, 4};
	v.insert(0, 99);
	CHECK(v[0] == 99);
	CHECK(v[1] == 0);
	CHECK(v.size() == 6);
	v.insert(v.size(), 77);
	CHECK(v.back() == 77);
	v.erase(0);
	CHECK(v[0] == 0);
	v.erase(v.size() - 1);
	CHECK(v.back() == 4);
}

TEST_CASE("Vector resize and clear")
{
	Vector<int> v;
	v.resize(5);
	CHECK(v.size() == 5);
	for (int i = 0; i < 5; ++i)
		CHECK(v[i] == 0);
	v.resize(2);
	CHECK(v.size() == 2);
	v.clear();
	CHECK(v.empty());
}

TEST_CASE("Vector copy and move semantics")
{
	Vector<int> a{1, 2, 3};
	Vector<int> b = a; // copy
	CHECK(b.size() == 3);
	b[0] = 99;
	CHECK(a[0] == 1); // deep copy
	Vector<int> c = std::move(a);
	CHECK(c.size() == 3);
	CHECK(a.empty()); // moved-from
}

TEST_CASE("Vector destroys elements (no leaks with Tracked)")
{
	g_live = 0;
	{
		Vector<Tracked> v;
		for (int i = 0; i < 50; ++i)
			v.emplace_back(i); // forces reallocation via move
		CHECK(g_live == 50);
		v.erase(10);
		CHECK(g_live == 49);
		Vector<Tracked> w = v; // copy
		CHECK(g_live == 98);
	}
	CHECK(g_live == 0);
}

TEST_CASE("Vector fuzz vs std::vector")
{
	std::mt19937_64 rng(12345);
	Vector<int> mine;
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
		int op = static_cast<int>(rng() % 6);
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
		{
			int x = static_cast<int>(rng());
			auto pos = static_cast<intptr_t>(rng() % (ref.size() + 1));
			mine.insert(pos, x);
			ref.insert(ref.begin() + pos, x);
			break;
		}
		case 4:
			if (!ref.empty())
			{
				auto pos = static_cast<intptr_t>(rng() % ref.size());
				mine.erase(pos);
				ref.erase(ref.begin() + pos);
			}
			break;
		case 5:
		{
			auto n = static_cast<intptr_t>(rng() % 64);
			mine.resize(n);
			ref.resize(n);
			break;
		}
		}
		REQUIRE(check_equal());
	}
}
