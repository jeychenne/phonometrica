// Phonometrica engine — callable cells implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/vm/function.hpp>

#include <phon/vm/isolate.hpp>

namespace phonometrica {

namespace {

PHON_FORCE_INLINE void release_value(Value v) noexcept
{
	if (v.owns_cell())
		release(v.cell_ptr());
}

void closure_finalize(Cell *c)
{
	auto *cl = reinterpret_cast<ClosureCell *>(c);
	for (int32_t i = 0; i < cl->nupvals; ++i)
		if (cl->upvals[i])
			release(reinterpret_cast<Cell *>(cl->upvals[i]));
}

void upvalue_finalize(Cell *c)
{
	auto *uv = reinterpret_cast<UpvalueCell *>(c);
	if (uv->is_open())
	{
		// Died while still open: drop it from the isolate's open list first.
		if (Isolate *iso = current_isolate())
			iso->unlink_open_upvalue(uv);
	}
	else
	{
		release_value(uv->closed);
	}
}

// Cycle-collector traversal (§8.2). A closure's captured upvalues are cells; an
// upvalue's closed value may be a cell. An *open* upvalue points at a live stack
// slot (a root the collector never owns), so it enumerates nothing.
void closure_trace(Cell *c, void (*visit)(Cell *))
{
	auto *cl = reinterpret_cast<ClosureCell *>(c);
	for (int32_t i = 0; i < cl->nupvals; ++i)
		if (cl->upvals[i])
			visit(reinterpret_cast<Cell *>(cl->upvals[i]));
}

void upvalue_trace(Cell *c, void (*visit)(Cell *))
{
	auto *uv = reinterpret_cast<UpvalueCell *>(c);
	if (!uv->is_open() && uv->closed.is_cell())
		visit(uv->closed.as_cell());
}

Class *g_closure = nullptr;
Class *g_native = nullptr;
Class *g_upvalue = nullptr;

} // namespace

void register_function_classes()
{
	if (g_closure)
		return;
	Class *object = get_class(CID_OBJECT);
	// Reference classes (identity). Closures/upvalues are potentially cyclic;
	// natives never are.
	g_closure = add_class("Function", object, CLASS_BUILTIN | CLASS_REF);
	g_closure->finalize = &closure_finalize;
	g_closure->trace = &closure_trace;
	g_native = add_class("Function", object, CLASS_BUILTIN | CLASS_REF | CLASS_ACYCLIC);
	g_native->finalize = nullptr;
	g_upvalue = add_class("Upvalue", object, CLASS_BUILTIN | CLASS_REF);
	g_upvalue->finalize = &upvalue_finalize;
	g_upvalue->trace = &upvalue_trace;
}

Class *closure_class() noexcept { return g_closure; }
Class *native_class() noexcept { return g_native; }
Class *upvalue_class() noexcept { return g_upvalue; }

ClosureCell *make_closure(Proto *proto)
{
	int32_t n = static_cast<int32_t>(proto->upvals.size());
	intptr_t size = static_cast<intptr_t>(sizeof(ClosureCell)) +
	                static_cast<intptr_t>(n) * static_cast<intptr_t>(sizeof(UpvalueCell *));
	Cell *c = cell_alloc(g_closure->id, size);
	auto *cl = reinterpret_cast<ClosureCell *>(c);
	cl->proto = proto;
	cl->nupvals = n;
	for (int32_t i = 0; i < n; ++i)
		cl->upvals[i] = nullptr;
	return cl;
}

NativeCell *make_native(NativeFn fn, Symbol name, int min_arity, int max_arity)
{
	Cell *c = cell_alloc(g_native->id, static_cast<intptr_t>(sizeof(NativeCell)));
	auto *nf = reinterpret_cast<NativeCell *>(c);
	nf->fn = fn;
	nf->name = name;
	nf->min_arity = min_arity;
	nf->max_arity = max_arity;
	return nf;
}

UpvalueCell *make_upvalue(Value *slot)
{
	Cell *c = cell_alloc(g_upvalue->id, static_cast<intptr_t>(sizeof(UpvalueCell)));
	auto *uv = reinterpret_cast<UpvalueCell *>(c);
	uv->slot = slot;
	uv->closed = Value::make_null();
	uv->next = nullptr;
	return uv;
}

UpvalueCell *make_reference_box(Value initial)
{
	Cell *c = cell_alloc(g_upvalue->id, static_cast<intptr_t>(sizeof(UpvalueCell)));
	auto *uv = reinterpret_cast<UpvalueCell *>(c);
	uv->closed = initial;
	if (initial.owns_cell())
		retain(initial.cell_ptr()); // the box owns the value
	uv->slot = &uv->closed; // closed: not tracking any stack slot
	uv->next = nullptr;
	return uv;
}

} // namespace phonometrica
