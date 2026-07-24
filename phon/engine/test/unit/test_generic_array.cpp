// Phonometrica engine — generic Array<T> tests (CoW, growth, matrix, queries).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Array<T> (core/array.hpp) is the application-facing generic container adopted
// from Phonometrica's phon/base/array.hpp: 0-based, copy-on-write, npos == -1.

#include <phon/engine/core/array.hpp>
#include <phon/engine/types/string.hpp>
#include "test_framework.hpp"

#include <complex>
#include <memory>
#include <stdexcept>

using namespace phonometrica;

TEST_CASE("Array<T>: construction and 0-based access")
{
	Array<double> e;
	CHECK(e.empty());
	CHECK(e.size() == 0);
	CHECK(e.ndim() == 1);

	Array<double> filled(3, 2.5);
	CHECK(filled.size() == 3);
	CHECK(filled[0] == 2.5);
	CHECK(filled[2] == 2.5);

	Array<int> init{1, 2, 3, 4};
	CHECK(init.size() == 4);
	CHECK(init.first() == 1);
	CHECK(init.last() == 4);

	const int raw[3] = {7, 8, 9};
	Array<int> from_range(raw, 3);
	CHECK(from_range[1] == 8);

	Array<std::complex<double>> c;
	c.append(std::complex<double>(1.0, -1.0));
	CHECK(c.size() == 1);
	CHECK(c[0] == std::complex<double>(1.0, -1.0));
	// Brace arguments select the initializer-list overload: two complex elements.
	c.append({2.0, 3.0});
	CHECK(c.size() == 3);
	CHECK(c[2] == std::complex<double>(3.0, 0.0));
}

TEST_CASE("Array<T>: copy shares the buffer, mutation detaches (CoW)")
{
	Array<String> a{String("one"), String("two")};
	Array<String> b = a;
	CHECK(a.use_count() == 2);
	CHECK(a.shared());
	CHECK(a.raw_data() == b.raw_data()); // same buffer

	b.append(String("three"));
	CHECK(a.size() == 2);
	CHECK(b.size() == 3);
	CHECK(a.unique());
	CHECK(b.unique());
	CHECK(a.raw_data() != b.raw_data());
	CHECK(b[2] == "three");

	// Reads through a const handle never detach.
	Array<String> c = a;
	CHECK(std::as_const(c)[0] == "one");
	CHECK(std::as_const(c).begin() == std::as_const(a).begin());
	CHECK(a.use_count() == 2);

	// A non-const accessor detaches even without a write.
	(void) c[0];
	CHECK(a.unique());
	CHECK(c.unique());
}

TEST_CASE("Array<T>: growth, insert, prepend, remove")
{
	Array<int> a;
	for (int i = 0; i < 100; ++i)
		a.append(i);
	CHECK(a.size() == 100);
	CHECK(a[99] == 99);

	a.insert(intptr_t(0), -1);
	CHECK(a.first() == -1);
	a.prepend(-2);
	CHECK(a.first() == -2);
	CHECK(a.size() == 102);

	a.remove_at(intptr_t(0));
	a.remove_at(intptr_t(0));
	CHECK(a.first() == 0);
	CHECK(a.size() == 100);

	a.remove(intptr_t(10), 80); // range removal
	CHECK(a.size() == 20);
	CHECK(a[9] == 9);
	CHECK(a[10] == 90);

	// insert(pos == size()) appends.
	a.insert(a.size(), 1234);
	CHECK(a.last() == 1234);

	// insert of a range.
	Array<int> b{500, 501};
	a.insert(intptr_t(1), b);
	CHECK(a[1] == 500);
	CHECK(a[2] == 501);
	CHECK(a[3] == 1);

	// Self-insertion is safe (the source is copied first).
	Array<int> s{1, 2};
	s.insert(intptr_t(1), s);
	CHECK(s.size() == 4);
	CHECK(s[0] == 1);
	CHECK(s[1] == 1);
	CHECK(s[2] == 2);
	CHECK(s[3] == 2);

	// Self-append after growth stays valid.
	Array<int> ap{1, 2, 3};
	ap.append(ap);
	CHECK(ap.size() == 6);
	CHECK(ap[3] == 1);
	CHECK(ap[5] == 3);
}

TEST_CASE("Array<T>: find/rfind return npos when absent, match at boundaries")
{
	Array<int> a{5, 3, 7, 3, 9};
	CHECK(a.find(3) == 1);
	CHECK(a.find(3, 2) == 3);
	CHECK(a.find(42) == Array<int>::npos);
	CHECK(a.rfind(3) == 3);
	CHECK(a.rfind(3, 2) == 1);
	CHECK(a.rfind(42) == Array<int>::npos);
	// A match in the last slot is found (the old engine's rfind bug).
	CHECK(a.rfind(9) == 4);
	CHECK(a.find(5) == 0);
	CHECK(a.contains(7));
	CHECK(!a.contains(-7));
	CHECK(a.starts_with(5));
	CHECK(a.ends_with(9));

	Array<int> empty;
	CHECK(empty.find(1) == Array<int>::npos);
	CHECK(empty.rfind(1) == Array<int>::npos);
}

