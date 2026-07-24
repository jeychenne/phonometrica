// Public forwarding header — the error types an embedder catches.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// RuntimeError (thrown by the running VM) lives in the isolate; SyntaxError (raised by
// the front end during compilation) lives in the compiler's diagnostic layer.

#ifndef PHON_ERROR_INC_HPP
#define PHON_ERROR_INC_HPP

#include <phon/engine/vm/isolate.hpp>
#include <phon/engine/compile/diagnostic.hpp>

#endif // PHON_ERROR_INC_HPP
