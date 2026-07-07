// Phonometrica engine — the parser. See header.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/compile/parser.hpp>

#include <phon/types/atom.hpp>

#include <charconv>
#include <string>

namespace phonometrica {

namespace {

// Strip '_' digit separators from a numeric literal's spelling.
std::string strip_underscores(Substring s)
{
	std::string out;
	out.reserve(s.size());
	for (char c : s)
		if (c != '_')
			out.push_back(c);
	return out;
}

bool is_overloadable_operator(Lexeme l) noexcept
{
	switch (l)
	{
	case Lexeme::Plus:
	case Lexeme::Minus:
	case Lexeme::Star:
	case Lexeme::Slash:
	case Lexeme::Caret:
	case Lexeme::Concat:
	case Lexeme::Div:
	case Lexeme::Mod:
	case Lexeme::Eq:
	case Lexeme::NotEq:
	case Lexeme::Less:
	case Lexeme::LessEq:
	case Lexeme::Greater:
	case Lexeme::GreaterEq:
		return true;
	default:
		return false;
	}
}

} // namespace

Parser::Parser(const Source &source) : m_scanner(source) {}

// ---------------------------------------------------------------------------
// Token stream
// ---------------------------------------------------------------------------

void Parser::advance()
{
	if (m_has_peek)
	{
		m_tok = std::move(m_peek);
		m_has_peek = false;
	}
	else
	{
		m_tok = m_scanner.next();
	}
}

const Token &Parser::peek()
{
	if (!m_has_peek)
	{
		m_peek = m_scanner.next();
		m_has_peek = true;
	}
	return m_peek;
}

bool Parser::accept(Lexeme l)
{
	if (m_tok.is(l))
	{
		advance();
		return true;
	}
	return false;
}

void Parser::expect(Lexeme l, const char *what)
{
	if (!m_tok.is(l))
		error(std::string("expected ") + what + ", but got " + std::string(m_tok.describe().view()));
	advance();
}

Symbol Parser::expect_identifier(const char *what)
{
	if (!m_tok.is(Lexeme::Identifier))
		error(std::string("expected ") + what + ", but got " + std::string(m_tok.describe().view()));
	Symbol s = intern(m_tok.spelling.view());
	advance();
	return s;
}

void Parser::skip_separators()
{
	while (m_tok.is(Lexeme::Newline) || m_tok.is(Lexeme::Semicolon))
		advance();
}

bool Parser::is_block_terminator(Lexeme l) noexcept
{
	switch (l)
	{
	case Lexeme::End:
	case Lexeme::Else:
	case Lexeme::Elsif:
	case Lexeme::Until:
	case Lexeme::Catch:
	case Lexeme::Finally:
	case Lexeme::Eot:
		return true;
	default:
		return false;
	}
}

void Parser::expect_end_of_statement()
{
	if (m_tok.is(Lexeme::Newline) || m_tok.is(Lexeme::Semicolon))
	{
		skip_separators();
		return;
	}
	if (is_block_terminator(m_tok.id))
		return; // the enclosing block will consume it
	error("expected the end of the statement (a newline or ';')");
}

void Parser::error(const std::string &message)
{
	error_at(m_tok, message);
}

void Parser::error_at(const Token &tok, const std::string &message)
{
	intptr_t len = tok.spelling.empty() ? 1 : tok.spelling.size();
	error_at(tok.line, tok.column, len, message);
}

void Parser::error_at(intptr_t line, intptr_t col, intptr_t len, const std::string &message)
{
	const Source &src = m_scanner.source();
	std::string full = "[Syntax error] File \"";
	full += src.name();
	full += "\" at line ";
	full += std::to_string(line);
	full += '\n';
	full += src.caret(line, col, len);
	full += '\n';
	full += message;
	throw SyntaxError(std::move(full), line, col, len);
}

bool Parser::is_assignment_op(Lexeme l) noexcept
{
	switch (l)
	{
	case Lexeme::Assign:
	case Lexeme::PlusEq:
	case Lexeme::MinusEq:
	case Lexeme::StarEq:
	case Lexeme::SlashEq:
	case Lexeme::ConcatEq:
		return true;
	default:
		return false;
	}
}

bool Parser::is_lvalue(const Ast *node) noexcept
{
	return node->kind == NodeKind::Variable || node->kind == NodeKind::IndexExpression
	       || node->kind == NodeKind::FieldAccess;
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

AutoAst Parser::parse()
{
	advance(); // load the first token
	AstList stmts;
	skip_separators();
	while (!m_tok.is(Lexeme::Eot))
	{
		stmts.push_back(parse_statement());
		expect_end_of_statement();
	}
	return make<StatementList>(1, 0, std::move(stmts), /*scope*/ false);
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

AutoAst Parser::parse_statement()
{
	switch (m_tok.id)
	{
	case Lexeme::Local:
	case Lexeme::Global:
	case Lexeme::Open:
	case Lexeme::Ref:
		return parse_modified_declaration();
	case Lexeme::Var:
	case Lexeme::Const:
		return parse_declaration(DeclModifier::None);
	case Lexeme::Function:
		return parse_function(DeclModifier::None, /*open*/ false);
	case Lexeme::Class:
		return parse_class(DeclModifier::None, /*ref*/ false, /*open*/ false);
	case Lexeme::If:
		return parse_if_statement();
	case Lexeme::While:
		return parse_while_statement();
	case Lexeme::Repeat:
		return parse_repeat_statement();
	case Lexeme::For:
		return parse_for_statement();
	case Lexeme::Return:
		return parse_return_statement();
	case Lexeme::Break:
	case Lexeme::Continue:
	{
		if (m_loop_depth == 0)
			error(m_tok.is(Lexeme::Break) ? "'break' outside of a loop"
			                              : "'continue' outside of a loop");
		Token t = m_tok;
		advance();
		return make<LoopControl>(t.line, t.column, t.id);
	}
	case Lexeme::Try:
		return parse_try_statement();
	case Lexeme::Throw:
		return parse_throw_statement();
	case Lexeme::Spawn:
		return parse_spawn_statement();
	case Lexeme::Import:
		return parse_import_statement();
	default:
		return parse_expression_or_assignment();
	}
}

AutoAst Parser::parse_block(bool scope)
{
	int line = m_tok.line, col = m_tok.column;
	AstList stmts;
	skip_separators();
	while (!is_block_terminator(m_tok.id))
	{
		stmts.push_back(parse_statement());
		expect_end_of_statement();
	}
	return make<StatementList>(line, col, std::move(stmts), scope);
}

AutoAst Parser::parse_modified_declaration()
{
	// Modifiers come in a fixed order: `local`/`global`, then `open`, then `ref`,
	// then the declaration keyword. So the canonical spelling is
	// `local open ref class Name ...`. Out-of-order or inapplicable modifiers are
	// rejected here, anchored at the offending token.
	DeclModifier modifier = DeclModifier::None;
	bool is_open = false;
	bool is_ref = false;

	if (accept(Lexeme::Local))
		modifier = DeclModifier::Local;
	else if (accept(Lexeme::Global))
		modifier = DeclModifier::Global;

	if (accept(Lexeme::Open))
		is_open = true;

	if (accept(Lexeme::Ref))
		is_ref = true;

	switch (m_tok.id)
	{
	case Lexeme::Var:
	case Lexeme::Const:
		if (is_open)
			error("'open' cannot apply to a variable");
		if (is_ref)
			error("'ref' cannot apply to a variable");
		return parse_declaration(modifier);
	case Lexeme::Function:
		if (is_ref)
			error("'ref' cannot apply to a function");
		if (modifier == DeclModifier::Global)
			error("'global' cannot apply to a function");
		return parse_function(modifier, is_open);
	case Lexeme::Class:
		if (modifier == DeclModifier::Global)
			error("'global' cannot apply to a class");
		return parse_class(modifier, is_ref, is_open);
	default:
		error("expected a declaration ('var', 'const', 'function', or 'class'); "
		      "modifiers must be written in the order 'local'/'global', 'open', 'ref'");
	}
}

AutoAst Parser::parse_declaration(DeclModifier modifier)
{
	int line = m_tok.line, col = m_tok.column;
	bool is_const = m_tok.is(Lexeme::Const);
	advance(); // 'var' or 'const'

	Symbol name = expect_identifier("a variable name");
	AutoAst type;
	if (accept(Lexeme::As))
		type = parse_type();
	AutoAst init;
	if (accept(Lexeme::Assign))
		init = parse_expression();

	if (is_const && !init)
		error("a 'const' declaration must have an initializer");

	return make<Declaration>(line, col, name, std::move(type), std::move(init), is_const, modifier);
}

AutoAst Parser::parse_function(DeclModifier modifier, bool is_open)
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'function'

	Symbol name = parse_function_name();
	AstList params = parse_parameters();
	AutoAst return_type;
	if (accept(Lexeme::As))
		return_type = parse_type();

	AutoAst body = parse_block(/*scope*/ true);
	expect(Lexeme::End, "'end' to close the function");

	return make<FunctionDefinition>(line, col, name, std::move(params), std::move(return_type),
	                                std::move(body), modifier, /*method*/ false, is_open);
}

AutoAst Parser::parse_class(DeclModifier modifier, bool is_ref, bool is_open)
{
	int line = m_tok.line, col = m_tok.column;
	expect(Lexeme::Class, "'class'");

	Symbol name = expect_identifier("a class name");
	AutoAst parent;
	if (accept(Lexeme::Is))
		parent = parse_type();

	AstList fields, methods;
	skip_separators();
	while (!m_tok.is(Lexeme::End) && !m_tok.is(Lexeme::Eot))
	{
		if (m_tok.is(Lexeme::Local))
		{
			// `local field` — a private field, reachable only through `this`.
			advance();
			if (!m_tok.is(Lexeme::Field))
				error("only 'field' may be marked 'local' in a class body");
			auto f = parse_field();
			auto *fd = f->as<FieldDeclaration>();
			fd->is_private = true;
			if (fd->has_accessors())
				error_at(f->line, f->column, 1, "a 'local' field cannot have get/set accessors");
			fields.push_back(std::move(f));
		}
		else if (m_tok.is(Lexeme::Field))
			fields.push_back(parse_field());
		else if (m_tok.is(Lexeme::Method))
			methods.push_back(parse_method());
		else
			error("a class body may only contain 'field' and 'method' declarations");
		expect_end_of_statement();
	}

	expect(Lexeme::End, "'end' to close the class");
	return make<ClassDeclaration>(line, col, name, std::move(parent), std::move(fields),
	                              std::move(methods), modifier, is_ref, is_open);
}

// `get` / `set` are contextual: they matter only at the head of an accessor block
// inside a field body, and remain ordinary identifiers everywhere else.
static bool is_contextual(const Token &t, const char *word)
{
	return t.is(Lexeme::Identifier) && t.spelling.view() == word;
}

AutoAst Parser::parse_field()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'field'
	Symbol name = expect_identifier("a field name");
	AutoAst type;
	if (accept(Lexeme::As))
		type = parse_type();
	AutoAst default_value;
	if (accept(Lexeme::Assign))
		default_value = parse_expression();

	auto node = make<FieldDeclaration>(line, col, name, std::move(type), std::move(default_value));

	// Optional accessor block: the header is followed by a newline and then `get`
	// and/or `set`, closed by the field's own `end`.
	if (m_tok.is(Lexeme::Newline) && (is_contextual(peek(), "get") || is_contextual(peek(), "set")))
	{
		auto *fd = node->as<FieldDeclaration>();
		advance(); // the newline
		skip_separators();
		while (is_contextual(m_tok, "get") || is_contextual(m_tok, "set"))
		{
			bool is_get = is_contextual(m_tok, "get");
			int aline = m_tok.line, acol = m_tok.column;
			advance(); // 'get' / 'set'
			// An accessor body is a method body: `this` is valid inside it.
			++m_method_depth;
			if (is_get)
			{
				if (fd->getter)
					error_at(aline, acol, 3, "duplicate 'get' accessor");
				fd->getter = parse_block(/*scope*/ true);
			}
			else
			{
				if (fd->setter)
					error_at(aline, acol, 3, "duplicate 'set' accessor");
				expect(Lexeme::LParen, "'(' after 'set'");
				fd->setter_param = expect_identifier("the setter's parameter name");
				if (accept(Lexeme::As))
					fd->setter_param_type = parse_type();
				expect(Lexeme::RParen, "')' to close the setter parameter");
				fd->setter = parse_block(/*scope*/ true);
			}
			--m_method_depth;
			expect(Lexeme::End, "'end' to close the accessor");
			skip_separators();
		}
		expect(Lexeme::End, "'end' to close the field");
	}
	return node;
}

AutoAst Parser::parse_method()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'method'
	Symbol name = expect_identifier("a method name");
	AstList params = parse_parameters();
	AutoAst return_type;
	if (accept(Lexeme::As))
		return_type = parse_type();

