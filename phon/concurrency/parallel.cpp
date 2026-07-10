// Phonometrica engine — parallel_map implementation (§13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/concurrency/parallel.hpp>

#include <phon/concurrency/thread_pool.hpp>
#include <phon/concurrency/transfer.hpp>
#include <phon/core/cell.hpp>
#include <phon/dispatch/generic.hpp> // for_each_generic, dispatch_enter/exit_thread
#include <phon/object/class.hpp>
#include <phon/types/array.hpp>
#include <phon/types/list.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/function.hpp>
#include <phon/vm/interpreter.hpp>
#include <phon/vm/isolate.hpp>
#include <phon/vm/proto.hpp>

#include <string>
#include <thread>
#include <vector>

namespace phonometrica {

namespace {

// --- freeze every constant a concurrently-run function might touch -----------------
//
// A non-frozen cell's refcount is a relaxed load+store (thread-confined by design), so
// many workers loading the same constant would lose updates and free it early. Freezing
// flips those cells to the atomic-refcount (SHARED_BUFFER) regime. Constants are
// immutable, so freezing is semantically free; it is done here on the main thread before
// any worker starts (mark_frozen_shared's precondition).

void freeze_constant(Value v)
{
	if (!v.is_cell())
		return; // immediates (numbers, bool, null, symbols) carry no cell
	Cell *c = v.as_cell();
	if (c->is_shared_buffer())
		return; // already frozen
	switch (c->class_id())
	{
	case CID_STRING:
		String::from_value(v).make_frozen(); // also builds the lazy caches eagerly
		break;
	case CID_ARRAY:
		Array::from_value(v).make_frozen();
		break;
	default:
		mark_frozen_shared(c); // class objects and any other cell-valued constant
		break;
	}
}

void freeze_proto_tree(Proto *p)
{
	for (intptr_t i = 0; i < p->constants.size(); ++i)
		freeze_constant(p->constants[i].value());
	for (intptr_t i = 0; i < p->children.size(); ++i)
		freeze_proto_tree(p->children[i].get());
}

// A method's `code` is an executable body only when it is a real heap cell (a ClosureCell
// or NativeCell). It is otherwise an opaque callable identity: dispatch-only test generics
// register small integer sentinels as code. Guard against dereferencing such a non-cell
// before asking is_closure. A real cell is heap-allocated — well above the zero page and
// pointer-aligned — so no genuine closure is ever filtered out.
bool is_cell_code(void *code) noexcept
{
	auto p = reinterpret_cast<uintptr_t>(code);
	return p >= 4096 && (p & (alignof(void *) - 1)) == 0;
}

void freeze_generic_constants(GenericFunction *g, void *)
{
	for (intptr_t i = 0; i < g->methods.size(); ++i)
	{
		void *code = g->methods[i].code;
		if (!is_cell_code(code))
			continue;
		Value cv = Value::make_cell(reinterpret_cast<Cell *>(code));
		if (is_closure(cv)) // native methods have no proto to walk
			freeze_proto_tree(reinterpret_cast<ClosureCell *>(code)->proto);
	}
}

// --- workers -----------------------------------------------------------------------

struct MapWorker
{
	Value callee = Value::make_null();
	Value *inputs = nullptr;  // shared; this worker owns [lo, hi)
	Value *results = nullptr; // shared; this worker writes [lo, hi)
	intptr_t lo = 0;
	intptr_t hi = 0;
	bool errored = false;
	std::string error_msg;
};

// Process one contiguous chunk on a fresh Isolate. Each input carries a +1 handed over by
// the spawner (released here); each result is transferred out of this worker's heap so it
// survives the Isolate's destruction (collector-independent, like a channel send).
void map_worker_main(MapWorker *w)
{
	Isolate iso;
	set_current_isolate(&iso);
	set_current_collector(&iso.collector());
	dispatch_enter_thread(); // bypass the single-threaded dispatch memo while we run
	try
	{
		for (intptr_t i = w->lo; i < w->hi; ++i)
		{
			Value r = run_callable(iso, w->callee, &w->inputs[i], 1);
			w->results[i] = transfer_across_threads(iso, r);
			if (r.is_cell())
				release(r.as_cell());
		}
	}
	catch (RuntimeError &e)
	{
		w->errored = true;
		w->error_msg.assign(e.message.data(), static_cast<size_t>(e.message.size()));
		if (e.error.is_cell())
			release(e.error.as_cell());
		iso.unwind_on_error();
	}
	catch (...)
	{
		w->errored = true;
		w->error_msg = "[Runtime error] unknown error in parallel_map worker";
	}
	// Release the inputs this worker was handed (main transferred each a +1).
	for (intptr_t i = w->lo; i < w->hi; ++i)
		if (w->inputs[i].is_cell())
			release(w->inputs[i].as_cell());
	dispatch_exit_thread();
	set_current_collector(nullptr);
	set_current_isolate(nullptr);
	// iso destructs here; the transferred results are independent of its heap.
}

} // namespace

// Freeze the constants of every script generic method plus the callee's own proto tree.
// A non-capturing callee can only reach: builtins (natives, no constants), other global
// generics (covered by the registry walk), and functions nested in its own proto tree —
// so this coverage is complete.
void freeze_reachable_constants(Value callee)
{
	for_each_generic(&freeze_generic_constants, nullptr);
	if (is_closure(callee))
		freeze_proto_tree(reinterpret_cast<ClosureCell *>(callee.as_cell())->proto);
}

List vm_parallel_map(Isolate &main_iso, Value callee, const List &input, int line)
{
	// Only a spawnable callable may run on a worker (same rule as `spawn`): a capturing
	// closure would share mutable upvalue boxes across threads.
	if (is_closure(callee))
	{
		auto *cl = reinterpret_cast<ClosureCell *>(callee.as_cell());
		if (cl->nupvals != 0)
			main_iso.raise(
			    String("[Type error] a parallel_map function cannot capture variables"), line);
	}
	else if (!is_native(callee))
	{
		main_iso.raise(String("[Type error] parallel_map expects a function as its second argument"),
		               line);
	}

	intptr_t n = input.size();
	if (n == 0)
		return List(0);

	// Make every constant the workers might touch cross-thread-safe.
	freeze_reachable_constants(callee);

	// Transfer each input into an independent copy on this (the main) thread. Raises on a
	// non-sendable element (a reference type); undo the copies already built first.
	Value *inputs = new Value[static_cast<size_t>(n)];
	Value *results = new Value[static_cast<size_t>(n)];
	for (intptr_t i = 0; i < n; ++i)
	{
		inputs[i] = Value::make_null();
		results[i] = Value::make_null();
	}
	intptr_t built = 0;
	try
	{
		for (intptr_t i = 0; i < n; ++i)
		{
			inputs[i] = transfer_across_threads(main_iso, input.get(i + 1).value());
			++built;
		}
	}
	catch (...)
	{
		for (intptr_t i = 0; i < built; ++i)
			if (inputs[i].is_cell())
				release(inputs[i].as_cell());
		delete[] inputs;
		delete[] results;
		throw;
	}

	// Partition [0, n) into P contiguous chunks, one worker thread each (P bounded by the
	// pool size and by n). The first `rem` chunks get one extra element.
	int hw = global_thread_pool().worker_count() + 1;
	intptr_t P = (static_cast<intptr_t>(hw) < n) ? static_cast<intptr_t>(hw) : n;
	if (P < 1)
		P = 1;

	std::vector<MapWorker> workers(static_cast<size_t>(P));
	std::vector<std::thread> threads;
	threads.reserve(static_cast<size_t>(P));
	intptr_t chunk = n / P;
	intptr_t rem = n % P;
	intptr_t base = 0;
	for (intptr_t j = 0; j < P; ++j)
	{
		intptr_t len = chunk + (j < rem ? 1 : 0);
		MapWorker &w = workers[static_cast<size_t>(j)];
		w.callee = callee;
		w.inputs = inputs;
		w.results = results;
		w.lo = base;
		w.hi = base + len;
		base += len;
		threads.emplace_back(map_worker_main, &w);
	}
	for (auto &t : threads)
		t.join();

	// The workers released the inputs; free the backing array.
	delete[] inputs;

	// Surface the first worker error, releasing the partial results first.
	for (intptr_t j = 0; j < P; ++j)
	{
		if (workers[static_cast<size_t>(j)].errored)
		{
			String msg(workers[static_cast<size_t>(j)].error_msg);
			for (intptr_t i = 0; i < n; ++i)
				if (results[i].is_cell())
					release(results[i].as_cell());
			delete[] results;
			main_iso.raise(msg, line); // [[noreturn]]
		}
	}

	// Stitch the results into a List, moving each +1 into its slot.
	List out(n);
	Value *slots = out.writable_slots();
	for (intptr_t i = 0; i < n; ++i)
		slots[i] = results[i];
	delete[] results;
	return out;
}

} // namespace phonometrica
