// Phonometrica engine — the parser (design/design.md §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Recursive descent with Pratt-style precedence climbing for expressions
// (architecture §9.1). Drives the Scanner one token at a time with a single-token
// lookahead (`peek()`), and builds the AST (ast.hpp). Errors throw SyntaxError.
//
// A module parses to a top-level StatementList; the two-pass top-level treatment
// (design §11) is a lowering concern (M4) — the parser only produces the tree.

#ifndef PHON_COMPILE_PARSER_HPP
#define PHON_COMPILE_PARSER_HPP

#include <phon/engine/compile/ast.hpp>
#include <phon/engine/compile/scanner.hpp>
#include <phon/engine/compile/source.hpp>

#include <string>

namespace phonometrica {

class Parser final
{
public:
	explicit Parser(const Source &source);

	// Parse the whole source into a module-level StatementList.
	AutoAst parse();

private:
	// --- token stream (current + one lookahead) ---
	void advance();
	const Token &peek();
	bool check(Lexeme l) const noexcept { return m_tok.is(l); }
	bool accept(Lexeme l);                 // consume if current is `l`
	void expect(Lexeme l, const char *what);
	void skip_separators();                // newlines / semicolons
	void expect_end_of_statement();
	static bool is_block_terminator(Lexeme l) noexcept;

	[[noreturn]] void error(const std::string &message);
	[[noreturn]] void error_at(const Token &tok, const std::string &message);
	[[noreturn]] void error_at(intptr_t line, intptr_t col, intptr_t len, const std::string &message);

	template<class T, class... Args>
	AutoAst make(int line, int col, Args &&...args)
	{
		return std::make_unique<T>(line, col, std::forward<Args>(args)...);
	}

	// --- statements ---
	AutoAst parse_statement();
	AutoAst parse_block(bool scope);
	AutoAst parse_modified_declaration();
	AutoAst parse_declaration(DeclModifier modifier);
	AutoAst parse_function(DeclModifier modifier, bool is_open);
	AutoAst parse_class(DeclModifier modifier, bool is_ref, bool is_open);
	AutoAst parse_field();
	AutoAst parse_method();
	AutoAst parse_if_statement();
	AutoAst parse_while_statement();
	AutoAst parse_repeat_statement();
	AutoAst parse_for_statement();
	AutoAst parse_return_statement();
	AutoAst parse_try_statement();
	AutoAst parse_throw_statement();
	AutoAst parse_spawn_statement();
	AutoAst parse_import_statement();
	AutoAst parse_expression_or_assignment();

	// --- parameters / arguments ---
	AstList parse_parameters();
	AutoAst parse_parameter();
	void parse_call_arguments(AstList &args, AstList &options);

	// --- expressions (precedence climbing) ---
	AutoAst parse_expression();
	AutoAst parse_or();
	AutoAst parse_and();
	AutoAst parse_not();
	AutoAst parse_comparison();
	AutoAst parse_concat();
	AutoAst parse_additive();
	AutoAst parse_multiplicative();
	AutoAst parse_unary();
	AutoAst parse_power();
	AutoAst parse_postfix();
	AutoAst parse_primary();
	AutoAst parse_list_literal();
	// Called from parse_list_literal once `for` is seen, with the already-parsed
	// yield expression and the `[`'s position.
	AutoAst parse_list_comprehension_tail(int line, int col, AutoAst yield_expr);
	AutoAst parse_array_literal();
	AutoAst parse_brace_literal();
	AutoAst parse_anonymous_function();
	AutoAst parse_conditional_expression();
	AutoAst parse_cast_expression();
	AutoAst parse_lambda(Symbol param, int line, int col);
	AutoAst parse_index_element();

	// --- helpers ---
	AutoAst parse_type();
	Symbol parse_function_name();
	Symbol expect_identifier(const char *what);
	static bool is_assignment_op(Lexeme l) noexcept;
	static bool is_lvalue(const Ast *node) noexcept;

	Scanner m_scanner;
	Token m_tok;          // current token
	Token m_peek;         // lookahead (valid iff m_has_peek)
	bool m_has_peek = false;

	// Nesting depth of enclosing method bodies, so `this` outside a method errors.
	int m_method_depth = 0;
	// Nesting depth of enclosing loops, so break/continue outside a loop errors.
	int m_loop_depth = 0;
};

} // namespace phonometrica

#endif // PHON_COMPILE_PARSER_HPP
