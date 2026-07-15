// Phonometrica engine — typed C++ registration front end (architecture §11.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The primary style for exposing C++ to scripts (M8). Instead of hand-writing a
// `Value(Isolate&, NativeCell*, Value*, int)` callback that unboxes arguments by
// hand, the embedder registers an ordinary C++ callable and a template layer maps
// its parameter/return types to (dispatch class, box/unbox) pairs, generates a
// per-signature thunk, and installs it as a properly typed method on the generic:
//
//     rt.add_function("duration", [](Handle<Interval> i) -> double {
//         return i->xmax - i->xmin;
//     });
//     rt.add_function("greet", [](const String &who) { return "Hi " & who; });
//
// A capturing lambda is moved into a cell-managed environment (NativeEnvCell) owned
// by the NativeCell; the thunk recovers it through `self` (design §11.3). An
// optional leading `Isolate &` parameter is passed through by the thunk and is *not*
// part of the dispatch signature — the hook for callables that need to raise or call
// back into the VM.
//
// Supported parameter/return types (M8 stage 1): the numeric scalars (bool, any
// integer type -> Integer, any floating type -> Real), String/List/Table/Set/NumArray
// wrappers, Handle<T> for registered reference classes (T::phon_class, stage 2),
// Variant and raw Value (untyped pass-through), and `void` return. `ref` parameters
// (phon::Ref<T>) are a later sub-stage — see DEVIATIONS.

#ifndef PHON_RUNTIME_NATIVE_TRAITS_HPP
#define PHON_RUNTIME_NATIVE_TRAITS_HPP

#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/handle.hpp>
#include <phon/engine/core/reference.hpp>
#include <phon/engine/core/small_vector.hpp>
#include <phon/engine/core/value.hpp>
#include <phon/engine/core/variant.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/types/array.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/set.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/types/table.hpp>
#include <phon/engine/vm/function.hpp>

#include <cstddef>
#include <new>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace phonometrica {

class Isolate;

// `ref` (by-reference) parameters use the natural C++ spelling: a **non-const lvalue
// reference** parameter is a `ref` that writes back to the caller's slot; a by-value
// or `const T&` parameter is read-only (design §11.3, references.md §4). So:
//
//     rt.add_function("normalize", [](NumArray &x) { /* mutates x in place */ });
//     rt.add_function("trim",      [](String &s) { s = s.trim(); });
//     double duration(const Interval &i);   // read-only — NOT a ref
//
// ref-ness is uniform per generic; the registration installs the matching ref-mask so
// direct calls promote the argument (references.md §6). Supported ref types: numeric
// scalars and the String/List/Table/NumArray wrappers (see RefMarshal).

// The semantics of a registered C++ class (design §11.2): a Reference class has
// identity (no copy), a Value class copies on write (needs a clone hook).
enum class ClassKind
{
	Reference,
	Value,
};

// --- registration core (non-template; defined in runtime.cpp) -----------------

// Install `fn` (a generated thunk) as a method on the generic `name`, dispatched on
// `sig` (the C++ parameter classes), with fixed arity. `ref_mask` marks the `ref`
// (non-const-lvalue-reference) parameters. `env`, if non-null, is a NativeEnvCell whose
// +1 the created NativeCell adopts. Throws std::runtime_error if the signature is
// ambiguous against, or its ref-mask conflicts with, an existing overload.
void register_typed_native(const char *name, NativeFn fn, Cell *env,
                           const SmallVector<Class *, 4> &sig, int arity, uint64_t ref_mask);

// Register a C++ type as a phon class (design §11.2): records `instance_size`, wires the
// finalizer/clone hooks, and returns the Class. The template add_class<T> below binds
// `T::phon_class`. The class is globally nameable (like a builtin) so scripts can use it
// in `is`/type annotations and dispatch, but is *not* script-constructible — instances
// come from C++ (Handle<T>::make) or a registered factory function.
// `trace` (may be null) lets the cycle collector see engine cells held inside the boxed
// C++ value; `acyclic` marks the class a GC leaf (cells born GREEN, never buffered) — set
// when the type holds no traceable cells, so a foreign object that captures no script
// references costs nothing at the collector.
Class *register_foreign_class(const char *name, Class *base, bool is_reference,
                              intptr_t instance_size, FinalizeHook finalize, CloneHook clone,
                              TraceHook trace, GcFreeHook gc_free, bool acyclic);

