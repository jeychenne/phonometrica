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

namespace phonometrica {

class Isolate;
struct Class;

// Register the Channel reference class (idempotent; called by init_runtime()).
void register_channel_class();
Class *channel_class() noexcept;

// Builtins (registered by register_builtins()): Channel()/Channel(n), send(ch, value),
// receive(ch). Each returns a value carrying +1 (receive) or null (send).
Value builtin_channel(Isolate &iso, Value *args, int argc);
Value builtin_send(Isolate &iso, Value *args, int argc);
Value builtin_receive(Isolate &iso, Value *args, int argc);

} // namespace phonometrica

#endif // PHON_CONCURRENCY_CHANNEL_HPP
