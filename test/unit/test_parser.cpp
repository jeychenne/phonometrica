// Phonometrica engine — parser tests (M3 step 3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Structural checks on the AST the parser builds. The exhaustive corpus lives in
// the golden-dump tests (step 4); here we pin down node shapes, precedence, and
// error positions.
//
// NOTE: the parsed module (an AutoAst) owns the whole tree, so every test binds
// it to a local (`auto m = parse(...)`) and only then navigates via raw pointers.

#include <phon/compile/parser.hpp>
#include <phon/compile/source.hpp>
#include <phon/compile/diagnostic.hpp>
#include <phon/types/atom.hpp>

#include "test_framework.hpp"

#include <string>

using namespace phonometrica;

namespace {

AutoAst parse(const std::string &code)
{
	Source src = Source::from_string(code, "<test>");
	Parser p(src);
	return p.parse();
}

// The i-th top-level statement.
Ast *stmt(const AutoAst &module, size_t i)
{
	return module->as<StatementList>()->statements[i].get();
}

// The expression of the i-th top-level statement (an ExpressionStatement).
Ast *expr_of(const AutoAst &module, size_t i = 0)
{
	return stmt(module, i)->as<ExpressionStatement>()->expr.get();
}

bool sym_is(Symbol s, const char *name) { return symbol_name(s) == name; }

} // namespace

TEST_CASE("parser: variable declarations with modifiers")
{
	auto m = parse("var x = 1\nconst SR as Integer = 16000\nlocal var cache = {}\nglobal var g = 0");
	auto *d0 = stmt(m, 0)->as<Declaration>();
	REQUIRE(d0 != nullptr);
	CHECK(sym_is(d0->name, "x"));
	CHECK(!d0->is_const);
	CHECK(d0->modifier == DeclModifier::None);
	CHECK(d0->type == nullptr);
	CHECK(d0->init->is<IntegerLiteral>());

	auto *d1 = stmt(m, 1)->as<Declaration>();
	CHECK(d1->is_const);
	CHECK(sym_is(d1->name, "SR"));
	REQUIRE(d1->type != nullptr);
	CHECK(sym_is(d1->type->as<Variable>()->name, "Integer"));

	CHECK(stmt(m, 2)->as<Declaration>()->modifier == DeclModifier::Local);
	CHECK(stmt(m, 2)->as<Declaration>()->init->is<TableLiteral>());
	CHECK(stmt(m, 3)->as<Declaration>()->modifier == DeclModifier::Global);
}

TEST_CASE("parser: assignment forms and lvalues")
{
	auto a = parse("x = 1");
	CHECK(stmt(a, 0)->as<Assignment>()->op == Lexeme::Assign);
	auto b = parse("s &= \".txt\"");
	CHECK(stmt(b, 0)->as<Assignment>()->op == Lexeme::ConcatEq);
	auto c = parse("a[i] = 0");
	CHECK(stmt(c, 0)->as<Assignment>()->target->is<IndexExpression>());
	auto d = parse("obj.x = 1");
	CHECK(stmt(d, 0)->as<Assignment>()->target->is<FieldAccess>());
}

