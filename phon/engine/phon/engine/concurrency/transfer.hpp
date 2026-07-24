// Phonometrica engine — the cross-thread transfer walk (architecture §8.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A value crosses between script threads (channel send, spawn arguments) only after
// being reconstructed into a self-contained graph the receiver can own without ever
// touching the sender's heap:
//
//   * immediates (Int, Float, Bool, Null, Symbol) copy trivially — no cell;
//   * a frozen String or frozen NumArray buffer is *shared* (atomic retain), zero-copy;
//   * Lists, Tables, Sets, unfrozen Strings/Arrays and value-class instances are
//     deep-copied, with a seen-map so in-graph sharing (a DAG of value objects) is
//     preserved and pathological duplication is bounded;
//   * reference-type values — ref-class instances, functions, class objects — are not
//     sendable and raise a script error (design §8.3). A later stage lets specific ref
//     classes opt in (Channel shares itself).
//
// The walk lives in `concurrency/` (above `types`/`object`/`vm`) so it may see every
// concrete type and raise through the Isolate, without those lower layers depending on
// it.

#ifndef PHON_CONCURRENCY_TRANSFER_HPP
#define PHON_CONCURRENCY_TRANSFER_HPP

#include <phon/engine/core/value.hpp>

namespace phonometrica {

class Isolate;

// Deep-copy `v` into a fresh graph safe to hand to another thread (§8.3). Returns a
// Value carrying one reference (+1). Raises a `[Type error]` through `iso` if `v` (or
// anything reachable from it) is a reference-type value that cannot be sent.
Value transfer_across_threads(Isolate &iso, Value v);

} // namespace phonometrica

#endif // PHON_CONCURRENCY_TRANSFER_HPP
