// Phonometrica engine — Array: the numeric workhorse (design §9, architecture §5.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Two cells: a value-semantic **view** (`ArrayCell`) and a separately refcounted
// **buffer** (`ArrayBuffer`) of `double`s. Slicing produces a new view sharing the
// buffer (zero copy); mutation is copy-on-write and checks BOTH the view rc and the
// buffer rc. Storage is **column-major** (stride[0] == 1 for a contiguous array) so
// buffers interoperate with LAPACK/FFTW/Eigen conventions. Element type is `double`
// only for now (a boolean mask type is a later addition). Indices are 1-based at the
// C++/script boundary, 0-based internally.
//
// Both classes are acyclic (a buffer holds only doubles; a view holds only numbers
// plus one internal buffer pointer that can never point back), so they are born GREEN
// and the cycle collector never touches them. 64-byte AVX alignment for the buffer is
// deferred until the SIMD/thread-pool work (M7); scalar kernels do not need it.

#ifndef PHON_TYPES_ARRAY_HPP
#define PHON_TYPES_ARRAY_HPP

#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/handle.hpp>
#include <phon/engine/core/value.hpp>

namespace phonometrica {

// Soft cap on rank. Not an architectural limit — a guard against absurd shapes.
inline constexpr int PHON_MAX_RANK = 8;

enum ArrayFlags : uint32_t
{
	// The view spans its whole buffer with the canonical column-major strides and a
	// zero offset, so its elements are `buf->data[0 .. size)` — the flat-loop fast path.
	ARRAY_CONTIGUOUS = 1u << 0,
};

// The refcounted double storage. `count` doubles inline after the header.
struct ArrayBuffer
{
	Cell header;
	intptr_t count;
	double data[]; // inline
};

// The script-visible Array value: a strided view into an ArrayBuffer.
struct ArrayCell
{
	Cell header;
	ArrayBuffer *buf; // owned reference (+1)
	intptr_t offset;  // element offset of this view's origin into buf->data
	int32_t rank;     // 1..PHON_MAX_RANK
	uint32_t flags;
	intptr_t dim[PHON_MAX_RANK];    // extent along each axis
	intptr_t stride[PHON_MAX_RANK]; // in elements, column-major (stride[0]==1 if contiguous)
};

void register_array_class();

class Array final
{
public:
	Array(const Array &) = default;
	Array(Array &&) noexcept = default;
	Array &operator=(const Array &) = default;
	Array &operator=(Array &&) noexcept = default;

	// --- construction ---

	// A fresh contiguous, zero-filled array of the given shape (column-major).
	static Array make(int rank, const intptr_t *dims);
	static Array make_1d(intptr_t n) { return make(1, &n); }
	static Array make_2d(intptr_t nrow, intptr_t ncol);

	// A new strided view over an existing buffer (zero-copy slicing): retains `buf`.
	// The CONTIGUOUS flag is set iff the given offset/strides are canonical column-major.
	static Array make_view(ArrayBuffer *buf, intptr_t offset, int rank, const intptr_t *dim,
	                       const intptr_t *stride);

	// --- shape ---

	int rank() const noexcept { return m_impl->rank; }
	intptr_t dim(int k) const noexcept { return m_impl->dim[k]; }
	intptr_t stride(int k) const noexcept { return m_impl->stride[k]; }
	intptr_t offset() const noexcept { return m_impl->offset; }
	intptr_t size() const noexcept; // product of dims (element count of the view)
	bool is_contiguous() const noexcept { return m_impl->flags & ARRAY_CONTIGUOUS; }
	bool unique() const noexcept;

	// --- element access (0-based multi-index, internal) ---

	// Buffer element offset for the 0-based multi-index `idx` (length == rank).
	intptr_t elem_offset(const intptr_t *idx) const noexcept;
	double get(const intptr_t *idx) const noexcept { return m_impl->buf->data[elem_offset(idx)]; }

	const double *data() const noexcept { return m_impl->buf->data; }

	// A contiguous version of this array (design §5.3): shares the buffer when already
	// contiguous, otherwise a fresh compacted column-major copy. Lets kernels run over
	// flat spans (its data() + offset()==0 is the element origin).
	Array contiguous() const;

	// Ensure this view uniquely owns a contiguous buffer (CoW): if the view or its
	// buffer is shared, or the view is non-contiguous, build a fresh contiguous buffer
	// (copying the spanned elements) and repoint. Returns the writable element base
	// (buf->data + offset, with offset now 0). Rewrites the owning Handle slot.
	double *detach();

	// Freeze this array: give the view a private, contiguous buffer and mark that buffer
	// frozen + shared, so it becomes an immutable double[] shareable zero-copy across
	// threads (freeze() builtin, §8.3). Idempotent. Subsequent mutations copy-on-write.
	void make_frozen();

	// Produce a copy safe to hand to another thread (transfer walk, §8.3): if the buffer
	// is frozen it is shared zero-copy (atomic retain) and only the view is copied;
	// otherwise a fully independent contiguous copy is made. Returns a +1 view.
	Array transfer_to_thread() const;

	// --- engine interop ---

	Value to_value() const noexcept { return Value::make_cell(m_impl.cell()); }
	static Array from_value(Value v) noexcept;
	static Array adopt(Value v) noexcept; // take ownership without retaining
	ArrayCell *cell() const noexcept { return m_impl.get(); }

private:
	explicit Array(Handle<ArrayCell> h) noexcept : m_impl(std::move(h)) {}

	Handle<ArrayCell> m_impl;
};

} // namespace phonometrica

#endif // PHON_TYPES_ARRAY_HPP
