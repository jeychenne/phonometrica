// Phonometrica engine — Array tests (view/buffer, column-major, CoW).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/types/array.hpp>
#include <phon/engine/object/class.hpp>
#include "test_framework.hpp"

using namespace phonometrica;

namespace {

// Column-major linear index of element (r, c) in an nrow×ncol contiguous array.
intptr_t cm2(intptr_t r, intptr_t c, intptr_t nrow) { return r + c * nrow; }

} // namespace

TEST_CASE("Array construction: shape, zero-fill, contiguity")
{
	Array a = Array::make_2d(2, 3);
	CHECK(a.rank() == 2);
	CHECK(a.dim(0) == 2);
	CHECK(a.dim(1) == 3);
	CHECK(a.size() == 6);
	CHECK(a.is_contiguous());
	CHECK(a.offset() == 0);
	// Column-major contiguous strides: [1, nrow].
	CHECK(a.stride(0) == 1);
	CHECK(a.stride(1) == 2);
	// Zero-filled.
	for (intptr_t i = 0; i < a.size(); ++i)
		CHECK(a.data()[i] == 0.0);
}

TEST_CASE("Array element write/read (column-major)")
{
	Array a = Array::make_2d(2, 3);
	double *d = a.detach(); // unique+contiguous: in-place base
	for (intptr_t r = 0; r < 2; ++r)
		for (intptr_t c = 0; c < 3; ++c)
			d[cm2(r, c, 2)] = static_cast<double>(10 * r + c);

	// get() via the 0-based multi-index and the view's strides.
	intptr_t idx[2];
	idx[0] = 1;
	idx[1] = 2;
	CHECK(a.get(idx) == 12.0);
	idx[0] = 0;
	idx[1] = 0;
	CHECK(a.get(idx) == 0.0);
	idx[0] = 1;
	idx[1] = 0;
	CHECK(a.get(idx) == 10.0);
}

TEST_CASE("Array detach on a unique contiguous array mutates in place")
{
	Array a = Array::make_1d(4);
	double *d1 = a.detach();
	double *d2 = a.detach(); // still unique+contiguous
	CHECK(d1 == d2);         // no clone
	CHECK(a.cell()->buf->header.is_unique());
}

TEST_CASE("Array CoW: a shared view detaches on mutation")
{
	Array a = Array::make_1d(3);
	{
		double *d = a.detach();
		d[0] = 1.0;
		d[1] = 2.0;
		d[2] = 3.0;
	}

	Array b = a; // shares the same view cell (refcount 2)
	CHECK(!a.unique());
	CHECK(!b.unique());
	CHECK(a.cell() == b.cell()); // same view before any mutation

	double *db = b.detach(); // b is shared -> clones view + buffer
	CHECK(a.cell() != b.cell());
	CHECK(a.unique()); // a is now sole owner of the original again
	db[0] = 99.0;      // mutate b's private copy

	// a is untouched by b's mutation.
	intptr_t i0 = 0;
	CHECK(a.get(&i0) == 1.0);
	i0 = 0;
	CHECK(b.get(&i0) == 99.0);
}

TEST_CASE("Array equality: shape + elements")
{
	Array a = Array::make_1d(3);
	Array b = Array::make_1d(3);
	Array c = Array::make_1d(2);
	{
		double *da = a.detach();
		double *db = b.detach();
		da[0] = db[0] = 1.0;
		da[1] = db[1] = 2.0;
		da[2] = db[2] = 3.0;
	}
	Class *ka = get_class(CID_ARRAY);
	auto as_cell = [](Array &x) { return reinterpret_cast<const Cell *>(x.cell()); };
	CHECK(ka->equals(as_cell(a), as_cell(b)));  // same shape + elements
	CHECK(!ka->equals(as_cell(a), as_cell(c))); // different shape
	double *db2 = b.detach();
	db2[1] = 99.0;
	CHECK(!ka->equals(as_cell(a), as_cell(b))); // different element
}
