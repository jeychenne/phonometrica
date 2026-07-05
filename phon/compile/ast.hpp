// Phonometrica engine — abstract syntax tree (design/design.md §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The parser (M3 step 3) builds an AST out of these nodes; later passes (the AST
// dumper for golden tests, and lowering in M4) walk it via the visitor pattern —
// following Phonometrica's design. Each node derives from `Ast`, carries its
// source position, tags itself with a `NodeKind` (a cheap, RTTI-free type test),
// and accepts an `AstVisitor`. Ownership is a plain `unique_ptr` tree
// (`AutoAst`); `std::vector`/`unique_ptr` are permitted in the compiler layer
// (architecture §0). Names are interned to `Symbol` at parse time (§9.1).

#ifndef PHON_COMPILE_AST_HPP
#define PHON_COMPILE_AST_HPP

#include <phon/compile/token.hpp> // Lexeme (operator tags)
#include <phon/core/symbol.hpp>
#include <phon/types/string.hpp>

#include <memory>
#include <vector>

namespace phonometrica {

class AstVisitor;
struct Ast;

using AutoAst = std::unique_ptr<Ast>;
using AstList = std::vector<AutoAst>;

// Every concrete node kind. Used for RTTI-free `is<T>()` / `as<T>()` tests.
enum class NodeKind
{
	// expressions
	NullLiteral, BoolLiteral, IntegerLiteral, FloatLiteral, StringLiteral,
	StringInterpolation, ListLiteral, TableLiteral, SetLiteral, Variable,
	ThisExpression, UnaryExpression, BinaryExpression, ConcatExpression,
	IsExpression, CastExpression, IndexExpression, SliceExpression, FieldAccess,
	CallExpression, SplatExpression, RefExpression, NamedArgument,
	ConditionalExpression, FunctionDefinition,
	// statements
	StatementList, Declaration, Assignment, ExpressionStatement, IfStatement,
	WhileStatement, RepeatStatement, ForNumeric, ForEach, LoopControl,
	ReturnStatement, Parameter, ClassDeclaration, FieldDeclaration,
	TryStatement, CatchClause, ThrowStatement, SpawnStatement, ImportStatement
};

// Syntactic visibility modifier on a top-level declaration (§11). Which storage
// tier this maps to (module vs. function-local) is resolved in lowering, from
// nesting; the parser only records the keyword the user wrote.
enum class DeclModifier
{
	None,  // default: module-public at top level, lexical local inside a function
	Local, // `local` — module-private
	Global // `global` — isolate-global (top level only)
};

// Base class for all AST nodes.
struct Ast
{
	Ast(NodeKind kind, int line, int column) : kind(kind), line(line), column(column) {}
	virtual ~Ast() = default;

	virtual void visit(AstVisitor &v) = 0;

	template<class T>
	bool is() const noexcept { return kind == T::KIND; }

	template<class T>
	T *as() noexcept { return kind == T::KIND ? static_cast<T *>(this) : nullptr; }

