// Forwarding header — the engine's embedding umbrella.
// Pulls in Runtime, which transitively exposes Variant, String, Handle and the
// Isolate/RuntimeError types an embedder catches, plus SyntaxError (raised by the
// front end during compilation) so that BOTH script-error types arrive from this
// one header. phon/error.hpp deliberately carries neither — it is the light
// formatted-throw helper, included from TUs that never touch the engine (see its
// own note). Until A8 the engine shipped a phon/error.hpp of its own that was
// exactly isolate.hpp + diagnostic.hpp; absorbing the engine dropped that copy in
// favour of the app's, which is why diagnostic.hpp is pulled in here.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#ifndef PHONOMETRICA_RUNTIME_INC_HPP
#define PHONOMETRICA_RUNTIME_INC_HPP

#include <phon/engine/runtime/runtime.hpp>
#include <phon/engine/compile/diagnostic.hpp>

#endif // PHONOMETRICA_RUNTIME_INC_HPP
