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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/hashmap.hpp>
#include <phon/runtime/error.hpp>
#include <phon/runtime/compiler/ast_default_visitor.hpp>
#include <phon/runtime/compiler/parser.hpp>
#include <phon/runtime/compiler/script_index.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────
//  Indexer
// ─────────────────────────────────────────────────
//
// Walks an AST and emits a Symbol for every declaring identifier. Inherits the
// recurse-everywhere default behaviour from DefaultAstVisitor and only
// overrides nodes that introduce names.
//
// Scope is tracked just enough to distinguish class-context declarations
// (which become Field symbols) from non-class declarations (Variable).
// Full scope-chain tracking is intentionally deferred to a later pass —
// for autocompletion, a flat name list is what the user actually wants.

class Indexer final : public DefaultAstVisitor
{
public:

	explicit Indexer(ScriptIndex &idx) : m_index(idx) { }

	void visit_declaration(Declaration *n) override
	{
		// Each entry in `lhs` is a Variable AST node holding a declared name.
		// In a class body these are fields; everywhere else they are variables.
		SymbolKind k = m_in_class ? SymbolKind::Field : SymbolKind::Variable;
		for (auto &p : n->lhs) {
			if (auto v = dynamic_cast<Variable *>(p.get())) {
				m_index.add({ v->name, k, v->line_no, v->column });
			}
		}
		// `rhs` may contain nested function/class literals that declare further names.
		for (auto &p : n->rhs) if (p) p->visit(*this);
	}

	void visit_routine(RoutineDefinition *n) override
	{
		// The parser synthesises an internal `_ctor` for every class and a default
		// `init` when the user does not supply one. Neither is a user-typeable name,
		// so they should not appear as completion candidates.
		bool synthetic = n->is_constructor || (n->is_initializer && n->is_default_initializer());

		if (!synthetic) {
			if (auto v = dynamic_cast<Variable *>(n->name.get())) {
				SymbolKind k = n->method ? SymbolKind::Method : SymbolKind::Function;
				Symbol sym{ v->name, k, v->line_no, v->column, String() };
				sym.signature = format_signature(v->name, n->params, n->method);
				m_index.add(std::move(sym));
			}
		}

		// Recurse: parameters yield Parameter symbols, the body may declare locals,
		// nested functions, nested classes, etc.
		for (auto &p : n->params) if (p) p->visit(*this);
		if (n->body) n->body->visit(*this);
	}

	void visit_parameter(RoutineParameter *n) override
	{
		// The wrapped variable is the parameter name; unwrap a ReferenceExpression
		// if present (`ref x`).
		const Ast *p = n->variable.get();
		if (auto ref = dynamic_cast<const ReferenceExpression *>(p)) {
			p = ref->expr.get();
		}
		if (auto v = dynamic_cast<const Variable *>(p)) {
			m_index.add({ v->name, SymbolKind::Parameter, v->line_no, v->column });
		}
	}

	void visit_class_declaration(ClassDeclaration *n) override
	{
		if (auto v = dynamic_cast<Variable *>(n->name.get())) {
			m_index.add({ v->name, SymbolKind::Class, v->line_no, v->column });
		}
		// Walk fields and methods in class context so visit_declaration tags
		// declarations as Field instead of Variable.
		bool prev = m_in_class;
		m_in_class = true;
		for (auto &f : n->fields)  if (f) f->visit(*this);
		for (auto &m : n->methods) if (m) m->visit(*this);
		m_in_class = prev;
		// Note: `parent` is a reference to an existing class, not a declaration.
	}

	void visit_for_statement(ForStatement *n) override
	{
		// `for i = 1 to 10` binds `i` for the loop body.
		if (auto v = dynamic_cast<Variable *>(n->var.get())) {
			m_index.add({ v->name, SymbolKind::Variable, v->line_no, v->column });
		}
		if (n->start) n->start->visit(*this);
		if (n->end)   n->end->visit(*this);
		if (n->step)  n->step->visit(*this);
		if (n->block) n->block->visit(*this);
	}

	void visit_foreach_statement(ForeachStatement *n) override
	{
		// `foreach k, v in coll`: either of `k` and `v` may be wrapped in a
		// ReferenceExpression (`ref x`), which we unwrap to get to the Variable.
		emit_foreach_var(n->key);
		emit_foreach_var(n->value);
		if (n->collection) n->collection->visit(*this);
		if (n->block)      n->block->visit(*this);
	}

private:

