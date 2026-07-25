// Phonometrica engine — AST visit dispatch. See ast.hpp.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Each node's visit() forwards to the matching AstVisitor hook. Kept in one file
// so ast.hpp stays declaration-only and AstVisitor is complete at the point of
// dispatch.

#include <phon/engine/compile/ast.hpp>

namespace phonometrica {

#define PHON_AST_VISIT(Name, hook)                                             \
	void Name::visit(AstVisitor &v) { v.hook(this); }

// expressions
PHON_AST_VISIT(NullLiteral, visit_null_literal)
PHON_AST_VISIT(BoolLiteral, visit_bool_literal)
PHON_AST_VISIT(IntegerLiteral, visit_integer_literal)
PHON_AST_VISIT(FloatLiteral, visit_float_literal)
PHON_AST_VISIT(StringLiteral, visit_string_literal)
PHON_AST_VISIT(StringInterpolation, visit_string_interpolation)
PHON_AST_VISIT(ListLiteral, visit_list_literal)
PHON_AST_VISIT(ListComprehension, visit_list_comprehension)
PHON_AST_VISIT(ArrayLiteral, visit_array_literal)
PHON_AST_VISIT(TableLiteral, visit_table_literal)
PHON_AST_VISIT(SetLiteral, visit_set_literal)
PHON_AST_VISIT(Variable, visit_variable)
PHON_AST_VISIT(ThisExpression, visit_this_expression)
PHON_AST_VISIT(UnaryExpression, visit_unary_expression)
PHON_AST_VISIT(BinaryExpression, visit_binary_expression)
PHON_AST_VISIT(ConcatExpression, visit_concat_expression)
PHON_AST_VISIT(IsExpression, visit_is_expression)
PHON_AST_VISIT(CastExpression, visit_cast_expression)
PHON_AST_VISIT(IndexExpression, visit_index_expression)
PHON_AST_VISIT(SliceExpression, visit_slice_expression)
PHON_AST_VISIT(FieldAccess, visit_field_access)
PHON_AST_VISIT(CallExpression, visit_call_expression)
PHON_AST_VISIT(SplatExpression, visit_splat_expression)
PHON_AST_VISIT(RefExpression, visit_ref_expression)
PHON_AST_VISIT(NamedArgument, visit_named_argument)
PHON_AST_VISIT(ConditionalExpression, visit_conditional_expression)
PHON_AST_VISIT(FunctionDefinition, visit_function_definition)
// statements
PHON_AST_VISIT(StatementList, visit_statement_list)
PHON_AST_VISIT(Declaration, visit_declaration)
PHON_AST_VISIT(Assignment, visit_assignment)
PHON_AST_VISIT(ExpressionStatement, visit_expression_statement)
PHON_AST_VISIT(IfStatement, visit_if_statement)
PHON_AST_VISIT(WhileStatement, visit_while_statement)
PHON_AST_VISIT(RepeatStatement, visit_repeat_statement)
PHON_AST_VISIT(ForNumeric, visit_for_numeric)
PHON_AST_VISIT(ForEach, visit_for_each)
PHON_AST_VISIT(LoopControl, visit_loop_control)
PHON_AST_VISIT(ReturnStatement, visit_return_statement)
PHON_AST_VISIT(Parameter, visit_parameter)
PHON_AST_VISIT(ClassDeclaration, visit_class_declaration)
PHON_AST_VISIT(FieldDeclaration, visit_field_declaration)
PHON_AST_VISIT(TryStatement, visit_try_statement)
PHON_AST_VISIT(CatchClause, visit_catch_clause)
PHON_AST_VISIT(ThrowStatement, visit_throw_statement)
PHON_AST_VISIT(SpawnStatement, visit_spawn_statement)
PHON_AST_VISIT(ImportStatement, visit_import_statement)

#undef PHON_AST_VISIT

} // namespace phonometrica