	++m_method_depth;
	AutoAst body = parse_block(/*scope*/ true);
	--m_method_depth;
	expect(Lexeme::End, "'end' to close the method");

	return make<FunctionDefinition>(line, col, name, std::move(params), std::move(return_type),
	                                std::move(body), DeclModifier::None, /*method*/ true, /*open*/ false);
}

AutoAst Parser::parse_if_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'if'

	AstList conds, bodies;
	AutoAst cond = parse_expression();
	expect(Lexeme::Then, "'then'");
	conds.push_back(std::move(cond));
	bodies.push_back(parse_block(/*scope*/ true));

	while (accept(Lexeme::Elsif))
	{
		conds.push_back(parse_expression());
		expect(Lexeme::Then, "'then'");
		bodies.push_back(parse_block(/*scope*/ true));
	}

	AutoAst else_body;
	if (accept(Lexeme::Else))
		else_body = parse_block(/*scope*/ true);

	expect(Lexeme::End, "'end' to close the 'if'");
	return make<IfStatement>(line, col, std::move(conds), std::move(bodies), std::move(else_body));
}

AutoAst Parser::parse_while_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'while'
	AutoAst cond = parse_expression();
	expect(Lexeme::Do, "'do'");
	++m_loop_depth;
	AutoAst body = parse_block(/*scope*/ true);
	--m_loop_depth;
	expect(Lexeme::End, "'end' to close the 'while'");
	return make<WhileStatement>(line, col, std::move(cond), std::move(body));
}