TEST_CASE("parser: arithmetic precedence and associativity")
{
	// "total: " & n + 1  =>  "total: " & (n + 1)
	auto m1 = parse("\"total: \" & n + 1");
	auto *c = expr_of(m1)->as<ConcatExpression>();
	REQUIRE(c != nullptr);
	REQUIRE(c->parts.size() == 2);
	CHECK(c->parts[0]->is<StringLiteral>());
	CHECK(c->parts[1]->as<BinaryExpression>()->op == Lexeme::Plus);

	// a + b * c  =>  a + (b * c)
	auto m2 = parse("a + b * c");
	auto *e = expr_of(m2)->as<BinaryExpression>();
	CHECK(e->op == Lexeme::Plus);
	CHECK(e->rhs->as<BinaryExpression>()->op == Lexeme::Star);

	// 2 ^ 3 ^ 2  =>  2 ^ (3 ^ 2)  (right-associative)
	auto m3 = parse("2 ^ 3 ^ 2");
	auto *p = expr_of(m3)->as<BinaryExpression>();
	CHECK(p->op == Lexeme::Caret);
	CHECK(p->lhs->as<IntegerLiteral>()->value == 2);
	CHECK(p->rhs->as<BinaryExpression>()->op == Lexeme::Caret);

	// -2 ^ 2  =>  -(2 ^ 2)
	auto m4 = parse("-2 ^ 2");
	auto *u = expr_of(m4)->as<UnaryExpression>();
	REQUIRE(u != nullptr);
	CHECK(u->op == Lexeme::Minus);
	CHECK(u->operand->is<BinaryExpression>());

	// not a == b  =>  not (a == b)
	auto m5 = parse("not a == b");
	auto *n = expr_of(m5)->as<UnaryExpression>();
	REQUIRE(n != nullptr);
	CHECK(n->op == Lexeme::Not);
	CHECK(n->operand->as<BinaryExpression>()->op == Lexeme::Eq);
}

TEST_CASE("parser: is and cast expressions")
{
	auto m1 = parse("x is Sound");
	auto *is = expr_of(m1)->as<IsExpression>();
	REQUIRE(is != nullptr);
	CHECK(is->expr->is<Variable>());
	CHECK(sym_is(is->type->as<Variable>()->name, "Sound"));

	auto m2 = parse("cast x + 1 as Float");
	auto *cast = expr_of(m2)->as<CastExpression>();
	REQUIRE(cast != nullptr);
	CHECK(cast->expr->as<BinaryExpression>()->op == Lexeme::Plus); // inner is greedy up to 'as'
	CHECK(sym_is(cast->type->as<Variable>()->name, "Float"));
}

TEST_CASE("parser: calls — positional, named, ref, splat")
{
	auto m1 = parse("pitch(snd, ceiling = 300)");
	auto *call = expr_of(m1)->as<CallExpression>();
	REQUIRE(call != nullptr);
	CHECK(sym_is(call->callee->as<Variable>()->name, "pitch"));
	REQUIRE(call->args.size() == 1);
	CHECK(call->args[0]->is<Variable>());
	REQUIRE(call->options.size() == 1);
	CHECK(sym_is(call->options[0]->as<NamedArgument>()->name, "ceiling"));

	// Call-site `ref` is no longer accepted (design/references.md §1): ref-ness is a
	// property of the callee's signature, determined without a call-site marker.
	bool ref_threw = false;
	try
	{
		parse("normalize(ref samples)");
	}
	catch (const SyntaxError &)
	{
		ref_threw = true;
	}
	CHECK(ref_threw);
	auto m3 = parse("print(\"x\", values...)");
	CHECK(expr_of(m3)->as<CallExpression>()->args[1]->is<SplatExpression>());

	auto m4 = parse("phon.actions.run(x)");
	CHECK(expr_of(m4)->as<CallExpression>()->callee->is<FieldAccess>());
}

TEST_CASE("parser: indexing and slicing")
{
	auto m1 = parse("a[i]");
	CHECK(expr_of(m1)->as<IndexExpression>()->indices[0]->is<Variable>());

	auto m2 = parse("a[2:5]");
	auto *slice = expr_of(m2)->as<IndexExpression>()->indices[0]->as<SliceExpression>();
	REQUIRE(slice != nullptr);
	CHECK(slice->start->as<IntegerLiteral>()->value == 2);
	CHECK(slice->stop->as<IntegerLiteral>()->value == 5);
	CHECK(slice->step == nullptr);

	auto m3 = parse("a[1:100 step 2]");
	CHECK(expr_of(m3)->as<IndexExpression>()->indices[0]->as<SliceExpression>()->step->as<IntegerLiteral>()->value == 2);

	// m[:, 3]  — bare colon then a plain index
	auto m4 = parse("m[:, 3]");
	auto *idx = expr_of(m4)->as<IndexExpression>();
	REQUIRE(idx->indices.size() == 2);
	auto *bare = idx->indices[0]->as<SliceExpression>();
	CHECK(bare->start == nullptr);
	CHECK(bare->stop == nullptr);
	CHECK(idx->indices[1]->is<IntegerLiteral>());

	auto m5 = parse("a[3:]");
	CHECK(expr_of(m5)->as<IndexExpression>()->indices[0]->as<SliceExpression>()->stop == nullptr);
	auto m6 = parse("track[track > 0]");
	CHECK(expr_of(m6)->as<IndexExpression>()->indices[0]->is<BinaryExpression>());
}

