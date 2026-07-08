// Phonometrica engine — spawn: run a function on a fresh Isolate + OS thread (§13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// `spawn f(args…)` launches `f` with transferred copies of its arguments on a brand-new
// Isolate (fresh heap, its own collector) running on a dedicated OS thread. The worker is
// asynchronous: the spawning Isolate owns the returned thread handle and joins every
// worker when it is torn down (structured concurrency). Workers coordinate with the rest
// of the program through channels (the design sample), which is the intended pattern.
//
// The SPAWN opcode calls `vm_spawn` (declared in vm/interpreter.hpp as a seam and defined
// here, mirroring the cc_* collector seams) — vm sits below concurrency, so the
// dependency is inverted through that declaration. `wait(handle)` joins a worker and
// re-raises an error it died with; it is registered for completeness (join-only
// semantics), though the statement form of `spawn` does not yet surface the handle to
// scripts (see DEVIATIONS).

#ifndef PHON_CONCURRENCY_SPAWN_HPP
#define PHON_CONCURRENCY_SPAWN_HPP

#include <phon/core/value.hpp>

namespace phonometrica {

class Isolate;
struct Class;

// Register the Thread reference class (idempotent; called by init_runtime()).
void register_thread_class();
Class *thread_class() noexcept;

// wait(handle): block until the worker finishes; re-raise (join-only) an error it died
// with. Returns null.
Value builtin_wait(Isolate &iso, Value *args, int argc);

} // namespace phonometrica

#endif // PHON_CONCURRENCY_SPAWN_HPP