AutoAst Parser::parse_repeat_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'repeat'
	++m_loop_depth;
	AutoAst body = parse_block(/*scope*/ true);
	--m_loop_depth;
	expect(Lexeme::Until, "'until'");
	AutoAst cond = parse_expression();
	return make<RepeatStatement>(line, col, std::move(body), std::move(cond));
}

AutoAst Parser::parse_for_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'for'

	// An optional `ref` before the first variable is only meaningful for the
	// single-variable iteration form (`for ref x in`). If the first variable
	// turns out to be a key/index (pair form) or a counted-loop variable, taking
	// it by reference is a syntax error, reported below.
	int first_ref_line = m_tok.line, first_ref_col = m_tok.column;
	bool first_ref = accept(Lexeme::Ref);

	Symbol first = expect_identifier("a loop variable");

	if (accept(Lexeme::Comma))
	{
		// for key, [ref] value in collection do ... end
		if (first_ref)
			error_at(first_ref_line, first_ref_col, 3,
			         "the key/index of a 'for ... in' loop cannot be taken by reference");
		bool value_ref = accept(Lexeme::Ref);
		Symbol value = expect_identifier("a second loop variable");
		expect(Lexeme::In, "'in'");
		AutoAst coll = parse_expression();
		expect(Lexeme::Do, "'do'");
		++m_loop_depth;
		AutoAst body = parse_block(/*scope*/ true);
		--m_loop_depth;
		expect(Lexeme::End, "'end' to close the 'for'");
		return make<ForEach>(line, col, first, value, value_ref, std::move(coll), std::move(body));
	}

	if (accept(Lexeme::Assign))
	{
		// counted: for i = start to stop [step s] do ... end
		if (first_ref)
			error_at(first_ref_line, first_ref_col, 3,
			         "a counted-loop variable cannot be taken by reference");
		AutoAst start = parse_expression();
		expect(Lexeme::To, "'to'");
		AutoAst stop = parse_expression();
		AutoAst step;
		if (accept(Lexeme::Step))
			step = parse_expression();
		expect(Lexeme::Do, "'do'");
		++m_loop_depth;
		AutoAst body = parse_block(/*scope*/ true);
		--m_loop_depth;
		expect(Lexeme::End, "'end' to close the 'for'");
		return make<ForNumeric>(line, col, first, std::move(start), std::move(stop),
		                        std::move(step), std::move(body));
	}

	if (accept(Lexeme::In))
	{
		// for [ref] value in collection do ... end
		AutoAst coll = parse_expression();
		expect(Lexeme::Do, "'do'");
		++m_loop_depth;
		AutoAst body = parse_block(/*scope*/ true);
		--m_loop_depth;
		expect(Lexeme::End, "'end' to close the 'for'");
		return make<ForEach>(line, col, NO_SYMBOL, first, first_ref, std::move(coll), std::move(body));
	}

	error("expected '=', 'in', or ',' after the loop variable");
}

