// Phonometrica engine — lowering: AST -> bytecode implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A recursive code generator (not the void-returning AstVisitor: codegen threads
// destination registers and returns register indices, which the visitor cannot).
// Register model: stack-discipline allocation over a 256-slot frame (Lua-style).
// Named locals occupy low registers for their scope; expression temporaries pile
// above a `free_reg` watermark and are reclaimed at statement boundaries.
//
// M4 scope: expressions, control flow, functions, closures/upvalues, direct and
// generic (builtin) calls, list/table/set literals, indexing, concat/interpolation.
// Deferred to later milestones with a clear `[…]` error: classes/fields/`this`,
// `cast`, `try/throw`, `for … in`, `ref`/splat arguments, named call options,
// script-defined variadics, and `spawn` (recorded in DEVIATIONS).

#include <phon/compile/lower.hpp>

#include <phon/compile/diagnostic.hpp>
#include <phon/dispatch/generic.hpp>
#include <phon/object/class.hpp>
#include <phon/types/atom.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/opcode.hpp>

#include <string>
#include <vector>

namespace phonometrica {

namespace {

// A named local binding: its register and const-ness. `captured` is set when an
// inner closure takes it as an upvalue (drives CLOSE emission on scope exit).
struct Local
{
	Symbol name;
	int reg;
	bool is_const;
	bool captured;
};

struct BlockInfo
{
	int locals_count; // locals stack size at block entry
	int free_reg;     // watermark at block entry
	bool has_capture; // any local declared in this block was captured
};

// Per-function compilation state. `proto` is the (non-owning) Proto being built —
// owned by its parent's `children` (or by CompiledModule for the module). `prev`
// is the enclosing function's state, or null at the module top level.
struct FuncState
{
	FuncState(Proto &proto, FuncState *prev, bool is_module)
	    : proto(proto), prev(prev), is_module(is_module)
	{
	}

	Proto &proto;
	FuncState *prev;
	std::vector<Local> locals;
	std::vector<BlockInfo> blocks;
	int free_reg = 0;
	int max_reg = 0;
	bool is_module;
};

// Break/continue patch targets for the innermost enclosing loop.
struct LoopCtx
{
	std::vector<intptr_t> breaks;
	std::vector<intptr_t> continues;
};

class Lowerer
{
public:
	void compile_module(Ast *module_ast, CompiledModule &out);

private:
	// --- error ---
	[[noreturn]] void error(Ast *node, const std::string &msg)
	{
		throw SyntaxError(msg, node ? node->line : 0, node ? node->column : 0, 1);
	}

	// --- emission ---
	Proto &P() { return fs->proto; }
	intptr_t emit(Instruction ins, uint32_t line) { return P().emit(ins, line); }
	static uint32_t ln(Ast *n) { return static_cast<uint32_t>(n ? n->line : 0); }

	intptr_t emit_ABC(Opcode op, int a, int b, int c, uint32_t line)
	{
		return emit(encode_ABC(op, a, b, c), line);
	}
	intptr_t emit_ABx(Opcode op, int a, uint32_t bx, uint32_t line)
	{
		return emit(encode_ABx(op, a, bx), line);
	}
	intptr_t emit_AsBx(Opcode op, int a, int sbx, uint32_t line)
	{
		return emit(encode_AsBx(op, a, sbx), line);
	}

	// Emit a jump with a placeholder offset; returns its ip for later patching.
	intptr_t emit_jump(Opcode op, int a, uint32_t line) { return emit_AsBx(op, a, 0, line); }

	// Patch a previously emitted jump to target the current end of code.
	void patch_jump(intptr_t ip)
	{
		Instruction ins = P().code[ip];
		int off = static_cast<int>(P().code.size() - (ip + 1));
		P().code[ip] = encode_AsBx(op_of(ins), op_a(ins), off);
	}

	// --- registers ---
	int reg_alloc(Ast *node)
	{
		int r = fs->free_reg++;
		if (fs->free_reg > kMaxRegisters)
			error(node, "[Compile error] function needs too many registers (limit 256)");
		if (fs->free_reg > fs->max_reg)
			fs->max_reg = fs->free_reg;
		return r;
	}
	void reg_free_to(int n) { fs->free_reg = n; }

	// --- scopes and names ---
	void enter_block()
	{
		fs->blocks.push_back({static_cast<int>(fs->locals.size()), fs->free_reg, false});
	}
	void exit_block(uint32_t line)
	{
		BlockInfo b = fs->blocks.back();
		fs->blocks.pop_back();
		bool captured = false;
		for (size_t i = static_cast<size_t>(b.locals_count); i < fs->locals.size(); ++i)
			captured = captured || fs->locals[i].captured;
		if (captured)
			emit_ABC(Opcode::CLOSE, b.free_reg, 0, 0, line);
		fs->locals.resize(static_cast<size_t>(b.locals_count));
		fs->free_reg = b.free_reg;
	}

	int declare_local(Symbol name, bool is_const, Ast *node)
	{
		int r = reg_alloc(node);
		fs->locals.push_back({name, r, is_const, false});
		return r;
	}

	int module_define(Symbol name)
	{
		auto it = m_module_slots.find(name.id);
		if (it != m_module_slots.end())
			return it->second;
		int slot = m_num_slots++;
		m_module_slots.insert(name.id, slot);
		return slot;
	}
	int module_lookup(Symbol name) const
	{
		auto it = m_module_slots.find(name.id);
		return it == m_module_slots.end() ? -1 : it->second;
	}

