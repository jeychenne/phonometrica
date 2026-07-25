// Phonometrica engine — AST node/visitor tests (M3 step 2).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The parser (step 3) and dumper (step 4) exercise the AST fully; this file just
// checks the node/visitor plumbing: construction, ownership, the RTTI-free
// is<T>()/as<T>() tests, and that visit() dispatches to the right hook.

#include <phon/engine/compile/ast.hpp>
#include <phon/engine/types/atom.hpp>

#include "test_framework.hpp"

#include <memory>
#include <vector>

using namespace phonometrica;

namespace {

template<class T, class... Args>
AutoAst node(Args &&...args)
{
	return std::make_unique<T>(1, 0, std::forward<Args>(args)...);
}

// A visitor that records the kind of every node it is told to visit. It does not
// recurse (Step 2 has no traversal helper); tests drive it node by node.
struct KindRecorder final : AstVisitor
{
	std::vector<NodeKind> seen;
	void rec(Ast *n) { seen.push_back(n->kind); }

	void visit_null_literal(NullLiteral *n) override { rec(n); }
	void visit_bool_literal(BoolLiteral *n) override { rec(n); }
	void visit_integer_literal(IntegerLiteral *n) override { rec(n); }
	void visit_float_literal(FloatLiteral *n) override { rec(n); }
	void visit_string_literal(StringLiteral *n) override { rec(n); }
	void visit_string_interpolation(StringInterpolation *n) override { rec(n); }
	void visit_list_literal(ListLiteral *n) override { rec(n); }
	void visit_list_comprehension(ListComprehension *n) override { rec(n); }
	void visit_array_literal(ArrayLiteral *n) override { rec(n); }
	void visit_table_literal(TableLiteral *n) override { rec(n); }
	void visit_set_literal(SetLiteral *n) override { rec(n); }
	void visit_variable(Variable *n) override { rec(n); }
	void visit_this_expression(ThisExpression *n) override { rec(n); }
	void visit_unary_expression(UnaryExpression *n) override { rec(n); }
	void visit_binary_expression(BinaryExpression *n) override { rec(n); }
	void visit_concat_expression(ConcatExpression *n) override { rec(n); }
	void visit_is_expression(IsExpression *n) override { rec(n); }
	void visit_cast_expression(CastExpression *n) override { rec(n); }
	void visit_index_expression(IndexExpression *n) override { rec(n); }
	void visit_slice_expression(SliceExpression *n) override { rec(n); }
	void visit_field_access(FieldAccess *n) override { rec(n); }
	void visit_call_expression(CallExpression *n) override { rec(n); }
	void visit_splat_expression(SplatExpression *n) override { rec(n); }
	void visit_ref_expression(RefExpression *n) override { rec(n); }
	void visit_named_argument(NamedArgument *n) override { rec(n); }
	void visit_conditional_expression(ConditionalExpression *n) override { rec(n); }
	void visit_function_definition(FunctionDefinition *n) override { rec(n); }
	void visit_statement_list(StatementList *n) override { rec(n); }
	void visit_declaration(Declaration *n) override { rec(n); }
	void visit_assignment(Assignment *n) override { rec(n); }
	void visit_expression_statement(ExpressionStatement *n) override { rec(n); }
	void visit_if_statement(IfStatement *n) override { rec(n); }
	void visit_while_statement(WhileStatement *n) override { rec(n); }
	void visit_repeat_statement(RepeatStatement *n) override { rec(n); }
	void visit_for_numeric(ForNumeric *n) override { rec(n); }
	void visit_for_each(ForEach *n) override { rec(n); }
	void visit_loop_control(LoopControl *n) override { rec(n); }
	void visit_return_statement(ReturnStatement *n) override { rec(n); }
	void visit_parameter(Parameter *n) override { rec(n); }
	void visit_class_declaration(ClassDeclaration *n) override { rec(n); }
	void visit_field_declaration(FieldDeclaration *n) override { rec(n); }
	void visit_try_statement(TryStatement *n) override { rec(n); }
	void visit_catch_clause(CatchClause *n) override { rec(n); }
	void visit_throw_statement(ThrowStatement *n) override { rec(n); }
	void visit_spawn_statement(SpawnStatement *n) override { rec(n); }
	void visit_import_statement(ImportStatement *n) override { rec(n); }
};

} // namespace

