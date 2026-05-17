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
 * Purpose: A base class for "passive" AST visitors that want to walk the tree but only care about a handful of node   *
 * types. AstVisitor is a pure-virtual interface with 35 methods; implementing all of them as no-ops in every passive  *
 * pass would be both verbose and brittle (a new node type silently bypasses every existing visitor). DefaultAstVisitor*
 * provides default implementations: leaf nodes do nothing, parent nodes recurse into all of their children. Concrete  *
 * passive passes (the script indexer, the future error-collector for live squiggles, etc.) inherit from this class   *
 * and override only the visit methods they care about.                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_AST_DEFAULT_VISITOR_HPP
#define PHONOMETRICA_AST_DEFAULT_VISITOR_HPP

#include <phon/runtime/compiler/ast.hpp>

namespace phonometrica {

class DefaultAstVisitor : public AstVisitor
{
public:

	~DefaultAstVisitor() override = default;

	// Leaf nodes: nothing to recurse into.
	void visit_constant(ConstantLiteral *) override { }
	void visit_integer(IntegerLiteral *) override { }
	void visit_float(FloatLiteral *) override { }
	void visit_string(StringLiteral *) override { }
	void visit_variable(Variable *) override { }
	void visit_loop_exit(LoopExitStatement *) override { }

	// Composite nodes: recurse into every child by default.
	void visit_list(ListLiteral *n) override { visit_all(n->items); }
	void visit_array(ArrayLiteral *n) override { visit_all(n->items); }
	void visit_table(TableLiteral *n) override { visit_all(n->keys); visit_all(n->values); }
	void visit_set(SetLiteral *n) override { visit_all(n->values); }
	void visit_unary(UnaryExpression *n) override { visit_opt(n->expr); }
	void visit_binary(BinaryExpression *n) override { visit_opt(n->lhs); visit_opt(n->rhs); }
	void visit_concat_expression(ConcatExpression *n) override { visit_all(n->list); }
	void visit_reference_expression(ReferenceExpression *n) override { visit_opt(n->expr); }
	void visit_statements(StatementList *n) override { visit_all(n->statements); }
	void visit_declaration(Declaration *n) override { visit_all(n->lhs); visit_all(n->rhs); }
	void visit_print_statement(PrintStatement *n) override { visit_all(n->list); }
	void visit_debug_statement(DebugStatement *n) override { visit_opt(n->block); }
	void visit_throw_statement(ThrowStatement *n) override { visit_opt(n->expr); }
	void visit_try_statement(TryStatement *n) override { visit_opt(n->body); visit_opt(n->catch_body); }
	void visit_assert_statement(AssertStatement *n) override { visit_opt(n->expr); visit_opt(n->msg); }
	void visit_if_condition(IfCondition *n) override { visit_opt(n->cond); visit_opt(n->block); }
	void visit_if_statement(IfStatement *n) override { visit_all(n->if_conds); visit_opt(n->else_block); }
	void visit_while_statement(WhileStatement *n) override { visit_opt(n->cond); visit_opt(n->body); }
	void visit_repeat_statement(RepeatStatement *n) override { visit_opt(n->cond); visit_opt(n->body); }
	void visit_for_statement(ForStatement *n) override { visit_opt(n->var); visit_opt(n->start); visit_opt(n->end); visit_opt(n->step); visit_opt(n->block); }
	void visit_foreach_statement(ForeachStatement *n) override { visit_opt(n->key); visit_opt(n->value); visit_opt(n->collection); visit_opt(n->block); }
	void visit_parameter(RoutineParameter *n) override { visit_opt(n->variable); visit_opt(n->type); }
	void visit_routine(RoutineDefinition *n) override { visit_opt(n->name); visit_all(n->params); visit_opt(n->body); }
	void visit_class_declaration(ClassDeclaration *n) override { visit_opt(n->name); visit_opt(n->parent); visit_all(n->fields); visit_all(n->methods); }
	void visit_call(CallExpression *n) override { visit_opt(n->expr); visit_all(n->args); }
	void visit_index(IndexedExpression *n) override { visit_opt(n->expr); visit_all(n->indexes); }
	void visit_return_statement(ReturnStatement *n) override { visit_opt(n->expr); }
	void visit_assignment(Assignment *n) override { visit_opt(n->lhs); visit_opt(n->rhs); }
	void visit_multi_assignment(MultiAssignment *n) override { visit_all(n->lhs); visit_all(n->rhs); }

protected:

	// Recurse into a possibly-null child (e.g. ReturnStatement::expr is null for `return`
	// without a value, RoutineDefinition::name is null for anonymous function expressions,
	// IfStatement::else_block is null when there is no `else`, etc.).
	void visit_opt(const AutoAst &p)
	{
		if (p) p->visit(*this);
	}

	void visit_all(const AstList &lst)
	{
		for (auto &p : lst) visit_opt(p);
	}
};

} // namespace phonometrica

#endif // PHONOMETRICA_AST_DEFAULT_VISITOR_HPP
