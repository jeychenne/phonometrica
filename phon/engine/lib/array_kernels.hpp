// Phonometrica engine — elementwise NumArray kernels (architecture §5.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// [INVARIANT] All elementwise NumArray kernels live here as free functions over raw
// contiguous `double` spans, so the future SIMD/thread-pool work targets one file
// (architecture §5.3). Callers gather strided views into contiguous buffers first.
// Scalar loops for now; vectorization is deferred to the concurrency milestone.

#ifndef PHON_LIB_ARRAY_KERNELS_HPP
#define PHON_LIB_ARRAY_KERNELS_HPP

#include <cstdint>

namespace phonometrica {

// The scalar op for a single pair, `op` in {'+','-','*','/','^'}.
double array_scalar_op(char op, double a, double b);

// out[i] = a[i] op b[i]     (elementwise, both operands contiguous, length n)
void array_binop(char op, double *out, const double *a, const double *b, intptr_t n);

// out[i] = a[i] op s        (array on the left, scalar broadcast)
void array_binop_as(char op, double *out, const double *a, double s, intptr_t n);

// out[i] = s op b[i]        (scalar on the left, array broadcast)
void array_binop_sa(char op, double *out, double s, const double *b, intptr_t n);

} // namespace phonometrica

#endif // PHON_LIB_ARRAY_KERNELS_HPP