	static Class *class_by_name(Symbol name)
	{
		std::string_view want = symbol_name(name);
		intptr_t n = class_count();
		for (intptr_t i = 0; i < n; ++i)
		{
			if (!has_class(static_cast<uint32_t>(i)))
				continue;
			Class *c = get_class(static_cast<uint32_t>(i));
			if (c && c->name && want == c->name && !(c->flags & CLASS_META))
				return c;
		}
		return nullptr;
	}

	enum class NameKind
	{
		None,
		Local,
		Upvalue,
		Module,
		ClassObject,
		Generic
	};
	struct NameRef
	{
		NameKind kind = NameKind::None;
		int index = 0;      // reg / upval idx / module slot
		bool is_const = false;
		Class *cls = nullptr;
	};

	static int find_local(FuncState &f, Symbol name)
	{
		for (int i = static_cast<int>(f.locals.size()) - 1; i >= 0; --i)
			if (f.locals[i].name == name)
				return i;
		return -1;
	}

	static int add_upvalue(FuncState &f, bool in_stack, int index)
	{
		for (intptr_t i = 0; i < f.proto.upvals.size(); ++i)
			if (f.proto.upvals[i].in_stack == in_stack && f.proto.upvals[i].index == index)
				return static_cast<int>(i);
		f.proto.upvals.push_back(UpvalDesc{in_stack, static_cast<uint8_t>(index)});
		return static_cast<int>(f.proto.upvals.size() - 1);
	}

	static int resolve_upvalue(FuncState &f, Symbol name)
	{
		if (!f.prev)
			return -1;
		int li = find_local(*f.prev, name);
		if (li >= 0)
		{
			f.prev->locals[li].captured = true;
			return add_upvalue(f, true, f.prev->locals[li].reg);
		}
		int u = resolve_upvalue(*f.prev, name);
		if (u >= 0)
			return add_upvalue(f, false, u);
		return -1;
	}

	NameRef resolve(Symbol name)
	{
		NameRef r;
		int li = find_local(*fs, name);
		if (li >= 0)
		{
			r.kind = NameKind::Local;
			r.index = fs->locals[li].reg;
			r.is_const = fs->locals[li].is_const;
			return r;
		}
		int u = resolve_upvalue(*fs, name);
		if (u >= 0)
		{
			r.kind = NameKind::Upvalue;
			r.index = u;
			return r;
		}
		int slot = module_lookup(name);
		if (slot >= 0)
		{
			r.kind = NameKind::Module;
			r.index = slot;
			return r;
		}
		if (Class *c = class_by_name(name))
		{
			r.kind = NameKind::ClassObject;
			r.cls = c;
			return r;
		}
		if (find_generic(name))
		{
			r.kind = NameKind::Generic;
			return r;
		}
		return r;
	}

	// --- constants ---
	int k_int(int64_t v) { return P().add_constant(Variant(Value::make_int(v))); }
	int k_double(double v) { return P().add_constant(Variant(Value::make(v))); }
	int k_symbol(Symbol s) { return P().add_constant(Variant(Value::make_symbol(s))); }
	int k_string(const String &s) { return P().add_constant(Variant(s.to_value())); }
	int k_class(Class *c) { return P().add_constant(Variant(class_object(c))); }

	// --- expression compilation ---
	void expr_to(Ast *node, int dest);
	int expr_any(Ast *node);

	void compile_binary(BinaryExpression *b, int dest);
	void compile_logical(BinaryExpression *b, int dest); // and/or
	void compile_concat_parts(AstList &parts, int dest, uint32_t line);
	void compile_call(CallExpression *c, int dest);
	void compile_conditional(ConditionalExpression *c, int dest);
	int compile_function(FunctionDefinition *f);

	// --- statement compilation ---
	void compile_stmt(Ast *node);
	void compile_block(StatementList *sl);
	void compile_declaration(Declaration *d);
	void compile_assignment(Assignment *a);
	void assign_plain(Ast *target, Ast *value);
	// Load an index target's object into a register for in-place mutation. `wb`
	// selects the write-back (0 none / 1 module / 2 upvalue) needed after SETINDEX
	// so a mutated value class propagates to its binding; `wbidx` is the slot/index.
	int load_index_object(IndexExpression *ix, int &wb, int &wbidx);
	void emit_index_writeback(int o, int wb, int wbidx, uint32_t line);
	void compile_if(IfStatement *s);
	void compile_while(WhileStatement *s);
	void compile_repeat(RepeatStatement *s);
	void compile_for_numeric(ForNumeric *s);
	void compile_return(ReturnStatement *s);
	void compile_named_function(FunctionDefinition *f); // nested (local) named fn

	static Opcode arith_opcode(Lexeme op, bool &swap_operands, bool &is_compare);