namespace detail {

// A cell owning one C++ callable inline. The header prefix mirrors NativeEnvHeader in
// vm/function.cpp (which finalizes it without seeing this template) — keep the two in
// sync: {Cell, void(*destroy)(void*), uint32_t payload_off}.
struct NativeEnvCell
{
	Cell header;
	void (*destroy)(void *payload);
	uint32_t payload_off;
};

template<class F>
constexpr uint32_t env_payload_offset() noexcept
{
	uint32_t base = static_cast<uint32_t>(sizeof(NativeEnvCell));
	uint32_t a = static_cast<uint32_t>(alignof(F));
	return (base + a - 1) / a * a;
}

// Allocate an env cell holding a copy/move of `f`. cell_alloc + placement-new; the
// finalizer (function.cpp) runs `destroy`.
template<class Fn>
Cell *make_native_env(Fn &&f)
{
	using F = std::decay_t<Fn>;
	static_assert(alignof(F) <= alignof(std::max_align_t), "captured callable is over-aligned");
	uint32_t off = env_payload_offset<F>();
	intptr_t size = static_cast<intptr_t>(off) + static_cast<intptr_t>(sizeof(F));
	Cell *c = cell_alloc(native_env_class()->id, size);
	auto *env = reinterpret_cast<NativeEnvCell *>(c);
	env->destroy = [](void *p) { static_cast<F *>(p)->~F(); };
	env->payload_off = off;
	::new (reinterpret_cast<char *>(c) + off) F(std::forward<Fn>(f));
	return c;
}

template<class F>
F *env_callable(NativeCell *self) noexcept
{
	auto *env = reinterpret_cast<NativeEnvCell *>(self->env);
	return reinterpret_cast<F *>(reinterpret_cast<char *>(env) + env->payload_off);
}

// A parameter is by-reference iff it is a non-const lvalue reference.
template<class A>
inline constexpr bool is_ref_param =
    std::is_lvalue_reference_v<A> && !std::is_const_v<std::remove_reference_t<A>>;

// A registered application class (Sound, File, Regex, …): any *class* type that is not
// one of the engine's own value wrappers, Handle, or Isolate. Such a type is passed by
// reference (`T&`/`const T&`) or by Handle<T>, reaching the shared object with identity
// preserved — never a write-back ref and never copied.
template<class T>
struct is_handle_type : std::false_type
{
};
template<class U>
struct is_handle_type<Handle<U>> : std::true_type
{
};

template<class T>
inline constexpr bool is_registered_class_v =
    std::is_class_v<T> && !is_handle_type<T>::value && !std::is_same_v<T, String> &&
    !std::is_same_v<T, List> && !std::is_same_v<T, Table> && !std::is_same_v<T, Set> &&
    !std::is_same_v<T, NumArray> && !std::is_same_v<T, Variant> && !std::is_same_v<T, Value> &&
    !std::is_same_v<T, Isolate>;

// A write-back (`ref`) parameter is a non-const lvalue reference of a *value* type —
// a registered class reference is identity, not write-back, so it is excluded.
template<class A>
inline constexpr bool is_writeback_ref_v =
    is_ref_param<A> && !is_registered_class_v<std::decay_t<A>>;

// --- argument traits: C++ param type -> (dispatch class, unbox) ---------------
//
// unbox() runs *after* dispatch has confirmed the argument's class, so it may
// assume the encoding (asserts in debug). Wrapper unboxers return owning RAII
// values; a partially-applied argument pack cleans up correctly if a later unbox
// throws (only Handle<T> of a wrong cell could, which dispatch already excludes).

template<class T, class = void>
struct ArgTraits;

template<class T>
struct ArgTraits<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
{
	static Class *dispatch_class() { return get_class(CID_INTEGER); }
	static T unbox(Value v) { return static_cast<T>(v.as_int()); }
};

template<class T>
struct ArgTraits<T, std::enable_if_t<std::is_floating_point_v<T>>>
{
	static Class *dispatch_class() { return get_class(CID_REAL); }
	static T unbox(Value v) { return static_cast<T>(v.to_double()); }
};

template<>
struct ArgTraits<bool, void>
{
	static Class *dispatch_class() { return get_class(CID_BOOLEAN); }
	static bool unbox(Value v) { return v.as_bool(); }
};

template<>
struct ArgTraits<String, void>
{
	static Class *dispatch_class() { return get_class(CID_STRING); }
	static String unbox(Value v) { return String::from_value(v); }
};

template<>
struct ArgTraits<List, void>
{
	static Class *dispatch_class() { return get_class(CID_LIST); }
	static List unbox(Value v) { return List::from_value(v); }
};

template<>
struct ArgTraits<Table, void>
{
	static Class *dispatch_class() { return get_class(CID_TABLE); }
	static Table unbox(Value v) { return Table::from_value(v); }
};

template<>
struct ArgTraits<NumArray, void>
{
	static Class *dispatch_class() { return get_class(CID_ARRAY); }
	static NumArray unbox(Value v) { return NumArray::from_value(v); }
};

// Untyped pass-through. Variant retains (owning); raw Value is borrowed.
template<>
struct ArgTraits<Variant, void>
{
	static Class *dispatch_class() { return get_class(CID_OBJECT); }
	static Variant unbox(Value v) { return Variant(v); }
};

template<>
struct ArgTraits<Value, void>
{
	static Class *dispatch_class() { return get_class(CID_OBJECT); }
	static Value unbox(Value v) { return v; }
};

// Handle<T> for a registered application class (a plain C++ class registered by
// add_class<T>, which set class_of<T>()). Dispatches on that class; unbox retains and
// reaches the payload transparently (cell-headed or boxed) via Handle<T>::from_cell.
template<class T>
struct ArgTraits<Handle<T>, void>
{
	static Class *dispatch_class() { return class_of<T>(); }
	static Handle<T> unbox(Value v) { return Handle<T>::from_cell(v.as_cell()); }
};

// A registered application class passed by reference (`T&` / `const T&`): dispatches on
// its class; the reference is materialized by the ClassRef binder (below), which keeps
// the cell alive and hands the callable a reference into the shared object.
template<class T>
struct ArgTraits<T, std::enable_if_t<is_registered_class_v<T>>>
{
	static Class *dispatch_class() { return class_of<T>(); }
};

// --- return traits: C++ return type -> boxed Value (+1) -----------------------

template<class T, class = void>
struct RetTraits;

template<class T>
struct RetTraits<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
{
	static Value box(T x) { return Value::make_int(static_cast<int64_t>(x)); }
};

template<class T>
struct RetTraits<T, std::enable_if_t<std::is_floating_point_v<T>>>
{
	static Value box(T x) { return Value::make(static_cast<double>(x)); }
};

template<>
struct RetTraits<bool, void>
{
	static Value box(bool b) { return Value::make_bool(b); }
};

// Cell-wrapper returns hand out +1: retain the cell, then the argument wrapper's
// destructor drops the callable's own reference, netting one for the caller.
template<class W>
static Value box_wrapper(const W &w)
{
	Value v = w.to_value();
	if (v.owns_cell())
		retain(v.cell_ptr());
	return v;
}

template<>
struct RetTraits<String, void>
{
	static Value box(const String &s) { return box_wrapper(s); }
};
template<>
struct RetTraits<List, void>
{
	static Value box(const List &l) { return box_wrapper(l); }
};
template<>
struct RetTraits<Table, void>
{
	static Value box(const Table &t) { return box_wrapper(t); }
};
template<>
struct RetTraits<NumArray, void>
{
	static Value box(const NumArray &a) { return box_wrapper(a); }
};

template<>
struct RetTraits<Variant, void>
{
	static Value box(const Variant &var)
	{
		Value v = var.value();
		if (v.owns_cell())
			retain(v.cell_ptr());
		return v;
	}
};

template<>
struct RetTraits<Value, void>
{
	static Value box(Value v)
	{
		if (v.owns_cell())
			retain(v.cell_ptr());
		return v;
	}
};

template<class T>
struct RetTraits<Handle<T>, void>
{
	static Value box(const Handle<T> &h)
	{
		Cell *c = h.cell();
		if (!c)
			return Value::make_null();
		retain(c);
		return Value::make_cell(c);
	}
};

// --- `ref` (T&) parameter marshalling -----------------------------------------
//
// A non-const lvalue-reference parameter is a `ref`: the argument arrives promoted to
// a first-class reference box (references.md §6). The referent is moved *out* of the
// box slot (transferring the slot's reference) so a CoW value type mutates in place
// when uniquely held and copies when shared; the possibly-changed value is written
// back through the box after the call. The slot is null for the duration — an
// accepted wart shared with `for ref` (references.md).

template<class T, class = void>
struct RefMarshal
{
	static_assert(sizeof(T) == 0,
	              "unsupported ref (T&) parameter type; supported: numeric scalars and "
	              "the String/List/Table/Array wrappers");
};

// Scalars (bool/integer/floating): the slot holds an immediate.
template<class T>
struct RefMarshal<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	static T move_out(UpvalueCell *box, Value v)
	{
		return ArgTraits<T>::unbox(box ? *box->slot : v);
	}
	static void write_back(UpvalueCell *box, const T &storage)
	{
		Value nv = RetTraits<T>::box(storage); // immediate; no cell
		Value *slot = box->slot;
		if (slot->owns_cell())
			release(slot->cell_ptr());
		*slot = nv;
	}
};

