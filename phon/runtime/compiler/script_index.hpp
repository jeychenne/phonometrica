/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 16/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: A passive symbol index for a single Phonometrica script source. Built by parsing the source and walking    *
 * the resulting AST. Used by the script editor for autocompletion of user-declared names, and (in upcoming work) for  *
 * click-on-symbol go-to-definition and live error highlighting. Index construction is best-effort: if the source      *
 * fails to parse, whatever symbols were collected before the error are returned.                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SCRIPT_INDEX_HPP
#define PHONOMETRICA_SCRIPT_INDEX_HPP

#include <phon/array.hpp>
#include <phon/string.hpp>

namespace phonometrica {

class Runtime;

enum class SymbolKind
{
	Variable,   // `let x`, loop variables, top-level declarations
	Function,   // `function f(...)`
	Class,      // `class Foo`
	Method,     // method declarations inside a class
	Field,      // declarations inside a class body
	Parameter   // function/method parameters
};

struct Symbol
{
	String     name;
	SymbolKind kind;
	int        line;     // 1-based line of the declaring identifier
	int        column;   // 0-based byte column of the declaring identifier
};

class ScriptIndex final
{
public:

	ScriptIndex() = default;

	void add(Symbol s) { m_symbols.append(std::move(s)); }

	const Array<Symbol> &symbols() const { return m_symbols; }

	// All distinct names in the index, in insertion order of first occurrence.
	// Suitable for feeding into QsciAPIs for autocompletion.
	Array<String> distinct_names() const;

	// Look up the first declaration of `name`, in source order. Returns
	// nullptr if no declaration with this name was recorded. v1 doesn't
	// distinguish scopes — if the same name is declared twice (e.g. shadowed
	// in a nested function), only the first declaration is reachable through
	// this helper. Good enough for click-on-symbol navigation; refinement to
	// scope-aware resolution can be layered on later without API changes.
	const Symbol *find(const String &name) const;

	bool empty() const { return m_symbols.empty(); }

	intptr_t size() const { return m_symbols.size(); }

	// ── Live-error metadata ───────────────────────────────────────────────
	//
	// If the underlying parse threw a RuntimeError, `has_error` is true and
	// the following fields locate it. When `has_error` is false the parse
	// succeeded and the index reflects the full AST. When true, `m_symbols`
	// will typically be empty (the parser bails on the first error) — the
	// editor uses this in two ways: (1) live-squiggle painting at
	// (error_line, error_column) for `error_length` bytes, (2) keeping the
	// last successful index's completions in place while the user fixes the
	// error (the editor checks `has_error` before rebuilding completions
	// from an empty symbol list).

	bool    has_error    = false;
	int     error_line   = 0;    // 1-based; 0 when has_error is false
	int     error_column = -1;   // 0-based byte column; -1 when unavailable
	int     error_length = 0;    // bytes to squiggle; 0 means "use minimum"
	String  error_message;       // formatted message from RuntimeError::what()

private:

	Array<Symbol> m_symbols;
};

// Parse `source` and build a symbol index from the resulting AST. If parsing
// fails, a (possibly empty) index containing whatever was collected before the
// failure is returned. The runtime is needed because the parser interns
// identifiers into the runtime's string table. This call is noexcept: it will
// never throw, even on malformed input — making it safe to invoke from a
// debounced GUI timer slot.
ScriptIndex index_script(Runtime &rt, const String &source) noexcept;

} // namespace phonometrica

#endif // PHONOMETRICA_SCRIPT_INDEX_HPP
