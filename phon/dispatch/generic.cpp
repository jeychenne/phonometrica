// Phonometrica engine — generic functions and multiple dispatch implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/dispatch/generic.hpp>

#include <phon/core/cell.hpp>    // retain/release for cell-valued constants
#include <phon/vm/function.hpp> // deref() reads through a first-class reference argument

#include <atomic>
#include <unordered_map>
#include <utility>

namespace phonometrica {

namespace {

// --- specificity relations over signatures (single inheritance) ---
//
// A method's *effective* type at a position expands the vararg: a fixed position
// uses its declared class, and any trailing position of a variadic uses the vararg
// element type. Comparisons happen at a "comparison arity" cmp at which both
// methods are applicable (cmp = max fixed count); design §6.

// Effective declared class at position i, for a method applicable at some argc > i.
Class *eff_type(const Method &m, int i)
{
	int fx = m.fixed_count();
	return (i < fx) ? m.sig[i] : m.vararg_type();
}

// a ≤ b pointwise over [0, cmp): every effective type of a is-a b's.
bool pointwise_le(const Method &a, const Method &b, int cmp)
{
	for (int i = 0; i < cmp; ++i)
		if (!is_a(eff_type(a, i), eff_type(b, i)))
			return false;
	return true;
}

bool comparable(const Class *a, const Class *b)
{
	return is_a(a, b) || is_a(b, a);
}

// The more specific of two comparable classes.
const Class *more_specific(const Class *a, const Class *b)
{
	return is_a(a, b) ? a : b;
}

// Whether a and b can be applicable at a common argc (their arity ranges intersect).
bool arities_overlap(const Method &a, const Method &b)
{
	int fa = a.fixed_count(), fb = b.fixed_count();
	if (a.is_vararg && b.is_vararg)
		return true;
	if (a.is_vararg)
		return fb >= fa; // b fixed at fb; a (variadic) reaches it iff fb >= fa
	if (b.is_vararg)
		return fa >= fb;
	return fa == fb; // both fixed
}

// The representative argc at which two overlapping methods are compared. The larger
// fixed count exposes every fixed position; when both are variadic one extra position
// is added so their vararg element types are compared too (design §6 tie-break). This
// single arity is representative of the whole co-applicable range.
int compare_arity(const Method &a, const Method &b)
{
	int fa = a.fixed_count(), fb = b.fixed_count();
	int m = fa > fb ? fa : fb;
	return (a.is_vararg && b.is_vararg) ? m + 1 : m;
}

bool applicable_at(const Method &m, int argc)
{
	return m.is_vararg ? argc >= m.fixed_count() : argc == m.fixed_count();
}

// Is a strictly more specific than b, both applicable at argc? Types are primary
// (pointwise subtyping); when the effective types are identical the design's kind
// tiebreak decides — a fixed method beats a variadic, then more fixed params win.
// The co-applicable set is totally ordered (ambiguity is rejected at definition),
// so this drives an unambiguous argmax in full_resolve.
bool more_specific_at(const Method &a, const Method &b, int argc)
{
	bool ab = pointwise_le(a, b, argc), ba = pointwise_le(b, a, argc);
	if (ab && !ba)
		return true;
	if (ba && !ab)
		return false;
	if (ab && ba)
	{
		if (a.is_vararg != b.is_vararg)
			return !a.is_vararg; // fixed beats variadic
		return a.fixed_count() > b.fixed_count(); // more fixed params wins
	}
	return false; // incomparable (a disambiguator exists among the applicable set)
}

// Registry of generic functions keyed by symbol id.
struct GenericRegistry
{
	FlatHashMap<uint32_t, GenericFunction *> by_name;
	Vector<GenericFunction *> owned;