// Cell-backed wrappers: transfer the slot's reference into the wrapper (nulling the
// slot) so mutation is in-place when unique, then write the (moved) wrapper back.
template<class T>
struct WrapperRefMarshal
{
	static T move_out(UpvalueCell *box, Value v)
	{
		if (!box)
			return ArgTraits<T>::unbox(v); // unpromoted (indirect call): no write-back
		Value cur = *box->slot;
		T w = T::from_value(cur); // retain
		if (cur.owns_cell())
			release(cur.cell_ptr());     // drop the slot's reference (transfer to w)
		*box->slot = Value::make_null(); // slot emptied; w now owns the value
		return w;
	}
	static void write_back(UpvalueCell *box, const T &storage)
	{
		Value nv = storage.to_value();
		if (nv.owns_cell())
			retain(nv.cell_ptr()); // slot will own a reference
		Value *slot = box->slot;
		if (slot->owns_cell())
			release(slot->cell_ptr()); // release whatever is there (null after move_out)
		*slot = nv;
	}
};
template<>
struct RefMarshal<String, void> : WrapperRefMarshal<String>
{
};
template<>
struct RefMarshal<List, void> : WrapperRefMarshal<List>
{
};
template<>
struct RefMarshal<Table, void> : WrapperRefMarshal<Table>
{
};
template<>
struct RefMarshal<NumArray, void> : WrapperRefMarshal<NumArray>
{
};

