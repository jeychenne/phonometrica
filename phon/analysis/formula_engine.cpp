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
 * Created: 03/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <phon/analysis/formula_engine.hpp>

namespace phonometrica {

static constexpr double PI = 3.14159265358979323846;
static constexpr double E  = 2.71828182845904523536;
static constexpr double NAN_VAL = std::numeric_limits<double>::quiet_NaN();

// ── Lexer ──────────────────────────────────────────────────

void FormulaEngine::Lex::next()
{
	// Skip whitespace.
	while (cur < end && std::isspace(static_cast<unsigned char>(*cur))) cur++;

	if (cur >= end) { tok = Tok::End; return; }

	char c = *cur;

	// Number literal.
	if (std::isdigit(static_cast<unsigned char>(c)) ||
	    (c == '.' && cur + 1 < end && std::isdigit(static_cast<unsigned char>(cur[1]))))
	{
		char *ep;
		num = std::strtod(cur, &ep);
		cur = ep;
		tok = Tok::Num;
		return;
	}

	// Identifier (variable, constant, or function name).
	if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
	{
		const char *start = cur;
		while (cur < end && (std::isalnum(static_cast<unsigned char>(*cur)) || *cur == '_')) cur++;
		id.assign(start, cur);
		tok = Tok::Id;
		return;
	}

	// Single-character tokens.
	cur++;
	switch (c) {
	case '+': tok = Tok::Plus;  return;
	case '-': tok = Tok::Minus; return;
	case '*': tok = Tok::Star;  return;
	case '/': tok = Tok::Slash; return;
	case '^': tok = Tok::Caret; return;
	case '(': tok = Tok::LPar;  return;
	case ')': tok = Tok::RPar;  return;
	case ',': tok = Tok::Comma; return;
	default:
		throw std::runtime_error(std::string("unexpected character: '") + c + "'");
	}
}

// ── Parser ─────────────────────────────────────────────────
//
// Grammar (precedence low → high):
//   Expr   = Term (('+' | '-') Term)*
//   Term   = Unary (('*' | '/') Unary)*
//   Unary  = ('-' | '+') Unary | Power
//   Power  = Primary ('^' Unary)?          (right-associative)
//   Primary = NUMBER | 'x' | 'pi' | 'e'
//           | IDENT '(' Expr (',' Expr)* ')'
//           | '(' Expr ')'

FormulaEngine::NodePtr FormulaEngine::pExpr(Lex &L)
{
	auto left = pTerm(L);
	while (L.tok == Tok::Plus || L.tok == Tok::Minus)
	{
		char op = (L.tok == Tok::Plus) ? '+' : '-';
		L.next();
		auto right = pTerm(L);
		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::BinOp;
		n->op = op;
		n->left = std::move(left);
		n->right = std::move(right);
		left = std::move(n);
	}
	return left;
}

FormulaEngine::NodePtr FormulaEngine::pTerm(Lex &L)
{
	auto left = pUnary(L);
	while (L.tok == Tok::Star || L.tok == Tok::Slash)
	{
		char op = (L.tok == Tok::Star) ? '*' : '/';
		L.next();
		auto right = pUnary(L);
		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::BinOp;
		n->op = op;
		n->left = std::move(left);
		n->right = std::move(right);
		left = std::move(n);
	}
	return left;
}

FormulaEngine::NodePtr FormulaEngine::pUnary(Lex &L)
{
	if (L.tok == Tok::Minus)
	{
		L.next();
		auto child = pUnary(L);
		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::Neg;
		n->left = std::move(child);
		return n;
	}
	if (L.tok == Tok::Plus)
	{
		L.next();
		return pUnary(L); // unary + is a no-op
	}
	return pPower(L);
}

FormulaEngine::NodePtr FormulaEngine::pPower(Lex &L)
{
	auto left = pPrimary(L);
	if (L.tok == Tok::Caret)
	{
		L.next();
		auto right = pUnary(L); // right-associative
		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::BinOp;
		n->op = '^';
		n->left = std::move(left);
		n->right = std::move(right);
		return n;
	}
	return left;
}

FormulaEngine::NodePtr FormulaEngine::pPrimary(Lex &L)
{
	// Number literal.
	if (L.tok == Tok::Num)
	{
		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::Num;
		n->num = L.num;
		L.next();
		return n;
	}

	// Identifier: variable, constant, or function call.
	if (L.tok == Tok::Id)
	{
		auto name = L.id;
		L.next();

		// Not followed by '(' → variable or constant.
		if (L.tok != Tok::LPar)
		{
			if (name == "x")
			{
				auto n = std::make_unique<Node>();
				n->kind = Node::Kind::Var;
				return n;
			}
			if (name == "pi")
			{
				auto n = std::make_unique<Node>();
				n->kind = Node::Kind::Num;
				n->num = PI;
				return n;
			}
			if (name == "e")
			{
				auto n = std::make_unique<Node>();
				n->kind = Node::Kind::Num;
				n->num = E;
				return n;
			}
			throw std::runtime_error("unknown variable: '" + name + "'");
		}

		// Function call.
		auto func = lookupFunc(name);
		L.next(); // consume '('
		auto arg1 = pExpr(L);

		NodePtr arg2;
		if (L.tok == Tok::Comma)
		{
			L.next();
			arg2 = pExpr(L);
		}

		if (L.tok != Tok::RPar) {
			throw std::runtime_error("expected ')' after function arguments");
		}
		L.next(); // consume ')'

		bool has_arg2 = (arg2 != nullptr);

		// Arity checks.
		if (func == Func::Pow && !has_arg2) {
			throw std::runtime_error("pow() requires 2 arguments");
		}
		if (func != Func::Pow && func != Func::St && has_arg2) {
			throw std::runtime_error(name + "() takes only 1 argument");
		}

		// st() with 1 arg: default reference = 100 Hz.
		if (func == Func::St && !has_arg2)
		{
			arg2 = std::make_unique<Node>();
			arg2->kind = Node::Kind::Num;
			arg2->num = 100.0;
		}

		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::Call;
		n->func = func;
		n->left = std::move(arg1);
		n->right = std::move(arg2);
		return n;
	}

	// Parenthesized expression.
	if (L.tok == Tok::LPar)
	{
		L.next();
		auto expr = pExpr(L);
		if (L.tok != Tok::RPar) {
			throw std::runtime_error("expected ')'");
		}
		L.next();
		return expr;
	}

	throw std::runtime_error("expected a number, variable, or function call");
}

// ── Function lookup ────────────────────────────────────────

FormulaEngine::Func FormulaEngine::lookupFunc(const std::string &name)
{
	if (name == "log")   return Func::Log;
	if (name == "log10") return Func::Log10;
	if (name == "log2")  return Func::Log2;
	if (name == "sqrt")  return Func::Sqrt;
	if (name == "abs")   return Func::Abs;
	if (name == "exp")   return Func::Exp;
	if (name == "round") return Func::Round;
	if (name == "floor") return Func::Floor;
	if (name == "ceil")  return Func::Ceil;
	if (name == "bark")  return Func::Bark;
	if (name == "erb")   return Func::Erb;
	if (name == "mel")   return Func::Mel;
	if (name == "st")    return Func::St;
	if (name == "pow")   return Func::Pow;
	throw std::runtime_error("unknown function: '" + name + "'");
}

// ── Evaluator ──────────────────────────────────────────────

double FormulaEngine::eval(const Node *n, double x)
{
	switch (n->kind)
	{
	case Node::Kind::Num:
		return n->num;

	case Node::Kind::Var:
		return x;

	case Node::Kind::Neg:
		return -eval(n->left.get(), x);

	case Node::Kind::BinOp:
	{
		double l = eval(n->left.get(), x);
		double r = eval(n->right.get(), x);
		switch (n->op) {
		case '+': return l + r;
		case '-': return l - r;
		case '*': return l * r;
		case '/': return (r == 0.0) ? NAN_VAL : l / r;
		case '^': return std::pow(l, r);
		default:  return NAN_VAL;
		}
	}

	case Node::Kind::Call:
	{
		double a = eval(n->left.get(), x);
		double b = n->right ? eval(n->right.get(), x) : 0.0;

		switch (n->func) {
		case Func::Log:   return (a > 0) ? std::log(a) : NAN_VAL;
		case Func::Log10: return (a > 0) ? std::log10(a) : NAN_VAL;
		case Func::Log2:  return (a > 0) ? std::log2(a) : NAN_VAL;
		case Func::Sqrt:  return (a >= 0) ? std::sqrt(a) : NAN_VAL;
		case Func::Abs:   return std::abs(a);
		case Func::Exp:   return std::exp(a);
		case Func::Round: return std::round(a);
		case Func::Floor: return std::floor(a);
		case Func::Ceil:  return std::ceil(a);
		// Traunmüller (1990): Hz → Bark
		case Func::Bark:  return (a > 0) ? 26.81 / (1.0 + 1960.0 / a) - 0.53 : NAN_VAL;
		// Glasberg & Moore (1990): Hz → ERB-rate
		case Func::Erb:   return 21.4 * std::log10(0.00437 * a + 1.0);
		// O'Shaughnessy (1987): Hz → mel
		case Func::Mel:   return 2595.0 * std::log10(1.0 + a / 700.0);
		// Semitones: st(x) = 12·log₂(x/100), st(x, ref) = 12·log₂(x/ref)
		case Func::St:    return (a > 0 && b > 0) ? 12.0 * std::log2(a / b) : NAN_VAL;
		case Func::Pow:   return std::pow(a, b);
		default:          return NAN_VAL;
		}
	}

	default:
		return NAN_VAL;
	}
}

// ── Public interface ───────────────────────────────────────

void FormulaEngine::parse(const std::string &formula)
{
	if (formula.empty()) {
		throw std::runtime_error("empty formula");
	}

	Lex L;
	L.cur = formula.c_str();
	L.end = L.cur + formula.size();
	L.next();

	m_root = pExpr(L);

	if (L.tok != Tok::End) {
		throw std::runtime_error("unexpected characters after expression");
	}
}

double FormulaEngine::evaluate(double x) const
{
	if (!m_root) return NAN_VAL;
	return eval(m_root.get(), x);
}

std::string FormulaEngine::validate(const std::string &formula)
{
	try {
		FormulaEngine engine;
		engine.parse(formula);
		return {};
	}
	catch (const std::runtime_error &e) {
		return e.what();
	}
}

} // namespace phonometrica