AutoAst Parser::parse_return_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'return'
	AutoAst expr;
	if (!m_tok.is(Lexeme::Newline) && !m_tok.is(Lexeme::Semicolon) && !is_block_terminator(m_tok.id))
		expr = parse_expression();
	return make<ReturnStatement>(line, col, std::move(expr));
}

AutoAst Parser::parse_try_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'try'

	AutoAst body = parse_block(/*scope*/ true);

	AstList catches;
	while (m_tok.is(Lexeme::Catch))
	{
		int cline = m_tok.line, ccol = m_tok.column;
		advance(); // 'catch'
		Symbol name = NO_SYMBOL;
		AutoAst type;
		if (m_tok.is(Lexeme::Identifier))
		{
			name = intern(m_tok.spelling.view());
			advance();
			if (accept(Lexeme::As))
				type = parse_type();
		}
		AutoAst cbody = parse_block(/*scope*/ true);
		catches.push_back(make<CatchClause>(cline, ccol, name, std::move(type), std::move(cbody)));
	}

	AutoAst finally_body;
	if (accept(Lexeme::Finally))
		finally_body = parse_block(/*scope*/ true);

	if (catches.empty() && !finally_body)
		error("a 'try' needs at least one 'catch' or a 'finally'");

	expect(Lexeme::End, "'end' to close the 'try'");
	return make<TryStatement>(line, col, std::move(body), std::move(catches), std::move(finally_body));
}

AutoAst Parser::parse_throw_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'throw'
	AutoAst expr = parse_expression();
	return make<ThrowStatement>(line, col, std::move(expr));
}

AutoAst Parser::parse_spawn_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'spawn'
	AutoAst call = parse_expression();
	if (call->kind != NodeKind::CallExpression)
		error_at(m_tok, "'spawn' must be followed by a function call");
	return make<SpawnStatement>(line, col, std::move(call));
}

AutoAst Parser::parse_import_statement()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'import'
	Symbol module = expect_identifier("a module name");
	return make<ImportStatement>(line, col, module);
}