// One stack-local binder per argument, constructed before the call and destroyed after
// (so a ref binder writes back once the callable returns). Built as a chain of locals
// by Thunk::bind — never copied or moved. Three kinds:
//   Value    — by value / const&: unbox a copy.
//   WriteRef — `T&` of a value type: move out, mutate in place, write back (references.md).
//   ClassRef — `T&`/`const T&` of a registered class: a reference into the shared object.
enum class BindKind
{
	Value,
	WriteRef,
	ClassRef,
};

template<class A>
constexpr BindKind bind_kind() noexcept
{
	if constexpr (is_registered_class_v<std::decay_t<A>>)
		return BindKind::ClassRef;
	else if constexpr (is_ref_param<A>)
		return BindKind::WriteRef;
	else
		return BindKind::Value;
}

template<class A, BindKind K = bind_kind<A>()>
struct Binder;

// By-value (A is `T`, `const T&`, or `T&&`): unbox once; hand the callable a movable T.
template<class A>
struct Binder<A, BindKind::Value>
{
	using T = std::decay_t<A>;
	T value;
	Binder(Isolate &, Value v) : value(ArgTraits<T>::unbox(v)) {}
	Binder(const Binder &) = delete;
	Binder &operator=(const Binder &) = delete;
	T &&get() noexcept { return std::move(value); }
};