	NodeKind kind;
	int line;   // 1-based line of the node's first token
	int column; // 0-based byte column of the node's first token
};

// CRTP helper: gives each concrete node its `KIND` constant, a `visit` that
// dispatches to `AstVisitor::visit_x`, and a base-forwarding constructor.
// (visit bodies live in ast.cpp, where AstVisitor is complete.)
#define PHON_AST_NODE(Name, Kind)                                              \
	static constexpr NodeKind KIND = NodeKind::Kind;                           \
	void visit(AstVisitor &v) override;

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

struct NullLiteral final : Ast
{
	NullLiteral(int line, int col) : Ast(KIND, line, col) {}
	PHON_AST_NODE(NullLiteral, NullLiteral)
};

struct BoolLiteral final : Ast
{
	BoolLiteral(int line, int col, bool value) : Ast(KIND, line, col), value(value) {}
	PHON_AST_NODE(BoolLiteral, BoolLiteral)
	bool value;
};

struct IntegerLiteral final : Ast
{
	IntegerLiteral(int line, int col, int64_t value) : Ast(KIND, line, col), value(value) {}
	PHON_AST_NODE(IntegerLiteral, IntegerLiteral)
	int64_t value;
};

struct FloatLiteral final : Ast
{
	FloatLiteral(int line, int col, double value) : Ast(KIND, line, col), value(value) {}
	PHON_AST_NODE(FloatLiteral, FloatLiteral)
	double value;
};

struct StringLiteral final : Ast
{
	StringLiteral(int line, int col, String value) : Ast(KIND, line, col), value(std::move(value)) {}
	PHON_AST_NODE(StringLiteral, StringLiteral)
	String value;
};

// "a{x}b" — an interpolated string. `parts` alternates literal chunks (as
// StringLiteral nodes, possibly empty) and embedded expressions; it lowers to a
// single stringifying concatenation (design §10). Kept as a distinct node so
// golden dumps stay readable.
struct StringInterpolation final : Ast
{
	StringInterpolation(int line, int col, AstList parts) : Ast(KIND, line, col), parts(std::move(parts)) {}
	PHON_AST_NODE(StringInterpolation, StringInterpolation)
	AstList parts;
};

// [a, b, c] — a List literal.
struct ListLiteral final : Ast
{
	ListLiteral(int line, int col, AstList items) : Ast(KIND, line, col), items(std::move(items)) {}
	PHON_AST_NODE(ListLiteral, ListLiteral)
	AstList items;
};

// {k: v, ...} — a Table literal. `keys[i]` pairs with `values[i]`.
struct TableLiteral final : Ast
{
	TableLiteral(int line, int col, AstList keys, AstList values)
	    : Ast(KIND, line, col), keys(std::move(keys)), values(std::move(values)) {}
	PHON_AST_NODE(TableLiteral, TableLiteral)
	AstList keys, values;
};

// {a, b, c} — a Set literal (no colons). Empty `{}` parses as a Table.
struct SetLiteral final : Ast
{
	SetLiteral(int line, int col, AstList items) : Ast(KIND, line, col), items(std::move(items)) {}
	PHON_AST_NODE(SetLiteral, SetLiteral)
	AstList items;
};

// A bare identifier used as a value / assignment target.
struct Variable final : Ast
{
	Variable(int line, int col, Symbol name) : Ast(KIND, line, col), name(name) {}
	PHON_AST_NODE(Variable, Variable)
	Symbol name;
};

// `this`, valid only inside a method body.
struct ThisExpression final : Ast
{
	ThisExpression(int line, int col) : Ast(KIND, line, col) {}
	PHON_AST_NODE(ThisExpression, ThisExpression)
};

// Prefix `-x`, `+x`, `not x`. `op` is Minus / Plus / Not.
struct UnaryExpression final : Ast
{
	UnaryExpression(int line, int col, Lexeme op, AutoAst operand)
	    : Ast(KIND, line, col), op(op), operand(std::move(operand)) {}
	PHON_AST_NODE(UnaryExpression, UnaryExpression)
	Lexeme op;
	AutoAst operand;
};

// Binary arithmetic/comparison/logic: + - * / ^ div mod == != < <= > >= and or.
// (`&` chains use ConcatExpression; `is`/`cast` have their own nodes.)
struct BinaryExpression final : Ast
{
	BinaryExpression(int line, int col, Lexeme op, AutoAst lhs, AutoAst rhs)
	    : Ast(KIND, line, col), op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
	PHON_AST_NODE(BinaryExpression, BinaryExpression)
	Lexeme op;
	AutoAst lhs, rhs;
};

// `a & b & c` — a stringifying concatenation chain, folded into one variadic
// node (lowers to a single CONCAT, design §10).
struct ConcatExpression final : Ast
{
	ConcatExpression(int line, int col, AstList parts) : Ast(KIND, line, col), parts(std::move(parts)) {}
	PHON_AST_NODE(ConcatExpression, ConcatExpression)
	AstList parts;
};

// `x is T` — runtime type test. `type` is the class expression.
struct IsExpression final : Ast
{
	IsExpression(int line, int col, AutoAst expr, AutoAst type)
	    : Ast(KIND, line, col), expr(std::move(expr)), type(std::move(type)) {}
	PHON_AST_NODE(IsExpression, IsExpression)
	AutoAst expr, type;
};

// `cast x as T` — checked conversion (lowers to the `cast` generic, §7).
struct CastExpression final : Ast
{
	CastExpression(int line, int col, AutoAst expr, AutoAst type)
	    : Ast(KIND, line, col), expr(std::move(expr)), type(std::move(type)) {}
	PHON_AST_NODE(CastExpression, CastExpression)
	AutoAst expr, type;
};

// `a[i]`, `a[i, j]`, `m[:, 3]` — indexing/slicing. Each element of `indices` is
// an ordinary expression or a SliceExpression.
struct IndexExpression final : Ast
{
	IndexExpression(int line, int col, AutoAst object, AstList indices)
	    : Ast(KIND, line, col), object(std::move(object)), indices(std::move(indices)) {}
	PHON_AST_NODE(IndexExpression, IndexExpression)
	AutoAst object;
	AstList indices;
};

// A slice inside `[...]`: `a:b`, `a:b step s`, `:`, `3:`, `:5`. Any of the three
// parts may be null (open end / bare colon). Never appears outside brackets.
struct SliceExpression final : Ast
{
	SliceExpression(int line, int col, AutoAst start, AutoAst stop, AutoAst step)
	    : Ast(KIND, line, col), start(std::move(start)), stop(std::move(stop)), step(std::move(step)) {}
	PHON_AST_NODE(SliceExpression, SliceExpression)
	AutoAst start, stop, step;
};

// `x.field`, `this.xmin`, `a.b.c` — field/member access.
struct FieldAccess final : Ast
{
	FieldAccess(int line, int col, AutoAst object, Symbol name)
	    : Ast(KIND, line, col), object(std::move(object)), name(name) {}
	PHON_AST_NODE(FieldAccess, FieldAccess)
	AutoAst object;
	Symbol name;
};

// `f(a, b, ref x, xs..., name = v)`. The parser splits arguments into the
// dispatched positional part (`args`, which may hold RefExpression/SplatExpression
// wrappers) and the keyword-only `options` (each a NamedArgument), per §6.
struct CallExpression final : Ast
{
	CallExpression(int line, int col, AutoAst callee, AstList args, AstList options)
	    : Ast(KIND, line, col), callee(std::move(callee)), args(std::move(args)), options(std::move(options)) {}
	PHON_AST_NODE(CallExpression, CallExpression)
	AutoAst callee;
	AstList args;
	AstList options;
};

// `xs...` at a call site — splat a List into positional arguments (§6).
struct SplatExpression final : Ast
{
	SplatExpression(int line, int col, AutoAst expr) : Ast(KIND, line, col), expr(std::move(expr)) {}
	PHON_AST_NODE(SplatExpression, SplatExpression)
	AutoAst expr;
};

// `ref x` at a call site — pass a caller slot by reference (§7).
struct RefExpression final : Ast
{
	RefExpression(int line, int col, AutoAst expr) : Ast(KIND, line, col), expr(std::move(expr)) {}
	PHON_AST_NODE(RefExpression, RefExpression)
	AutoAst expr;
};

// `name = value` — a keyword-only option at a call site.
struct NamedArgument final : Ast
{
	NamedArgument(int line, int col, Symbol name, AutoAst value)
	    : Ast(KIND, line, col), name(name), value(std::move(value)) {}
	PHON_AST_NODE(NamedArgument, NamedArgument)
	Symbol name;
	AutoAst value;
};

// `if c then e1 elsif c2 then e2 else e3 end` in expression position (design §13).
// `conds[i]` pairs with `values[i]` (first is the `if`, rest are `elsif`);
// `else_value` is required.
struct ConditionalExpression final : Ast
{
	ConditionalExpression(int line, int col, AstList conds, AstList values, AutoAst else_value)
	    : Ast(KIND, line, col), conds(std::move(conds)), values(std::move(values)), else_value(std::move(else_value)) {}
	PHON_AST_NODE(ConditionalExpression, ConditionalExpression)
	AstList conds, values;
	AutoAst else_value;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

// A block of statements. `scope` marks whether it introduces a lexical scope
// (function/loop/if bodies do; the module top level does not).
struct StatementList final : Ast
{
	StatementList(int line, int col, AstList statements, bool scope)
	    : Ast(KIND, line, col), statements(std::move(statements)), scope(scope) {}
	PHON_AST_NODE(StatementList, StatementList)
	AstList statements;
	bool scope;
};

// `var name as T = init`, `const NAME = init`, with an optional `local`/`global`
// modifier. `type`/`init` may be null. (Single-target; the new grammar has no
// multiple declaration — the only multi-binding form is `for k, v in`.)
struct Declaration final : Ast
{
	Declaration(int line, int col, Symbol name, AutoAst type, AutoAst init,
	            bool is_const, DeclModifier modifier)
	    : Ast(KIND, line, col), name(name), type(std::move(type)), init(std::move(init)),
	      is_const(is_const), modifier(modifier) {}
	PHON_AST_NODE(Declaration, Declaration)
	Symbol name;
	AutoAst type, init;
	bool is_const;
	DeclModifier modifier;
};

// `target op= value`: plain `=` or a compound assignment. `target` is an lvalue
// (Variable, IndexExpression, or FieldAccess).
struct Assignment final : Ast
{
	Assignment(int line, int col, Lexeme op, AutoAst target, AutoAst value)
	    : Ast(KIND, line, col), op(op), target(std::move(target)), value(std::move(value)) {}
	PHON_AST_NODE(Assignment, Assignment)
	Lexeme op;
	AutoAst target, value;
};

// A bare expression used as a statement (typically a call: `print(x)`).
struct ExpressionStatement final : Ast
{
	ExpressionStatement(int line, int col, AutoAst expr) : Ast(KIND, line, col), expr(std::move(expr)) {}
	PHON_AST_NODE(ExpressionStatement, ExpressionStatement)
	AutoAst expr;
};

// `if ... then ... elsif ... else ... end`. `conds[i]` pairs with `bodies[i]`
// (first is `if`, rest `elsif`); `else_body` may be null.
struct IfStatement final : Ast
{
	IfStatement(int line, int col, AstList conds, AstList bodies, AutoAst else_body)
	    : Ast(KIND, line, col), conds(std::move(conds)), bodies(std::move(bodies)), else_body(std::move(else_body)) {}
	PHON_AST_NODE(IfStatement, IfStatement)
	AstList conds, bodies;
	AutoAst else_body;
};

struct WhileStatement final : Ast
{
	WhileStatement(int line, int col, AutoAst cond, AutoAst body)
	    : Ast(KIND, line, col), cond(std::move(cond)), body(std::move(body)) {}
	PHON_AST_NODE(WhileStatement, WhileStatement)
	AutoAst cond, body;
};

// `repeat ... until cond`.
struct RepeatStatement final : Ast
{
	RepeatStatement(int line, int col, AutoAst body, AutoAst cond)
	    : Ast(KIND, line, col), body(std::move(body)), cond(std::move(cond)) {}
	PHON_AST_NODE(RepeatStatement, RepeatStatement)
	AutoAst body, cond;
};

// Counted loop `for var = start to stop [step step] do ... end` (§12). `step`
// may be null.
struct ForNumeric final : Ast
{
	ForNumeric(int line, int col, Symbol var, AutoAst start, AutoAst stop, AutoAst step, AutoAst body)
	    : Ast(KIND, line, col), var(var), start(std::move(start)), stop(std::move(stop)),
	      step(std::move(step)), body(std::move(body)) {}
	PHON_AST_NODE(ForNumeric, ForNumeric)
	Symbol var;
	AutoAst start, stop, step, body;
};

// Iteration loop `for value in coll do ... end`, or `for key, value in coll`.
// `key` is NO_SYMBOL for the single-variable form. `value_by_ref` is set when the
// value variable is taken by reference (`for ref x in`, `for k, ref v in`), so
// the loop variable aliases the collection element; the key/index is never by
// reference (a parse error).
struct ForEach final : Ast
{
	ForEach(int line, int col, Symbol key, Symbol value, bool value_by_ref, AutoAst collection, AutoAst body)
	    : Ast(KIND, line, col), key(key), value(value), value_by_ref(value_by_ref),
	      collection(std::move(collection)), body(std::move(body)) {}
	PHON_AST_NODE(ForEach, ForEach)
	Symbol key, value;
	bool value_by_ref;
	AutoAst collection, body;
};

// `break` / `continue`. `kind_tok` is Lexeme::Break or Lexeme::Continue.
struct LoopControl final : Ast
{
	LoopControl(int line, int col, Lexeme kind_tok) : Ast(KIND, line, col), kind_tok(kind_tok) {}
	PHON_AST_NODE(LoopControl, LoopControl)
	Lexeme kind_tok;
};

// `return [expr]`. `expr` may be null (bare return).
struct ReturnStatement final : Ast
{
	ReturnStatement(int line, int col, AutoAst expr) : Ast(KIND, line, col), expr(std::move(expr)) {}
	PHON_AST_NODE(ReturnStatement, ReturnStatement)
	AutoAst expr;
};

// One parameter of a function/method: `name as T`, `ref name as T`,
// `name as T...` (variadic), or `name as T = default` (keyword-only option).
struct Parameter final : Ast
{
	Parameter(int line, int col, Symbol name, AutoAst type, bool by_ref, bool variadic, AutoAst default_value)
	    : Ast(KIND, line, col), name(name), type(std::move(type)), by_ref(by_ref),
	      variadic(variadic), default_value(std::move(default_value)) {}
	PHON_AST_NODE(Parameter, Parameter)
	Symbol name;
	AutoAst type;
	bool by_ref;   // `ref`
	bool variadic; // trailing `...`
	AutoAst default_value; // non-null => keyword-only option
};

// `function name(params) as RetType ... end`, a `method` in a class body, or an
// anonymous function expression (`name == NO_SYMBOL`). Lambdas `x -> e` are
// parsed to an anonymous definition whose body is `return e`.
struct FunctionDefinition final : Ast
{
	FunctionDefinition(int line, int col, Symbol name, AstList params, AutoAst return_type,
	                   AutoAst body, DeclModifier modifier, bool is_method, bool is_open)
	    : Ast(KIND, line, col), name(name), params(std::move(params)), return_type(std::move(return_type)),
	      body(std::move(body)), modifier(modifier), is_method(is_method), is_open(is_open) {}
	PHON_AST_NODE(FunctionDefinition, FunctionDefinition)
	bool is_anonymous() const noexcept { return name == NO_SYMBOL; }
	Symbol name;
	AstList params;
	AutoAst return_type, body;
	DeclModifier modifier; // None or Local
	bool is_method;        // declared with `method` inside a class body
	bool is_open;          // `open function`
};

// `field name as T = default` inside a class body. `type`/`default_value` may be
// null. A field may also carry `get`/`set` accessor blocks (design "Field
// accessors"): `getter`/`setter` hold their bodies (StatementList) when present,
// with `setter_param`/`setter_param_type` describing the setter's value parameter.
struct FieldDeclaration final : Ast
{
	FieldDeclaration(int line, int col, Symbol name, AutoAst type, AutoAst default_value)
	    : Ast(KIND, line, col), name(name), type(std::move(type)), default_value(std::move(default_value)) {}
	PHON_AST_NODE(FieldDeclaration, FieldDeclaration)
	Symbol name;
	AutoAst type, default_value;
	AutoAst getter;            // `get ... end` body, or null
	AutoAst setter;            // `set(v) ... end` body, or null
	Symbol setter_param = NO_SYMBOL;
	AutoAst setter_param_type; // the setter parameter's declared type, or null
	bool is_private = false;   // `local field` — reachable only through `this`
	bool has_accessors() const noexcept { return getter || setter; }
};

// `class Name is Parent ... end`, `ref class`, `open class`, `local class`.
// `fields` are FieldDeclaration nodes; `methods` are FunctionDefinition nodes.
struct ClassDeclaration final : Ast
{
	ClassDeclaration(int line, int col, Symbol name, AutoAst parent, AstList fields, AstList methods,
	                 DeclModifier modifier, bool is_ref, bool is_open)
	    : Ast(KIND, line, col), name(name), parent(std::move(parent)), fields(std::move(fields)),
	      methods(std::move(methods)), modifier(modifier), is_ref(is_ref), is_open(is_open) {}
	PHON_AST_NODE(ClassDeclaration, ClassDeclaration)
	Symbol name;
	AutoAst parent; // null if no `is Parent`
	AstList fields, methods;
	DeclModifier modifier; // None or Local
	bool is_ref;           // `ref class`
	bool is_open;          // `open class`
};

// One `catch e as T ... ` clause. `type` may be null (a bare `catch`), `name`
// may be NO_SYMBOL (no bound variable).
struct CatchClause final : Ast
{
	CatchClause(int line, int col, Symbol name, AutoAst type, AutoAst body)
	    : Ast(KIND, line, col), name(name), type(std::move(type)), body(std::move(body)) {}
	PHON_AST_NODE(CatchClause, CatchClause)
	Symbol name;
	AutoAst type, body;
};

// `try ... catch ... [catch ...] [finally ...] end`. `finally_body` may be null.
struct TryStatement final : Ast
{
	TryStatement(int line, int col, AutoAst body, AstList catches, AutoAst finally_body)
	    : Ast(KIND, line, col), body(std::move(body)), catches(std::move(catches)), finally_body(std::move(finally_body)) {}
	PHON_AST_NODE(TryStatement, TryStatement)
	AutoAst body;
	AstList catches; // CatchClause nodes
	AutoAst finally_body;
};

// `throw expr`.
struct ThrowStatement final : Ast
{
	ThrowStatement(int line, int col, AutoAst expr) : Ast(KIND, line, col), expr(std::move(expr)) {}
	PHON_AST_NODE(ThrowStatement, ThrowStatement)
	AutoAst expr;
};

// `spawn call(args...)` — run a call in a fresh thread/heap (§12). `call` is a
// CallExpression.
struct SpawnStatement final : Ast
{
	SpawnStatement(int line, int col, AutoAst call) : Ast(KIND, line, col), call(std::move(call)) {}
	PHON_AST_NODE(SpawnStatement, SpawnStatement)
	AutoAst call;
};

// `import name`. (Selective import / renaming / qualified access are deferred —
// design §15; the parser accepts a single module identifier.)
struct ImportStatement final : Ast
{
	ImportStatement(int line, int col, Symbol module) : Ast(KIND, line, col), module(module) {}
	PHON_AST_NODE(ImportStatement, ImportStatement)
	Symbol module;
};

// ---------------------------------------------------------------------------
// Visitor
// ---------------------------------------------------------------------------

class AstVisitor
{
public:
	virtual ~AstVisitor() = default;

