// Phonometrica engine — spawn implementation (§13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/concurrency/spawn.hpp>

#include <phon/concurrency/transfer.hpp>
#include <phon/core/cell.hpp>
#include <phon/dispatch/generic.hpp> // dispatch_enter_thread/exit (memo concurrency guard)
#include <phon/memory/cycle_collector.hpp>
#include <phon/object/class.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/function.hpp>
#include <phon/vm/interpreter.hpp>
#include <phon/vm/isolate.hpp>

#include <cstdio>
#include <string>
#include <thread>

namespace phonometrica {

namespace {

// The worker thread and its result. `callee`/`error_msg`/`errored` are written by the
// worker before it exits and read by the joiner after join(), which provides the
// happens-before edge — no atomics needed. `callee` carries a +1 taken on the spawner and
// released here at finalize (also on the spawner), so the closure is never refcounted
// across threads; the worker only *reads* it.
struct ThreadState
{
	std::thread thr;
	Value callee = Value::make_null();
	bool joined = false;
	bool errored = false;
	std::string error_msg;
};

struct ThreadCell
{
	Cell header;
	ThreadState *state;
};

Class *g_thread = nullptr;

PHON_FORCE_INLINE ThreadState *state_of(Cell *c) noexcept
{
	return reinterpret_cast<ThreadCell *>(c)->state;
}

bool is_thread_handle(Value v) noexcept
{
	return v.is_cell() && g_thread && v.as_cell()->class_id() == g_thread->id;
}

void thread_finalize(Cell *c)
{
	ThreadState *st = state_of(c);
	// Structured concurrency: block until the worker finishes if nobody waited on it.
	if (st->thr.joinable())
		st->thr.join();
	// A worker that died with an uncaught error and was never wait()ed on would otherwise
	// fail silently; surface it on stderr so the failure is at least visible. (Once the
	// handle reaches scripts via an expression-form spawn, wait() re-raises it instead.)
	if (st->errored && !st->joined)
		std::fprintf(stderr, "[spawn] uncaught error in worker: %s\n", st->error_msg.c_str());
	// Release the +1 on the callee (taken on the spawner in vm_spawn); this runs on the
	// spawner too — the handle is confined to it — so the closure's refcount stays
	// single-threaded.
	if (st->callee.is_cell())
		release(st->callee.as_cell());
	delete st;
}

// The worker thread's entry: a fresh Isolate runs `callee`(args) to completion. `args`
// are the transferred copies (each +1), owned and released here.
void worker_main(Value callee, Value *args, int nargs, ThreadState *st)
{
	Isolate iso;
	set_current_isolate(&iso);
	set_current_collector(&iso.collector());
	// Mark dispatch as multi-threaded for this worker's lifetime, so the shared per-
	// generic memo is bypassed while more than the main thread may dispatch (§13).
	dispatch_enter_thread();
	try
	{
		Value r = run_callable(iso, callee, args, nargs);
		if (r.is_cell())
			release(r.as_cell());
	}
	catch (RuntimeError &e)
	{
		st->errored = true;
		st->error_msg.assign(e.message.data(), static_cast<size_t>(e.message.size()));
		if (e.error.is_cell())
			release(e.error.as_cell());
		iso.unwind_on_error();
	}
	catch (...)
	{
		st->errored = true;
		st->error_msg = "[Runtime error] unknown error in spawned thread";
	}
	for (int i = 0; i < nargs; ++i)
		if (args[i].is_cell())
			release(args[i].as_cell());
	delete[] args;
	dispatch_exit_thread();
	set_current_collector(nullptr);
	set_current_isolate(nullptr);
	// iso destructs here: releases its own heap, joins any threads *it* spawned.
}

} // namespace

void register_thread_class()
{
	if (g_thread)
		return;
	// Not CLASS_BUILTIN (invisible to the name resolver; created only by SPAWN, never
	// `Thread(...)`). Acyclic: a handle is confined to the spawner and holds no script
	// values, so it never joins a reference cycle.
	g_thread = add_class("Thread", get_class(CID_OBJECT), CLASS_REF | CLASS_ACYCLIC);
	g_thread->finalize = &thread_finalize;
}

Class *thread_class() noexcept { return g_thread; }

Value builtin_wait(Isolate &iso, NativeCell *, Value *args, int argc)
{
	(void) argc;
	if (!is_thread_handle(args[0]))
		iso.raise(String("[Type error] 'wait' expects a thread handle"), 0);
	ThreadState *st = state_of(args[0].as_cell());
	if (!st->joined)
	{
		st->thr.join();
		st->joined = true;
	}
	if (st->errored)
		iso.raise(String(st->error_msg), 0); // re-raise the worker's error on the waiter
	return Value::make_null();
}

Cell *vm_spawn(Isolate &iso, Value callee, Value *args, int nargs, int line)
{
	// A capturing closure would share its upvalue boxes (mutable, non-atomic) across
	// threads; only a top-level function (no upvalues) or a native may be spawned.
	if (is_closure(callee))
	{
		auto *cl = reinterpret_cast<ClosureCell *>(callee.as_cell());
		if (cl->nupvals != 0)
			iso.raise(String("[Type error] cannot spawn a function that captures variables"), line);
	}
	else if (!is_native(callee))
	{
		iso.raise(String("[Type error] spawn target is not callable"), line);
	}

	// Transfer the arguments on THIS (the spawner's) thread. May raise on a non-sendable
	// payload; clean up whatever was already transferred before propagating.
	Value *targs = nargs > 0 ? new Value[static_cast<size_t>(nargs)] : nullptr;
	int built = 0;
	try
	{
		for (int i = 0; i < nargs; ++i)
		{
			targs[i] = transfer_across_threads(iso, args[i]);
			++built;
		}
	}
	catch (...)
	{
		for (int i = 0; i < built; ++i)
			if (targs[i].is_cell())
				release(targs[i].as_cell());
		delete[] targs;
		throw;
	}

	// Keep the callee alive for the worker without cross-thread refcounting: a +1 taken
	// here (spawner) and released at finalize (spawner); the worker only reads it.
	if (callee.is_cell())
		retain(callee.as_cell());

	auto *st = new ThreadState();
	st->callee = callee;

	Cell *c = cell_alloc(g_thread->id, static_cast<intptr_t>(sizeof(ThreadCell)));
	reinterpret_cast<ThreadCell *>(c)->state = st;

	st->thr = std::thread(worker_main, callee, targs, nargs, st);
	return c; // +1
}

} // namespace phonometrica