// By-reference (`T&`): expose the moved-out referent for in-place mutation, write it
// back on destruction.
template<class A>
struct Binder<A, BindKind::WriteRef>
{
	using T = std::decay_t<A>;
	UpvalueCell *box;
	T storage;
	Binder(Isolate &, Value v)
	    : box(v.is_reference() ? reference_box(v) : nullptr),
	      storage(RefMarshal<T>::move_out(box, v))
	{
	}
	Binder(const Binder &) = delete;
	Binder &operator=(const Binder &) = delete;
	T &get() noexcept { return storage; }
	~Binder()
	{
		if (box)
			RefMarshal<T>::write_back(box, storage);
	}
};

// A registered application class by reference: a Handle keeps the cell alive for the
// call; get() yields a reference into the shared object (identity, no copy, no write-
// back). Passing such a class by value is rejected (it would copy a shared object).
template<class A>
struct Binder<A, BindKind::ClassRef>
{
	using T = std::decay_t<A>;
	static_assert(std::is_reference_v<A>,
	              "pass a registered class by reference (T& / const T&) or by Handle<T>, "
	              "not by value");
	Handle<T> h;
	Binder(Isolate &, Value v) : h(Handle<T>::from_cell(deref(v).as_cell())) {}
	Binder(const Binder &) = delete;
	Binder &operator=(const Binder &) = delete;
	T &get() noexcept { return *h; }
};

template<class... A>
constexpr uint64_t compute_ref_mask() noexcept
{
	uint64_t mask = 0;
	std::size_t i = 0;
	((mask |= (static_cast<uint64_t>(is_writeback_ref_v<A>) << i), ++i), ...);
	return mask;
}

// --- signature extraction -----------------------------------------------------

template<class T>
struct fn_signature; // { using ret; using args = std::tuple<...>; }

template<class R, class... A>
struct fn_signature<R (*)(A...)>
{
	using ret = R;
	using args = std::tuple<A...>;
};
template<class C, class R, class... A>
struct fn_signature<R (C::*)(A...)>
{
	using ret = R;
	using args = std::tuple<A...>;
};
template<class C, class R, class... A>
struct fn_signature<R (C::*)(A...) const>
{
	using ret = R;
	using args = std::tuple<A...>;
};

// A functor (lambda) resolves through operator(); a plain function pointer directly.
template<class F, class = void>
struct callable_signature : fn_signature<F>
{
};
template<class F>
struct callable_signature<F, std::void_t<decltype(&F::operator())>>
    : fn_signature<decltype(&F::operator())>
{
};

// Strip an optional leading `Isolate &` from the argument tuple; report whether one
// was present (the thunk passes it through, sig-building skips it).
template<class Tuple>
struct strip_isolate
{
	static constexpr bool has_iso = false;
	using type = Tuple;
};
template<class A0, class... Rest>
struct strip_isolate<std::tuple<A0, Rest...>>
{
	static constexpr bool has_iso = std::is_same_v<std::remove_reference_t<A0>, Isolate>;
	using type = std::conditional_t<has_iso, std::tuple<Rest...>, std::tuple<A0, Rest...>>;
};