	// expressions
	virtual void visit_null_literal(NullLiteral *node) = 0;
	virtual void visit_bool_literal(BoolLiteral *node) = 0;
	virtual void visit_integer_literal(IntegerLiteral *node) = 0;
	virtual void visit_float_literal(FloatLiteral *node) = 0;
	virtual void visit_string_literal(StringLiteral *node) = 0;
	virtual void visit_string_interpolation(StringInterpolation *node) = 0;
	virtual void visit_list_literal(ListLiteral *node) = 0;
	virtual void visit_table_literal(TableLiteral *node) = 0;
	virtual void visit_set_literal(SetLiteral *node) = 0;
	virtual void visit_variable(Variable *node) = 0;
	virtual void visit_this_expression(ThisExpression *node) = 0;
	virtual void visit_unary_expression(UnaryExpression *node) = 0;
	virtual void visit_binary_expression(BinaryExpression *node) = 0;
	virtual void visit_concat_expression(ConcatExpression *node) = 0;
	virtual void visit_is_expression(IsExpression *node) = 0;
	virtual void visit_cast_expression(CastExpression *node) = 0;
	virtual void visit_index_expression(IndexExpression *node) = 0;
	virtual void visit_slice_expression(SliceExpression *node) = 0;
	virtual void visit_field_access(FieldAccess *node) = 0;
	virtual void visit_call_expression(CallExpression *node) = 0;
	virtual void visit_splat_expression(SplatExpression *node) = 0;
	virtual void visit_ref_expression(RefExpression *node) = 0;
	virtual void visit_named_argument(NamedArgument *node) = 0;
	virtual void visit_conditional_expression(ConditionalExpression *node) = 0;
	virtual void visit_function_definition(FunctionDefinition *node) = 0;
	// statements
	virtual void visit_statement_list(StatementList *node) = 0;
	virtual void visit_declaration(Declaration *node) = 0;
	virtual void visit_assignment(Assignment *node) = 0;
	virtual void visit_expression_statement(ExpressionStatement *node) = 0;
	virtual void visit_if_statement(IfStatement *node) = 0;
	virtual void visit_while_statement(WhileStatement *node) = 0;
	virtual void visit_repeat_statement(RepeatStatement *node) = 0;
	virtual void visit_for_numeric(ForNumeric *node) = 0;
	virtual void visit_for_each(ForEach *node) = 0;
	virtual void visit_loop_control(LoopControl *node) = 0;
	virtual void visit_return_statement(ReturnStatement *node) = 0;
	virtual void visit_parameter(Parameter *node) = 0;
	virtual void visit_class_declaration(ClassDeclaration *node) = 0;
	virtual void visit_field_declaration(FieldDeclaration *node) = 0;
	virtual void visit_try_statement(TryStatement *node) = 0;
	virtual void visit_catch_clause(CatchClause *node) = 0;
	virtual void visit_throw_statement(ThrowStatement *node) = 0;
	virtual void visit_spawn_statement(SpawnStatement *node) = 0;
	virtual void visit_import_statement(ImportStatement *node) = 0;
};

} // namespace phonometrica

#endif // PHON_COMPILE_AST_HPP
