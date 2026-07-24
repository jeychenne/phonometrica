// Phonometrica engine — compile-time diagnostics (syntax errors).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The scanner and parser report errors by throwing SyntaxError. Exceptions are
// permitted in the compiler layer (architecture §0 [INVARIANT]): they never cross
// the VM dispatch hot path. A SyntaxError carries a human-readable message plus
// the source position (1-based line, 0-based byte column, and a span length) so
// the embedder can render an editor caret. The message already follows the
// engine's `[Syntax error] ...` convention (architecture §16.5).

#ifndef PHON_COMPILE_DIAGNOSTIC_HPP
#define PHON_COMPILE_DIAGNOSTIC_HPP

#include <phon/engine/base/definitions.hpp>

#include <stdexcept>
#include <string>

namespace phonometrica {

class SyntaxError final : public std::runtime_error
{
public:
	SyntaxError(std::string message, intptr_t line, intptr_t column, intptr_t length)
	    : std::runtime_error(std::move(message)), line(line), column(column), length(length)
	{
	}

	// 1-based line of the offending token/character.
	intptr_t line;

	// 0-based byte column from the start of `line`.
	intptr_t column;

	// Span length in bytes (>= 1) for the editor underline.
	intptr_t length;
};

} // namespace phonometrica

#endif // PHON_COMPILE_DIAGNOSTIC_HPP
