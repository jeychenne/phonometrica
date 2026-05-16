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
 * =================================================================================================================== *
 * This parser is partly based on the parser from MuJS, which came with the following license and copyright            *
 * information:                                                                                                        *
 *                                                                                                                     *
 * Copyright (C) 2013-2019, Artifex Software                                                                           *
 *                                                                                                                     *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby      *
 * granted, provided that the above copyright notice and this permission notice appear in all copies.                  *
 *                                                                                                                     *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING     *
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL,      *
 * DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,   *
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE     *
 * USE OR PERFORMANCE OF THIS SOFTWARE.                                                                                *
 * =================================================================================================================== *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PARSER_HPP
#define PHONOMETRICA_PARSER_HPP

#include <phon/runtime/compiler/scanner.hpp>
#include <phon/runtime/compiler/ast.hpp>

namespace phonometrica {

class Runtime;

class Parser final
{
public:

	explicit Parser(Runtime *rt);

	AutoAst parse_file(const String &path);

	AutoAst parse_string(const String &path);

private:

	using Lexeme = Token::Lexeme;

	// Make node with default debug info.
	template <class T, class... Args>
	std::unique_ptr<T> make(Args &&... args)
	{
		return std::make_unique<T>(get_line(), get_column(), std::forward<Args>(args)...);
	}

	void clear();

	int get_line();

	int get_column();

	void initialize();

	// Unconditionally move to the next token.
	void accept();

	// Check the next token's type.
	bool check(Lexeme lex);

	// Move to the next token only if the current token is 'c'. Returns true if we moved.
	bool accept(Lexeme lex);

	void expect(Lexeme lex, const char *hint);

	void skip_empty_lines() { while (token.is(Lexeme::Eol)) accept(); }

	void expect_separator();

	void report_error(const std::string &hint, const char *error_type = "Syntax");

	// Throw a syntax error anchored to an explicit (line, column, length)
	// rather than to the scanner's current position. Used when the offending
	// construct is structurally far from where parsing actually failed —
	// most obviously, unclosed blocks: by the time the parser notices a
	// missing `end`, the scanner is at EOF (an unpaintable, blank-line
	// position) but the user wants the squiggle inside the unclosed block.
	void report_error_at(intptr_t line, intptr_t col, intptr_t len,
	                     const std::string &hint, const char *error_type = "Syntax");

	AutoAst parse();

	void parse_option();

	AutoAst parse_statement();

	AutoAst parse_statements(bool open_scope);

	AutoAst parse_print_statement();

	AutoAst parse_expression_statement();

	AutoAst parse_expression();

	AutoAst parse_conditional_expression();

	AutoAst parse_declaration();

	AutoAst parse_formula();

	AutoAst parse_or_expression();

	AutoAst parse_and_expression();

	AutoAst parse_not_expression();

	AutoAst parse_comp_expression();

	AutoAst parse_additive_expression(); // +, -, &

	AutoAst parse_multiplicative_expression(); // *, /, %

	AutoAst parse_signed_expression(); // +x, -x

	AutoAst parse_exponential_expression(); // ^

	AutoAst parse_pipe_expression(); // |

	AutoAst parse_call_expression();

	AutoAst parse_ref_expression();

	AutoAst parse_primary_expression();

	AstList parse_arguments();

	AstList parse_parameters(bool method = false);

	AutoAst parse_parameter();

	AutoAst parse_identifier(const char *msg);

	AutoAst parse_assertion();

	AutoAst parse_concat_expression(AutoAst e);

	AutoAst parse_if_statement();

	AutoAst parse_if_block();

	AutoAst parse_while_statement();

	AutoAst parse_repeat_statement();

	AutoAst parse_for_statement();

	AutoAst parse_foreach_statement();

	AutoAst parse_class_declaration(bool local);

	AutoAst parse_function_declaration(bool local, bool method);

	AutoAst parse_function_expression();

	AutoAst parse_return_statement();

	AutoAst parse_member_expression();

	AutoAst parse_list_literal();

	AutoAst parse_array_literal();

	AutoAst parse_table_literal();

	AutoAst parse_debug_statement();

	AutoAst parse_throw_statement();

	AutoAst parse_try_statement();


	// Instance of the scanner (reads one token at a time).
	Scanner scanner;

	// Current token.
	Token token;

	// Pointer to the runtime, for string interning.
	Runtime *runtime;

	// Flag to ensure that `this` is only used in a class declaration.
	bool parsing_class = false;

	// Initializers are special because users can't return a value, but the compiler must return an instance.
	// We use this to track return statements. This is a stack because it is valid to declare a function inside
	// an initialiser.
	std::vector<bool> parsing_initializer;

	static String this_keyword;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PARSER_HPP
