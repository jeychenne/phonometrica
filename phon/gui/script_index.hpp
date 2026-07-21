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
 * Created: 21/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: symbol index for the script editor (autocompletion, call tips, go-to-definition, live error squiggles).    *
 * This is the app-owned successor of the old engine's phon/runtime/compiler/script_index.hpp: the data model is       *
 * unchanged, but `index_script` is currently a stub — the old implementation walked the OLD engine's AST. TODO(A4/A5):*
 * re-implement indexing on the NEW engine's front end (compile/ast) so editor completions and squiggles come back.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_GUI_SCRIPT_INDEX_HPP
#define PHONOMETRICA_GUI_SCRIPT_INDEX_HPP

#include <phon/array.hpp>
#include <phon/string.hpp>
#include <phon/hashmap.hpp>

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

struct ScriptSymbol
{
	String     name;
	SymbolKind kind;
	int        line;     // 1-based line of the declaring identifier
	int        column;   // 0-based byte column of the declaring identifier

	// Formatted call-tip signature for routines, e.g. "compute(values as List, mode as String)".
	// Populated only for SymbolKind::Function and SymbolKind::Method; empty for everything else.
	String     signature;
};

class ScriptIndex final
{
public:

	ScriptIndex() = default;

	void add(ScriptSymbol s) { m_symbols.append(std::move(s)); }

	const Array<ScriptSymbol> &symbols() const { return m_symbols; }

	// All distinct names in the index, in insertion order of first occurrence.
	Array<String> distinct_names() const
	{
		Array<String> out;
		Hashmap<String, bool> seen;
		for (auto &s : m_symbols) {
			if (seen.find(s.name) == seen.end()) {
				seen[s.name] = true;
				out.append(s.name);
			}
		}
		return out;
	}

	// Look up the first declaration of `name`, in source order; nullptr if absent.
	const ScriptSymbol *find(const String &name) const
	{
		for (auto &s : m_symbols) {
			if (s.name == name) {
				return &s;
			}
		}
		return nullptr;
	}

	bool empty() const { return m_symbols.empty(); }

	intptr_t size() const { return m_symbols.size(); }

	// Live-error metadata (see the old header for the editor contract).
	bool    has_error    = false;
	int     error_line   = 0;    // 1-based; 0 when has_error is false
	int     error_column = -1;   // 0-based byte column; -1 when unavailable
	int     error_length = 0;    // bytes to squiggle; 0 means "use minimum"
	String  error_message;       // formatted message

private:

	Array<ScriptSymbol> m_symbols;
};

// Parse `source` and build a symbol index. STUB since the A1 engine swap: always
// returns an empty, error-free index, so the editor keeps builtin completions but
// loses script-local symbols and live squiggles. TODO(A4/A5): rebuild on the new
// engine's front end.
inline ScriptIndex index_script(Runtime &, const String &) noexcept
{
	return ScriptIndex();
}

} // namespace phonometrica

#endif // PHONOMETRICA_GUI_SCRIPT_INDEX_HPP