AutoAst Parser::parse_expression_or_assignment()
{
	int line = m_tok.line, col = m_tok.column;
	AutoAst expr = parse_expression();

	if (is_assignment_op(m_tok.id))
	{
		Lexeme op = m_tok.id;
		if (!is_lvalue(expr.get()))
			error_at(expr->line, expr->column, 1,
			         "the left-hand side of an assignment must be a variable, field, or index");
		advance();
		AutoAst value = parse_expression();
		return make<Assignment>(line, col, op, std::move(expr), std::move(value));
	}

	return make<ExpressionStatement>(line, col, std::move(expr));
}

// ---------------------------------------------------------------------------
// Parameters and arguments
// ---------------------------------------------------------------------------

AstList Parser::parse_parameters()
{
	expect(Lexeme::LParen, "'(' to open the parameter list");
	AstList params;

	bool seen_variadic = false;
	bool seen_option = false;

	if (!m_tok.is(Lexeme::RParen))
	{
		do
		{
			AutoAst p = parse_parameter();
			auto *param = static_cast<Parameter *>(p.get());

			bool is_option = param->default_value != nullptr;
			if (seen_option && !is_option)
				error("a required parameter cannot follow a keyword-only option");
			if (seen_variadic && !is_option)
				error("only keyword-only options may follow a variadic parameter");
			if (param->variadic)
			{
				if (seen_variadic)
					error("a function may have at most one variadic parameter");
				if (param->by_ref)
					error("a variadic parameter cannot be 'ref'");
				if (is_option)
					error("a variadic parameter cannot have a default value");
				seen_variadic = true;
			}
			if (is_option)
				seen_option = true;

			params.push_back(std::move(p));
		} while (accept(Lexeme::Comma));
	}

	expect(Lexeme::RParen, "')' to close the parameter list");
	return params;
}

AutoAst Parser::parse_parameter()
{
	int line = m_tok.line, col = m_tok.column;
	bool by_ref = accept(Lexeme::Ref);
	Symbol name = expect_identifier("a parameter name");
	AutoAst type;
	if (accept(Lexeme::As))
		type = parse_type();
	bool variadic = accept(Lexeme::Ellipsis);
	AutoAst default_value;
	if (accept(Lexeme::Assign))
		default_value = parse_expression();
	return make<Parameter>(line, col, name, std::move(type), by_ref, variadic, std::move(default_value));
}