TEST_CASE("ast: node construction carries position and kind")
{
	auto n = node<IntegerLiteral>(int64_t(42));
	CHECK(n->line == 1);
	CHECK(n->column == 0);
	CHECK(n->kind == NodeKind::IntegerLiteral);
	CHECK(n->as<IntegerLiteral>()->value == 42);
}

TEST_CASE("ast: is<T>() and as<T>() are RTTI-free type tests")
{
	AutoAst n = node<FloatLiteral>(3.5);
	CHECK(n->is<FloatLiteral>());
	CHECK(!n->is<IntegerLiteral>());
	CHECK(n->as<FloatLiteral>() != nullptr);
	CHECK(n->as<IntegerLiteral>() == nullptr);
	CHECK(n->as<FloatLiteral>()->value == 3.5);
}

TEST_CASE("ast: visit() dispatches to the matching hook")
{
	KindRecorder v;

	node<NullLiteral>()->visit(v);
	node<BoolLiteral>(true)->visit(v);
	node<Variable>(intern("x"))->visit(v);
	node<ThisExpression>()->visit(v);

	REQUIRE(v.seen.size() == 4);
	CHECK(v.seen[0] == NodeKind::NullLiteral);
	CHECK(v.seen[1] == NodeKind::BoolLiteral);
	CHECK(v.seen[2] == NodeKind::Variable);
	CHECK(v.seen[3] == NodeKind::ThisExpression);
}

TEST_CASE("ast: composite nodes own their children")
{
	// x = 1 + 2
	auto lhs = node<IntegerLiteral>(int64_t(1));
	auto rhs = node<IntegerLiteral>(int64_t(2));
	auto sum = node<BinaryExpression>(Lexeme::Plus, std::move(lhs), std::move(rhs));
	auto target = node<Variable>(intern("x"));
	auto assign = node<Assignment>(Lexeme::Assign, std::move(target), std::move(sum));

	auto *a = assign->as<Assignment>();
	REQUIRE(a != nullptr);
	CHECK(a->op == Lexeme::Assign);
	CHECK(a->target->is<Variable>());
	REQUIRE(a->value->is<BinaryExpression>());
	auto *b = a->value->as<BinaryExpression>();
	CHECK(b->op == Lexeme::Plus);
	CHECK(b->lhs->as<IntegerLiteral>()->value == 1);
	CHECK(b->rhs->as<IntegerLiteral>()->value == 2);

	KindRecorder v;
	assign->visit(v);
	REQUIRE(v.seen.size() == 1); // visit does not auto-recurse
	CHECK(v.seen[0] == NodeKind::Assignment);
}

TEST_CASE("ast: variadic and ref parameters, declaration modifiers")
{
	auto p = node<Parameter>(intern("values"), node<Variable>(intern("Object")),
	                         /*by_ref*/ false, /*variadic*/ true, /*default*/ AutoAst());
	auto *param = p->as<Parameter>();
	REQUIRE(param != nullptr);
	CHECK(param->variadic);
	CHECK(!param->by_ref);
	CHECK(param->default_value == nullptr);

	auto d = node<Declaration>(intern("cache"), AutoAst(), node<TableLiteral>(AstList(), AstList()),
	                           /*is_const*/ false, DeclModifier::Local);
	auto *decl = d->as<Declaration>();
	REQUIRE(decl != nullptr);
	CHECK(decl->modifier == DeclModifier::Local);
	CHECK(!decl->is_const);
	CHECK(decl->init->is<TableLiteral>());
}
