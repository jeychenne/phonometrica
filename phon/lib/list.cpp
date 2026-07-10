// Phonometrica engine — list standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Ported from the old engine's func_list.hpp against the typed registration API.
// Queries take the list by `const List &`; the old engine's REF-marked mutators
// (append/prepend/insert/remove_at/clear/pop/shift/reverse) take it by `List &` and
// write back. `pop`/`shift` show a ref parameter and a value return together. Elements
// are untyped, so element parameters are `Variant` (dispatch class Object). Positions
// are 1-based. Ordering-dependent functions (sort, sorted_insert, sample, shuffle, the
// set operations) are deferred — they need a value-ordering primitive the engine does
// not expose yet (see DEVIATIONS).

#include <phon/concurrency/parallel.hpp> // vm_parallel_map
#include <phon/lib/lib.hpp>
#include <phon/object/value_ops.hpp> // value_equals / value_compare
#include <phon/runtime/native_traits.hpp>
#include <phon/types/list.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/interpreter.hpp> // stringify (for join)
#include <phon/vm/isolate.hpp>     // Isolate::raise

#include <algorithm>
#include <random>
#include <vector>

namespace phonometrica {

namespace {

intptr_t last_index_of(const List &xs, const Variant &v)
{
	for (intptr_t i = xs.size(); i >= 1; --i)
		if (value_equals(xs.get(i).value(), v.value()))
			return i;
	return 0;
}

// A shared, thread-local generator for sample/shuffle (never races across threads).
std::mt19937_64 &rng()
{
	static thread_local std::mt19937_64 g(std::random_device{}());
	return g;
}

// Three-way compare that raises a clear error on an unorderable pair (used by the
// sorted-list functions). `what` names the operation for the message.
int ordered(Isolate &iso, const Variant &a, const Variant &b, const char *what)
{
	int c;
	if (!value_compare(a.value(), b.value(), c))
		iso.raise(String("[Type error] '") + what + "' requires ordered values", 0);
	return c;
}

} // namespace

void register_list_lib()
{
	// --- queries and derivations ---
	register_function("contains", [](const List &xs, Variant v) { return xs.contains(v); });
	register_function("find", [](const List &xs, Variant v) { return xs.index_of(v); });
	register_function("is_empty", [](const List &xs) { return xs.empty(); });
	register_function("first", [](Isolate &iso, const List &xs) {
		if (xs.empty())
			iso.raise(String("[Value error] 'first' of an empty List"), 0);
		return xs.get(1);
	});
	register_function("last", [](Isolate &iso, const List &xs) {
		if (xs.empty())
			iso.raise(String("[Value error] 'last' of an empty List"), 0);
		return xs.get(xs.size());
	});
	register_function("left", [](const List &xs, int64_t n) {
		List out;
		intptr_t m = n < xs.size() ? static_cast<intptr_t>(n) : xs.size();
		for (intptr_t i = 1; i <= m; ++i)
			out.append(xs.get(i));
		return out;
	});
	register_function("right", [](const List &xs, int64_t n) {
		List out;
		intptr_t start = xs.size() - static_cast<intptr_t>(n) + 1;
		if (start < 1)
			start = 1;
		for (intptr_t i = start; i <= xs.size(); ++i)
			out.append(xs.get(i));
		return out;
	});
	register_function("join", [](const List &xs, const String &sep) {
		String out;
		for (intptr_t i = 1; i <= xs.size(); ++i)
		{
			if (i > 1)
				out.append(sep);
			out.append(stringify(xs.get(i).value()));
		}
		return out;
	});

	// --- in-place mutators (the `List &` parameter writes back) ---
	register_function("append", [](List &xs, Variant v) { xs.append(v); });
	register_function("prepend", [](List &xs, Variant v) { xs.prepend(v); });
	register_function("insert", [](List &xs, int64_t i, Variant v) { xs.insert(i, v); });
	register_function("remove_at", [](List &xs, int64_t i) { xs.remove_at(i); });
	register_function("clear", [](List &xs) { xs.clear(); });
	register_function("pop", [](Isolate &iso, List &xs) {
		if (xs.empty())
			iso.raise(String("[Value error] 'pop' of an empty List"), 0);
		return xs.pop();
	});
	register_function("shift", [](Isolate &iso, List &xs) {
		if (xs.empty())
			iso.raise(String("[Value error] 'shift' of an empty List"), 0);
		Variant v = xs.get(1);
		xs.remove_at(1);
		return v;
	});
	register_function("reverse", [](List &xs) {
		List out;
		for (intptr_t i = xs.size(); i >= 1; --i)
			out.append(xs.get(i));
		xs = out;
	});
	register_function("find_back", [](const List &xs, Variant v) { return last_index_of(xs, v); });
	register_function("remove", [](List &xs, Variant v) {
		List out;
		for (intptr_t i = 1; i <= xs.size(); ++i)
		{
			Variant e = xs.get(i);
			if (!value_equals(e.value(), v.value()))
				out.append(e);
		}
		xs = out;
	});
	register_function("remove_first", [](List &xs, Variant v) {
		intptr_t i = xs.index_of(v);
		if (i > 0)
			xs.remove_at(i);
	});
	register_function("remove_last", [](List &xs, Variant v) {
		intptr_t i = last_index_of(xs, v);
		if (i > 0)
			xs.remove_at(i);
	});

	// --- ordering (via value_compare) ---
	register_function("sort", [](Isolate &iso, List &xs) {
		std::vector<Variant> v;
		v.reserve(static_cast<size_t>(xs.size()));
		for (intptr_t i = 1; i <= xs.size(); ++i)
			v.push_back(xs.get(i));
		std::stable_sort(v.begin(), v.end(), [&](const Variant &a, const Variant &b) {
			return ordered(iso, a, b, "sort") < 0;
		});
		List out;
		for (auto &e : v)
			out.append(e);
		xs = out;
	});
	register_function("is_sorted", [](Isolate &iso, const List &xs) {
		for (intptr_t i = 2; i <= xs.size(); ++i)
			if (ordered(iso, xs.get(i - 1), xs.get(i), "is_sorted") > 0)
				return false;
		return true;
	});
	register_function("sorted_find", [](Isolate &iso, const List &xs, Variant key) {
		intptr_t lo = 1, hi = xs.size();
		while (lo <= hi)
		{
			intptr_t mid = lo + (hi - lo) / 2;
			int c = ordered(iso, xs.get(mid), key, "sorted_find");
			if (c == 0)
				return mid;
			if (c < 0)
				lo = mid + 1;
			else
				hi = mid - 1;
		}
		return intptr_t(0);
	});
	register_function("sorted_insert", [](Isolate &iso, List &xs, Variant v) {
		// lower_bound: first position whose element is >= v; insert there (append if none).
		intptr_t lo = 1, hi = xs.size(), pos = xs.size() + 1;
		while (lo <= hi)
		{
			intptr_t mid = lo + (hi - lo) / 2;
			if (ordered(iso, xs.get(mid), v, "sorted_insert") >= 0)
			{
				pos = mid;
				hi = mid - 1;
			}
			else
				lo = mid + 1;
		}
		if (pos > xs.size())
			xs.append(v);
		else
			xs.insert(pos, v);
	});

	// --- random (thread-local generator) ---
	register_function("shuffle", [](List &xs) {
		std::vector<Variant> v;
		v.reserve(static_cast<size_t>(xs.size()));
		for (intptr_t i = 1; i <= xs.size(); ++i)
			v.push_back(xs.get(i));
		std::shuffle(v.begin(), v.end(), rng());
		List out;
		for (auto &e : v)
			out.append(e);
		xs = out;
	});
	register_function("sample", [](Isolate &iso, const List &xs, int64_t k) {
		if (k < 0 || k > xs.size())
			iso.raise(String("[Value error] 'sample' size is out of range"), 0);
		std::vector<intptr_t> idx(static_cast<size_t>(xs.size()));
		for (intptr_t i = 0; i < xs.size(); ++i)
			idx[static_cast<size_t>(i)] = i + 1;
		// Partial Fisher–Yates: the first k slots become a uniform sample without replacement.
		for (intptr_t i = 0; i < k; ++i)
		{
			std::uniform_int_distribution<intptr_t> pick(i, xs.size() - 1);
			std::swap(idx[static_cast<size_t>(i)], idx[static_cast<size_t>(pick(rng()))]);
		}
		List out;
		for (intptr_t i = 0; i < k; ++i)
			out.append(xs.get(idx[static_cast<size_t>(i)]));
		return out;
	});

	// --- set operations (by value equality, order-preserving, deduplicated) ---
	register_function("unite", [](const List &a, const List &b) {
		List out;
		for (intptr_t i = 1; i <= a.size(); ++i)
			if (!out.contains(a.get(i)))
				out.append(a.get(i));
		for (intptr_t i = 1; i <= b.size(); ++i)
			if (!out.contains(b.get(i)))
				out.append(b.get(i));
		return out;
	});
	register_function("intersect", [](const List &a, const List &b) {
		List out;
		for (intptr_t i = 1; i <= a.size(); ++i)
		{
			Variant e = a.get(i);
			if (b.contains(e) && !out.contains(e))
				out.append(e);
		}
		return out;
	});
	register_function("subtract", [](const List &a, const List &b) {
		List out;
		for (intptr_t i = 1; i <= a.size(); ++i)
		{
			Variant e = a.get(i);
			if (!b.contains(e) && !out.contains(e))
				out.append(e);
		}
		return out;
	});

	// parallel_map(list, fn): apply `fn` to each element on the runtime thread pool,
	// returning a new list of results in order (concurrency §13). `fn` must be a
	// top-level function or non-capturing lambda (a spawn-style target).
	register_function("parallel_map", [](Isolate &iso, const List &xs, Value fn) {
		return vm_parallel_map(iso, fn, xs, 0);
	});
}

} // namespace phonometrica