// --- the generated thunk ------------------------------------------------------
//
// `A...` are the *dispatched* parameter types (raw — including `&`/`const` — after
// stripping a leading Isolate&). Each argument is bound to a stack-local Binder in a
// recursion whose frames stay live across the callable invocation, so a by-reference
// Binder's write-back runs after the call returns (as the recursion unwinds).

template<class F, class R, bool HasIso, class... A>
struct Thunk
{
	// Bind args[Next..N) as stack locals, accumulating references in `bound`, then call.
	template<std::size_t Next, class... Bound>
	static Value bind(Isolate &iso, F &f, Value *args, Bound &&...bound)
	{
		if constexpr (Next == sizeof...(A))
		{
			if constexpr (std::is_void_v<R>)
			{
				if constexpr (HasIso)
					f(iso, std::forward<Bound>(bound)...);
				else
					f(std::forward<Bound>(bound)...);
				return Value::make_null();
			}
			else if constexpr (HasIso)
				return RetTraits<std::decay_t<R>>::box(f(iso, std::forward<Bound>(bound)...));
			else
				return RetTraits<std::decay_t<R>>::box(f(std::forward<Bound>(bound)...));
		}
		else
		{
			using A_Next = std::tuple_element_t<Next, std::tuple<A...>>;
			Binder<A_Next> b(iso, args[Next]);
			return bind<Next + 1>(iso, f, args, std::forward<Bound>(bound)..., b.get());
		}
	}

	static Value entry(Isolate &iso, NativeCell *self, Value *args, int argc)
	{
		(void) argc; // arity is checked at the call site against the NativeCell
		F &f = *env_callable<F>(self);
		return bind<0>(iso, f, args);
	}
};

template<class F, class R, bool HasIso, class Tuple>
struct Registrar;

template<class F, class R, bool HasIso, class... A>
struct Registrar<F, R, HasIso, std::tuple<A...>>
{
	static void reg(const char *name, F f)
	{
		Cell *env = make_native_env<F>(std::move(f));
		NativeFn thunk = &Thunk<F, R, HasIso, A...>::entry;
		SmallVector<Class *, 4> sig;
		(sig.push_back(ArgTraits<std::decay_t<A>>::dispatch_class()), ...);
		register_typed_native(name, thunk, env, sig, static_cast<int>(sizeof...(A)),
		                      compute_ref_mask<A...>());
	}
};

// The finalizer for a registered application class: run T's destructor (on the boxed
// value, at its offset after the header) before the cell is freed — releasing any
// C++-owned members (vectors, buffers, file handles).
template<class T>
void foreign_finalize(Cell *c)
{
	box_value<T>(c)->~T();
}

// The clone hook for a Value application class (CoW copy): copy-construct T from the
// source's boxed value into the destination's box, leaving dst's own header intact.
template<class T>
void foreign_clone(Cell *dst, const Cell *src)
{
	::new (static_cast<void *>(box_value<T>(dst))) T(*box_value<T>(const_cast<Cell *>(src)));
}

// A registered C++ type opts into cycle collection by defining
//   void gc_trace(void (*visit)(Cell *)) const;
// which calls `visit` once per engine cell it owns (a `Handle`/`Variant`/`Value` member).
// Detected here; when present the class is traceable (and not a GC leaf), so a garbage
// cycle routed through the object is reclaimed instead of leaking.
template<class T, class = void>
inline constexpr bool has_gc_trace_v = false;
template<class T>
inline constexpr bool has_gc_trace_v<
    T, std::void_t<decltype(std::declval<const T &>().gc_trace(std::declval<void (*)(Cell *)>()))>> =
    true;

// The trace hook: forward the collector's visitor to the boxed value's gc_trace.
template<class T>
void foreign_trace(Cell *c, void (*visit)(Cell *))
{
	box_value<T>(c)->gc_trace(visit);
}

