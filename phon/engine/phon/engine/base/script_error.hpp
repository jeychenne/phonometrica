// Phonometrica engine — recoverable script-facing failures from low layers.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A ScriptError reports a condition that a *script* caused and a script can
// `catch`: an out-of-range index, an invalid argument, an operation on an empty
// container. The types layer throws it instead of aborting the process
// (PHON_CHECK remains for genuine invariants: OOM, API misuse, corrupt state).
// The VM's interpreter loop converts an in-flight ScriptError into a thrown
// script Error at the current line, dispatching it exactly like Isolate::raise;
// outside a running script it surfaces to the embedder as the std::exception it
// is. Messages carry the usual "[Index error] …" style prefix.

#ifndef PHON_BASE_SCRIPT_ERROR_HPP
#define PHON_BASE_SCRIPT_ERROR_HPP

#include <stdexcept>

#include <phon/engine/base/definitions.hpp>

namespace phonometrica {

class ScriptError : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

[[noreturn]] PHON_NOINLINE inline void script_fail(const char *msg)
{
	throw ScriptError(msg);
}

} // namespace phonometrica

// A script-recoverable check: like PHON_CHECK, but the failure is thrown as a
// ScriptError (catchable from scripts) rather than aborting the process.
#define PHON_SCRIPT_CHECK(cond, msg) \
	(PHON_LIKELY(cond) ? (void) 0 : ::phonometrica::script_fail(msg))

#endif // PHON_BASE_SCRIPT_ERROR_HPP
