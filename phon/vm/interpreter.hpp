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

#include <phon/core/value.hpp>

namespace phonometrica {

class Isolate;
struct ClosureCell;

// Execute `main` (a closure over a module Proto) to completion on `iso`, returning
// the module result with one reference the caller adopts. Throws RuntimeError on a
// script-level error (division by zero, bad type, arity mismatch, …).
Value vm_execute(Isolate &iso, ClosureCell *main);

} // namespace phonometrica

#endif // PHON_VM_INTERPRETER_HPP