TEST_CASE("Array<T>: take/pop/drop/clear/resize")
{
	Array<String> a{String("a"), String("b"), String("c"), String("d")};
	CHECK(a.take_first() == "a");
	CHECK(a.take_last() == "d");
	CHECK(a.take_at(0) == "b");
	CHECK(a.size() == 1);

	a.append(String("x"));
	a.append(String("y"));
	a.pop_first();
	a.pop_last();
	CHECK(a.size() == 1);
	CHECK(a[0] == "x");

	a.append(String("z"));
	a.drop(1);
	CHECK(a.size() == 1);

	// clear() on a shared buffer leaves the alias untouched.
	Array<String> b{String("k"), String("l")};
	Array<String> alias = b;
	b.clear();
	CHECK(b.empty());
	CHECK(alias.size() == 2);
	CHECK(alias[0] == "k");

	Array<int> r;
	r.resize(5);
	CHECK(r.size() == 5);
	CHECK(r[4] == 0); // value-initialized
	r.resize(2);
	CHECK(r.size() == 2);
}

TEST_CASE("Array<T>: remove by value")
{
	Array<int> a{1, 2, 1, 3, 1};
	a.remove_first(1);
	CHECK(a == (Array<int>{2, 1, 3, 1}));
	a.remove_last(1);
	CHECK(a == (Array<int>{2, 1, 3}));
	a.append(1);
	a.remove(1); // all occurrences
	CHECK(a == (Array<int>{2, 3}));
}

TEST_CASE("Array<T>: matrix (2-D, column-major)")
{
	// Matrix ctor: pass intptr_t extents (with an int literal and T=double the
	// (count, fill-value) and (nrow, ncol) overloads would be ambiguous, as in the
	// old header).
	Array<double> m(intptr_t(2), intptr_t(3)); // nrow, ncol; zero-filled
	CHECK(m.ndim() == 2);
	CHECK(m.nrow() == 2);
	CHECK(m.ncol() == 3);
	CHECK(m.size() == 6);
	CHECK(m(1, 2) == 0.0);

	m(0, 0) = 1.0;
	m(1, 2) = 6.0;
	CHECK(m.at(0, 0) == 1.0);
	CHECK(m.at(1, 2) == 6.0);
	// Column-major layout: (row, col) flattens to col*nrow + row.
	CHECK(m.raw_data()[2 * 2 + 1] == 6.0);

	// Matrices are CoW too.
	Array<double> alias = m;
	m(0, 1) = 3.0;
	CHECK(alias(0, 1) == 0.0);
	CHECK(m(0, 1) == 3.0);

	Array<double> filled(intptr_t(2), intptr_t(2), 7.0);
	CHECK(filled(1, 1) == 7.0);

	bool threw = false;
	try
	{
		m.check_dim(filled);
	}
	catch (std::runtime_error &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("Array<T>: bounds-checked access throws; operator[] asserts only")
{
	Array<int> a{1, 2, 3};
	bool threw = false;
	try
	{
		(void) a.at(3);
	}
	catch (std::runtime_error &)
	{
		threw = true;
	}
	CHECK(threw);

	threw = false;
	try
	{
		a.insert(intptr_t(5), 0);
	}
	catch (std::runtime_error &)
	{
		threw = true;
	}
	CHECK(threw);
}

TEST_CASE("Array<T>: struct elements and move-only elements")
{
	struct Point
	{
		double x = 0, y = 0;
		bool operator==(const Point &o) const { return x == o.x && y == o.y; }
	};
	Array<Point> pts;
	pts.append(Point{1, 2});
	pts.emplace_back(3.0, 4.0);
	CHECK(pts.size() == 2);
	CHECK(pts[1].y == 4.0);
	Array<Point> copy = pts;
	pts[0].x = 9;
	CHECK(copy[0].x == 1.0);

	// A move-only element type: single owner, never shared.
	Array<std::unique_ptr<int>> owners;
	owners.append(std::make_unique<int>(42));
	owners.emplace_back(new int(7));
	CHECK(owners.size() == 2);
	CHECK(*owners[0] == 42);
	Array<std::unique_ptr<int>> stolen = std::move(owners);
	CHECK(stolen.size() == 2);
	CHECK(*stolen[1] == 7);
	auto taken = stolen.take_last();
	CHECK(*taken == 7);
	CHECK(stolen.size() == 1);
}

TEST_CASE("Array<T>: equality, iteration, span conversion")
{
	Array<int> a{1, 2, 3};
	Array<int> b = a;
	Array<int> c{1, 2, 3};
	Array<int> d{1, 2};
	CHECK(a == b); // shared buffer short-circuit
	CHECK(a == c);
	CHECK(a != d);

	int sum = 0;
	for (int x : std::as_const(a))
		sum += x;
	CHECK(sum == 6);

	std::span<const int> sp = std::as_const(a);
	CHECK(sp.size() == 3);
	CHECK(sp[2] == 3);

	Array<int> rev{1, 2, 3};
	int last = 0;
	for (auto it = std::as_const(rev).rbegin(); it != std::as_const(rev).rend(); ++it)
		last = last * 10 + *it;
	CHECK(last == 321);
}

TEST_CASE("Array<T>: reserve/capacity growth schedule")
{
	Array<int> a;
	a.reserve(10);
	CHECK(a.capacity() >= 10);
	CHECK(a.empty());
	auto cap = a.capacity();
	for (int i = 0; i < 10; ++i)
		a.append(i);
	CHECK(a.capacity() == cap); // no regrow within the reservation
	CHECK(a[9] == 9);
}