TEST_CASE("parser: list, table, set literals")
{
	auto m1 = parse("[]");
	CHECK(expr_of(m1)->as<ListLiteral>()->items.empty());
	auto m2 = parse("[1, 2, 3]");
	CHECK(expr_of(m2)->as<ListLiteral>()->items.size() == 3);
	auto m3 = parse("[1, 2,]");
	CHECK(expr_of(m3)->as<ListLiteral>()->items.size() == 2); // trailing comma

	auto m4 = parse("{}");
	CHECK(expr_of(m4)->is<TableLiteral>()); // empty braces => empty table
	auto m5 = parse("{\"vowel\": 1, \"consonant\": 2}");
	auto *t = expr_of(m5)->as<TableLiteral>();
	REQUIRE(t != nullptr);
	CHECK(t->keys.size() == 2);
	CHECK(t->values.size() == 2);

	auto m6 = parse("{a, b, c}");
	CHECK(expr_of(m6)->as<SetLiteral>()->items.size() == 3);
}

TEST_CASE("parser: string interpolation")
{
	auto m1 = parse("\"Analyzing {path}: {n} found\"");
	auto *si = expr_of(m1)->as<StringInterpolation>();
	REQUIRE(si != nullptr);
	// parts: "Analyzing " , path , ": " , n , " found"
	REQUIRE(si->parts.size() == 5);
	CHECK(si->parts[0]->as<StringLiteral>()->value == "Analyzing ");
	CHECK(sym_is(si->parts[1]->as<Variable>()->name, "path"));
	CHECK(si->parts[2]->as<StringLiteral>()->value == ": ");
	CHECK(sym_is(si->parts[3]->as<Variable>()->name, "n"));
	CHECK(si->parts[4]->as<StringLiteral>()->value == " found");

	// Empty literal chunks are dropped: "{x}" => [ x ]
	auto m2 = parse("\"{x}\"");
	auto *lone = expr_of(m2)->as<StringInterpolation>();
	REQUIRE(lone->parts.size() == 1);
	CHECK(lone->parts[0]->is<Variable>());
}

TEST_CASE("parser: control flow")
{
	auto m1 = parse("if a then\n x = 1\nelsif b then\n x = 2\nelse\n x = 3\nend");
	auto *iff = stmt(m1, 0)->as<IfStatement>();
	REQUIRE(iff != nullptr);
	CHECK(iff->conds.size() == 2); // if + elsif
	CHECK(iff->bodies.size() == 2);
	CHECK(iff->else_body != nullptr);

	auto m2 = parse("while x < 10 do\n x += 1\nend");
	CHECK(stmt(m2, 0)->as<WhileStatement>()->cond->as<BinaryExpression>()->op == Lexeme::Less);

	auto m3 = parse("repeat\n x += 1\nuntil x == 10");
	CHECK(stmt(m3, 0)->as<RepeatStatement>()->cond->as<BinaryExpression>()->op == Lexeme::Eq);

	auto m4 = parse("for i = 1 to 10 step 2 do\n total += i\nend");
	auto *fn = stmt(m4, 0)->as<ForNumeric>();
	REQUIRE(fn != nullptr);
	CHECK(sym_is(fn->var, "i"));
	CHECK(fn->step != nullptr);

	auto m5 = parse("for interval in tier do\n print(interval)\nend");
	auto *fe = stmt(m5, 0)->as<ForEach>();
	REQUIRE(fe != nullptr);
	CHECK(fe->key == NO_SYMBOL);
	CHECK(sym_is(fe->value, "interval"));

	auto m6 = parse("for k, v in codes do\n print(k)\nend");
	auto *fp = stmt(m6, 0)->as<ForEach>();
	CHECK(sym_is(fp->key, "k"));
	CHECK(sym_is(fp->value, "v"));

	auto m7 = parse("while a do\n break\nend");
	CHECK(stmt(m7, 0)->as<WhileStatement>()->body->as<StatementList>()->statements[0]->is<LoopControl>());
}