	void emit_foreach_var(const AutoAst &node)
	{
		if (!node) return;
		const Ast *p = node.get();
		if (auto ref = dynamic_cast<const ReferenceExpression *>(p)) {
			p = ref->expr.get();
		}
		if (auto v = dynamic_cast<const Variable *>(p)) {
			m_index.add({ v->name, SymbolKind::Variable, v->line_no, v->column });
		}
	}

	// Format a routine declaration's parameter list as a single-line call-tip signature,
	// e.g. "compute(values as List, mode as String)". Skips the synthetic `this` parameter
	// that the parser prepends for methods (see parser.cpp:812) so user-facing tips don't
	// expose the implicit receiver. Falls back gracefully on missing pieces: a null param,
	// a non-Variable name AST, or a complex type expression all degrade to a partial
	// signature rather than aborting. Empty return is allowed and is the editor's signal
	// to skip emitting a call tip entry for this routine.
	static String format_signature(const String &routine_name, const AstList &params, bool is_method)
	{
		String out(routine_name);
		out.append("(");

		bool first = true;
		intptr_t i = 0;
		for (auto &p : params)
		{
			// Skip the implicit `this` parameter at index 0 for methods. Defensive on the
			// index — if a future parser change ever moved `this` elsewhere, the name check
			// inside the loop body below would still catch it.
			++i;
			if (!p) continue;
			auto param = dynamic_cast<const RoutineParameter *>(p.get());
			if (!param || !param->variable) continue;

			// Unwrap an enclosing ReferenceExpression (defensive — `ref` is usually carried
			// by RoutineParameter::by_ref, but the variable AST may itself be wrapped).
			const Ast *var_ast = param->variable.get();
			if (auto ref = dynamic_cast<const ReferenceExpression *>(var_ast)) {
				var_ast = ref->expr.get();
			}
			auto var = dynamic_cast<const Variable *>(var_ast);
			if (!var) continue;

			// Skip the synthetic receiver. Cheap name compare against the parser's
			// canonical "this" — matches what parse_parameters injects at index 0.
			if (is_method && i == 1 && var->name == "this") {
				continue;
			}

			if (!first) out.append(", ");
			first = false;

			if (param->by_ref) out.append("ref ");
			out.append(var->name);

			// Type annotation is optional in Phonometrica. When the user wrote `name as T`,
			// `type` is typically a Variable node holding the type name. Anything more exotic
			// (parameterised types if ever added) silently drops the annotation rather than
			// guessing — the bare parameter name is still useful in a call tip.
			if (param->type) {
				if (auto t = dynamic_cast<const Variable *>(param->type.get())) {
					out.append(" as ");
					out.append(t->name);
				}
			}
		}

		out.append(")");
		return out;
	}

	ScriptIndex &m_index;
	bool m_in_class = false;
};


// ─────────────────────────────────────────────────
//  ScriptIndex helpers
// ─────────────────────────────────────────────────

Array<String> ScriptIndex::distinct_names() const
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

const Symbol *ScriptIndex::find(const String &name) const
{
	for (auto &s : m_symbols) {
		if (s.name == name) {
			return &s;
		}
	}
	return nullptr;
}


// ─────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────

ScriptIndex index_script(Runtime &rt, const String &source) noexcept
{
	ScriptIndex idx;
	try {
		Parser parser(&rt);
		auto ast = parser.parse_string(source);
		if (ast) {
			Indexer indexer(idx);
			ast->visit(indexer);
		}
	}
	catch (const RuntimeError &e) {
		// Record location + message so the editor can paint a live-error
		// squiggle. The symbol array is left as-built (typically empty when
		// the parser bails on the first error).
		idx.has_error    = true;
		idx.error_line   = int(e.line_no());
		idx.error_column = int(e.column_no());
		idx.error_length = int(e.error_length());
		idx.error_message = String(e.what());
	}
	catch (...) {
		// Any other exception is swallowed silently — see the function
		// contract (noexcept). The editor's user-symbol completion just won't
		// refresh until the user types something the parser can handle.
	}
	return idx;
}

} // namespace phonometrica
