/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 27/05/2020                                                                                                 *
 *                                                                                                                     *
 * purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime/compiler/ast.hpp>

#define VISIT(NODE) v.visit_##NODE(this);

namespace phonometrica {

void phonometrica::UnaryExpression::visit(AstVisitor &v)
{
	VISIT(unary);
}

void BinaryExpression::visit(AstVisitor &v)
{
	VISIT(binary);
}

void ConstantLiteral::visit(AstVisitor &v)
{
	VISIT(constant);
}

void FloatLiteral::visit(AstVisitor &v)
{
	VISIT(float);
}

void IntegerLiteral::visit(AstVisitor &v)
{
	VISIT(integer);
}

void StringLiteral::visit(AstVisitor &v)
{
	VISIT(string);
}

void StatementList::visit(AstVisitor &v)
{
	VISIT(statements);
}

void Declaration::visit(AstVisitor &v)
{
	VISIT(declaration);
}

void PrintStatement::visit(AstVisitor &v)
{
	VISIT(print_statement);
}

void CallExpression::visit(AstVisitor &v)
{
	VISIT(call);
}

void Variable::visit(AstVisitor &v)
{
	VISIT(variable);
}

void Assignment::visit(AstVisitor &v)
{
	VISIT(assignment);
}

Ast::Lexeme Assignment::get_operator() const
{
	switch (op)
	{
		case Lexeme::OpAssignConcat:
			return Lexeme::OpConcat;
		case Lexeme::OpAssignMinus:
			return Lexeme::OpMinus;
		case Lexeme::OpAssignMod:
			return Lexeme::OpMod;
		case Lexeme::OpAssignPlus:
			return Lexeme::OpPlus;
		case Lexeme::OpAssignPower:
			return Lexeme::OpPower;
		case Lexeme::OpAssignSlash:
			return Lexeme::OpSlash;
		case Lexeme::OpAssignStar:
			return Lexeme::OpStar;
		default:
			return Lexeme::OpAssign;
	}
}

void AssertStatement::visit(AstVisitor &v)
{
	VISIT(assert_statement);
}

void ConcatExpression::visit(AstVisitor &v)
{
	VISIT(concat_expression);
}

void IfStatement::visit(AstVisitor &v)
{
	VISIT(if_statement);
}

void IfCondition::visit(AstVisitor &v)
{
	VISIT(if_condition);
}

void WhileStatement::visit(AstVisitor &v)
{
	VISIT(while_statement);
}

void ForStatement::visit(AstVisitor &v)
{
	VISIT(for_statement);
}

void LoopExitStatement::visit(AstVisitor &v)
{
	VISIT(loop_exit);
}

void RoutineParameter::visit(AstVisitor &v)
{
	VISIT(parameter);
}

void RoutineDefinition::visit(AstVisitor &v)
{
	VISIT(routine);
}

void ReturnStatement::visit(AstVisitor &v)
{
	VISIT(return_statement);
}

void ReferenceExpression::visit(AstVisitor &v)
{
	VISIT(reference_expression);
}

void ListLiteral::visit(AstVisitor &v)
{
	VISIT(list);
}

void ArrayLiteral::visit(AstVisitor &v)
{
	VISIT(array);
}

void TableLiteral::visit(AstVisitor &v)
{
	VISIT(table);
}

void ForeachStatement::visit(AstVisitor &v)
{
	VISIT(foreach_statement);
}

void DebugStatement::visit(AstVisitor &v)
{
	VISIT(debug_statement);
}

void ThrowStatement::visit(AstVisitor &v)
{
	VISIT(throw_statement);
}

void RepeatStatement::visit(AstVisitor &v)
{
	VISIT(repeat_statement);
}

void SetLiteral::visit(AstVisitor &v)
{
	VISIT(set);
}

void IndexedExpression::visit(AstVisitor &v)
{
	VISIT(index);
}

void ClassDeclaration::visit(AstVisitor &v)
{
	VISIT(class_declaration);
}
} // namespace phonometrica
#undef VISIT
