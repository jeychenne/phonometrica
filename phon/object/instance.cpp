// Phonometrica engine — instances of user-defined classes (implementation).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/object/instance.hpp>

namespace phonometrica {

namespace {

PHON_FORCE_INLINE void retain_value(Value v) noexcept
{
	if (v.owns_cell())
		retain(v.cell_ptr());
}
PHON_FORCE_INLINE void release_value(Value v) noexcept
{
	if (v.owns_cell())
		release(v.cell_ptr());
}

intptr_t instance_bytes(Class *c) noexcept
{
	return static_cast<intptr_t>(sizeof(Cell)) +
	       static_cast<intptr_t>(c->field_count) * static_cast<intptr_t>(sizeof(Value));
}

} // namespace

Cell *make_instance(Class *c)
{
	Cell *cell = cell_alloc(c->id, instance_bytes(c));
	Value *f = instance_fields(cell);
	Value null = Value::make_null();
	for (int32_t i = 0; i < c->field_count; ++i)
		f[i] = null;
	return cell;
}

Cell *instance_clone(const Cell *src)
{
	Class *c = get_class(src->class_id());
	Cell *dst = cell_alloc(c->id, instance_bytes(c));
	const Value *sf = reinterpret_cast<const InstanceCell *>(src)->fields;
	Value *df = instance_fields(dst);
	for (int32_t i = 0; i < c->field_count; ++i)
	{
		Value v = sf[i];
		retain_value(v);
		df[i] = v;
	}
	return dst;
}

void instance_finalize(Cell *c)
{
	Value *f = instance_fields(c);
	int32_t n = get_class(c->class_id())->field_count;
	for (int32_t i = 0; i < n; ++i)
		release_value(f[i]);
}

void instance_trace(Cell *c, void (*visit)(Cell *))
{
	Value *f = instance_fields(c);
	int32_t n = get_class(c->class_id())->field_count;
	for (int32_t i = 0; i < n; ++i)
		if (f[i].is_cell())
			visit(f[i].as_cell());
}

void instance_clone_hook(Cell *dst, const Cell *src)
{
	// Fill a caller-allocated `dst` of matching class from `src` (the CloneHook ABI).
	const Value *sf = reinterpret_cast<const InstanceCell *>(src)->fields;
	Value *df = instance_fields(dst);
	int32_t n = get_class(src->class_id())->field_count;
	for (int32_t i = 0; i < n; ++i)
	{
		Value v = sf[i];
		retain_value(v);
		df[i] = v;
	}
}

} // namespace phonometrica