	FuncState *fs = nullptr;
	std::vector<LoopCtx> m_loops;
	FlatHashMap<uint32_t, int> m_module_slots;
	int m_num_slots = 0;
};

// --- operator mapping ---------------------------------------------------------

Opcode Lowerer::arith_opcode(Lexeme op, bool &swap_operands, bool &is_compare)
{
	swap_operands = false;
	is_compare = false;
	switch (op)
	{
	case Lexeme::Plus: return Opcode::ADD;
	case Lexeme::Minus: return Opcode::SUB;
	case Lexeme::Star: return Opcode::MUL;
	case Lexeme::Slash: return Opcode::DIV;
	case Lexeme::Caret: return Opcode::POW;
	case Lexeme::Div: return Opcode::IDIV;
	case Lexeme::Mod: return Opcode::MOD;
	case Lexeme::Eq: is_compare = true; return Opcode::EQ;
	case Lexeme::NotEq: is_compare = true; return Opcode::NE;
	case Lexeme::Less: is_compare = true; return Opcode::LT;
	case Lexeme::LessEq: is_compare = true; return Opcode::LE;
	case Lexeme::Greater: is_compare = true; swap_operands = true; return Opcode::LT;
	case Lexeme::GreaterEq: is_compare = true; swap_operands = true; return Opcode::LE;
	default: return Opcode::HALT; // unreachable for valid ASTs
	}
}

// --- expressions --------------------------------------------------------------

int Lowerer::expr_any(Ast *node)
{
	if (auto *v = node->as<Variable>())
	{
		NameRef nr = resolve(v->name);
		if (nr.kind == NameKind::Local)
			return nr.index; // borrow the local's register
	}
	int d = reg_alloc(node);
	expr_to(node, d);
	return d;
}

void Lowerer::expr_to(Ast *node, int dest)
{
	switch (node->kind)
	{
	case NodeKind::NullLiteral:
		emit_ABC(Opcode::LOADNULL, dest, 0, 0, ln(node));
		break;
	case NodeKind::BoolLiteral:
		emit_ABC(Opcode::LOADBOOL, dest, node->as<BoolLiteral>()->value ? 1 : 0, 0, ln(node));
		break;
	case NodeKind::IntegerLiteral:
	{
		int64_t v = node->as<IntegerLiteral>()->value;
		if (v < Value::INT_MIN_VALUE || v > Value::INT_MAX_VALUE)
			error(node, "[Value error] integer literal out of range (47-bit)");
		if (v >= -32768 && v <= 32767)
			emit_AsBx(Opcode::LOADI, dest, static_cast<int>(v), ln(node));
		else
			emit_ABx(Opcode::LOADK, dest, static_cast<uint32_t>(k_int(v)), ln(node));
		break;
	}
	case NodeKind::FloatLiteral:
		emit_ABx(Opcode::LOADK, dest, static_cast<uint32_t>(k_double(node->as<FloatLiteral>()->value)),
		         ln(node));
		break;
	case NodeKind::StringLiteral:
		emit_ABx(Opcode::LOADK, dest, static_cast<uint32_t>(k_string(node->as<StringLiteral>()->value)),
		         ln(node));
		break;
	case NodeKind::StringInterpolation:
		compile_concat_parts(node->as<StringInterpolation>()->parts, dest, ln(node));
		break;
	case NodeKind::ConcatExpression:
		compile_concat_parts(node->as<ConcatExpression>()->parts, dest, ln(node));
		break;
	case NodeKind::Variable:
	{
		auto *v = node->as<Variable>();
		NameRef nr = resolve(v->name);
		switch (nr.kind)
		{
		case NameKind::Local:
			if (nr.index != dest)
				emit_ABC(Opcode::MOVE, dest, nr.index, 0, ln(node));
			break;
		case NameKind::Upvalue:
			emit_ABC(Opcode::GETUPVAL, dest, nr.index, 0, ln(node));
			break;
		case NameKind::Module:
			emit_ABx(Opcode::GETMODULE, dest, static_cast<uint32_t>(nr.index), ln(node));
			break;
		case NameKind::ClassObject:
			emit_ABx(Opcode::LOADK, dest, static_cast<uint32_t>(k_class(nr.cls)), ln(node));
			break;
		case NameKind::Generic:
			error(node, "[Name error] '" + std::string(symbol_name(v->name)) +
			                "' is a generic function and cannot be used as a value yet (M8)");
		case NameKind::None:
			error(node, "[Name error] undeclared name '" + std::string(symbol_name(v->name)) + "'");
		}
		break;
	}
	case NodeKind::UnaryExpression:
	{
		auto *u = node->as<UnaryExpression>();
		if (u->op == Lexeme::Plus)
		{
			expr_to(u->operand.get(), dest);
		}
		else
		{
			int save = fs->free_reg;
			int r = expr_any(u->operand.get());
			emit_ABC(u->op == Lexeme::Minus ? Opcode::NEG : Opcode::NOT, dest, r, 0, ln(node));
			reg_free_to(save);
		}
		break;
	}
	case NodeKind::BinaryExpression:
	{
		auto *b = node->as<BinaryExpression>();
		if (b->op == Lexeme::And || b->op == Lexeme::Or)
			compile_logical(b, dest);
		else
			compile_binary(b, dest);
		break;
	}
	case NodeKind::IsExpression:
	{
		auto *e = node->as<IsExpression>();
		int save = fs->free_reg;
		int b = expr_any(e->expr.get());
		auto *tv = e->type->as<Variable>();
		Class *c = tv ? class_by_name(tv->name) : nullptr;
		if (!c)
			error(e->type.get(), "[Type error] expected a class name after 'is'");
		int cr = reg_alloc(e->type.get());
		emit_ABx(Opcode::LOADK, cr, static_cast<uint32_t>(k_class(c)), ln(node));
		emit_ABC(Opcode::IS, dest, b, cr, ln(node));
		reg_free_to(save);
		break;
	}
	case NodeKind::IndexExpression:
	{
		auto *e = node->as<IndexExpression>();
		if (e->indices.size() != 1 || e->indices[0]->is<SliceExpression>())
			error(node, "[Index error] slices and multi-dimensional indexing arrive in M6");
		int save = fs->free_reg;
		int b = expr_any(e->object.get());
		int c = expr_any(e->indices[0].get());
		emit_ABC(Opcode::GETINDEX, dest, b, c, ln(node));
		reg_free_to(save);
		break;
	}
	case NodeKind::ListLiteral:
	{
		auto *e = node->as<ListLiteral>();
		int save = fs->free_reg;
		int base = reg_alloc(node);
		for (auto &it : e->items)
		{
			int r = reg_alloc(node);
			expr_to(it.get(), r);
		}
		emit_ABC(Opcode::NEWLIST, base, static_cast<int>(e->items.size()), 0, ln(node));
		reg_free_to(save);
		if (dest != base)
			emit_ABC(Opcode::MOVE, dest, base, 0, ln(node));
		break;
	}
	case NodeKind::TableLiteral:
	{
		auto *e = node->as<TableLiteral>();
		int save = fs->free_reg;
		int base = reg_alloc(node);
		for (intptr_t i = 0; i < static_cast<intptr_t>(e->keys.size()); ++i)
		{
			int kr = reg_alloc(node);
			expr_to(e->keys[i].get(), kr);
			int vr = reg_alloc(node);
			expr_to(e->values[i].get(), vr);
		}
		emit_ABC(Opcode::NEWTABLE, base, static_cast<int>(e->keys.size()), 0, ln(node));
		reg_free_to(save);
		if (dest != base)
			emit_ABC(Opcode::MOVE, dest, base, 0, ln(node));
		break;
	}
	case NodeKind::SetLiteral:
	{
		auto *e = node->as<SetLiteral>();
		int save = fs->free_reg;
		int base = reg_alloc(node);
		for (auto &it : e->items)
		{
			int r = reg_alloc(node);
			expr_to(it.get(), r);
		}
		emit_ABC(Opcode::NEWSET, base, static_cast<int>(e->items.size()), 0, ln(node));
		reg_free_to(save);
		if (dest != base)
			emit_ABC(Opcode::MOVE, dest, base, 0, ln(node));
		break;
	}
	case NodeKind::CallExpression:
		compile_call(node->as<CallExpression>(), dest);
		break;
	case NodeKind::ConditionalExpression:
		compile_conditional(node->as<ConditionalExpression>(), dest);
		break;
	case NodeKind::FunctionDefinition:
	{
		auto *f = node->as<FunctionDefinition>();
		int idx = compile_function(f);
		emit_ABx(Opcode::CLOSURE, dest, static_cast<uint32_t>(idx), ln(node));
		break;
	}
	case NodeKind::ThisExpression:
		error(node, "[Name error] 'this' is only valid in a method (classes arrive in M5)");
	case NodeKind::CastExpression:
		error(node, "[Type error] 'cast' arrives in M5");
	case NodeKind::FieldAccess:
		error(node, "[Name error] field access arrives in M5 (classes)");
	case NodeKind::SliceExpression:
		error(node, "[Index error] slices arrive in M6");
	case NodeKind::SplatExpression:
	case NodeKind::RefExpression:
		error(node, "[Compile error] ref/splat arguments arrive in M5");
	default:
		error(node, "[Compile error] unsupported expression");
	}
}

void Lowerer::compile_binary(BinaryExpression *b, int dest)
{
	bool swap = false, is_compare = false;
	Opcode op = arith_opcode(b->op, swap, is_compare);
	int save = fs->free_reg;
	int l = expr_any(b->lhs.get());
	int r = expr_any(b->rhs.get());
	if (swap)
		std::swap(l, r);
	emit_ABC(op, dest, l, r, ln(b));
	reg_free_to(save);
}

void Lowerer::compile_logical(BinaryExpression *b, int dest)
{
	// `a and b`: if a is falsy, result is a; else result is b.
	// `a or b` : if a is truthy, result is a; else result is b.
	expr_to(b->lhs.get(), dest);
	Opcode test = (b->op == Lexeme::And) ? Opcode::JMPF : Opcode::JMPT;
	intptr_t j = emit_jump(test, dest, ln(b));
	expr_to(b->rhs.get(), dest);
	patch_jump(j);
}

void Lowerer::compile_concat_parts(AstList &parts, int dest, uint32_t line)
{
	if (parts.empty())
	{
		emit_ABx(Opcode::LOADK, dest, static_cast<uint32_t>(k_string(String())), line);
		return;
	}
	int save = fs->free_reg;
	int base = reg_alloc(nullptr);
	for (auto &p : parts)
	{
		int r = (&p == &parts[0]) ? base : reg_alloc(nullptr);
		expr_to(p.get(), r);
	}
	int last = base + static_cast<int>(parts.size()) - 1;
	emit_ABC(Opcode::CONCAT, dest, base, last, line);
	reg_free_to(save);
}

void Lowerer::compile_conditional(ConditionalExpression *c, int dest)
{
	std::vector<intptr_t> end_jumps;
	for (intptr_t i = 0; i < static_cast<intptr_t>(c->conds.size()); ++i)
	{
		int save = fs->free_reg;
		int cr = expr_any(c->conds[i].get());
		intptr_t jf = emit_jump(Opcode::JMPF, cr, ln(c));
		reg_free_to(save);
		expr_to(c->values[i].get(), dest);
		end_jumps.push_back(emit_jump(Opcode::JMP, 0, ln(c)));
		patch_jump(jf);
	}
	expr_to(c->else_value.get(), dest);
	for (intptr_t j : end_jumps)
		patch_jump(j);
}

void Lowerer::compile_call(CallExpression *c, int dest)
{
	if (!c->options.empty())
		error(c, "[Compile error] named call options arrive in M5");
	for (auto &a : c->args)
		if (a->is<SplatExpression>() || a->is<RefExpression>())
			error(a.get(), "[Compile error] ref/splat arguments arrive in M5");

	int save = fs->free_reg;
	int base = reg_alloc(c);
	bool generic = false;

	if (auto *v = c->callee->as<Variable>())
	{
		NameRef nr = resolve(v->name);
		switch (nr.kind)
		{
		case NameKind::Local:
			emit_ABC(Opcode::MOVE, base, nr.index, 0, ln(c));
			break;
		case NameKind::Upvalue:
			emit_ABC(Opcode::GETUPVAL, base, nr.index, 0, ln(c));
			break;
		case NameKind::Module:
			emit_ABx(Opcode::GETMODULE, base, static_cast<uint32_t>(nr.index), ln(c));
			break;
		case NameKind::Generic:
			generic = true;
			emit_ABx(Opcode::LOADK, base, static_cast<uint32_t>(k_symbol(v->name)), ln(c));
			break;
		case NameKind::ClassObject:
			error(c, "[Compile error] constructor calls arrive in M5 (classes)");
		case NameKind::None:
			error(c, "[Name error] call to undeclared function '" +
			             std::string(symbol_name(v->name)) + "'");
		}
	}
	else
	{
		expr_to(c->callee.get(), base);
	}

	int nargs = static_cast<int>(c->args.size());
	for (auto &a : c->args)
	{
		int r = reg_alloc(a.get());
		expr_to(a.get(), r);
	}

	if (generic)
	{
		emit_ABC(Opcode::CALLG, base, nargs, 0, ln(c));
		int ic = P().num_ic++;
		emit(encode_Ax(Opcode::EXTRA_ARG, static_cast<uint32_t>(ic)), ln(c));
	}
	else
	{
		emit_ABC(Opcode::CALL, base, nargs, 0, ln(c));
	}

	reg_free_to(save);
	if (dest != base)
		emit_ABC(Opcode::MOVE, dest, base, 0, ln(c));
}

int Lowerer::compile_function(FunctionDefinition *f)
{
	for (auto &p : f->params)
	{
		auto *param = p->as<Parameter>();
		if (param->by_ref)
			error(p.get(), "[Compile error] 'ref' parameters arrive in M5");
		if (param->variadic)
			error(p.get(), "[Compile error] variadic parameters arrive in M5");
		if (param->default_value)
			error(p.get(), "[Compile error] default/named parameters arrive in M5");
	}

	auto child = std::make_unique<Proto>();
	child->name = f->name;
	child->num_params = static_cast<int>(f->params.size());

	FuncState cfs(*child, fs, false);
	FuncState *saved = fs;
	fs = &cfs;

	for (auto &p : f->params)
		declare_local(p->as<Parameter>()->name, false, p.get());

	auto *body = f->body->as<StatementList>();
	compile_block(body);

	// Guarantee a trailing return.
	emit_ABC(Opcode::RET, 0, 0, 0, ln(f));
	child->num_regs = cfs.max_reg > child->num_params ? cfs.max_reg : child->num_params;

	fs = saved;
	int idx = static_cast<int>(fs->proto.children.size());
	fs->proto.children.push_back(std::move(child));
	return idx;
}

// --- statements ---------------------------------------------------------------

void Lowerer::compile_block(StatementList *sl)
{
	bool own = sl->scope;
	if (own)
		enter_block();
	for (auto &s : sl->statements)
		compile_stmt(s.get());
	if (own)
		exit_block(static_cast<uint32_t>(sl->line));
}

void Lowerer::compile_stmt(Ast *node)
{
	switch (node->kind)
	{
	case NodeKind::StatementList:
		compile_block(node->as<StatementList>());
		break;
	case NodeKind::Declaration:
		compile_declaration(node->as<Declaration>());
		break;
	case NodeKind::Assignment:
		compile_assignment(node->as<Assignment>());
		break;
	case NodeKind::ExpressionStatement:
	{
		int save = fs->free_reg;
		int t = reg_alloc(node);
		expr_to(node->as<ExpressionStatement>()->expr.get(), t);
		reg_free_to(save);
		break;
	}
	case NodeKind::IfStatement:
		compile_if(node->as<IfStatement>());
		break;
	case NodeKind::WhileStatement:
		compile_while(node->as<WhileStatement>());
		break;
	case NodeKind::RepeatStatement:
		compile_repeat(node->as<RepeatStatement>());
		break;
	case NodeKind::ForNumeric:
		compile_for_numeric(node->as<ForNumeric>());
		break;
	case NodeKind::ReturnStatement:
		compile_return(node->as<ReturnStatement>());
		break;
	case NodeKind::LoopControl:
	{
		if (m_loops.empty())
			error(node, "[Compile error] 'break'/'continue' outside a loop");
		auto *lc = node->as<LoopControl>();
		intptr_t j = emit_jump(Opcode::JMP, 0, ln(node));
		if (lc->kind_tok == Lexeme::Break)
			m_loops.back().breaks.push_back(j);
		else
			m_loops.back().continues.push_back(j);
		break;
	}
	case NodeKind::FunctionDefinition:
		compile_named_function(node->as<FunctionDefinition>());
		break;
	case NodeKind::ForEach:
		error(node, "[Compile error] 'for … in' (iteration protocol) arrives in M5");
	case NodeKind::ClassDeclaration:
		error(node, "[Compile error] class declarations arrive in M5");
	case NodeKind::TryStatement:
	case NodeKind::ThrowStatement:
		error(node, "[Compile error] try/throw arrive in M5");
	case NodeKind::SpawnStatement:
		error(node, "[Compile error] 'spawn' arrives in M7");
	case NodeKind::ImportStatement:
		error(node, "[Compile error] 'import' arrives with modules (M8)");
	default:
		error(node, "[Compile error] unsupported statement");
	}
}

void Lowerer::compile_declaration(Declaration *d)
{
	if (fs->is_module)
	{
		int slot = module_lookup(d->name);
		if (slot < 0)
			slot = module_define(d->name);
		if (d->init)
		{
			int save = fs->free_reg;
			int t = reg_alloc(d);
			expr_to(d->init.get(), t);
			emit_ABx(Opcode::SETMODULE, t, static_cast<uint32_t>(slot), ln(d));
			reg_free_to(save);
		}
		return;
	}
	int L = declare_local(d->name, d->is_const, d);
	if (d->init)
		expr_to(d->init.get(), L);
	else
		emit_ABC(Opcode::LOADNULL, L, 0, 0, ln(d));
}

void Lowerer::assign_plain(Ast *target, Ast *value)
{
	if (auto *v = target->as<Variable>())
	{
		NameRef nr = resolve(v->name);
		switch (nr.kind)
		{
		case NameKind::Local:
			if (nr.is_const)
				error(target, "[Name error] cannot assign to const '" +
				                  std::string(symbol_name(v->name)) + "'");
			expr_to(value, nr.index);
			break;
		case NameKind::Upvalue:
		{
			int t = expr_any(value);
			emit_ABC(Opcode::SETUPVAL, t, nr.index, 0, ln(target));
			break;
		}
		case NameKind::Module:
		{
			int save = fs->free_reg;
			int t = reg_alloc(target);
			expr_to(value, t);
			emit_ABx(Opcode::SETMODULE, t, static_cast<uint32_t>(nr.index), ln(target));
			reg_free_to(save);
			break;
		}
		case NameKind::ClassObject:
		case NameKind::Generic:
		case NameKind::None:
			error(target, "[Name error] cannot assign to '" + std::string(symbol_name(v->name)) +
			                  "' (assignment never declares — use 'var')");
		}
		return;
	}
	if (auto *ix = target->as<IndexExpression>())
	{
		if (ix->indices.size() != 1 || ix->indices[0]->is<SliceExpression>())
			error(target, "[Index error] slice assignment arrives in M6");
		int save = fs->free_reg;
		int wb, wbidx;
		int o = load_index_object(ix, wb, wbidx);
		int i = expr_any(ix->indices[0].get());
		int val = expr_any(value);
		emit_ABC(Opcode::SETINDEX, o, i, val, ln(target));
		emit_index_writeback(o, wb, wbidx, ln(target));
		reg_free_to(save);
		return;
	}
	if (target->is<FieldAccess>())
		error(target, "[Name error] field assignment arrives in M5 (classes)");
	error(target, "[Compile error] invalid assignment target");
}

int Lowerer::load_index_object(IndexExpression *ix, int &wb, int &wbidx)
{
	wb = 0;
	wbidx = 0;
	if (auto *ov = ix->object->as<Variable>())
	{
		NameRef nr = resolve(ov->name);
		if (nr.kind == NameKind::Local)
			return nr.index; // the local register IS the storage; SETINDEX mutates it
		if (nr.kind == NameKind::Module)
		{
			int o = reg_alloc(ix);
			emit_ABx(Opcode::GETMODULE, o, static_cast<uint32_t>(nr.index), ln(ix));
			wb = 1;
			wbidx = nr.index;
			return o;
		}
		if (nr.kind == NameKind::Upvalue)
		{
			int o = reg_alloc(ix);
			emit_ABC(Opcode::GETUPVAL, o, nr.index, 0, ln(ix));
			wb = 2;
			wbidx = nr.index;
			return o;
		}
	}
	// A general expression object (e.g. f()[i] = v): mutation targets a temporary.
	return expr_any(ix->object.get());
}

void Lowerer::emit_index_writeback(int o, int wb, int wbidx, uint32_t line)
{
	if (wb == 1)
		emit_ABx(Opcode::SETMODULE, o, static_cast<uint32_t>(wbidx), line);
	else if (wb == 2)
		emit_ABC(Opcode::SETUPVAL, o, wbidx, 0, line);
}

void Lowerer::compile_assignment(Assignment *a)
{
	if (a->op == Lexeme::Assign)
	{
		assign_plain(a->target.get(), a->value.get());
		return;
	}

	// Compound assignment: rewrite target op= value into target = target <op> value,
	// evaluating any index/object subexpression exactly once.
	bool is_concat = (a->op == Lexeme::ConcatEq);
	Lexeme binop;
	switch (a->op)
	{
	case Lexeme::PlusEq: binop = Lexeme::Plus; break;
	case Lexeme::MinusEq: binop = Lexeme::Minus; break;
	case Lexeme::StarEq: binop = Lexeme::Star; break;
	case Lexeme::SlashEq: binop = Lexeme::Slash; break;
	case Lexeme::ConcatEq: binop = Lexeme::Concat; break;
	default: error(a, "[Compile error] unsupported compound assignment");
	}

	auto emit_combine = [&](int dest, int cur, Ast *value) {
		if (is_concat)
		{
			int save = fs->free_reg;
			int b0 = reg_alloc(a);
			emit_ABC(Opcode::MOVE, b0, cur, 0, ln(a));
			int b1 = reg_alloc(a);
			expr_to(value, b1);
			emit_ABC(Opcode::CONCAT, dest, b0, b1, ln(a));
			reg_free_to(save);
		}
		else
		{
			bool sw = false, cmp = false;
			Opcode op = arith_opcode(binop, sw, cmp);
			int save = fs->free_reg;
			int vr = expr_any(value);
			emit_ABC(op, dest, cur, vr, ln(a));
			reg_free_to(save);
		}
	};

	if (auto *v = a->target->as<Variable>())
	{
		NameRef nr = resolve(v->name);
		switch (nr.kind)
		{
		case NameKind::Local:
			if (nr.is_const)
				error(a->target.get(), "[Name error] cannot assign to const");
			emit_combine(nr.index, nr.index, a->value.get());
			break;
		case NameKind::Upvalue:
		{
			int save = fs->free_reg;
			int t = reg_alloc(a);
			emit_ABC(Opcode::GETUPVAL, t, nr.index, 0, ln(a));
			emit_combine(t, t, a->value.get());
			emit_ABC(Opcode::SETUPVAL, t, nr.index, 0, ln(a));
			reg_free_to(save);
			break;
		}
		case NameKind::Module:
		{
			int save = fs->free_reg;
			int t = reg_alloc(a);
			emit_ABx(Opcode::GETMODULE, t, static_cast<uint32_t>(nr.index), ln(a));
			emit_combine(t, t, a->value.get());
			emit_ABx(Opcode::SETMODULE, t, static_cast<uint32_t>(nr.index), ln(a));
			reg_free_to(save);
			break;
		}
		default:
			error(a->target.get(), "[Name error] cannot assign to this target");
		}
		return;
	}
	if (auto *ix = a->target->as<IndexExpression>())
	{
		if (ix->indices.size() != 1 || ix->indices[0]->is<SliceExpression>())
			error(a->target.get(), "[Index error] slice assignment arrives in M6");
		int save = fs->free_reg;
		int wb, wbidx;
		int o = load_index_object(ix, wb, wbidx);
		int i = expr_any(ix->indices[0].get());
		int t = reg_alloc(a);
		emit_ABC(Opcode::GETINDEX, t, o, i, ln(a));
		emit_combine(t, t, a->value.get());
		emit_ABC(Opcode::SETINDEX, o, i, t, ln(a));
		emit_index_writeback(o, wb, wbidx, ln(a));
		reg_free_to(save);
		return;
	}
	error(a->target.get(), "[Compile error] invalid compound-assignment target");
}

void Lowerer::compile_if(IfStatement *s)
{
	std::vector<intptr_t> end_jumps;
	for (intptr_t i = 0; i < static_cast<intptr_t>(s->conds.size()); ++i)
	{
		int save = fs->free_reg;
		int cr = expr_any(s->conds[i].get());
		intptr_t jf = emit_jump(Opcode::JMPF, cr, ln(s));
		reg_free_to(save);
		compile_stmt(s->bodies[i].get());
		if (i + 1 < static_cast<intptr_t>(s->conds.size()) || s->else_body)
			end_jumps.push_back(emit_jump(Opcode::JMP, 0, ln(s)));
		patch_jump(jf);
	}
	if (s->else_body)
		compile_stmt(s->else_body.get());
	for (intptr_t j : end_jumps)
		patch_jump(j);
}

void Lowerer::compile_while(WhileStatement *s)
{
	m_loops.emplace_back();
	intptr_t top = P().code.size();
	int save = fs->free_reg;
	int cr = expr_any(s->cond.get());
	intptr_t exit = emit_jump(Opcode::JMPF, cr, ln(s));
	reg_free_to(save);
	compile_stmt(s->body.get());
	emit_AsBx(Opcode::JMP, 0, static_cast<int>(top - (P().code.size() + 1)), ln(s));
	patch_jump(exit);
	// break -> here; continue -> loop top.
	LoopCtx &lc = m_loops.back();
	for (intptr_t b : lc.breaks)
		patch_jump(b);
	for (intptr_t c : lc.continues)
		P().code[c] = encode_AsBx(Opcode::JMP, 0, static_cast<int>(top - (c + 1)));
	m_loops.pop_back();
}

void Lowerer::compile_repeat(RepeatStatement *s)
{
	m_loops.emplace_back();
	intptr_t top = P().code.size();
	compile_stmt(s->body.get());
	intptr_t cont_target = P().code.size();
	int save = fs->free_reg;
	int cr = expr_any(s->cond.get());
	// repeat … until c: loop back while c is false.
	emit_AsBx(Opcode::JMPF, cr, static_cast<int>(top - (P().code.size() + 1)), ln(s));
	reg_free_to(save);
	LoopCtx &lc = m_loops.back();
	for (intptr_t b : lc.breaks)
		patch_jump(b);
	for (intptr_t c : lc.continues)
		P().code[c] = encode_AsBx(Opcode::JMP, 0, static_cast<int>(cont_target - (c + 1)));
	m_loops.pop_back();
}

void Lowerer::compile_for_numeric(ForNumeric *s)
{
	enter_block();
	int base = reg_alloc(s); // index
	expr_to(s->start.get(), base);
	int limit = reg_alloc(s);
	expr_to(s->stop.get(), limit);
	int step = reg_alloc(s);
	if (s->step)
		expr_to(s->step.get(), step);
	else
		emit_AsBx(Opcode::LOADI, step, 1, ln(s));
	// The loop variable is a fresh local at base+3, visible in the body.
	int var = reg_alloc(s);
	fs->locals.push_back({s->var, var, false, false});

	m_loops.emplace_back();
	intptr_t prep = emit_jump(Opcode::FORPREP, base, ln(s));
	intptr_t body = P().code.size();
	compile_stmt(s->body.get());
	intptr_t cont_target = P().code.size();
	emit_AsBx(Opcode::FORLOOP, base, static_cast<int>(body - (P().code.size() + 1)), ln(s));
	// FORPREP jumps forward to the FORLOOP instruction (which tests before the
	// first iteration), NOT past it.
	P().code[prep] = encode_AsBx(Opcode::FORPREP, base, static_cast<int>(cont_target - (prep + 1)));

	LoopCtx &lc = m_loops.back();
	for (intptr_t b : lc.breaks)
		patch_jump(b);
	for (intptr_t c : lc.continues)
		P().code[c] = encode_AsBx(Opcode::JMP, 0, static_cast<int>(cont_target - (c + 1)));
	m_loops.pop_back();
	exit_block(static_cast<uint32_t>(s->line));
}

void Lowerer::compile_return(ReturnStatement *s)
{
	if (s->expr)
	{
		int save = fs->free_reg;
		int t = reg_alloc(s);
		expr_to(s->expr.get(), t);
		emit_ABC(Opcode::RET, t, 1, 0, ln(s));
		reg_free_to(save);
	}
	else
	{
		emit_ABC(Opcode::RET, 0, 0, 0, ln(s));
	}
}

void Lowerer::compile_named_function(FunctionDefinition *f)
{
	// Nested named function: a local binding holding a closure. Declaring the local
	// before compiling the body lets the function recurse (it captures itself).
	int L = declare_local(f->name, false, f);
	int idx = compile_function(f);
	emit_ABx(Opcode::CLOSURE, L, static_cast<uint32_t>(idx), ln(f));
}

// --- module entry -------------------------------------------------------------

void Lowerer::compile_module(Ast *module_ast, CompiledModule &out)
{
	auto *list = module_ast->as<StatementList>();
	// `out` owns the module Proto from the outset, so a compile error thrown
	// mid-lowering still frees the partially-built tree (CompiledModule's destructor).
	out.main = std::make_unique<Proto>();
	Proto &main = *out.main;
	main.name = NO_SYMBOL;

	FuncState mfs(main, nullptr, true);
	fs = &mfs;

	// Pass 1: reserve module slots for top-level bindings so functions may refer to
	// declarations that appear later (design §11, two-pass top level).
	for (auto &s : list->statements)
	{
		if (auto *d = s->as<Declaration>())
			module_define(d->name);
		else if (auto *f = s->as<FunctionDefinition>())
			if (!f->is_anonymous())
				module_define(f->name);
	}

	// Pass 2a: hoist top-level function closures into their slots.
	for (auto &s : list->statements)
	{
		if (auto *f = s->as<FunctionDefinition>())
		{
			if (f->is_anonymous())
				continue;
			int idx = compile_function(f);
			int t = reg_alloc(f);
			emit_ABx(Opcode::CLOSURE, t, static_cast<uint32_t>(idx), ln(f));
			emit_ABx(Opcode::SETMODULE, t, static_cast<uint32_t>(module_lookup(f->name)), ln(f));
			reg_free_to(t);
		}
	}

	// Pass 2b: execute statements top to bottom. The value of a trailing expression
	// statement becomes the module result (so do_string returns it, REPL-style).
	intptr_t count = static_cast<intptr_t>(list->statements.size());
	for (intptr_t i = 0; i < count; ++i)
	{
		Ast *s = list->statements[i].get();
		if (auto *f = s->as<FunctionDefinition>(); f && !f->is_anonymous())
			continue; // already hoisted
		if (i == count - 1 && s->is<ExpressionStatement>())
		{
			int r = reg_alloc(s);
			expr_to(s->as<ExpressionStatement>()->expr.get(), r);
			emit_ABC(Opcode::HALT, r, 1, 0, ln(s));
			main.num_regs = mfs.max_reg;
			out.num_slots = m_num_slots;
			return;
		}
		compile_stmt(s);
	}
	emit_ABC(Opcode::HALT, 0, 0, 0, 0);
	main.num_regs = mfs.max_reg;
	out.num_slots = m_num_slots;
}

} // namespace

void compile_module(Ast *module_ast, CompiledModule &out)
{
	Lowerer lw;
	lw.compile_module(module_ast, out);
}

} // namespace phonometrica