TEST_CASE("parser: by-ref iteration")
{
	auto m1 = parse("for ref x in coll do\n f(x)\nend");
	auto *fe = stmt(m1, 0)->as<ForEach>();
	REQUIRE(fe != nullptr);
	CHECK(fe->key == NO_SYMBOL);
	CHECK(fe->value_by_ref);
	CHECK(sym_is(fe->value, "x"));

	auto m2 = parse("for k, ref v in table do\n g(v)\nend");
	auto *fp = stmt(m2, 0)->as<ForEach>();
	CHECK(sym_is(fp->key, "k"));
	CHECK(fp->value_by_ref);

	auto m3 = parse("for i, v in list do\n h(v)\nend");
	CHECK(!stmt(m3, 0)->as<ForEach>()->value_by_ref);

	auto bad = [](const std::string &code, intptr_t line, intptr_t col) {
		try
		{
			parse(code);
			CHECK_MESSAGE(false, "expected a SyntaxError");
		}
		catch (const SyntaxError &e)
		{
			CHECK(e.line == line);
			CHECK(e.column == col);
		}
	};
	bad("for ref k, v in t do\nend", 1, 4);    // ref on the key/index
	bad("for ref i = 1 to 2 do\nend", 1, 4);   // ref on a counted-loop variable
}

TEST_CASE("parser: functions, parameters, overloads")
{
	auto m1 = parse("function pitch(s as Sound, floor as Float = 70, ceiling as Float = 500)\n return s\nend");
	auto *f = stmt(m1, 0)->as<FunctionDefinition>();
	REQUIRE(f != nullptr);
	CHECK(sym_is(f->name, "pitch"));
	REQUIRE(f->params.size() == 3);
	CHECK(f->params[0]->as<Parameter>()->default_value == nullptr);
	CHECK(f->params[1]->as<Parameter>()->default_value != nullptr);
	CHECK(f->return_type == nullptr);

	auto m2 = parse("function log(tag as String, values as Object...)\n return tag\nend");
	CHECK(stmt(m2, 0)->as<FunctionDefinition>()->params[1]->as<Parameter>()->variadic);

	auto m3 = parse("function normalize(ref x as Array)\n return x\nend");
	CHECK(stmt(m3, 0)->as<FunctionDefinition>()->params[0]->as<Parameter>()->by_ref);

	auto m4 = parse("function +(a as Fraction, b as Fraction) as Fraction\n return a\nend");
	auto *op = stmt(m4, 0)->as<FunctionDefinition>();
	CHECK(sym_is(op->name, "+"));
	CHECK(op->return_type != nullptr);
}

TEST_CASE("parser: anonymous functions and lambdas")
{
	auto m1 = parse("var f = function(x) return x ^ 2 end");
	auto *anon = stmt(m1, 0)->as<Declaration>()->init->as<FunctionDefinition>();
	REQUIRE(anon != nullptr);
	CHECK(anon->is_anonymous());
	CHECK(anon->params.size() == 1);

	auto m2 = parse("map(x -> x * 2, values)");
	auto *lam = expr_of(m2)->as<CallExpression>()->args[0]->as<FunctionDefinition>();
	REQUIRE(lam != nullptr);
	CHECK(lam->is_anonymous());
	CHECK(sym_is(lam->params[0]->as<Parameter>()->name, "x"));
	CHECK(lam->body->as<StatementList>()->statements[0]->is<ReturnStatement>());
}

