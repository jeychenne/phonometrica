// Phonometrica engine — the embedding entry point for running scripts (M4).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A thin façade over the compile→execute pipeline: parse a script, lower it to
// bytecode, and run it on a fresh Isolate. This is the M4 stand-in for the full
// `Runtime` of architecture §11 (module search paths, imports, and the typed
// registration API arrive in M8). `do_string` returns the value of the script's
// trailing expression (REPL-style), so tests and the eventual console read results
// back directly. Compile errors surface as SyntaxError; runtime errors as
// RuntimeError (both carry a source line).

#ifndef PHON_RUNTIME_VM_HPP
#define PHON_RUNTIME_VM_HPP

#include <phon/core/value.hpp>
#include <phon/core/variant.hpp>
#include <phon/vm/isolate.hpp> // RuntimeError

#include <string>

namespace phonometrica {

// Register builtin classes, the callable classes, and the builtin generic library
// (print, len, …). Idempotent; safe to call from any entry point.
void vm_boot();

// Compile and run `src`, returning the value of its trailing expression (or null).
Variant do_string(const std::string &src);

// The stringification the runtime uses for `&`, print, and interpolation (the
// to_string generic; user overloads arrive in M5). Defined in vm/interpreter.cpp.
class String;
String vm_to_string(Value v);

// Compile `src` and return its disassembly (for golden tests / debugging).
std::string disassemble_source(const std::string &src);

} // namespace phonometrica

#endif // PHON_RUNTIME_VM_HPP