void Parser::parse_call_arguments(AstList &args, AstList &options)
{
	expect(Lexeme::LParen, "'('");
	if (m_tok.is(Lexeme::RParen))
	{
		advance();
		return;
	}

	do
	{
		// Keyword-only option: `name = value` (an identifier immediately before '=').
		if (m_tok.is(Lexeme::Identifier) && peek().is(Lexeme::Assign))
		{
			int line = m_tok.line, col = m_tok.column;
			Symbol name = intern(m_tok.spelling.view());
			advance(); // identifier
			advance(); // '='
			AutoAst value = parse_expression();
			options.push_back(make<NamedArgument>(line, col, name, std::move(value)));
			continue;
		}

		if (!options.empty())
			error("a positional argument cannot follow a keyword argument");

		int line = m_tok.line, col = m_tok.column;
		// Call-site `ref` is gone (design/references.md §1): whether an argument is
		// passed by reference is determined by the callee's signature, not the call.
		if (check(Lexeme::Ref))
			error("'ref' at a call site is no longer used; pass the argument directly "
			      "(a parameter's 'ref' in the function's signature makes it by-reference)");
		AutoAst e = parse_expression();
		if (accept(Lexeme::Ellipsis))
			e = make<SplatExpression>(line, col, std::move(e)); // `xs...` splat
		args.push_back(std::move(e));
	} while (accept(Lexeme::Comma));

	expect(Lexeme::RParen, "')' to close the argument list");
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

AutoAst Parser::parse_expression()
{
	// Thin-arrow lambda: `identifier -> expr`.
	if (m_tok.is(Lexeme::Identifier) && peek().is(Lexeme::Arrow))
	{
		int line = m_tok.line, col = m_tok.column;
		Symbol param = intern(m_tok.spelling.view());
		advance(); // identifier
		advance(); // '->'
		return parse_lambda(param, line, col);
	}
	return parse_or();
}

AutoAst Parser::parse_lambda(Symbol param, int line, int col)
{
	// Desugar `x -> e` to an anonymous function whose body is `return e`.
	AstList params;
	params.push_back(make<Parameter>(line, col, param, AutoAst(), /*ref*/ false, /*variadic*/ false, AutoAst()));

	AutoAst expr = parse_expression();
	int bl = expr->line, bc = expr->column;
	AstList body_stmts;
	body_stmts.push_back(make<ReturnStatement>(bl, bc, std::move(expr)));
	AutoAst body = make<StatementList>(bl, bc, std::move(body_stmts), /*scope*/ true);

	return make<FunctionDefinition>(line, col, NO_SYMBOL, std::move(params), AutoAst(),
	                                std::move(body), DeclModifier::None, /*method*/ false, /*open*/ false);
}

AutoAst Parser::parse_or()
{
	AutoAst left = parse_and();
	while (m_tok.is(Lexeme::Or))
	{
		int line = left->line, col = left->column;
		advance();
		AutoAst right = parse_and();
		left = make<BinaryExpression>(line, col, Lexeme::Or, std::move(left), std::move(right));
	}
	return left;
}

AutoAst Parser::parse_and()
{
	AutoAst left = parse_not();
	while (m_tok.is(Lexeme::And))
	{
		int line = left->line, col = left->column;
		advance();
		AutoAst right = parse_not();
		left = make<BinaryExpression>(line, col, Lexeme::And, std::move(left), std::move(right));
	}
	return left;
}

AutoAst Parser::parse_not()
{
	if (m_tok.is(Lexeme::Not))
	{
		int line = m_tok.line, col = m_tok.column;
		advance();
		AutoAst operand = parse_not();
		return make<UnaryExpression>(line, col, Lexeme::Not, std::move(operand));
	}
	return parse_comparison();
}

AutoAst Parser::parse_comparison()
{
	AutoAst left = parse_concat();
	for (;;)
	{
		Lexeme op = m_tok.id;
		if (op == Lexeme::Eq || op == Lexeme::NotEq || op == Lexeme::Less || op == Lexeme::LessEq
		    || op == Lexeme::Greater || op == Lexeme::GreaterEq)
		{
			int line = left->line, col = left->column;
			advance();
			AutoAst right = parse_concat();
			left = make<BinaryExpression>(line, col, op, std::move(left), std::move(right));
		}
		else if (op == Lexeme::Is)
		{
			int line = left->line, col = left->column;
			advance();
			AutoAst type = parse_type();
			left = make<IsExpression>(line, col, std::move(left), std::move(type));
		}
		else
		{
			break;
		}
	}
	return left;
}

AutoAst Parser::parse_concat()
{
	AutoAst left = parse_additive();
	if (!m_tok.is(Lexeme::Concat))
		return left;

	int line = left->line, col = left->column;
	AstList parts;
	parts.push_back(std::move(left));
	while (accept(Lexeme::Concat))
		parts.push_back(parse_additive());
	return make<ConcatExpression>(line, col, std::move(parts));
}

AutoAst Parser::parse_additive()
{
	AutoAst left = parse_multiplicative();
	while (m_tok.is(Lexeme::Plus) || m_tok.is(Lexeme::Minus))
	{
		int line = left->line, col = left->column;
		Lexeme op = m_tok.id;
		advance();
		AutoAst right = parse_multiplicative();
		left = make<BinaryExpression>(line, col, op, std::move(left), std::move(right));
	}
	return left;
}

AutoAst Parser::parse_multiplicative()
{
	AutoAst left = parse_unary();
	while (m_tok.is(Lexeme::Star) || m_tok.is(Lexeme::Slash) || m_tok.is(Lexeme::Div)
	       || m_tok.is(Lexeme::Mod))
	{
		int line = left->line, col = left->column;
		Lexeme op = m_tok.id;
		advance();
		AutoAst right = parse_unary();
		left = make<BinaryExpression>(line, col, op, std::move(left), std::move(right));
	}
	return left;
}

AutoAst Parser::parse_unary()
{
	if (m_tok.is(Lexeme::Minus) || m_tok.is(Lexeme::Plus))
	{
		int line = m_tok.line, col = m_tok.column;
		Lexeme op = m_tok.id;
		advance();
		AutoAst operand = parse_unary();
		return make<UnaryExpression>(line, col, op, std::move(operand));
	}
	return parse_power();
}

AutoAst Parser::parse_power()
{
	AutoAst base = parse_postfix();
	if (m_tok.is(Lexeme::Caret))
	{
		int line = base->line, col = base->column;
		advance();
		AutoAst exponent = parse_unary(); // right-associative; allows 2^-3
		return make<BinaryExpression>(line, col, Lexeme::Caret, std::move(base), std::move(exponent));
	}
	return base;
}

AutoAst Parser::parse_postfix()
{
	AutoAst e = parse_primary();
	for (;;)
	{
		if (m_tok.is(Lexeme::LParen))
		{
			int line = e->line, col = e->column;
			AstList args, options;
			parse_call_arguments(args, options);
			e = make<CallExpression>(line, col, std::move(e), std::move(args), std::move(options));
		}
		else if (m_tok.is(Lexeme::LSquare))
		{
			int line = e->line, col = e->column;
			advance(); // '['
			AstList indices;
			indices.push_back(parse_index_element());
			while (accept(Lexeme::Comma))
				indices.push_back(parse_index_element());
			expect(Lexeme::RSquare, "']' to close the index");
			e = make<IndexExpression>(line, col, std::move(e), std::move(indices));
		}
		else if (m_tok.is(Lexeme::Dot))
		{
			int line = e->line, col = e->column;
			advance(); // '.'
			Symbol name = expect_identifier("a field name after '.'");
			e = make<FieldAccess>(line, col, std::move(e), name);
		}
		else
		{
			break;
		}
	}
	return e;
}

AutoAst Parser::parse_index_element()
{
	int line = m_tok.line, col = m_tok.column;
	AutoAst start;
	if (!m_tok.is(Lexeme::Colon))
		start = parse_expression();

	if (accept(Lexeme::Colon))
	{
		AutoAst stop;
		if (!m_tok.is(Lexeme::Comma) && !m_tok.is(Lexeme::RSquare) && !m_tok.is(Lexeme::Step))
			stop = parse_expression();
		AutoAst step;
		if (accept(Lexeme::Step))
			step = parse_expression();
		return make<SliceExpression>(line, col, std::move(start), std::move(stop), std::move(step));
	}

	return start; // a plain (non-slice) index
}

AutoAst Parser::parse_primary()
{
	int line = m_tok.line, col = m_tok.column;
	switch (m_tok.id)
	{
	case Lexeme::Integer:
	{
		std::string digits = strip_underscores(m_tok.spelling.view());
		int64_t value = 0;
		auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
		if (ec != std::errc() || ptr != digits.data() + digits.size())
			error("integer literal is out of range");
		advance();
		return make<IntegerLiteral>(line, col, value);
	}
	case Lexeme::Float:
	{
		std::string digits = strip_underscores(m_tok.spelling.view());
		double value = 0;
		auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
		if (ec != std::errc() || ptr != digits.data() + digits.size())
			error("floating-point literal is malformed");
		advance();
		return make<FloatLiteral>(line, col, value);
	}
	case Lexeme::String:
	{
		String value = m_tok.spelling;
		advance();
		return make<StringLiteral>(line, col, std::move(value));
	}
	case Lexeme::InterpStart:
	{
		AstList parts;
		auto push_chunk = [&](const Token &t) {
			if (!t.spelling.empty())
				parts.push_back(make<StringLiteral>(t.line, t.column, t.spelling));
		};
		push_chunk(m_tok);
		advance();
		for (;;)
		{
			parts.push_back(parse_expression());
			if (m_tok.is(Lexeme::InterpMid))
			{
				push_chunk(m_tok);
				advance();
				continue;
			}
			if (m_tok.is(Lexeme::InterpEnd))
			{
				push_chunk(m_tok);
				advance();
				break;
			}
			error("malformed string interpolation");
		}
		return make<StringInterpolation>(line, col, std::move(parts));
	}
	case Lexeme::True:
		advance();
		return make<BoolLiteral>(line, col, true);
	case Lexeme::False:
		advance();
		return make<BoolLiteral>(line, col, false);
	case Lexeme::Null:
		advance();
		return make<NullLiteral>(line, col);
	case Lexeme::This:
		if (m_method_depth == 0)
			error("'this' is only valid inside a method body");
		advance();
		return make<ThisExpression>(line, col);
	case Lexeme::Identifier:
	{
		Symbol name = intern(m_tok.spelling.view());
		advance();
		return make<Variable>(line, col, name);
	}
	case Lexeme::LParen:
	{
		advance();
		AutoAst e = parse_expression();
		expect(Lexeme::RParen, "')'");
		return e;
	}
	case Lexeme::LSquare:
		return parse_list_literal();
	case Lexeme::At:
		return parse_array_literal();
	case Lexeme::LBrace:
		return parse_brace_literal();
	case Lexeme::Function:
		return parse_anonymous_function();
	case Lexeme::If:
		return parse_conditional_expression();
	case Lexeme::Cast:
		return parse_cast_expression();
	default:
		error(std::string("expected an expression, but got ") + std::string(m_tok.describe().view()));
	}
}

AutoAst Parser::parse_list_literal()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // '['
	AstList items;
	if (!m_tok.is(Lexeme::RSquare))
	{
		items.push_back(parse_expression());
		while (accept(Lexeme::Comma))
		{
			if (m_tok.is(Lexeme::RSquare))
				break; // trailing comma
			items.push_back(parse_expression());
		}
	}
	expect(Lexeme::RSquare, "']' to close the list");
	return make<ListLiteral>(line, col, std::move(items));
}

