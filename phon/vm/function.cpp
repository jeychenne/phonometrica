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

// A NativeCell that carries a captured C++ callable (typed registration, M8)
// releases its environment cell when it dies; a plain native has env == null.
void native_finalize(Cell *c)
{
	auto *nf = reinterpret_cast<NativeCell *>(c);
	if (nf->env)
		release(nf->env);
}

// A NativeEnvCell (runtime/native_traits.hpp) owns one C++ callable stored inline.
// Its finalizer runs the erased destructor via the stored function pointer; the
// callable's own captures (Handles/Variants) are freed by that destructor. The
// header prefix is fixed, so this file can finalize it without seeing the template.
struct NativeEnvHeader
{
	Cell header;
	void (*destroy)(void *payload);
	uint32_t payload_off; // byte offset of the callable from the cell base
};

void native_env_finalize(Cell *c)
{
	auto *env = reinterpret_cast<NativeEnvHeader *>(c);
	env->destroy(reinterpret_cast<char *>(c) + env->payload_off);
}

Class *g_closure = nullptr;
Class *g_native = nullptr;
Class *g_native_env = nullptr;
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
	g_native->finalize = &native_finalize;
	// The captured-callable environment for typed natives. Acyclic from the
	// collector's view: it never traces into the C++ callable, so a script cell
	// captured by a C++ lambda is an uncollectable root (a leak, not a crash) —
	// noted in DEVIATIONS. Never script-visible.
	g_native_env = add_class("NativeEnv", object, CLASS_BUILTIN | CLASS_REF | CLASS_ACYCLIC);
	g_native_env->finalize = &native_env_finalize;
	g_upvalue = add_class("Upvalue", object, CLASS_BUILTIN | CLASS_REF);
	g_upvalue->finalize = &upvalue_finalize;
	g_upvalue->trace = &upvalue_trace;
}

Class *closure_class() noexcept { return g_closure; }
Class *native_class() noexcept { return g_native; }
Class *native_env_class() noexcept { return g_native_env; }
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

NativeCell *make_native(NativeFn fn, Symbol name, int min_arity, int max_arity, Cell *env)
{
	Cell *c = cell_alloc(g_native->id, static_cast<intptr_t>(sizeof(NativeCell)));
	auto *nf = reinterpret_cast<NativeCell *>(c);
	nf->fn = fn;
	nf->name = name;
	nf->min_arity = min_arity;
	nf->max_arity = max_arity;
	nf->env = env; // adopts the +1 the caller allocated (released on finalize)
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
