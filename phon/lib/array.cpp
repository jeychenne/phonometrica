// Phonometrica engine — numeric Array standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Constructors (zeros/ones), reductions (sum/mean/min/max), shape queries, in-place
// clear, and elementwise math over arrays. The elementwise functions overload the
// same generics as the scalar math module (`sqrt`, `sin`, …): a Number argument takes
// the scalar method, an Array argument this one — type-based dispatch selecting the
// right kernel, exactly as the old engine's math_array_func overloads did. Indexing
// and slicing are language-level (opcodes), so they are not library functions.

#include <phon/lib/lib.hpp>
#include <phon/runtime/native_traits.hpp>
#include <phon/types/array.hpp>
#include <phon/vm/isolate.hpp> // Isolate::raise

#include <cmath>

namespace phonometrica {

namespace {

// Contiguous element origin of a view: after contiguous(), elements are flat at
// data()+offset() over size() entries.
const double *origin(const Array &a) { return a.data() + a.offset(); }

Array filled_1d(intptr_t n, double val)
{
	Array a = Array::make_1d(n);
	if (val != 0.0)
	{
		double *d = a.detach();
		for (intptr_t i = 0; i < n; ++i)
			d[i] = val;
	}
	return a;
}

Array filled_2d(intptr_t nrow, intptr_t ncol, double val)
{
	Array a = Array::make_2d(nrow, ncol);
	if (val != 0.0)
	{
		double *d = a.detach();
		intptr_t n = a.size();
		for (intptr_t i = 0; i < n; ++i)
			d[i] = val;
	}
	return a;
}

double reduce_sum(const Array &a)
{
	Array c = a.contiguous();
	const double *d = origin(c);
	intptr_t n = c.size();
	double s = 0.0;
	for (intptr_t i = 0; i < n; ++i)
		s += d[i];
	return s;
}

// Apply `f` elementwise, returning a fresh contiguous array of the same shape.
Array map(const Array &a, double (*f)(double))
{
	Array src = a.contiguous();
	int rank = src.rank();
	intptr_t dims[PHON_MAX_RANK];
	for (int k = 0; k < rank; ++k)
		dims[k] = src.dim(k);
	Array out = Array::make(rank, dims);
	const double *s = origin(src);
	double *d = out.detach();
	intptr_t n = out.size();
	for (intptr_t i = 0; i < n; ++i)
		d[i] = f(s[i]);
	return out;
}

} // namespace

void register_array_lib()
{
	// --- constructors ---
	register_function("zeros", [](Isolate &iso, int64_t n) {
		if (n < 0)
			iso.raise(String("[Value error] array size must be non-negative"), 0);
		return filled_1d(n, 0.0);
	});
	register_function("zeros", [](Isolate &iso, int64_t nrow, int64_t ncol) {
		if (nrow < 0 || ncol < 0)
			iso.raise(String("[Value error] array size must be non-negative"), 0);
		return filled_2d(nrow, ncol, 0.0);
	});
	register_function("ones", [](Isolate &iso, int64_t n) {
		if (n < 0)
			iso.raise(String("[Value error] array size must be non-negative"), 0);
		return filled_1d(n, 1.0);
	});
	register_function("ones", [](Isolate &iso, int64_t nrow, int64_t ncol) {
		if (nrow < 0 || ncol < 0)
			iso.raise(String("[Value error] array size must be non-negative"), 0);
		return filled_2d(nrow, ncol, 1.0);
	});

	// --- shape ---
	register_function("nrow", [](const Array &a) { return a.dim(0); });
	register_function("ncol", [](const Array &a) { return a.rank() >= 2 ? a.dim(1) : intptr_t(1); });

	// --- reductions ---
	register_function("sum", [](const Array &a) { return reduce_sum(a); });
	register_function("mean", [](Isolate &iso, const Array &a) {
		intptr_t n = a.size();
		if (n == 0)
			iso.raise(String("[Value error] 'mean' of an empty Array"), 0);
		return reduce_sum(a) / static_cast<double>(n);
	});
	register_function("min", [](Isolate &iso, const Array &a) {
		Array c = a.contiguous();
		intptr_t n = c.size();
		if (n == 0)
			iso.raise(String("[Value error] 'min' of an empty Array"), 0);
		const double *d = origin(c);
		double m = d[0];
		for (intptr_t i = 1; i < n; ++i)
			if (d[i] < m)
				m = d[i];
		return m;
	});
	register_function("max", [](Isolate &iso, const Array &a) {
		Array c = a.contiguous();
		intptr_t n = c.size();
		if (n == 0)
			iso.raise(String("[Value error] 'max' of an empty Array"), 0);
		const double *d = origin(c);
		double m = d[0];
		for (intptr_t i = 1; i < n; ++i)
			if (d[i] > m)
				m = d[i];
		return m;
	});

	// --- in-place ---
	register_function("clear", [](Array &a) {
		double *d = a.detach();
		intptr_t n = a.size();
		for (intptr_t i = 0; i < n; ++i)
			d[i] = 0.0;
	});

	// --- elementwise math (overloads of the scalar math generics) ---
	register_function("abs", [](const Array &a) { return map(a, [](double x) { return std::fabs(x); }); });
	register_function("sqrt", [](const Array &a) { return map(a, [](double x) { return std::sqrt(x); }); });
	register_function("exp", [](const Array &a) { return map(a, [](double x) { return std::exp(x); }); });
	register_function("log", [](const Array &a) { return map(a, [](double x) { return std::log(x); }); });
	register_function("sin", [](const Array &a) { return map(a, [](double x) { return std::sin(x); }); });
	register_function("cos", [](const Array &a) { return map(a, [](double x) { return std::cos(x); }); });
	register_function("floor", [](const Array &a) { return map(a, [](double x) { return std::floor(x); }); });
	register_function("ceil", [](const Array &a) { return map(a, [](double x) { return std::ceil(x); }); });
}

} // namespace phonometrica
