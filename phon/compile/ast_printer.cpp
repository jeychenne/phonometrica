// Phonometrica engine — AST pretty-printer. See header.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/compile/ast_printer.hpp>

#include <phon/types/atom.hpp>

#include <cstdio>
#include <string>

namespace phonometrica {

namespace {

std::string sym(Symbol s) { return std::string(symbol_name(s)); }

// Render a type annotation (a Variable or dotted FieldAccess) as `a.b.c`.
std::string render_type(Ast *t)
{
	if (auto *v = t->as<Variable>())
		return sym(v->name);
	if (auto *f = t->as<FieldAccess>())
		return render_type(f->object.get()) + "." + sym(f->name);
	return "?";
}

std::string escape(Substring s)
{
	std::string out;
	for (char c : s)
	{
		switch (c)
		{
		case '\n': out += "\\n"; break;
		case '\t': out += "\\t"; break;
		case '\r': out += "\\r"; break;
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		default: out.push_back(c);
		}
	}
	return out;
}

std::string format_double(double v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%g", v);
	return buf;
}

class AstDumper final : public AstVisitor
{
public:
	std::string out;

	void emit(const std::string &text)
	{
		for (int i = 0; i < m_depth; ++i)
			out += "  ";
		out += text;
		out += '\n';
	}

	// Dump `c` one level deeper than the current node.
	void child(Ast *c)
	{
		++m_depth;
		c->visit(*this);
		--m_depth;
	}

	void child_or_none(Ast *c)
	{
		if (c)
			child(c);
		else
			emit_child("None");
	}

	// A leaf label emitted one level deeper (no subtree).
	void emit_child(const std::string &text)
	{
		++m_depth;
		emit(text);
		--m_depth;
	}

	// --- expressions ---

	void visit_null_literal(NullLiteral *) override { emit("Null"); }
	void visit_bool_literal(BoolLiteral *n) override { emit(n->value ? "Bool true" : "Bool false"); }
	void visit_integer_literal(IntegerLiteral *n) override { emit("Int " + std::to_string(n->value)); }
	void visit_float_literal(FloatLiteral *n) override { emit("Float " + format_double(n->value)); }
	void visit_string_literal(StringLiteral *n) override { emit("Str \"" + escape(n->value.view()) + "\""); }

	void visit_string_interpolation(StringInterpolation *n) override
	{
		emit("StringInterp");
		for (auto &p : n->parts)
			child(p.get());
	}

	void visit_list_literal(ListLiteral *n) override
	{
		emit("List");
		for (auto &it : n->items)
			child(it.get());
	}

	void visit_table_literal(TableLiteral *n) override
	{
		emit("Table");
		for (size_t i = 0; i < n->keys.size(); ++i)
		{
			++m_depth;
			emit("Entry");
			child(n->keys[i].get());
			child(n->values[i].get());
			--m_depth;
		}
	}

	void visit_set_literal(SetLiteral *n) override
	{
		emit("Set");
		for (auto &it : n->items)
			child(it.get());
	}

	void visit_variable(Variable *n) override { emit("Var " + sym(n->name)); }
	void visit_this_expression(ThisExpression *) override { emit("This"); }

	void visit_unary_expression(UnaryExpression *n) override
	{
		emit(std::string("Unary ") + lexeme_name(n->op));
		child(n->operand.get());
	}

	void visit_binary_expression(BinaryExpression *n) override
	{
		emit(std::string("Binary ") + lexeme_name(n->op));
		child(n->lhs.get());
		child(n->rhs.get());
	}

	void visit_concat_expression(ConcatExpression *n) override
	{
		emit("Concat");
		for (auto &p : n->parts)
			child(p.get());
	}

	void visit_is_expression(IsExpression *n) override
	{
		emit("Is " + render_type(n->type.get()));
		child(n->expr.get());
	}

	void visit_cast_expression(CastExpression *n) override
	{
		emit("Cast " + render_type(n->type.get()));
		child(n->expr.get());
	}

	void visit_index_expression(IndexExpression *n) override
	{
		emit("Index");
		child(n->object.get()); // first child is the object
		for (auto &idx : n->indices)
			child(idx.get());
	}

	void visit_slice_expression(SliceExpression *n) override
	{
		emit("Slice");
		child_or_none(n->start.get());
		child_or_none(n->stop.get());
		child_or_none(n->step.get());
	}

	void visit_field_access(FieldAccess *n) override
	{
		emit("Field " + sym(n->name));
		child(n->object.get());
	}

	void visit_call_expression(CallExpression *n) override
	{
		emit("Call");
		child(n->callee.get()); // first child is the callee
		for (auto &a : n->args)
			child(a.get());
		for (auto &o : n->options)
			child(o.get());
	}

	void visit_splat_expression(SplatExpression *n) override
	{
		emit("Splat");
		child(n->expr.get());
	}

	void visit_ref_expression(RefExpression *n) override
	{
		emit("Ref");
		child(n->expr.get());
	}

	void visit_named_argument(NamedArgument *n) override
	{
		emit("Named " + sym(n->name));
		child(n->value.get());
	}

	void visit_conditional_expression(ConditionalExpression *n) override
	{
		emit("IfExpr");
		for (size_t i = 0; i < n->conds.size(); ++i)
		{
			++m_depth;
			emit("Case");
			child(n->conds[i].get());
			child(n->values[i].get());
			--m_depth;
		}
		++m_depth;
		emit("Else");
		child(n->else_value.get());
		--m_depth;
	}