// When a garbage cycle is reclaimed the object's destructor is bypassed (the collector has
// already balanced its cell edges via gc_trace), so a traceable type that ALSO owns non-cell
// resources — or needs a side effect on death — provides
//   void gc_free();
// which the collector calls on the cyclic-free path. It must free those non-cell resources
// but must NOT release the cells enumerated by gc_trace (the collector owns those). It is the
// exact counterpart of the engine's own gc_free hooks (List/Table). Optional: without it a
// cycle-collected foreign object only has its inline storage reclaimed.
template<class T, class = void>
inline constexpr bool has_gc_free_v = false;
template<class T>
inline constexpr bool has_gc_free_v<T, std::void_t<decltype(std::declval<T &>().gc_free())>> = true;

template<class T>
void foreign_gc_free(Cell *c)
{
	box_value<T>(c)->gc_free();
}

// --- Variant typed conversions (design §11.4) ---------------------------------
//
// Definitions of the helpers Variant::to<T>()/make() forward to (declared in
// core/variant.hpp). They reuse the same type mapping as function registration:
// `to<T>` type-checks then unboxes (an owning result); `make` boxes then adopts the
// +1 into a Variant.

template<class T>
T variant_to(Value v)
{
	Value d = deref(v);
	Class *want = ArgTraits<T>::dispatch_class();
	if (!value_is_a(d, want))
		throw std::runtime_error(std::string("[Type error] cannot convert a ") +
		                         class_of_desc(d)->name + " to " + want->name);
	return ArgTraits<T>::unbox(d);
}

template<class T>
Variant variant_from(const T &x)
{
	return Variant::adopt(RetTraits<std::decay_t<T>>::box(x));
}

} // namespace detail

// Register a C++ callable as a method on the generic `name` (design §11.3). Each
// call with an existing name adds an overload — C++ and script overloading are the
// same mechanism. See the file header for supported types.
template<class F>
void register_function(const char *name, F &&f)
{
	using DF = std::decay_t<F>;
	using Sig = detail::callable_signature<DF>;
	using Strip = detail::strip_isolate<typename Sig::args>;
	detail::Registrar<DF, typename Sig::ret, Strip::has_iso, typename Strip::type>::reg(
	    name, std::forward<F>(f));
}

// Register the C++ type `T` as a phon class named `name`, deriving from `base` (design
// §11.2). Records sizeof(T), wires ~T() as the finalizer (and, for a Value class, a CoW
// clone hook) and records `class_of<T>()` so Handle<T> and typed dispatch interoperate.
// `T` is an ordinary plain C++ class — it needs **nothing** on it (no `Cell` member, no
// static): the engine boxes it as `{ Cell header; T value }`, so the same class is usable
// as a standalone C++ object and as a script value. Instances come from Handle<T>::make.
template<class T>
Class *add_class(const char *name, Class *base, ClassKind kind = ClassKind::Reference)
{
	static_assert(!is_cell_headed_v<T>,
	              "add_class<T> is for plain application classes; a cell-headed engine type "
	              "registers through its own register_*_class");
	bool is_reference = (kind == ClassKind::Reference);
	// A type that defines gc_trace participates in cycle collection; otherwise it is a GC
	// leaf (holds no engine cells) and is marked acyclic so it never enters the collector.
	// `if constexpr` so foreign_trace<T> is only instantiated for a type that has gc_trace.
	constexpr bool traceable = detail::has_gc_trace_v<T>;
	TraceHook trace = nullptr;
	if constexpr (traceable)
		trace = &detail::foreign_trace<T>;
	GcFreeHook gc_free = nullptr;
	if constexpr (detail::has_gc_free_v<T>)
		gc_free = &detail::foreign_gc_free<T>;
	Class *c = register_foreign_class(name, base, is_reference,
	                                  static_cast<intptr_t>(box_total_size<T>()),
	                                  &detail::foreign_finalize<T>,
	                                  is_reference ? nullptr : &detail::foreign_clone<T>, trace,
	                                  gc_free, /*acyclic=*/!traceable);
	g_registered_class<T> = c;
	return c;
}

} // namespace phonometrica

#endif // PHON_RUNTIME_NATIVE_TRAITS_HPP
