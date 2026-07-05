// Phonometrica engine — generic functions and multiple dispatch implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/dispatch/generic.hpp>

#include <utility>

namespace phonometrica {

namespace {

// --- specificity relations over signatures (single inheritance) ---

// M ≤ E: M is at least as specific as E at every position.
bool sig_le(const Method &m, const Method &e)
{
	if (m.sig.size() != e.sig.size())
		return false;
	for (intptr_t i = 0; i < m.sig.size(); ++i)
		if (!is_a(m.sig[i], e.sig[i]))
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

// Encode one argument as (class_id << 1 | ref_bit). Class ids are < 2^24, so the
// result is < 2^25 and two of them pack losslessly into a uint64 key.
PHON_FORCE_INLINE uint64_t encode_arg(uint32_t cid, bool ref) noexcept
{
	return (static_cast<uint64_t>(cid) << 1) | (ref ? 1u : 0u);
}

// Dispatch class of an argument: a ref argument dispatches on its referent.
PHON_FORCE_INLINE uint32_t dispatch_class(Value a, bool &is_ref) noexcept
{
	if (a.is_ref())
	{
		is_ref = true;
		return class_of(*a.as_ref());
	}
	is_ref = false;
	return class_of(a);
}

// Full resolution: index of the most-specific applicable method, or -1.
int32_t full_resolve(GenericFunction *g, const uint32_t *arg_class, uint64_t arg_ref_mask, int argc)
{
	// Applicable = same arity, exact ref-mask match, subtype at every position.
	SmallVector<int32_t, 8> applicable;
	for (intptr_t mi = 0; mi < g->methods.size(); ++mi)
	{
		const Method &m = g->methods[mi];
		if (m.arity() != argc)
			continue;
		if (m.ref_mask != arg_ref_mask)
			continue; // ref-ness participates in applicability, not specificity
		bool ok = true;
		for (int i = 0; i < argc; ++i)
		{
			if (!is_a(get_class(arg_class[i]), m.sig[i]))
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

	// Most specific = the one that is ≤ every other applicable method. Ambiguity
	// is prevented at definition time, so exactly one such method exists.
	for (intptr_t a = 0; a < applicable.size(); ++a)
	{
		const Method &ma = g->methods[applicable[a]];
		bool dominates_all = true;
		for (intptr_t b = 0; b < applicable.size(); ++b)
		{
			if (a == b)
				continue;
			if (!sig_le(ma, g->methods[applicable[b]]))
			{
				dominates_all = false;
				break;
			}
		}
		if (dominates_all)
			return applicable[a];
	}
	PHON_UNREACHABLE_MSG("dispatch: no most-specific method (ambiguity escaped definition check)");
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
                     void *code)
{
	const int argc = static_cast<int>(sig.size());

	// Redefinition: identical (sig, ref_mask) overwrites the code in place.
	for (intptr_t i = 0; i < g->methods.size(); ++i)
	{
		Method &e = g->methods[i];
		if (e.arity() != argc || e.ref_mask != ref_mask)
			continue;
		bool same = true;
		for (int j = 0; j < argc; ++j)
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

	// Ambiguity check against every existing method that could co-apply (same
	// arity and ref-mask). Two signatures conflict when they are incomparable yet
	// overlap (comparable at every position); the overlap needs a method whose
	// signature is exactly the pointwise meet, else the definition is ambiguous.
	for (intptr_t i = 0; i < g->methods.size(); ++i)
	{
		const Method &e = g->methods[i];
		if (e.arity() != argc || e.ref_mask != ref_mask)
			continue;

		// Build a temporary Method view of the new signature for sig_le.
		Method incoming;
		incoming.ref_mask = ref_mask;
		for (int j = 0; j < argc; ++j)
			incoming.sig.push_back(sig[j]);

		if (sig_le(incoming, e) || sig_le(e, incoming))
			continue; // comparable -> totally ordered, no conflict

		// Incomparable. Overlap iff comparable at every position.
		bool overlap = true;
		for (int j = 0; j < argc; ++j)
			if (!comparable(sig[j], e.sig[j]))
			{
				overlap = false;
				break;
			}
		if (!overlap)
			continue; // disjoint -> can never both apply

		// Need a disambiguator: a method whose signature equals the pointwise
		// meet (the more specific class at each position).
		SmallVector<Class *, 4> meet;
		for (int j = 0; j < argc; ++j)
			meet.push_back(const_cast<Class *>(more_specific(sig[j], e.sig[j])));

		bool has_disambiguator = false;
		// The incoming method itself may be the meet.
		bool incoming_is_meet = true;
		for (int j = 0; j < argc; ++j)
			if (sig[j] != meet[j])
			{
				incoming_is_meet = false;
				break;
			}
		if (incoming_is_meet)
			has_disambiguator = true;
		if (!has_disambiguator)
		{
			for (intptr_t k = 0; k < g->methods.size(); ++k)
			{
				const Method &d = g->methods[k];
				if (d.arity() != argc || d.ref_mask != ref_mask)
					continue;
				bool eq = true;
				for (int j = 0; j < argc; ++j)
					if (d.sig[j] != meet[j])
					{
						eq = false;
						break;
					}
				if (eq)
				{
					has_disambiguator = true;
					break;
				}
			}
		}
		if (!has_disambiguator)
			return AddMethod::Ambiguous;
	}

	// Commit.
	Method m;
	m.ref_mask = ref_mask;
	m.code = code;
	for (int j = 0; j < argc; ++j)
		m.sig.push_back(sig[j]);
	g->methods.push_back(std::move(m));

	if (argc < g->min_arity)
		g->min_arity = static_cast<uint8_t>(argc);
	if (argc > g->max_arity)
		g->max_arity = static_cast<uint8_t>(argc);
	++g->generic_epoch;
	g->memo.clear();
	return AddMethod::Ok;
}

void remove_method(GenericFunction *g, const SmallVector<Class *, 4> &sig, uint64_t ref_mask)
{
	const int argc = static_cast<int>(sig.size());
	for (intptr_t i = 0; i < g->methods.size(); ++i)
	{
		Method &e = g->methods[i];
		if (e.arity() != argc || e.ref_mask != ref_mask)
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

		// Recompute the arity bounds; a stale-low min_arity would merely cost a
		// fruitless full_resolve, but keeping them tight is cheap and clean.
		g->min_arity = 255;
		g->max_arity = 0;
		for (intptr_t k = 0; k < g->methods.size(); ++k)
		{
			int ar = g->methods[k].arity();
			if (ar < g->min_arity)
				g->min_arity = static_cast<uint8_t>(ar);
			if (ar > g->max_arity)
				g->max_arity = static_cast<uint8_t>(ar);
		}
		return;
	}
}

Method *resolve(GenericFunction *g, const Value *args, int argc)
{
	PHON_ASSERT_MSG(argc <= 8, "dispatch arity > 8 not supported");
	if (argc < g->min_arity || argc > g->max_arity)
		return nullptr;

	// Invalidate the memo on a method-set or type-hierarchy change.
	if (g->memo_generic_epoch != g->generic_epoch || g->memo_type_epoch != type_epoch())
	{
		g->memo.clear();
		g->memo_generic_epoch = g->generic_epoch;
		g->memo_type_epoch = type_epoch();
	}

	uint32_t arg_class[8];
	uint64_t arg_ref_mask = 0;
	for (int i = 0; i < argc; ++i)
	{
		bool ref;
		arg_class[i] = dispatch_class(args[i], ref);
		if (ref)
			arg_ref_mask |= (uint64_t(1) << i);
	}

	// Fast memo for arity 1 and 2 (exact, collision-free keys).
	if (argc == 1 || argc == 2)
	{
		uint64_t key = encode_arg(arg_class[0], arg_ref_mask & 1);
		if (argc == 2)
			key = (key << 25) | encode_arg(arg_class[1], (arg_ref_mask >> 1) & 1);
		auto it = g->memo.find(key);
		int32_t idx = (it != g->memo.end()) ? it->second
		                                    : full_resolve(g, arg_class, arg_ref_mask, argc);
		if (it == g->memo.end())
			g->memo.insert(key, idx);
		return idx < 0 ? nullptr : &g->methods[idx];
	}

	// Arity 0 and 3..8: resolve fully (no memo).
	int32_t idx = full_resolve(g, arg_class, arg_ref_mask, argc);
	return idx < 0 ? nullptr : &g->methods[idx];
}

void generic_registry_shutdown()
{
	GenericRegistry &r = registry();
	for (intptr_t i = 0; i < r.owned.size(); ++i)
		delete r.owned[i];
	r.owned.clear();
	r.by_name = FlatHashMap<uint32_t, GenericFunction *>();
}

} // namespace phonometrica
