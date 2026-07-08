// Phonometrica engine — Array implementation (design §9, architecture §5.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/types/array.hpp>

#include <phon/object/class.hpp>

#include <cstddef>
#include <cstring>

namespace phonometrica {

namespace {

constexpr intptr_t BUFFER_HEADER = offsetof(ArrayBuffer, data);

ArrayBuffer *buffer_alloc(intptr_t count)
{
	if (count < 0)
		count = 0;
	Cell *c = cell_alloc(CID_ARRAYBUFFER, BUFFER_HEADER + count * static_cast<intptr_t>(sizeof(double)));
	auto *b = reinterpret_cast<ArrayBuffer *>(c);
	b->count = count;
	return b;
}

ArrayCell *view_alloc()
{
	Cell *c = cell_alloc(CID_ARRAY, static_cast<intptr_t>(sizeof(ArrayCell)));
	return reinterpret_cast<ArrayCell *>(c);
}

intptr_t product(const intptr_t *dims, int rank)
{
	intptr_t n = 1;
	for (int k = 0; k < rank; ++k)
		n *= dims[k];
	return n;
}

// Fill `dst` (contiguous, column-major) with the view's elements in column-major
// order (axis 0 varies fastest). Used to compact a shared/strided view into a fresh
// buffer during copy-on-write.
void gather_column_major(double *dst, const ArrayCell *v)
{
	intptr_t idx[PHON_MAX_RANK] = {0};
	intptr_t total = product(v->dim, v->rank);
	for (intptr_t lin = 0; lin < total; ++lin)
	{
		intptr_t off = v->offset;
		for (int k = 0; k < v->rank; ++k)
			off += idx[k] * v->stride[k];
		dst[lin] = v->buf->data[off];
		for (int k = 0; k < v->rank; ++k) // column-major increment: axis 0 fastest
		{
			if (++idx[k] < v->dim[k])
				break;
			idx[k] = 0;
		}
	}
}

void set_contiguous_strides(ArrayCell *v)
{
	intptr_t s = 1;
	for (int k = 0; k < v->rank; ++k)
	{
		v->stride[k] = s;
		s *= v->dim[k];
	}
	v->offset = 0;
	v->flags |= ARRAY_CONTIGUOUS;
}

// Set ARRAY_CONTIGUOUS iff the view spans its buffer with the canonical column-major
// strides at offset 0 (the flat-loop fast path). A strided/offset slice is not.
void mark_contiguous_if_canonical(ArrayCell *v)
{
	bool canon = (v->offset == 0);
	intptr_t s = 1;
	for (int k = 0; canon && k < v->rank; ++k)
	{
		if (v->stride[k] != s)
			canon = false;
		s *= v->dim[k];
	}
	if (canon)
		v->flags |= ARRAY_CONTIGUOUS;
	else
		v->flags &= ~ARRAY_CONTIGUOUS;
}

// --- class hooks ---

void array_finalize(Cell *c)
{
	auto *v = reinterpret_cast<ArrayCell *>(c);
	release(reinterpret_cast<Cell *>(v->buf)); // drop the view's buffer reference
}

// Two arrays are equal iff they have the same shape and equal elements (IEEE `==`
// per element, so NaN arrays are never equal). Strided on both sides.
bool array_equals_hook(const Cell *ca, const Cell *cb)
{
	auto *a = reinterpret_cast<const ArrayCell *>(ca);
	auto *b = reinterpret_cast<const ArrayCell *>(cb);
	if (a->rank != b->rank)
		return false;
	for (int k = 0; k < a->rank; ++k)
		if (a->dim[k] != b->dim[k])
			return false;
	intptr_t idx[PHON_MAX_RANK] = {0};
	intptr_t total = product(a->dim, a->rank);
	for (intptr_t lin = 0; lin < total; ++lin)
	{
		intptr_t oa = a->offset, ob = b->offset;
		for (int k = 0; k < a->rank; ++k)
		{
			oa += idx[k] * a->stride[k];
			ob += idx[k] * b->stride[k];
		}
		if (a->buf->data[oa] != b->buf->data[ob])
			return false;
		for (int k = 0; k < a->rank; ++k)
		{
			if (++idx[k] < a->dim[k])
				break;
			idx[k] = 0;
		}
	}
	return true;
}

Class g_array_class;
Class g_arraybuffer_class;

} // namespace

void register_array_class()
{
	// The double buffer: an internal, acyclic cell holding no child references.
	g_arraybuffer_class.id = CID_ARRAYBUFFER;
	g_arraybuffer_class.name = "ArrayBuffer";
	g_arraybuffer_class.base = get_class(CID_OBJECT);
	g_arraybuffer_class.flags = CLASS_BUILTIN | CLASS_ACYCLIC | CLASS_SEALED;
	g_arraybuffer_class.instance_size = -1;
	register_class(&g_arraybuffer_class);

	// The script-visible view: a value class (CoW). Acyclic (its only cell reference is
	// the buffer, which can never point back), so the collector never traces it; the
	// finalizer releases the buffer.
	g_array_class.id = CID_ARRAY;
	g_array_class.name = "Array";
	g_array_class.base = get_class(CID_OBJECT);
	g_array_class.flags = CLASS_BUILTIN | CLASS_VALUE | CLASS_ACYCLIC | CLASS_SEALED;
	g_array_class.instance_size = -1;
	g_array_class.finalize = &array_finalize;
	g_array_class.equals = &array_equals_hook;
	register_class(&g_array_class);
}

// ---------------------------------------------------------------------------

Array Array::make(int rank, const intptr_t *dims)
{
	PHON_ASSERT(rank >= 1 && rank <= PHON_MAX_RANK);
	intptr_t n = product(dims, rank);
	ArrayBuffer *buf = buffer_alloc(n);
	std::memset(buf->data, 0, static_cast<size_t>(n) * sizeof(double));
	ArrayCell *v = view_alloc();
	v->buf = buf; // adopt the buffer's rc-1
	v->rank = rank;
	v->flags = 0;
	for (int k = 0; k < rank; ++k)
		v->dim[k] = dims[k];
	set_contiguous_strides(v);
	return Array(Handle<ArrayCell>::adopt(v));
}

Array Array::make_2d(intptr_t nrow, intptr_t ncol)
{
	intptr_t dims[2] = {nrow, ncol};
	return make(2, dims);
}

Array Array::make_view(ArrayBuffer *buf, intptr_t offset, int rank, const intptr_t *dim,
                       const intptr_t *stride)
{
	PHON_ASSERT(rank >= 1 && rank <= PHON_MAX_RANK);
	ArrayCell *v = view_alloc();
	v->buf = buf;
	retain(reinterpret_cast<Cell *>(buf)); // the new view shares the buffer (zero-copy)
	v->offset = offset;
	v->rank = rank;
	v->flags = 0;
	for (int k = 0; k < rank; ++k)
	{
		v->dim[k] = dim[k];
		v->stride[k] = stride[k];
	}
	mark_contiguous_if_canonical(v);
	return Array(Handle<ArrayCell>::adopt(v));
}

Array Array::from_value(Value v) noexcept
{
	PHON_ASSERT(v.is_cell() && v.as_cell()->class_id() == CID_ARRAY);
	return Array(Handle<ArrayCell>(reinterpret_cast<ArrayCell *>(v.as_cell())));
}

Array Array::adopt(Value v) noexcept
{
	PHON_ASSERT(v.is_cell() && v.as_cell()->class_id() == CID_ARRAY);
	return Array(Handle<ArrayCell>::adopt(reinterpret_cast<ArrayCell *>(v.as_cell())));
}

intptr_t Array::size() const noexcept { return product(m_impl->dim, m_impl->rank); }

Array Array::contiguous() const
{
	if (m_impl->flags & ARRAY_CONTIGUOUS)
		return *this; // shares the buffer (Handle copy retains)
	Array out = make(m_impl->rank, m_impl->dim);
	gather_column_major(out.m_impl->buf->data, m_impl.get());
	return out;
}

bool Array::unique() const noexcept
{
	return m_impl.unique() && m_impl->buf->header.is_unique();
}

void Array::make_frozen()
{
	ArrayCell *v = m_impl.get();
	if (v->buf->header.is_frozen())
		return; // idempotent
	// The shared blob must be a private, contiguous double[] this view owns outright. If
	// the buffer is aliased or this view is strided/offset, compact into a fresh buffer
	// first (this leaves element values unchanged, so mutating the view cell in place is
	// benign even when the view is aliased — every alias keeps identical elements).
	if (!(v->flags & ARRAY_CONTIGUOUS) || v->buf->header.is_shared())
	{
		intptr_t n = size();
		ArrayBuffer *nb = buffer_alloc(n);
		gather_column_major(nb->data, v);
		release(reinterpret_cast<Cell *>(v->buf)); // drop the old (shared/strided) buffer
		v->buf = nb;                               // adopt nb's rc-1
		set_contiguous_strides(v);
	}
	mark_frozen_shared(&v->buf->header);
}

intptr_t Array::elem_offset(const intptr_t *idx) const noexcept
{
	intptr_t off = m_impl->offset;
	for (int k = 0; k < m_impl->rank; ++k)
		off += idx[k] * m_impl->stride[k];
	return off;
}

double *Array::detach()
{
	ArrayCell *v = m_impl.get();
	bool view_unique = m_impl.unique();
	bool buf_unique = v->buf->header.is_unique();
	// A frozen buffer is immutable and possibly shared across threads (§8.3): never write
	// in place, even if this view is its only local holder — always copy to a fresh one.
	bool buf_frozen = v->buf->header.is_frozen();
	if (view_unique && buf_unique && !buf_frozen && (v->flags & ARRAY_CONTIGUOUS))
		return v->buf->data + v->offset; // uniquely ours and contiguous: mutate in place

	// Build a fresh contiguous buffer holding this view's elements (copy BEFORE
	// dropping the old buffer). The buffer is cloned in every non-fast case; the view
	// cell is additionally cloned only when the view itself is shared.
	intptr_t n = size();
	ArrayBuffer *nb = buffer_alloc(n);
	gather_column_major(nb->data, v);

	if (view_unique)
	{
		release(reinterpret_cast<Cell *>(v->buf)); // drop the old (shared/strided) buffer
		v->buf = nb;                                // adopt nb's rc-1
		set_contiguous_strides(v);
		return nb->data;
	}
	// The view is shared: clone it so our mutation does not reach other holders.
	ArrayCell *nv = view_alloc();
	nv->buf = nb; // adopt nb's rc-1
	nv->rank = v->rank;
	nv->flags = 0;
	for (int k = 0; k < v->rank; ++k)
		nv->dim[k] = v->dim[k];
	set_contiguous_strides(nv);
	m_impl = Handle<ArrayCell>::adopt(nv); // releases the old shared view, adopts the clone
	return nb->data;
}

} // namespace phonometrica