TEST_CASE("parser: conditional (if) expression")
{
	auto m = parse("function f()\n return if n > 0 then mean(n) else 0 end\nend");
	auto *ret = stmt(m, 0)->as<FunctionDefinition>()->body->as<StatementList>()->statements[0]->as<ReturnStatement>();
	auto *cond = ret->expr->as<ConditionalExpression>();
	REQUIRE(cond != nullptr);
	CHECK(cond->conds.size() == 1);
	CHECK(cond->values.size() == 1);
	CHECK(cond->else_value != nullptr);
}

TEST_CASE("parser: classes")
{
	const char *code =
	    "class Interval\n"
	    "  field xmin as Float = 0\n"
	    "  field text as String = \"\"\n"
	    "  method init(xmin as Float)\n"
	    "    this.xmin = xmin\n"
	    "  end\n"
	    "  method to_string() as String\n"
	    "    return text\n"
	    "  end\n"
	    "end";
	auto m1 = parse(code);
	auto *c = stmt(m1, 0)->as<ClassDeclaration>();
	REQUIRE(c != nullptr);
	CHECK(sym_is(c->name, "Interval"));
	CHECK(!c->is_ref);
	CHECK(c->parent == nullptr);
	REQUIRE(c->fields.size() == 2);
	CHECK(sym_is(c->fields[0]->as<FieldDeclaration>()->name, "xmin"));
	REQUIRE(c->methods.size() == 2);
	CHECK(c->methods[0]->as<FunctionDefinition>()->is_method);

	auto m2 = parse("ref class PointTier is Tier\n field points as List = []\nend");
	auto *pt = stmt(m2, 0)->as<ClassDeclaration>();
	CHECK(pt->is_ref);
	CHECK(sym_is(pt->parent->as<Variable>()->name, "Tier"));

	auto m3 = parse("open class Foo\nend");
	CHECK(stmt(m3, 0)->as<ClassDeclaration>()->is_open);
}

TEST_CASE("parser: fields without type or default")
{
	auto m = parse(
	    "class C\n"
	    "  field a as Float\n"   // typed, no default
	    "  field b as Float = 0\n"
	    "  field c\n"            // untyped, no default
	    "  field d = \"\"\n"     // untyped, with default
	    "end");
	auto *c = stmt(m, 0)->as<ClassDeclaration>();
	REQUIRE(c->fields.size() == 4);
	auto *a = c->fields[0]->as<FieldDeclaration>();
	CHECK(a->type != nullptr);
	CHECK(a->default_value == nullptr);
	auto *cc = c->fields[2]->as<FieldDeclaration>();
	CHECK(cc->type == nullptr);
	CHECK(cc->default_value == nullptr);
	auto *dd = c->fields[3]->as<FieldDeclaration>();
	CHECK(dd->type == nullptr);
	CHECK(dd->default_value != nullptr);
}

TEST_CASE("parser: class modifier stack (local open ref class)")
{
	auto m = parse("local open ref class Handle is Resource\n field id as Integer = 0\nend");
	auto *c = stmt(m, 0)->as<ClassDeclaration>();
	REQUIRE(c != nullptr);
	CHECK(c->modifier == DeclModifier::Local);
	CHECK(c->is_open);
	CHECK(c->is_ref);
	CHECK(sym_is(c->parent->as<Variable>()->name, "Resource"));

	// Subsets and the canonical order all parse.
	CHECK(stmt(parse("open ref class A\nend"), 0)->as<ClassDeclaration>()->is_ref);
	CHECK(stmt(parse("local ref class B\nend"), 0)->as<ClassDeclaration>()->modifier == DeclModifier::Local);

	auto bad = [](const std::string &code, intptr_t line, intptr_t col) {
		try
		{
			parse(code);
			CHECK_MESSAGE(false, "expected a SyntaxError");
		}
		catch (const SyntaxError &e)
		{
			CHECK(e.line == line);
			CHECK(e.column == col);
		}
	};
	bad("open local class C\nend", 1, 5);   // wrong order: 'local' after 'open'
	bad("ref open class D\nend", 1, 4);      // wrong order: 'open' after 'ref'
	bad("ref function f()\nend", 1, 4);      // 'ref' cannot apply to a function
	bad("global function g()\nend", 1, 7);   // 'global' cannot apply to a function
}

