// Phonometrica engine — Channel: the script-visible cross-thread queue (§13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A Channel is a reference-class value carrying a mutex + condition variables + a FIFO
// of transferred values. `send` runs the transfer walk (§8.3) on the sender BEFORE
// enqueuing, so the receiving thread only ever touches values reconstructed into its
// own graph — never the sender's live objects. `Channel()` is unbounded; `Channel(n)`
// bounds the queue to n items and blocks the sender when full. `receive` blocks until an
// item is available.
//
// The channel cell itself is shared across threads, so it is born in the SHARED_BUFFER
// regime (atomic refcounts, §8.3). It is registered as a plain (non-builtin) ref class
// so the compiler's name resolver does not treat `Channel(...)` as a class-object
// construction — the name resolves to the `Channel` builtin generic instead. (A
// consequence: `x is Channel` is not yet available; see DEVIATIONS.)

#ifndef PHON_CONCURRENCY_CHANNEL_HPP
#define PHON_CONCURRENCY_CHANNEL_HPP

#include <phon/core/value.hpp>
#include <phon/core/variant.hpp>

namespace phonometrica {

class Isolate;
struct NativeCell;
struct Class;

// Register the Channel reference class (idempotent; called by init_runtime()).
void register_channel_class();
Class *channel_class() noexcept;

// Builtins (registered by register_builtins()): Channel()/Channel(n), send(ch, value),
// receive(ch). Each returns a value carrying +1 (receive) or null (send).
Value builtin_channel(Isolate &iso, NativeCell *, Value *args, int argc);
Value builtin_send(Isolate &iso, NativeCell *, Value *args, int argc);
Value builtin_receive(Isolate &iso, NativeCell *, Value *args, int argc);

// --- C++-side channel access for embedders (design §11.4) ---------------------
//
// A GUI thread receives results a worker script pushed onto a Channel. The queued
// values were already transferred into a standalone graph, so these need no Isolate
// and are safe to call from any thread (e.g. a Qt event-loop slot).

// Is `v` a Channel?
bool is_channel(Value v) noexcept;

// Block until an item is available, returning it (the Variant owns the +1). Precondition:
// `channel` is a Channel (is_channel).
Variant channel_receive(Value channel);

// Event-loop polling: wait at most `timeout_seconds` for an item. Returns true and moves
// the item into `out` if one arrived, else false (leaving `out` unchanged). A
// non-positive timeout polls without blocking.
bool channel_try_receive(Value channel, double timeout_seconds, Variant &out);

} // namespace phonometrica

#endif // PHON_CONCURRENCY_CHANNEL_HPP