AutoAst Parser::parse_array_literal()
{
	// `@[a, b, c]` (1-D) or `@[a, b; c, d]` (2-D). Rows are separated by `;` and must
	// all have the same number of columns (design §9). Elements are in row-major source
	// order; the shape drives the column-major construction at lowering.
	int line = m_tok.line, col = m_tok.column;
	advance(); // '@'
	expect(Lexeme::LSquare, "'[' after '@' to open an array literal");
	AstList elems;
	int rank = 1, nrow = 0, ncol = 0, row_start = 0;
	if (!m_tok.is(Lexeme::RSquare))
	{
		for (;;)
		{
			elems.push_back(parse_expression());
			if (accept(Lexeme::Comma))
			{
				if (m_tok.is(Lexeme::RSquare) || m_tok.is(Lexeme::Semicolon))
					error("expected an array element after ','");
				continue;
			}
			if (accept(Lexeme::Semicolon))
			{
				int this_cols = static_cast<int>(elems.size()) - row_start;
				if (rank == 1)
				{
					rank = 2;
					ncol = this_cols;
					nrow = 1;
				}
				else if (this_cols != ncol)
					error("array rows must all have the same number of columns");
				else
					++nrow;
				row_start = static_cast<int>(elems.size());
				continue;
			}
			break;
		}
		if (rank == 2)
		{
			int last_cols = static_cast<int>(elems.size()) - row_start;
			if (last_cols != ncol)
				error("array rows must all have the same number of columns");
			++nrow;
		}
	}
	expect(Lexeme::RSquare, "']' to close the array literal");
	if (rank == 1)
	{
		nrow = 1;
		ncol = static_cast<int>(elems.size());
	}
	return make<ArrayLiteral>(line, col, std::move(elems), rank, nrow, ncol);
}

