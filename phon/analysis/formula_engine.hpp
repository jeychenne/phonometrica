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
 * Purpose: Lightweight expression evaluator for column transformations. Parses a formula string into an AST and       *
 *          evaluates it for a given value of x. Supports standard math functions, phonetic scale conversions          *
 *          (bark, erb, mel, st), and the constants pi and e.                                                          *
 *                                                                                                                     *
 * Note: The core architecture and integration logic were designed and authored by Julien Eychenne. Portions of the    *
 * statistical estimation logic in this file were developed with the assistance of Claude Opus 4.6 (Anthropic), based  *
 * on published statistical literature and reference R implementations.                                                *
 * All AI-assisted logic has been manually audited, refactored, and validated against a diverse suite of datasets and  *
 * reference R packages to ensure mathematical accuracy and implementation integrity.                                  *
 * While every effort has been made to ensure reliability, this software is provided without a guarantee of being      *
 * bug-free. In the event that discrepancies or errors are discovered, the author will do his best to address them.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FORMULA_ENGINE_HPP
#define PHONOMETRICA_FORMULA_ENGINE_HPP

#include <memory>
#include <string>

namespace phonometrica {

class FormulaEngine
{
public:

	/// Parse a formula string (e.g. "log(x)", "bark(x)", "x / 1000").
	/// The variable `x` represents the cell value.
	/// Throws std::runtime_error on syntax errors.
	void parse(const std::string &formula);

	/// Evaluate the parsed formula for a given value of x.
	/// Returns NaN for domain errors (e.g. log of a negative number).
	double evaluate(double x) const;

	/// Validate a formula without keeping the result.
	/// Returns an empty string on success, or an error message on failure.
	static std::string validate(const std::string &formula);

private:

	struct Node;
	using NodePtr = std::unique_ptr<Node>;

	enum class Func {
		Log, Log10, Log2, Sqrt, Abs, Exp, Round, Floor, Ceil,
		Bark, Erb, Mel, St, Pow
	};

	struct Node {
		enum class Kind { Num, Var, BinOp, Neg, Call };
		Kind kind;
		double num = 0;
		char op = 0;
		Func func = Func::Log;
		NodePtr left, right;
	};

	enum class Tok { Num, Id, Plus, Minus, Star, Slash, Caret, LPar, RPar, Comma, End };

	struct Lex {
		const char *cur = nullptr;
		const char *end = nullptr;
		Tok tok = Tok::End;
		double num = 0;
		std::string id;

		void next();
	};

	static NodePtr pExpr(Lex &L);
	static NodePtr pTerm(Lex &L);
	static NodePtr pUnary(Lex &L);
	static NodePtr pPower(Lex &L);
	static NodePtr pPrimary(Lex &L);

	static double eval(const Node *n, double x);
	static Func lookupFunc(const std::string &name);

	NodePtr m_root;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FORMULA_ENGINE_HPP
