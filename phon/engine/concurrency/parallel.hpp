// Phonometrica engine — data-parallel map over the runtime thread pool (§13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// `parallel_map(list, fn)` applies `fn` to every element concurrently and returns a new
// List of the results, in order. Each participating worker runs on its own scratch
// Isolate (fresh heap); inputs are transferred to the worker (independent copies) and
// results transferred back, exactly like `spawn`/channels — so no cell is touched by two
// threads. The one extra subtlety over spawn: the same `fn` runs on *many* workers at
// once, so its shared constants (string/array literals, class objects — reached via
// LOADK) are frozen up front, flipping their refcount to the atomic path (a non-frozen
// cell's refcount is a relaxed load+store, which loses updates under contention). `fn`
// must be a top-level function or a non-capturing lambda (like a spawn target).

#ifndef PHON_CONCURRENCY_PARALLEL_HPP
#define PHON_CONCURRENCY_PARALLEL_HPP

#include <phon/engine/core/value.hpp>

namespace phonometrica {

class Isolate;
class List;

// Freeze every constant that `callee` (a spawn / parallel_map target) could touch once it
// runs on a worker thread: its own proto tree plus every script generic method's proto
// tree. Freezing flips those cells to the atomic-refcount (SHARED_BUFFER) regime, so many
// threads can LOADK-retain them without losing updates (a non-frozen cell's refcount is a
// relaxed load+store — thread-confined by design). Constants are immutable, so this is
// semantically free; it must run on the spawner thread before any worker starts. Both
// `spawn` and `parallel_map` call this — otherwise concurrent workers corrupt the shared
// constants' refcounts (a use-after-free ASan catches but TSan does not).
void freeze_reachable_constants(Value callee);

// Apply `callee` to each element of `input` on worker threads, returning a List of the
// results in the original order. Raises (on `iso`) if `callee` is not a spawnable
// function, or re-raises the first worker error. `line` is the call site for errors.
List vm_parallel_map(Isolate &iso, Value callee, const List &input, int line);

} // namespace phonometrica

#endif // PHON_CONCURRENCY_PARALLEL_HPP