AutoAst Parser::parse_brace_literal()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // '{'

	if (accept(Lexeme::RBrace))
		return make<TableLiteral>(line, col, AstList(), AstList()); // {} is an empty Table

	AutoAst first = parse_expression();

	if (accept(Lexeme::Colon))
	{
		// Table literal: { k: v, ... }
		AstList keys, values;
		keys.push_back(std::move(first));
		values.push_back(parse_expression());
		while (accept(Lexeme::Comma))
		{
			if (m_tok.is(Lexeme::RBrace))
				break; // trailing comma
			keys.push_back(parse_expression());
			expect(Lexeme::Colon, "':' between a key and its value");
			values.push_back(parse_expression());
		}
		expect(Lexeme::RBrace, "'}' to close the table");
		return make<TableLiteral>(line, col, std::move(keys), std::move(values));
	}

	// Set literal: { a, b, c }
	AstList items;
	items.push_back(std::move(first));
	while (accept(Lexeme::Comma))
	{
		if (m_tok.is(Lexeme::RBrace))
			break; // trailing comma
		items.push_back(parse_expression());
	}
	expect(Lexeme::RBrace, "'}' to close the set");
	return make<SetLiteral>(line, col, std::move(items));
}

AutoAst Parser::parse_anonymous_function()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'function'
	AstList params = parse_parameters();
	AutoAst return_type;
	if (accept(Lexeme::As))
		return_type = parse_type();
	AutoAst body = parse_block(/*scope*/ true);
	expect(Lexeme::End, "'end' to close the function");
	return make<FunctionDefinition>(line, col, NO_SYMBOL, std::move(params), std::move(return_type),
	                                std::move(body), DeclModifier::None, /*method*/ false, /*open*/ false);
}

AutoAst Parser::parse_conditional_expression()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'if'

	AstList conds, values;
	conds.push_back(parse_expression());
	expect(Lexeme::Then, "'then'");
	values.push_back(parse_expression());

	while (accept(Lexeme::Elsif))
	{
		conds.push_back(parse_expression());
		expect(Lexeme::Then, "'then'");
		values.push_back(parse_expression());
	}

	expect(Lexeme::Else, "'else' (an 'if' expression must have an 'else')");
	AutoAst else_value = parse_expression();
	expect(Lexeme::End, "'end' to close the 'if' expression");
	return make<ConditionalExpression>(line, col, std::move(conds), std::move(values), std::move(else_value));
}

AutoAst Parser::parse_cast_expression()
{
	int line = m_tok.line, col = m_tok.column;
	advance(); // 'cast'
	AutoAst expr = parse_expression();
	expect(Lexeme::As, "'as' in a cast expression");
	AutoAst type = parse_type();
	return make<CastExpression>(line, col, std::move(expr), std::move(type));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

AutoAst Parser::parse_type()
{
	int line = m_tok.line, col = m_tok.column;
	Symbol name = expect_identifier("a type name");
	AutoAst e = make<Variable>(line, col, name);
	while (accept(Lexeme::Dot))
	{
		int fline = m_tok.line, fcol = m_tok.column;
		Symbol field = expect_identifier("a type name after '.'");
		e = make<FieldAccess>(fline, fcol, std::move(e), field);
	}
	return e;
}

Symbol Parser::parse_function_name()
{
	if (m_tok.is(Lexeme::Identifier))
	{
		Symbol s = intern(m_tok.spelling.view());
		advance();
		return s;
	}
	if (is_overloadable_operator(m_tok.id))
	{
		Symbol s = intern(lexeme_name(m_tok.id));
		advance();
		return s;
	}
	error("expected a function name (an identifier or an overloadable operator)");
}

} // namespace phonometrica
