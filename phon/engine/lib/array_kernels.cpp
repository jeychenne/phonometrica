// Phonometrica engine — elementwise Array kernels (architecture §5.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/lib/array_kernels.hpp>

#include <phon/engine/concurrency/thread_pool.hpp>

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

// Each elementwise kernel splits into a per-range worker over disjoint output slices, run
// on the thread pool above PHON_PARALLEL_THRESHOLD (architecture §13); below it, the loop
// runs inline. The parallel and serial paths compute bit-identically (same arithmetic,
// disjoint indices), so results never depend on the element count.
namespace {

struct BinopCtx
{
	char op;
	double *out;
	const double *a;
	const double *b;
};

void binop_range(void *p, intptr_t lo, intptr_t hi)
{
	auto *c = static_cast<BinopCtx *>(p);
	for (intptr_t i = lo; i < hi; ++i)
		c->out[i] = array_scalar_op(c->op, c->a[i], c->b[i]);
}

struct ScalarCtx
{
	char op;
	double *out;
	const double *v; // the array operand
	double s;        // the scalar operand
	bool array_left; // true: out = v op s ; false: out = s op v
};

void scalar_range(void *p, intptr_t lo, intptr_t hi)
{
	auto *c = static_cast<ScalarCtx *>(p);
	if (c->array_left)
		for (intptr_t i = lo; i < hi; ++i)
			c->out[i] = array_scalar_op(c->op, c->v[i], c->s);
	else
		for (intptr_t i = lo; i < hi; ++i)
			c->out[i] = array_scalar_op(c->op, c->s, c->v[i]);
}

} // namespace

void array_binop(char op, double *out, const double *a, const double *b, intptr_t n)
{
	BinopCtx ctx{op, out, a, b};
	if (n >= PHON_PARALLEL_THRESHOLD)
		global_thread_pool().parallel_for(n, binop_range, &ctx);
	else
		binop_range(&ctx, 0, n);
}

void array_binop_as(char op, double *out, const double *a, double s, intptr_t n)
{
	ScalarCtx ctx{op, out, a, s, true};
	if (n >= PHON_PARALLEL_THRESHOLD)
		global_thread_pool().parallel_for(n, scalar_range, &ctx);
	else
		scalar_range(&ctx, 0, n);
}

void array_binop_sa(char op, double *out, double s, const double *b, intptr_t n)
{
	ScalarCtx ctx{op, out, b, s, false};
	if (n >= PHON_PARALLEL_THRESHOLD)
		global_thread_pool().parallel_for(n, scalar_range, &ctx);
	else
		scalar_range(&ctx, 0, n);
}

} // namespace phonometrica