TEST_CASE("parser: try / catch / finally, throw, spawn, import")
{
	const char *code =
	    "try\n"
	    "  var s = read(p)\n"
	    "catch e as IOError\n"
	    "  print(e)\n"
	    "catch e as Error\n"
	    "  rethrow(e)\n"
	    "finally\n"
	    "  cleanup()\n"
	    "end";
	auto m1 = parse(code);
	auto *t = stmt(m1, 0)->as<TryStatement>();
	REQUIRE(t != nullptr);
	REQUIRE(t->catches.size() == 2);
	auto *c0 = t->catches[0]->as<CatchClause>();
	CHECK(sym_is(c0->name, "e"));
	CHECK(sym_is(c0->type->as<Variable>()->name, "IOError"));
	CHECK(t->finally_body != nullptr);

	auto m2 = parse("throw MyError(\"boom\")");
	CHECK(stmt(m2, 0)->is<ThrowStatement>());
	auto m3 = parse("spawn worker(jobs, out)");
	CHECK(stmt(m3, 0)->as<SpawnStatement>()->call->is<CallExpression>());
	auto m4 = parse("import textgrid");
	CHECK(sym_is(stmt(m4, 0)->as<ImportStatement>()->module, "textgrid"));
}

TEST_CASE("parser: line continuation across operators")
{
	// The scanner suppresses the newline after '+'; the parser sees one expression.
	auto m = parse("a +\n b +\n c");
	CHECK(expr_of(m)->as<BinaryExpression>()->op == Lexeme::Plus);
}

TEST_CASE("parser: error positions")
{
	auto check_err = [](const std::string &code, intptr_t line, intptr_t col) {
		try
		{
			parse(code);
			CHECK_MESSAGE(false, "expected a SyntaxError");
		}
		catch (const SyntaxError &e)
		{
			CHECK(e.line == line);
			CHECK(e.column == col);
		}
	};

	check_err("var = 1", 1, 4);             // missing name; error at '='
	check_err("if a then\n x = 1", 2, 6);   // unclosed if: error at EOT on line 2
	check_err("x = ", 1, 4);                // missing expression at EOT
	check_err("1 = 2", 1, 0);               // invalid assignment target
	check_err("f(a, x = 1, b)", 1, 12);     // positional after named (at 'b')
	check_err("this.x = 1", 1, 0);          // 'this' outside a method
	check_err("obj.field", 1, 4);           // 'field' is a reserved word, not a name
}

TEST_CASE("parser: fuzz — token salad never crashes or hangs")
{
	// Random sequences drawn from a grammar-flavored vocabulary. The parser must
	// either build a tree or throw SyntaxError — never crash, hang, or loop.
	static const char *vocab[] = {
	    "var", "const", "function", "class", "method", "field", "if", "then",
	    "elsif", "else", "end", "while", "do", "for", "in", "to", "step",
	    "repeat", "until", "return", "try", "catch", "finally", "throw", "spawn",
	    "import", "and", "or", "not", "is", "as", "cast", "ref", "local", "global",
	    "open", "true", "false", "null", "this", "break", "continue",
	    "x", "y", "foo", "1", "2.5", "\"s\"", "\"a{x}b\"", "'raw'",
	    "+", "-", "*", "/", "^", "&", "=", "==", "!=", "<", ">", "->", "...",
	    "(", ")", "[", "]", "{", "}", ",", ":", ".", ";", "\n"};
	const int vocab_n = static_cast<int>(sizeof(vocab) / sizeof(vocab[0]));

	uint64_t state = 0xD1B54A32D192ED03ull;
	auto rng = [&]() -> uint32_t {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		return static_cast<uint32_t>(state >> 16);
	};

	for (int iter = 0; iter < 6000; ++iter)
	{
		int len = rng() % 24;
		std::string code;
		for (int i = 0; i < len; ++i)
		{
			code += vocab[rng() % vocab_n];
			code += ' ';
		}
		try
		{
			parse(code);
		}
		catch (const SyntaxError &)
		{
			// Acceptable.
		}
	}
	CHECK(true);
}
