// Phonometrica engine — elementwise Array kernels (architecture §5.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/lib/array_kernels.hpp>

#include <cmath>

namespace phonometrica {

double array_scalar_op(char op, double a, double b)
{
	switch (op)
	{
	case '+': return a + b;
	case '-': return a - b;
	case '*': return a * b;
	case '/': return a / b;
	case '^': return std::pow(a, b);
	default: return 0.0; // unreachable: callers pass a validated op
	}
}

void array_binop(char op, double *out, const double *a, const double *b, intptr_t n)
{
	for (intptr_t i = 0; i < n; ++i)
		out[i] = array_scalar_op(op, a[i], b[i]);
}

void array_binop_as(char op, double *out, const double *a, double s, intptr_t n)
{
	for (intptr_t i = 0; i < n; ++i)
		out[i] = array_scalar_op(op, a[i], s);
}

void array_binop_sa(char op, double *out, double s, const double *b, intptr_t n)
{
	for (intptr_t i = 0; i < n; ++i)
		out[i] = array_scalar_op(op, s, b[i]);
}

} // namespace phonometrica
