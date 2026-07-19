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

#include <phon/engine/core/variant.hpp>
#include <phon/engine/runtime/native_traits.hpp> // typed add_function front end (M8 §11.3)
#include <phon/engine/types/string.hpp>
#include <phon/engine/vm/isolate.hpp> // RuntimeError (the error embedders catch)

#include <memory>
#include <string>
#include <utility>

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

	// Run a script file. Unlike do_string, the chunk knows its source file: `import`
	// resolves in the script's own directory (before any add_import_path directory
	// and $PHON_MODULE_PATH), and get_script_path() reports the file, so scripts can
	// locate sibling data. Throws std::runtime_error if the file cannot be read.
	Variant do_file(const String &path);

	// Toggle the interactive (REPL) leniencies for subsequent do_string calls
	// (design §11): bare assignment to an unresolved name auto-declares a session
	// binding. Off by default; never applies to do_file.
	void set_interactive(bool on) noexcept;

	// Inject a value into this session's isolate-global namespace (design §11): the
	// embedder's channel for app state (`phon`, the current selection, …). The name
	// resolves from every chunk and module compiled after the call, like a
	// script-side `global var`. Re-injecting an existing name rebinds it.
	void add_global(const char *name, const Variant &value);

	// Expose a C++ callable to scripts as a method on the generic `name` (the typed
	// registration front end, design §11.3). The callable's parameter types map to a
	// dispatch signature and its return type is boxed automatically:
	//
	//     rt.add_function("duration", [](Handle<Interval> i) -> double {
	//         return i->xmax - i->xmin;
	//     });
	//
	// Each call with an existing name adds an overload (same mechanism as script method
	// definition). An optional leading `Isolate &` parameter is passed through and is not
	// part of the signature. See native_traits.hpp for the supported parameter/return
	// types. Registration is process-global; a Runtime is required only to guarantee the
	// engine is initialized.
	template<class F>
	void add_function(const char *name, F &&f)
	{
		register_function(name, std::forward<F>(f));
	}

	// Register a C++ type `T` as a phon class named `name` deriving from `base` (design
	// §11.2), e.g. `rt.add_class<Sound>("Sound", rt.get_class("Object"))`. Records
	// sizeof(T), wires ~T() (and, for a Value class, a CoW clone), and binds
	// `T::phon_class` so Handle<T> arguments/returns and `is`/annotation dispatch work.
	// Instances are created from C++ via Handle<T>::make; the class is not
	// script-constructible. Registration is process-global.
	template<class T>
	Class *add_class(const char *name, Class *base, ClassKind kind = ClassKind::Reference)
	{
		return phonometrica::add_class<T>(name, base, kind);
	}

	// Expose a read-only field on a class registered with add_class<T>: scripts read
	// `obj.name`, routed to the getter (writes raise read-only). See register_field in
	// native_traits.hpp for the supported getter shapes.
	//
	//     rt.add_field<Model>("loglik", [](const Model &m) { return m.loglik; });
	template<class T, class F>
	void add_field(const char *name, F &&f)
	{
		register_field<T>(name, std::forward<F>(f));
	}

	// Look up a registered class by name (e.g. "Object" for a base). Null if none.
	Class *get_class(const char *name) const noexcept { return find_class(name); }

	// Add a directory to this session's module search path (design §11). `import M`
	// looks for `M.phon` or `M/initialize.phon` in the importing file's directory, then
	// in each directory added here, then in $PHON_MODULE_PATH.
	void add_import_path(const String &dir);

	// Cooperatively interrupt the script currently running on this session (the GUI's
	// "stop script" button, architecture §9.4). Safe to call from another thread while
	// do_string runs: the interpreter notices at its next safepoint and the in-flight
	// do_string throws a RuntimeError carrying an "[Interrupt]" message. A no-op if no
	// run is in progress by the time the next run clears it.
	void request_interrupt() noexcept;

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