	~GenericRegistry()
	{
		for (intptr_t i = 0; i < owned.size(); ++i)
			delete owned[i];
	}
};

GenericRegistry &registry()
{
	static GenericRegistry r;
	return r;
}

// Encode one argument's class id into a key slot. Class ids are < 2^24; the `<< 1`
// keeps the key format bit-compatible with the interpreter's inline cache. Ref-ness
// is no longer folded in (it is uniform per generic, §4).
PHON_FORCE_INLINE uint64_t encode_arg(uint32_t cid) noexcept
{
	return static_cast<uint64_t>(cid) << 1;
}

// Dispatch class of an argument: a reference argument dispatches on its referent.
PHON_FORCE_INLINE uint32_t dispatch_class(Value a) noexcept
{
	return class_of(deref(a));
}

// Full resolution: index of the most-specific applicable method, or -1. A variadic
// method applies when argc ≥ its fixed count and every trailing argument subtypes
// the element type (design §6); ref-ness is uniform (§4) and plays no part.
int32_t full_resolve(GenericFunction *g, const uint32_t *arg_class, int argc)
{
	SmallVector<int32_t, 8> applicable;
	for (intptr_t mi = 0; mi < g->methods.size(); ++mi)
	{
		const Method &m = g->methods[mi];
		int fx = m.fixed_count();
		if (m.is_vararg ? argc < fx : argc != fx)
			continue;
		bool ok = true;
		for (int i = 0; i < fx; ++i)
			if (!is_a(get_class(arg_class[i]), m.sig[i]))
			{
				ok = false;
				break;
			}
		if (ok && m.is_vararg)
		{
			Class *et = m.vararg_type();
			for (int i = fx; i < argc; ++i)
				if (!is_a(get_class(arg_class[i]), et))
				{
					ok = false;
					break;
				}
		}
		if (ok)
			applicable.push_back(static_cast<int32_t>(mi));
	}

	if (applicable.empty())
		return -1;

	// Most specific = argmax under more_specific_at. The applicable set is totally
	// ordered (ambiguity is prevented at definition time), so the maximum is unique.
	int32_t best = applicable[0];
	for (intptr_t k = 1; k < applicable.size(); ++k)
		if (more_specific_at(g->methods[applicable[k]], g->methods[best], argc))
			best = applicable[k];
	return best;
}

} // namespace

GenericFunction *get_or_create_generic(Symbol name)
{
	GenericRegistry &r = registry();
	auto it = r.by_name.find(name.id);
	if (it != r.by_name.end())
		return it->second;
	auto *g = new GenericFunction{};
	g->name = name;
	r.owned.push_back(g);
	r.by_name.insert(name.id, g);
	return g;
}

GenericFunction *find_generic(Symbol name) noexcept
{
	GenericRegistry &r = registry();
	auto it = r.by_name.find(name.id);
	return it == r.by_name.end() ? nullptr : it->second;
}

AddMethod add_method(GenericFunction *g, const SmallVector<Class *, 4> &sig, uint64_t ref_mask,
                     bool is_vararg, void *code)
{
	// A view of the incoming method for the effective-type comparisons below.
	Method incoming;
	incoming.sig = sig;
	incoming.is_vararg = is_vararg;
	const int fixed = incoming.fixed_count();

	// Ref-ness is uniform across a generic's overloads (§4): the first method fixes
	// the mask; any later overload whose mask disagrees is rejected.
	if (g->methods.empty())
		g->ref_mask = ref_mask;
	else if (ref_mask != g->ref_mask)
		return AddMethod::RefMaskConflict;

	// Redefinition: an identical signature (same types AND same vararg-ness)
	// overwrites the code in place.
	for (intptr_t i = 0; i < g->methods.size(); ++i)
	{
		Method &e = g->methods[i];
		if (e.is_vararg != is_vararg || e.arity() != incoming.arity())
			continue;
		bool same = true;
		for (intptr_t j = 0; j < e.sig.size(); ++j)
			if (e.sig[j] != sig[j])
			{
				same = false;
				break;
			}
		if (same)
		{
			e.code = code;
			return AddMethod::Ok;
		}
	}

	// Ambiguity check against every existing method whose arity range overlaps the
	// incoming one. Compare effective types at the representative arity `cmp` (the
	// larger fixed count, where both apply). Two methods conflict when they are
	// incomparable yet overlap (comparable at every position); the overlap needs a
	// disambiguator whose effective types equal the pointwise meet, else ambiguous.
	for (intptr_t i = 0; i < g->methods.size(); ++i)
	{
		const Method &e = g->methods[i];
		if (!arities_overlap(incoming, e))
			continue;
		int cmp = compare_arity(incoming, e);

		if (pointwise_le(incoming, e, cmp) || pointwise_le(e, incoming, cmp))
			continue; // comparable -> totally ordered (kind tiebreak at resolve), no conflict

		bool overlap = true;
		for (int j = 0; j < cmp; ++j)
			if (!comparable(eff_type(incoming, j), eff_type(e, j)))
			{
				overlap = false;
				break;
			}
		if (!overlap)
			continue; // disjoint -> can never both apply

		SmallVector<Class *, 4> meet;
		for (int j = 0; j < cmp; ++j)
			meet.push_back(const_cast<Class *>(more_specific(eff_type(incoming, j), eff_type(e, j))));

		auto eff_equals_meet = [&](const Method &d) {
			if (!applicable_at(d, cmp))
				return false;
			for (int j = 0; j < cmp; ++j)
				if (eff_type(d, j) != meet[j])
					return false;
			return true;
		};

		bool has_disambiguator = eff_equals_meet(incoming);
		if (!has_disambiguator)
			for (intptr_t k = 0; k < g->methods.size(); ++k)
				if (eff_equals_meet(g->methods[k]))
				{
					has_disambiguator = true;
					break;
				}
		if (!has_disambiguator)
			return AddMethod::Ambiguous;
	}

	// Commit.
	Method m;
	m.code = code;
	m.is_vararg = is_vararg;
	m.sig = sig;
	g->methods.push_back(std::move(m));

	if (fixed < g->min_arity)
		g->min_arity = static_cast<uint8_t>(fixed);
	if (fixed > g->max_arity)
		g->max_arity = static_cast<uint8_t>(fixed);
	if (is_vararg)
		g->has_vararg = true;
	++g->generic_epoch;
	g->memo.clear();
	return AddMethod::Ok;
}

void remove_method(GenericFunction *g, const SmallVector<Class *, 4> &sig, bool is_vararg)
{
	const int argc = static_cast<int>(sig.size());
	for (intptr_t i = 0; i < g->methods.size(); ++i)
	{
		Method &e = g->methods[i];
		if (e.is_vararg != is_vararg || e.arity() != argc)
			continue;
		bool same = true;
		for (int j = 0; j < argc; ++j)
			if (e.sig[j] != sig[j])
			{
				same = false;
				break;
			}
		if (!same)
			continue;

		// Order among methods is irrelevant to resolution (most-specific is found by
		// pairwise comparison), so a swap-and-pop erase is safe.
		g->methods.erase_unordered(i);
		++g->generic_epoch;
		g->memo.clear();

		// Recompute the arity bounds and vararg flag; a stale-low min_arity would merely
		// cost a fruitless full_resolve, but keeping them tight is cheap and clean.
		g->min_arity = 255;
		g->max_arity = 0;
		g->has_vararg = false;
		for (intptr_t k = 0; k < g->methods.size(); ++k)
		{
			int fx = g->methods[k].fixed_count();
			if (fx < g->min_arity)
				g->min_arity = static_cast<uint8_t>(fx);
			if (fx > g->max_arity)
				g->max_arity = static_cast<uint8_t>(fx);
			if (g->methods[k].is_vararg)
				g->has_vararg = true;
		}
		return;
	}
}

// Live spawned-worker count (concurrency §13). Non-zero => dispatch is potentially
// multi-threaded, so the shared per-generic memo must not be touched (see resolve).
std::atomic<int> g_worker_threads{0};

void dispatch_enter_thread() noexcept
{
	g_worker_threads.fetch_add(1, std::memory_order_release);
}
void dispatch_exit_thread() noexcept
{
	g_worker_threads.fetch_sub(1, std::memory_order_release);
}

Method *resolve(GenericFunction *g, const Value *args, int argc)
{
	// A variadic overload has no upper arity bound; only the fixed minimum prunes.
	if (argc < g->min_arity || (!g->has_vararg && argc > g->max_arity))
		return nullptr;

	// Argument dispatch classes (a reference dispatches on its referent). Sized to
	// argc so a variadic call with many arguments needs no fixed-capacity buffer.
	SmallVector<uint32_t, 8> arg_class;
	arg_class.reserve(argc);
	for (int i = 0; i < argc; ++i)
		arg_class.push_back(dispatch_class(args[i]));

	// The memo (arity 1/2) is shared mutable state; use it only while single-threaded.
	// A worker always sees its own increment, so it never enters here — leaving the
	// memo the exclusive province of the main thread when no worker is live.
	bool memo_ok =
	    (argc == 1 || argc == 2) && g_worker_threads.load(std::memory_order_acquire) == 0;
	if (memo_ok)
	{
		// Invalidate the memo on a method-set or type-hierarchy change.
		if (g->memo_generic_epoch != g->generic_epoch || g->memo_type_epoch != type_epoch())
		{
			g->memo.clear();
			g->memo_generic_epoch = g->generic_epoch;
			g->memo_type_epoch = type_epoch();
		}
		uint64_t key = encode_arg(arg_class[0]);
		if (argc == 2)
			key = (key << 25) | encode_arg(arg_class[1]);
		auto it = g->memo.find(key);
		int32_t idx = (it != g->memo.end()) ? it->second
		                                    : full_resolve(g, arg_class.data(), argc);
		if (it == g->memo.end())
			g->memo.insert(key, idx);
		return idx < 0 ? nullptr : &g->methods[idx];
	}

	// Arity 0 and 3..8, or any call while a worker is live: resolve fully (no memo).
	// full_resolve only reads the stable method set, so concurrent callers are safe.
	int32_t idx = full_resolve(g, arg_class.data(), argc);
	return idx < 0 ? nullptr : &g->methods[idx];
}

// --- global value constants (bare-name builtin bindings like PI/E) ------------
//
// A small process-global name→Value table for builtin constants. Registered once at
// init_runtime (before any script compiles), then read-only during compilation — the
// compiler inlines a hit as a LOADK (compile-time substitution, no runtime lookup).
// Mirrors the generic registry's init-before-use, single-static-owner discipline.

namespace {
std::unordered_map<uint32_t, Value> &constant_table()
{
	static std::unordered_map<uint32_t, Value> t;
	return t;
}
} // namespace

void register_constant(Symbol name, Value v)
{
	auto &t = constant_table();
	auto it = t.find(name.id);
	if (it != t.end())
	{
		if (v.owns_cell())
			retain(v.cell_ptr());
		if (it->second.owns_cell())
			release(it->second.cell_ptr());
		it->second = v;
		return;
	}
	if (v.owns_cell())
		retain(v.cell_ptr());
	t.emplace(name.id, v);
}

bool find_constant(Symbol name, Value &out) noexcept
{
	auto &t = constant_table();
	auto it = t.find(name.id);
	if (it == t.end())
		return false;
	out = it->second;
	return true;
}

void generic_registry_shutdown()
{
	GenericRegistry &r = registry();
	for (intptr_t i = 0; i < r.owned.size(); ++i)
		delete r.owned[i];
	r.owned.clear();
	r.by_name = FlatHashMap<uint32_t, GenericFunction *>();

	auto &t = constant_table();
	for (auto &kv : t)
		if (kv.second.owns_cell())
			release(kv.second.cell_ptr());
	t.clear();
}

} // namespace phonometrica
