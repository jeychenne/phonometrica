// Phonometrica engine — the runtime façade: a persistent session that compiles
// and runs scripts (architecture §11). Copyright (C) 2019-2026 Julien Eychenne.
// GPLv3 (see LICENSE).
//
// This is the orchestration layer above the VM (`phon/vm/`, the register machine
// that runs bytecode), and the M4 stand-in for the full `Runtime` of §11 (module
// search paths, imports, and the typed registration API arrive in M8). The API
// mirrors Phonometrica's old `Runtime` so the application port stays mechanical:
// a long-lived object with `do_string` methods.
//
// A `Runtime` owns one long-lived Isolate (the main thread's, §10.1) and a
// persistent module namespace, so it is the right surface for the REPL/console and
// the script editor's "run selection": module-level bindings declared in one call
// remain visible in the next (design §11). `do_string` returns the value of the
// chunk's trailing expression (REPL-style), so a bare expression yields its value.
// Compile errors surface as SyntaxError; runtime errors as RuntimeError (both carry
// a source line).

#ifndef PHON_RUNTIME_RUNTIME_HPP
#define PHON_RUNTIME_RUNTIME_HPP

#include <phon/core/variant.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/isolate.hpp> // RuntimeError (the error embedders catch)

#include <memory>
#include <string>

namespace phonometrica {

// Boot the process-global engine state: register the builtin classes, the callable
// classes, and the builtin generic library (print, len, assert). Idempotent; a
// Runtime calls it on construction.
void init_runtime();

class Runtime final
{
public:
	Runtime();
	~Runtime();
	Runtime(const Runtime &) = delete;
	Runtime &operator=(const Runtime &) = delete;

	// Compile `code` against this session's persistent module namespace and run it
	// on this session's Isolate, returning the value of its trailing expression (or
	// null). Bindings persist across calls: `do_string("var x = 5")` then
	// `do_string("x + 1")` yields 6, and a function defined in one call is callable
	// in a later one.
	Variant do_string(const String &code);

	// Force a cycle-collection pass now (design/architecture §8.2). Normally the
	// collector runs itself at safepoints; this is the explicit hook for the
	// `collect_garbage()` builtin and for tests that assert reclamation.
	void collect_garbage();

	// Number of live cycle-collection candidates buffered on this session (tests).
	intptr_t gc_candidate_count() const;

private:
	struct State;
	std::unique_ptr<State> m_state;
};

// One-shot convenience: run `src` in a fresh, throwaway session and return the
// trailing expression's value. Use a Runtime instance when state must persist
// across calls (REPL, editor).
Variant do_string(const String &src);

// Compile `src` and return its disassembly. The result is a std::string on
// purpose: it is a textual debug listing built for tooling and diffed against
// golden files (like the AST dumper), not engine value data.
std::string disassemble_source(const String &src);

} // namespace phonometrica

#endif // PHON_RUNTIME_RUNTIME_HPP