	void visit_function_definition(FunctionDefinition *n) override
	{
		std::string h = "Function ";
		h += n->is_anonymous() ? "<anonymous>" : sym(n->name);
		if (n->modifier == DeclModifier::Local)
			h += " local";
		if (n->is_open)
			h += " open";
		if (n->is_method)
			h += " method";
		emit(h);
		for (auto &p : n->params)
			child(p.get());
		if (n->return_type)
			emit_child("Returns " + render_type(n->return_type.get()));
		child(n->body.get());
	}

	// --- statements ---

	void visit_statement_list(StatementList *n) override
	{
		emit("Block");
		for (auto &s : n->statements)
			child(s.get());
	}

	void visit_declaration(Declaration *n) override
	{
		std::string h = n->is_const ? "Const " : "Var ";
		if (n->modifier == DeclModifier::Local)
			h += "local ";
		else if (n->modifier == DeclModifier::Global)
			h += "global ";
		h += sym(n->name);
		if (n->type)
			h += " as " + render_type(n->type.get());
		emit(h);
		if (n->init)
			child(n->init.get());
	}

	void visit_assignment(Assignment *n) override
	{
		emit(std::string("Assign ") + lexeme_name(n->op));
		child(n->target.get());
		child(n->value.get());
	}

	void visit_expression_statement(ExpressionStatement *n) override
	{
		emit("ExprStmt");
		child(n->expr.get());
	}

	void visit_if_statement(IfStatement *n) override
	{
		emit("If");
		for (size_t i = 0; i < n->conds.size(); ++i)
		{
			++m_depth;
			emit("Case");
			child(n->conds[i].get());
			child(n->bodies[i].get());
			--m_depth;
		}
		if (n->else_body)
		{
			++m_depth;
			emit("Else");
			child(n->else_body.get());
			--m_depth;
		}
	}

	void visit_while_statement(WhileStatement *n) override
	{
		emit("While");
		child(n->cond.get());
		child(n->body.get());
	}

	void visit_repeat_statement(RepeatStatement *n) override
	{
		emit("Repeat");
		child(n->body.get());
		child(n->cond.get());
	}

	void visit_for_numeric(ForNumeric *n) override
	{
		emit("ForNumeric " + sym(n->var));
		child(n->start.get());
		child(n->stop.get());
		child_or_none(n->step.get());
		child(n->body.get());
	}

	void visit_for_each(ForEach *n) override
	{
		std::string h = "ForEach ";
		if (n->key != NO_SYMBOL)
			h += sym(n->key) + ", ";
		if (n->value_by_ref)
			h += "ref ";
		h += sym(n->value);
		emit(h);
		child(n->collection.get());
		child(n->body.get());
	}

	void visit_loop_control(LoopControl *n) override
	{
		emit(n->kind_tok == Lexeme::Break ? "Break" : "Continue");
	}

	void visit_return_statement(ReturnStatement *n) override
	{
		emit("Return");
		if (n->expr)
			child(n->expr.get());
	}

	void visit_parameter(Parameter *n) override
	{
		std::string h = "Param " + sym(n->name);
		if (n->type)
			h += " as " + render_type(n->type.get());
		if (n->by_ref)
			h += " ref";
		if (n->variadic)
			h += " ...";
		emit(h);
		if (n->default_value)
			child(n->default_value.get());
	}

	void visit_class_declaration(ClassDeclaration *n) override
	{
		std::string h = "Class " + sym(n->name);
		if (n->modifier == DeclModifier::Local)
			h += " local";
		if (n->is_open)
			h += " open";
		if (n->is_ref)
			h += " ref";
		if (n->parent)
			h += " is " + render_type(n->parent.get());
		emit(h);
		for (auto &f : n->fields)
			child(f.get());
		for (auto &m : n->methods)
			child(m.get());
	}

	void visit_field_declaration(FieldDeclaration *n) override
	{
		std::string h = "Field ";
		if (n->is_private)
			h += "local ";
		h += sym(n->name);
		if (n->type)
			h += " as " + render_type(n->type.get());
		emit(h);
		if (n->default_value)
			child(n->default_value.get());
		if (n->getter)
		{
			++m_depth;
			emit("Get");
			child(n->getter.get());
			--m_depth;
		}
		if (n->setter)
		{
			++m_depth;
			std::string s = "Set " + sym(n->setter_param);
			if (n->setter_param_type)
				s += " as " + render_type(n->setter_param_type.get());
			emit(s);
			child(n->setter.get());
			--m_depth;
		}
	}

	void visit_try_statement(TryStatement *n) override
	{
		emit("Try");
		child(n->body.get());
		for (auto &c : n->catches)
			child(c.get());
		if (n->finally_body)
		{
			++m_depth;
			emit("Finally");
			child(n->finally_body.get());
			--m_depth;
		}
	}

	void visit_catch_clause(CatchClause *n) override
	{
		std::string h = "Catch";
		if (n->name != NO_SYMBOL)
			h += " " + sym(n->name);
		if (n->type)
			h += " as " + render_type(n->type.get());
		emit(h);
		child(n->body.get());
	}

	void visit_throw_statement(ThrowStatement *n) override
	{
		emit("Throw");
		child(n->expr.get());
	}

	void visit_spawn_statement(SpawnStatement *n) override
	{
		emit("Spawn");
		child(n->call.get());
	}

	void visit_import_statement(ImportStatement *n) override { emit("Import " + sym(n->module)); }

private:
	int m_depth = 0;
};

} // namespace

std::string dump_ast(Ast *root)
{
	AstDumper d;
	if (auto *sl = root->as<StatementList>())
	{
		// The module root prints as "Module"; nested blocks print as "Block".
		d.emit("Module");
		for (auto &s : sl->statements)
			d.child(s.get());
	}
	else
	{
		root->visit(d);
	}
	return d.out;
}

} // namespace phonometrica
