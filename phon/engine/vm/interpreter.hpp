// Phonometrica engine — the bytecode interpreter (architecture §10.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A register machine over the Isolate's stack. M4 uses a switch-dispatch loop
// (computed-goto threading is an M8 performance concern, recorded in DEVIATIONS);
// each opcode body is a self-contained unit so the door to a copy-and-patch JIT
// stays open. Register reference counting is manual (design §3.1): every write
// releases the previous occupant and retains the new one; frames release their
// registers on return. Refcount elision on borrowed locals is an M8 optimization.

#ifndef PHON_VM_INTERPRETER_HPP
#define PHON_VM_INTERPRETER_HPP

#include <phon/engine/core/value.hpp>
#include <phon/engine/types/string.hpp>

namespace phonometrica {

class Isolate;
struct ClosureCell;

// Execute `main` (a closure over a module Proto) to completion on `iso`, returning
// the module result with one reference the caller adopts. Throws RuntimeError on a
// script-level error (division by zero, bad type, arity mismatch, …).
Value execute(Isolate &iso, ClosureCell *main);

// Run a callable (closure or native) with `argc` arguments to completion on a *fresh*
// Isolate that has no frames yet — the entry point a spawned worker thread uses to run
// its target function (architecture §13). Sets up a root frame at the base of the stack.
// The args are borrowed (retained into the frame as needed); the result carries +1.
// Throws RuntimeError on an uncaught worker error.
Value run_callable(Isolate &iso, Value callee, Value *args, int argc);

// Spawn seam (architecture §1: concurrency provides thread entry, vm calls it). Launches
// `callee`(transferred `args`) on a fresh Isolate + OS thread and returns a thread-handle
// cell (+1) joinable via `wait`. Declared here so the SPAWN opcode can call it; DEFINED in
// concurrency/spawn.cpp (like the cc_* collector seams). Raises through `iso` if `callee`
// captures upvalues or an argument is not sendable.
Cell *vm_spawn(Isolate &iso, Value callee, Value *args, int nargs, int line);

// Stringify a value the way `&`, print, and interpolation do. The Isolate overload
// dispatches a user-class `to_string` method (design §12); the plain overload is the
// builtin representation only (no script dispatch), for contexts without an Isolate.
String stringify(Value v);
String stringify(Isolate &iso, Value v);

} // namespace phonometrica

#endif // PHON_VM_INTERPRETER_HPP
