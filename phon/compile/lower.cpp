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
#include <phon/core/flat_hash_set.hpp>
#include <phon/core/vector.hpp>
#include <phon/dispatch/generic.hpp>
#include <phon/object/class.hpp>
#include <phon/types/atom.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/opcode.hpp>

#include <string> // SyntaxError messages (the M3 compiler diagnostic type)

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
	bool is_ref = false; // a `ref` parameter: its register holds a reference (§7)
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
	Vector<Local> locals;
	Vector<BlockInfo> blocks;
	int free_reg = 0;
	int max_reg = 0;
	int finally_base = 0; // m_finally_stack size at function entry (return unwinds to here)
	bool is_module;
};

// Break/continue patch targets for the innermost enclosing loop. `finally_base` is
// the enclosing-`try` depth at loop entry, so break/continue run the finally blocks
// of the `try`s they exit (design §12).
struct LoopCtx
{
	Vector<intptr_t> breaks;
	Vector<intptr_t> continues;
	int finally_base = 0;
};

class Lowerer
{
public:
	explicit Lowerer(ModuleNamespace &ns) : m_ns(ns) {}
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
		for (intptr_t i = b.locals_count; i < fs->locals.size(); ++i)
			captured = captured || fs->locals[i].captured;
		if (captured)
			emit_ABC(Opcode::CLOSE, b.free_reg, 0, 0, line);
		fs->locals.resize(b.locals_count);
		fs->free_reg = b.free_reg;
	}

	int declare_local(Symbol name, bool is_const, Ast *node)
	{
		int r = reg_alloc(node);
		fs->locals.push_back({name, r, is_const, false});
		return r;
	}

	// Declare a function parameter; a `ref` parameter's register holds a reference (§7).
	void declare_param(Parameter *p)
	{
		declare_local(p->name, false, p);
		fs->locals.back().is_ref = p->by_ref;
	}

	int module_define(Symbol name)
	{
		auto it = m_ns.name_to_slot.find(name.id);
		if (it != m_ns.name_to_slot.end())
			return it->second; // already bound (persists across REPL chunks)
		int slot = m_ns.num_slots++;
		m_ns.name_to_slot.insert(name.id, slot);
		return slot;
	}
	int module_lookup(Symbol name) const
	{
		auto it = m_ns.name_to_slot.find(name.id);
		return it == m_ns.name_to_slot.end() ? -1 : it->second;
	}

	// Only builtin classes are looked up by name globally; user classes are
	// module-scoped bindings (resolved via the namespace), so a stale user class
	// from a prior run in the process-global registry can never be matched here.
	static Class *class_by_name(Symbol name)
	{
		std::string_view want = symbol_name(name);
		intptr_t n = class_count();
		for (intptr_t i = 0; i < n; ++i)
		{
			if (!has_class(static_cast<uint32_t>(i)))
				continue;
			Class *c = get_class(static_cast<uint32_t>(i));
			if (c && c->name && want == c->name && (c->flags & CLASS_BUILTIN) &&
			    !(c->flags & CLASS_META))
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
		bool is_ref = false; // a `ref` local: `index` holds a reference, not the value
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
			r.is_ref = fs->locals[li].is_ref;
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
		// A top-level `function` defined anywhere in this module (pass 1), or an
		// already-registered generic with at least one method (builtins, or a
		// function from an earlier REPL chunk). A generic emptied by journal
		// retraction is treated as undefined, so a name resolves to `None` again.
		if (m_module_generics.contains(name.id))
		{
			r.kind = NameKind::Generic;
			return r;
		}
		if (GenericFunction *g = find_generic(name); g && g->methods.size() > 0)
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
	// Promote an lvalue argument to a reference at a `ref` parameter position,
	// driven by the callee's uniform ref-mask (design/references.md §6).
	void emit_promote_arg(Ast *arg, int r);
	// Load argument `arg` into register `r` for an *indirect* call whose callee sits
	// in register `callee`: the callee's ref-mask is consulted at runtime (§6.2).
	void emit_maybe_promote_arg(Ast *arg, int r, int callee);
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
	// Emit a slice's start/stop/step into the three consecutive registers base,
	// base+1, base+2 (an absent part becomes null → the runtime default).
	void emit_slice_parts(SliceExpression *sl, int base);
	// Record the keyword-only options (params with a default) on the proto and emit the
	// prologue that fills each unsupplied slot (the missing sentinel) with its default.
	void emit_option_prologue(AstList &params);
	void compile_if(IfStatement *s);
	void compile_while(WhileStatement *s);
	void compile_repeat(RepeatStatement *s);
	void compile_for_numeric(ForNumeric *s);
	void compile_for_each(ForEach *s);
	void compile_for_each_ref(ForEach *s); // `for ref v in …` (design §12)
	void compile_spawn(SpawnStatement *s); // `spawn f(args…)` (design §13)
	void compile_return(ReturnStatement *s);
	void compile_try(TryStatement *s);
	void compile_throw(ThrowStatement *s);
	void compile_named_function(FunctionDefinition *f); // nested (local) named fn

	// A top-level `function` / class `method` (design §6) is a method on a generic.
	// Resolve a declared type annotation to a TypeRef (Object if unannotated), and
	// register a method's signature as a Proto MethodDef. `self` is the enclosing
	// class for a method's implicit `this` param (null for a free function).
	TypeRef type_ref(Ast *type_node);
	int add_method_def(FunctionDefinition *f, TypeRef self, bool have_self);

	// --- classes (design §5.6/§6) ---
	static Symbol this_symbol(); // the reserved `this` local name (a keyword, never a user id)
	void compile_cast(CastExpression *c, int dest);
	int compile_method(FunctionDefinition *m, bool is_init, ClassDeclaration *cls);
	// Apply field defaults for the whole layout (base→derived) to the instance in
	// `thisreg`, at construction — so inherited defaults apply regardless of which
	// (possibly inherited) `init` runs.
	void emit_full_defaults(ClassDeclaration *cls, int thisreg);
	int compile_accessor(FieldDeclaration *fd, bool is_setter); // -> module child-proto index
	void compile_class(ClassDeclaration *c);
	void construct(ClassDeclaration *cls, int class_slot, Class *builtin, CallExpression *call,
	               int dest);
	// `this.field` reaches the raw slot (bypassing accessor routing and the runtime
	// privacy check) when it names the field whose own accessor is being compiled
	// (recursion safety) or a private field of the class hierarchy (which has no
	// accessor and is accessible only through `this`). Any other object expression
	// goes through the checked/routed GETFIELD/SETFIELD.
	bool this_raw_field(Ast *obj, Symbol field) const noexcept
	{
		if (!obj->is<ThisExpression>())
			return false;
		return (m_accessor_field != NO_SYMBOL && field == m_accessor_field) ||
		       m_local_fields.contains(field.id);
	}
	// Load an object expression for in-place field mutation, mirroring
	// load_index_object (wb/wbidx select the write-back).
	int load_object_for_write(Ast *obj, int &wb, int &wbidx);

	static Opcode arith_opcode(Lexeme op, bool &swap_operands, bool &is_compare);

	// One enclosing `try` per entry: its finally body (nullable) and whether its
	// handler is still on the VM handler stack (true in the try body, false once its
	// catch clauses run). `return`/`break`/`continue` walk this to run the finallys —
	// and POPTRY the still-active handlers — of the `try`s they exit.
	struct FinallyCtx
	{
		Ast *finally_body;
		bool handler_active;
	};
	Vector<FinallyCtx> m_finally_stack;
	void emit_finally_unwind(int down_to);

	FuncState *fs = nullptr;
	Vector<LoopCtx> m_loops;
	ModuleNamespace &m_ns; // persistent module namespace (owned by the caller)

	// Names declared as generic methods by this module's top-level `function`s
	// (non-`local`). Populated in pass 1 so a call to a function defined later —
	// or one that only exists once DEFMETHOD runs — resolves to a generic call.
	FlatHashSet<uint32_t> m_module_generics;

	// Uniform `ref` mask of each generic named in this module (bit i => argument
	// position i is `ref`). Populated in pass 1 so a call can promote the right
	// argument slots (design/references.md §6) without a runtime mask lookup. For a
	// generic not defined in this module (a builtin, or an earlier REPL chunk) the
	// mask comes from the registered GenericFunction instead (see generic_ref_mask).
	FlatHashMap<uint32_t, uint64_t> m_generic_ref_mask;

	// The compile-time `ref` mask of a call target named `name`: this module's
	// definition if it has one, else the registered generic's uniform mask, else 0.
	uint64_t generic_ref_mask(Symbol name) const
	{
		auto it = m_generic_ref_mask.find(name.id);
		if (it != m_generic_ref_mask.end())
			return it->second;
		if (GenericFunction *g = find_generic(name))
			return g->ref_mask;
		return 0;
	}

	// The uniform `ref` mask of a function/method definition, computed from its
	// parameters' `by_ref` flags. `have_self` shifts the explicit params past the
	// implicit `this` at position 0 (which is never `ref`).
	static uint64_t compute_ref_mask(FunctionDefinition *f, bool have_self)
	{
		uint64_t mask = 0;
		int pos = have_self ? 1 : 0;
		for (auto &p : f->params)
		{
			if (p->as<Parameter>()->by_ref)
				mask |= (uint64_t(1) << pos);
			++pos;
		}
		return mask;
	}

	// Names of top-level classes declared in this module → their AST (for
	// construction lowering and `this`/base type resolution). Classes are module
	// bindings holding a class object; the module slot is module_lookup(name).
	FlatHashMap<uint32_t, ClassDeclaration *> m_module_classes;

	// The field whose accessor body is currently being compiled (NO_SYMBOL if none),
	// so `this.<that field>` compiles to a raw slot access rather than re-entering
	// the accessor (design "Field accessors" recursion-safety rule).
	Symbol m_accessor_field = NO_SYMBOL;

	// Private field names reachable via `this` while compiling the current class's
	// methods/accessors: the class's own `local field`s plus, protected-style, those
	// of its in-module base chain.
	FlatHashSet<uint32_t> m_local_fields;
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
		// A `ref` local must be dereferenced (via expr_to), so it can't be borrowed.
		if (nr.kind == NameKind::Local && !nr.is_ref)
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
			if (nr.is_ref)
				emit_ABC(Opcode::DEREF, dest, nr.index, 0, ln(node)); // read through the ref
			else if (nr.index != dest)
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
		// `x is T`: load T's class object (a builtin loads a constant; a user class
		// loads its module binding) and test membership.
		auto *e = node->as<IsExpression>();
		if (!e->type->is<Variable>())
			error(e->type.get(), "[Type error] expected a class name after 'is'");
		int save = fs->free_reg;
		int b = expr_any(e->expr.get());
		int cr = reg_alloc(e->type.get());
		expr_to(e->type.get(), cr);
		emit_ABC(Opcode::IS, dest, b, cr, ln(node));
		reg_free_to(save);
		break;
	}
	case NodeKind::IndexExpression:
	{
		auto *e = node->as<IndexExpression>();
		bool has_slice = false;
		for (auto &ix : e->indices)
			if (ix->is<SliceExpression>())
				has_slice = true;
		if (e->indices.size() == 1 && has_slice)
		{
			int save = fs->free_reg;
			int b = expr_any(e->object.get());
			int c = reg_alloc(node); // slice base: start, stop, step (3 consecutive regs)
			reg_alloc(node);
			reg_alloc(node);
			emit_slice_parts(e->indices[0]->as<SliceExpression>(), c);
			emit_ABC(Opcode::GETSLICE, dest, b, c, ln(node));
			reg_free_to(save);
			break;
		}
		if (e->indices.size() > 1 && has_slice)
		{
			// Multi-dimensional slicing (Array): object then a 3-register slice-part block
			// per axis; scalar axes (marked in the mask) collapse. GETVIEW builds a view.
			int save = fs->free_reg;
			int objbase = reg_alloc(node);
			expr_to(e->object.get(), objbase);
			uint32_t scalar_mask = 0;
			int rank = static_cast<int>(e->indices.size());
			for (int k = 0; k < rank; ++k)
			{
				int pbase = reg_alloc(node); // 3 regs per axis (start, stop, step)
				reg_alloc(node);
				reg_alloc(node);
				if (auto *sl = e->indices[k]->as<SliceExpression>())
					emit_slice_parts(sl, pbase);
				else
				{
					expr_to(e->indices[k].get(), pbase); // scalar axis: index in the first reg
					scalar_mask |= (1u << k);
				}
			}
			emit_ABC(Opcode::GETVIEW, dest, objbase, rank, ln(node));
			emit(encode_Ax(Opcode::EXTRA_ARG, scalar_mask), ln(node));
			reg_free_to(save);
			break;
		}
		if (e->indices.size() > 1)
		{
			// Multi-dimensional scalar indexing (Array only): object then one register per
			// index, staged contiguously; GETIDXN reads the element.
			int save = fs->free_reg;
			int objbase = reg_alloc(node);
			expr_to(e->object.get(), objbase);
			for (auto &ix : e->indices)
			{
				int r = reg_alloc(node);
				expr_to(ix.get(), r);
			}
			emit_ABC(Opcode::GETIDXN, dest, objbase, static_cast<int>(e->indices.size()), ln(node));
			reg_free_to(save);
			break;
		}
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
	case NodeKind::ArrayLiteral:
	{
		// Stage every element (row-major source order) then NEWARRAY. C carries nrow
		// (0 => a 1-D array); the runtime stores column-major and promotes ints to Float.
		auto *e = node->as<ArrayLiteral>();
		int save = fs->free_reg;
		int base = reg_alloc(node);
		for (auto &el : e->elems)
		{
			int r = reg_alloc(node);
			expr_to(el.get(), r);
		}
		int nrow_code = (e->rank == 1) ? 0 : e->nrow;
		emit_ABC(Opcode::NEWARRAY, base, static_cast<int>(e->elems.size()), nrow_code, ln(node));
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
	{
		// `this` is a hidden local at register 0 of a method; a closure nested in a
		// method reaches it as an upvalue (via the reserved `this` symbol).
		NameRef nr = resolve(this_symbol());
		if (nr.kind == NameKind::Local)
		{
			if (nr.index != dest)
				emit_ABC(Opcode::MOVE, dest, nr.index, 0, ln(node));
		}
		else if (nr.kind == NameKind::Upvalue)
			emit_ABC(Opcode::GETUPVAL, dest, nr.index, 0, ln(node));
		else
			error(node, "[Name error] 'this' is only valid inside a method");
		break;
	}
	case NodeKind::CastExpression:
		compile_cast(node->as<CastExpression>(), dest);
		break;
	case NodeKind::FieldAccess:
	{
		auto *fa = node->as<FieldAccess>();
		int save = fs->free_reg;
		int o = expr_any(fa->object.get());
		int s = reg_alloc(fa);
		emit_ABx(Opcode::LOADK, s, static_cast<uint32_t>(k_symbol(fa->name)), ln(fa));
		Opcode get = this_raw_field(fa->object.get(), fa->name) ? Opcode::GETFIELDRAW
		                                                        : Opcode::GETFIELD;
		emit_ABC(get, dest, o, s, ln(fa));
		reg_free_to(save);
		break;
	}
	case NodeKind::SliceExpression:
		error(node, "[Index error] a slice is only valid inside '[ ]'");
	case NodeKind::RefExpression:
		error(node, "[Compile error] 'ref' is only valid as a call argument (design §7)");
	case NodeKind::SplatExpression:
		error(node, "[Compile error] '...' is only valid as a call argument");
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
	Vector<intptr_t> end_jumps;
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

void Lowerer::emit_promote_arg(Ast *arg, int r)
{
	// The callee's signature marks this position `ref`, so the argument must be an
	// lvalue whose storage the callee can write through (design/references.md §6). A
	// non-lvalue (literal or computed expression) is a compile error — the mutation
	// would have nowhere to go. In this stage the reference is still the second-class
	// register pointer (MAKEREF); Stage 4 replaces it with a boxed reference, and
	// container-element / field sources arrive with the CoW×ref stage.
	if (auto *v = arg->as<Variable>())
	{
		NameRef nr = resolve(v->name);
		switch (nr.kind)
		{
		case NameKind::Local:
			// A plain local promotes to a fresh box; a `ref` local forwards its box.
			emit_ABC(nr.is_ref ? Opcode::MOVE : Opcode::MAKEREF, r, nr.index, 0, ln(arg));
			break;
		case NameKind::Upvalue:
			emit_ABC(Opcode::PROMOTEUPVAL, r, nr.index, 0, ln(arg));
			break;
		case NameKind::Module:
			emit_ABx(Opcode::PROMOTEMODULE, r, static_cast<uint32_t>(nr.index), ln(arg));
			break;
		case NameKind::None:
			error(arg, "[Name error] reference to undeclared variable '" +
			               std::string(symbol_name(v->name)) + "'");
		default:
			error(arg, "[Compile error] this variable cannot be passed by reference");
		}
		return;
	}
	// A list element `c[i]`: load the container for in-place mutation, promote the
	// element to a boxed reference, then write the (detached) container back (§7).
	if (auto *ix = arg->as<IndexExpression>())
	{
		if (ix->indices.size() != 1 || ix->indices[0]->is<SliceExpression>())
			error(arg, "[Index error] only a single-element reference is supported");
		int wb, wbidx;
		int cont = load_index_object(ix, wb, wbidx);
		int idxr = expr_any(ix->indices[0].get());
		emit_ABC(Opcode::PROMOTEINDEX, r, cont, idxr, ln(arg));
		emit_index_writeback(cont, wb, wbidx, ln(arg));
		return;
	}
	// An object field `o.field`: as above, for a field slot.
	if (auto *fa = arg->as<FieldAccess>())
	{
		int wb, wbidx;
		int obj = load_object_for_write(fa->object.get(), wb, wbidx);
		int sreg = reg_alloc(fa);
		emit_ABx(Opcode::LOADK, sreg, static_cast<uint32_t>(k_symbol(fa->name)), ln(arg));
		emit_ABC(Opcode::PROMOTEFIELD, r, obj, sreg, ln(arg));
		emit_index_writeback(obj, wb, wbidx, ln(arg));
		return;
	}
	error(arg, "[Compile error] a 'ref' parameter requires an lvalue: a variable, a list "
	           "element, or an object field");
}

void Lowerer::emit_maybe_promote_arg(Ast *arg, int r, int callee)
{
	// The callee is only known at runtime, so we cannot decide promotion now. A local
	// variable is a promotable lvalue: MAYBEPROMOTE forwards/boxes it iff the callee
	// marks the position `ref`. Any other argument is computed, then MAYBEBOX gives it
	// a (write-back-less) box only if the callee expects a reference there.
	if (auto *v = arg->as<Variable>())
	{
		NameRef nr = resolve(v->name);
		if (nr.kind == NameKind::Local)
		{
			emit_ABC(Opcode::MAYBEPROMOTE, r, nr.index, callee, ln(arg));
			return;
		}
	}
	expr_to(arg, r);
	emit_ABC(Opcode::MAYBEBOX, r, 0, callee, ln(arg));
}

void Lowerer::compile_call(CallExpression *c, int dest)
{
	// `ClassName(args)` is construction, not a call (design §6): a user class, or the
	// builtin Error hierarchy (`throw Error("…")`).
	if (auto *v = c->callee->as<Variable>())
	{
		auto it = m_module_classes.find(v->name.id);
		if (it != m_module_classes.end())
		{
			construct(it->second, module_lookup(v->name), nullptr, c, dest);
			return;
		}
		if (Class *bc = class_by_name(v->name); bc && is_a(bc, error_class()))
		{
			construct(nullptr, -1, bc, c, dest);
			return;
		}
	}

	bool has_splat = false;
	for (auto &a : c->args)
		if (a->is<SplatExpression>())
			has_splat = true;

	int save = fs->free_reg;
	int base = reg_alloc(c);
	bool generic = false;
	// The callee's uniform ref-mask drives which argument slots are promoted to
	// references (design/references.md §6). Known at compile time for a named
	// generic; 0 otherwise (an indirect call promotes at runtime — Stage 5).
	uint64_t ref_mask = 0;

	if (auto *v = c->callee->as<Variable>())
	{
		NameRef nr = resolve(v->name);
		switch (nr.kind)
		{
		case NameKind::Local:
			emit_ABC(nr.is_ref ? Opcode::DEREF : Opcode::MOVE, base, nr.index, 0, ln(c));
			break;
		case NameKind::Upvalue:
			emit_ABC(Opcode::GETUPVAL, base, nr.index, 0, ln(c));
			break;
		case NameKind::Module:
			emit_ABx(Opcode::GETMODULE, base, static_cast<uint32_t>(nr.index), ln(c));
			break;
		case NameKind::Generic:
			generic = true;
			ref_mask = generic_ref_mask(v->name);
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

	if (has_splat)
	{
		// A splat makes the positional arity dynamic. Build one List of all positional
		// arguments (singles appended, splats' elements spread in), then CALLD unpacks
		// it into the callee window at runtime — resolving through the generic memo, not
		// the inline cache (design §6). Ref promotion and keyword options are not
		// supported on a splat call in M6 (see DEVIATIONS).
		if (!c->options.empty())
			error(c, "[Compile error] a splat call cannot also pass keyword options (M6)");
		int listreg = reg_alloc(c); // == base + 1; CALLD reads the positional List here
		emit_ABC(Opcode::NEWLIST, listreg, 0, 0, ln(c));
		for (auto &arg : c->args)
		{
			int save2 = fs->free_reg;
			int tmp = reg_alloc(arg.get());
			if (auto *sp = arg->as<SplatExpression>())
			{
				expr_to(sp->expr.get(), tmp);
				emit_ABC(Opcode::LISTEXTEND, listreg, tmp, 0, ln(arg.get()));
			}
			else
			{
				expr_to(arg.get(), tmp);
				emit_ABC(Opcode::LISTAPPEND, listreg, tmp, 0, ln(arg.get()));
			}
			reg_free_to(save2);
		}
		emit_ABC(Opcode::CALLD, base, listreg, 0, ln(c));
		reg_free_to(save);
		if (dest != base)
			emit_ABC(Opcode::MOVE, dest, base, 0, ln(c));
		return;
	}

	int nargs = static_cast<int>(c->args.size());
	for (int i = 0; i < nargs; ++i)
	{
		Ast *a = c->args[i].get();
		int r = reg_alloc(a);
		if (generic)
		{
			// Direct call: the callee's uniform ref-mask is known now (§6.1).
			if ((ref_mask >> i) & 1u)
				emit_promote_arg(a, r);
			else
				expr_to(a, r);
		}
		else
		{
			// Indirect call through a value: promote per the callee's runtime mask.
			emit_maybe_promote_arg(a, r, base);
		}
	}

	// Keyword options follow the positional args as (Symbol, value) pairs; they do not
	// dispatch — the callee matches them into its option slots by name (design §6).
	int nnamed = static_cast<int>(c->options.size());
	for (auto &opt : c->options)
	{
		auto *na = opt->as<NamedArgument>();
		int symreg = reg_alloc(na);
		emit_ABx(Opcode::LOADK, symreg, static_cast<uint32_t>(k_symbol(na->name)), ln(na));
		int valreg = reg_alloc(na);
		expr_to(na->value.get(), valreg);
	}

	if (generic)
	{
		emit_ABC(Opcode::CALLG, base, nargs, nnamed, ln(c));
		int ic = P().num_ic++;
		emit(encode_Ax(Opcode::EXTRA_ARG, static_cast<uint32_t>(ic)), ln(c));
	}
	else
	{
		emit_ABC(Opcode::CALL, base, nargs, nnamed, ln(c));
	}

	reg_free_to(save);
	if (dest != base)
		emit_ABC(Opcode::MOVE, dest, base, 0, ln(c));
}

int Lowerer::compile_function(FunctionDefinition *f)
{
	int num_fixed = 0;
	bool is_vararg = false;
	for (auto &p : f->params)
	{
		auto *param = p->as<Parameter>();
		if (param->default_value)
			continue; // keyword-only option: a trailing slot, not a fixed positional
		if (param->variadic)
			is_vararg = true; // its slot holds the packed List (design §6)
		else
			++num_fixed; // fixed positional (parser guarantees these precede the vararg)
	}

	auto child = std::make_unique<Proto>();
	child->name = f->name;
	child->num_params = num_fixed; // fixed positional count; vararg/option slots are locals
	child->is_vararg = is_vararg;
	child->ref_mask = compute_ref_mask(f, false); // for indirect-call promotion (§6.2)

	FuncState cfs(*child, fs, false);
	FuncState *saved = fs;
	fs = &cfs;
	cfs.finally_base = static_cast<int>(m_finally_stack.size());

	for (auto &p : f->params)
		declare_param(p->as<Parameter>());
	emit_option_prologue(f->params); // bind keyword-option defaults before the body

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
		// Run the finally of every `try` between here and the loop before jumping.
		emit_finally_unwind(m_loops.back().finally_base);
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
	case NodeKind::TryStatement:
		compile_try(node->as<TryStatement>());
		break;
	case NodeKind::ThrowStatement:
		compile_throw(node->as<ThrowStatement>());
		break;
	case NodeKind::ForEach:
		compile_for_each(node->as<ForEach>());
		break;
	case NodeKind::ClassDeclaration:
		error(node, "[Compile error] classes may only be declared at the top level of a module");
	case NodeKind::SpawnStatement:
		compile_spawn(node->as<SpawnStatement>());
		break;
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
			if (nr.is_ref)
			{
				// Write through the reference so the caller's variable is updated (§7).
				int save = fs->free_reg;
				int t = reg_alloc(target);
				expr_to(value, t);
				emit_ABC(Opcode::SETREF, nr.index, t, 0, ln(target));
				reg_free_to(save);
			}
			else
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
		if (ix->indices.size() > 1)
			for (auto &ixx : ix->indices)
				if (ixx->is<SliceExpression>())
					error(target, "[Index error] assignment to a multi-dimensional Array slice is not yet supported");
		int save = fs->free_reg;
		int wb, wbidx;
		int o = load_index_object(ix, wb, wbidx);
		if (ix->indices.size() == 1 && ix->indices[0]->is<SliceExpression>())
		{
			int c = reg_alloc(target); // slice base: start, stop, step
			reg_alloc(target);
			reg_alloc(target);
			emit_slice_parts(ix->indices[0]->as<SliceExpression>(), c);
			int val = expr_any(value);
			emit_ABC(Opcode::SETSLICE, o, c, val, ln(target));
			emit_index_writeback(o, wb, wbidx, ln(target));
			reg_free_to(save);
			return;
		}
		if (ix->indices.size() > 1)
		{
			// Multi-dimensional scalar assignment (Array): indices in a contiguous block,
			// value in its own register; the object stays at `o` for in-place / write-back.
			int rank = static_cast<int>(ix->indices.size());
			int ibase = reg_alloc(target);
			for (int k = 1; k < rank; ++k)
				reg_alloc(target);
			for (int k = 0; k < rank; ++k)
				expr_to(ix->indices[k].get(), ibase + k);
			int val = expr_any(value);
			emit_ABC(Opcode::SETIDXN, o, ibase, val, ln(target));
			emit(encode_Ax(Opcode::EXTRA_ARG, static_cast<uint32_t>(rank)), ln(target));
			emit_index_writeback(o, wb, wbidx, ln(target));
			reg_free_to(save);
			return;
		}
		int i = expr_any(ix->indices[0].get());
		int val = expr_any(value);
		emit_ABC(Opcode::SETINDEX, o, i, val, ln(target));
		emit_index_writeback(o, wb, wbidx, ln(target));
		reg_free_to(save);
		return;
	}
	if (auto *fa = target->as<FieldAccess>())
	{
		// obj.field = value: load obj (with write-back for a value class that
		// detaches under SETFIELD), set the field, then propagate the object back.
		int save = fs->free_reg;
		int wb, wbidx;
		int o = load_object_for_write(fa->object.get(), wb, wbidx);
		int s = reg_alloc(fa);
		emit_ABx(Opcode::LOADK, s, static_cast<uint32_t>(k_symbol(fa->name)), ln(target));
		int val = expr_any(value);
		Opcode set = this_raw_field(fa->object.get(), fa->name) ? Opcode::SETFIELDRAW
		                                                        : Opcode::SETFIELD;
		emit_ABC(set, o, s, val, ln(target));
		emit_index_writeback(o, wb, wbidx, ln(target));
		reg_free_to(save);
		return;
	}
	error(target, "[Compile error] invalid assignment target");
}

int Lowerer::load_object_for_write(Ast *obj, int &wb, int &wbidx)
{
	wb = 0;
	wbidx = 0;
	// `this.f = v` mutates the receiver's own register directly (no write-back);
	// nested-closure `this` is an upvalue and is written back like any upvalue.
	if (obj->is<ThisExpression>())
	{
		NameRef nr = resolve(this_symbol());
		if (nr.kind == NameKind::Local)
			return nr.index;
		if (nr.kind == NameKind::Upvalue)
		{
			int o = reg_alloc(obj);
			emit_ABC(Opcode::GETUPVAL, o, nr.index, 0, ln(obj));
			wb = 2;
			wbidx = nr.index;
			return o;
		}
		error(obj, "[Name error] 'this' is only valid inside a method");
	}
	if (auto *ov = obj->as<Variable>())
	{
		NameRef nr = resolve(ov->name);
		if (nr.kind == NameKind::Local)
		{
			if (nr.is_ref)
			{
				// Deref to a temp; a detaching mutation writes the copy back through the ref.
				int o = reg_alloc(obj);
				emit_ABC(Opcode::DEREF, o, nr.index, 0, ln(obj));
				wb = 3;
				wbidx = nr.index;
				return o;
			}
			return nr.index; // the local register IS the storage; mutate it in place
		}
		if (nr.kind == NameKind::Module)
		{
			int o = reg_alloc(obj);
			emit_ABx(Opcode::GETMODULE, o, static_cast<uint32_t>(nr.index), ln(obj));
			wb = 1;
			wbidx = nr.index;
			return o;
		}
		if (nr.kind == NameKind::Upvalue)
		{
			int o = reg_alloc(obj);
			emit_ABC(Opcode::GETUPVAL, o, nr.index, 0, ln(obj));
			wb = 2;
			wbidx = nr.index;
			return o;
		}
	}
	// A general expression object (e.g. f()[i] = v): mutation targets a temporary.
	return expr_any(obj);
}

int Lowerer::load_index_object(IndexExpression *ix, int &wb, int &wbidx)
{
	return load_object_for_write(ix->object.get(), wb, wbidx);
}

void Lowerer::emit_index_writeback(int o, int wb, int wbidx, uint32_t line)
{
	if (wb == 1)
		emit_ABx(Opcode::SETMODULE, o, static_cast<uint32_t>(wbidx), line);
	else if (wb == 2)
		emit_ABC(Opcode::SETUPVAL, o, wbidx, 0, line);
	else if (wb == 3)
		emit_ABC(Opcode::SETREF, wbidx, o, 0, line); // *ref = o (the mutated object)
}

void Lowerer::emit_option_prologue(AstList &params)
{
	// Options are declared (and therefore registered) after the fixed params and any
	// vararg, in source order, so their names line up with the slots setup_callee_frame
	// fills. For each: if the slot is still the missing sentinel (not supplied by the
	// caller), evaluate the default into it; otherwise JMPSET skips the default.
	for (auto &p : params)
	{
		auto *param = p->as<Parameter>();
		if (!param->default_value)
			continue;
		fs->proto.option_names.push_back(param->name);
		NameRef nr = resolve(param->name); // the option's own register (a Local)
		intptr_t j = emit_jump(Opcode::JMPSET, nr.index, ln(param));
		expr_to(param->default_value.get(), nr.index);
		patch_jump(j);
	}
}

void Lowerer::emit_slice_parts(SliceExpression *sl, int base)
{
	auto part = [&](Ast *p, int r) {
		if (p)
			expr_to(p, r);
		else
			emit_ABC(Opcode::LOADNULL, r, 0, 0, ln(sl)); // absent part → runtime default
	};
	part(sl->start.get(), base);
	part(sl->stop.get(), base + 1);
	part(sl->step.get(), base + 2);
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
			if (nr.is_ref)
			{
				int save = fs->free_reg;
				int cur = reg_alloc(a);
				emit_ABC(Opcode::DEREF, cur, nr.index, 0, ln(a));
				int res = reg_alloc(a);
				emit_combine(res, cur, a->value.get());
				emit_ABC(Opcode::SETREF, nr.index, res, 0, ln(a));
				reg_free_to(save);
			}
			else
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
		if (ix->indices.size() != 1)
			error(a->target.get(),
			      "[Index error] compound assignment to a multi-dimensional index is not supported");
		if (ix->indices[0]->is<SliceExpression>())
			error(a->target.get(), "[Index error] compound assignment to a slice is not supported");
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
	Vector<intptr_t> end_jumps;
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
	m_loops.back().finally_base = static_cast<int>(m_finally_stack.size());
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
	m_loops.back().finally_base = static_cast<int>(m_finally_stack.size());
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
	m_loops.back().finally_base = static_cast<int>(m_finally_stack.size());
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

void Lowerer::compile_for_each(ForEach *s)
{
	// `for [k,] v in coll` over a builtin collection. Hidden state occupies a
	// contiguous register block; the loop variables sit at fixed offsets so ITER_NEXT
	// writes them directly (design §12, fast path — no iterator object, no dispatch).
	//   base+0 source   base+1 cursor   base+2 aux(table keys)
	//   base+3 value var   base+4 key var (pair form)
	if (s->value_by_ref)
	{
		compile_for_each_ref(s);
		return;
	}

	enter_block();
	int base = reg_alloc(s);
	expr_to(s->collection.get(), base); // source
	reg_alloc(s);                       // base+1 cursor
	reg_alloc(s);                       // base+2 aux
	int valreg = reg_alloc(s);          // base+3 value
	fs->locals.push_back({s->value, valreg, false, false});
	bool pair = (s->key != NO_SYMBOL);
	if (pair)
	{
		int keyreg = reg_alloc(s); // base+4 key
		fs->locals.push_back({s->key, keyreg, false, false});
	}
	int done = reg_alloc(s); // ITER_NEXT's exhausted flag

	emit_ABC(Opcode::ITER_INIT, base, 0, 0, ln(s));

	m_loops.emplace_back();
	m_loops.back().finally_base = static_cast<int>(m_finally_stack.size());
	intptr_t top = P().code.size();
	emit_ABC(Opcode::ITER_NEXT, base, done, pair ? 2 : 1, ln(s));
	intptr_t exit = emit_jump(Opcode::JMPT, done, ln(s)); // exhausted -> leave
	compile_stmt(s->body.get());
	emit_AsBx(Opcode::JMP, 0, static_cast<int>(top - (P().code.size() + 1)), ln(s));
	patch_jump(exit);

	// break -> here; continue -> the ITER_NEXT at `top`.
	LoopCtx &lc = m_loops.back();
	for (intptr_t b : lc.breaks)
		patch_jump(b);
	for (intptr_t c : lc.continues)
		P().code[c] = encode_AsBx(Opcode::JMP, 0, static_cast<int>(top - (c + 1)));
	m_loops.pop_back();
	exit_block(static_cast<uint32_t>(s->line));
}

void Lowerer::compile_for_each_ref(ForEach *s)
{
	// By-reference iteration (design §12): the value variable aliases each List element,
	// so writes propagate back into the collection. The collection is loaded into the
	// loop's source register and, if it names a mutable binding, written back after the
	// loop. A non-lvalue collection still iterates, mutating a copy that is discarded.
	enter_block();
	int base = reg_alloc(s);

	// How to restore the (mutated) collection to its binding after the loop. A local is
	// *moved* into `base` (and its slot nulled) so a uniquely-owned list is not cloned.
	enum Wb
	{
		WB_NONE,
		WB_LOCAL,
		WB_MODULE,
		WB_UPVALUE
	};
	Wb wb = WB_NONE;
	int wbidx = 0;
	if (auto *cv = s->collection->as<Variable>())
	{
		NameRef nr = resolve(cv->name);
		if (nr.kind == NameKind::Local && !nr.is_ref)
		{
			emit_ABC(Opcode::MOVE, base, nr.index, 0, ln(s));
			emit_ABC(Opcode::LOADNULL, nr.index, 0, 0, ln(s)); // source emptied for the loop
			wb = WB_LOCAL;
			wbidx = nr.index;
		}
		else if (nr.kind == NameKind::Module)
		{
			emit_ABx(Opcode::GETMODULE, base, static_cast<uint32_t>(nr.index), ln(s));
			wb = WB_MODULE;
			wbidx = nr.index;
		}
		else if (nr.kind == NameKind::Upvalue)
		{
			emit_ABC(Opcode::GETUPVAL, base, nr.index, 0, ln(s));
			wb = WB_UPVALUE;
			wbidx = nr.index;
		}
		else
			expr_to(s->collection.get(), base);
	}
	else
		expr_to(s->collection.get(), base);

	reg_alloc(s);              // base+1 cursor
	reg_alloc(s);              // base+2 aux
	int valreg = reg_alloc(s); // base+3 value — bound by reference
	fs->locals.push_back({s->value, valreg, false, false, /*is_ref=*/true});
	bool pair = (s->key != NO_SYMBOL);
	if (pair)
	{
		int keyreg = reg_alloc(s); // base+4 key/index — always by value
		fs->locals.push_back({s->key, keyreg, false, false});
	}
	int done = reg_alloc(s);

	emit_ABC(Opcode::ITER_INITREF, base, 0, 0, ln(s));

	m_loops.emplace_back();
	m_loops.back().finally_base = static_cast<int>(m_finally_stack.size());
	intptr_t top = P().code.size();
	emit_ABC(Opcode::ITER_NEXTREF, base, done, pair ? 2 : 1, ln(s));
	intptr_t exit = emit_jump(Opcode::JMPT, done, ln(s));
	compile_stmt(s->body.get());
	emit_AsBx(Opcode::JMP, 0, static_cast<int>(top - (P().code.size() + 1)), ln(s));
	patch_jump(exit);

	LoopCtx &lc = m_loops.back();
	for (intptr_t b : lc.breaks)
		patch_jump(b);
	for (intptr_t c : lc.continues)
		P().code[c] = encode_AsBx(Opcode::JMP, 0, static_cast<int>(top - (c + 1)));
	m_loops.pop_back();

	// Write the mutated collection back to its binding (the loop registers, incl. `base`,
	// are still live until exit_block below).
	if (wb == WB_LOCAL)
		emit_ABC(Opcode::MOVE, wbidx, base, 0, ln(s));
	else if (wb == WB_MODULE)
		emit_ABx(Opcode::SETMODULE, base, static_cast<uint32_t>(wbidx), ln(s));
	else if (wb == WB_UPVALUE)
		emit_ABC(Opcode::SETUPVAL, base, wbidx, 0, ln(s));

	exit_block(static_cast<uint32_t>(s->line));
}

void Lowerer::compile_return(ReturnStatement *s)
{
	// The return value is evaluated first, then the finally blocks of every enclosing
	// `try` run (design §12), then the frame returns.
	if (s->expr)
	{
		int save = fs->free_reg;
		int t = reg_alloc(s);
		expr_to(s->expr.get(), t); // keep `t` live across the finallys (regs above it)
		emit_finally_unwind(fs->finally_base);
		emit_ABC(Opcode::RET, t, 1, 0, ln(s));
		reg_free_to(save);
	}
	else
	{
		emit_finally_unwind(fs->finally_base);
		emit_ABC(Opcode::RET, 0, 0, 0, ln(s));
	}
}

void Lowerer::compile_throw(ThrowStatement *s)
{
	int save = fs->free_reg;
	int r = expr_any(s->expr.get());
	emit_ABC(Opcode::THROW, r, 0, 0, ln(s));
	reg_free_to(save);
}

void Lowerer::emit_finally_unwind(int down_to)
{
	// Leaving one or more `try`s early (return/break/continue): POPTRY each still-live
	// handler and run its finally, innermost first, down to `down_to`.
	for (intptr_t i = static_cast<intptr_t>(m_finally_stack.size()) - 1; i >= down_to; --i)
	{
		if (m_finally_stack[i].handler_active)
			emit(encode_ABC(Opcode::POPTRY, 0, 0, 0), 0);
		if (m_finally_stack[i].finally_body)
			compile_block(m_finally_stack[i].finally_body->as<StatementList>());
	}
}

void Lowerer::compile_try(TryStatement *s)
{
	// Layout (design §12 / architecture §10.5):
	//     PUSHTRY err, -> LAND
	//     <body> ; POPTRY ; pending = null ; JMP -> FINALLY
	//   LAND:                      ; err holds the thrown value
	//     for each `catch e as T`: if err is-a T -> bind e, run handler, pending=null, -> FINALLY
	//     pending = err            ; no clause matched
	//   FINALLY:
	//     <finally> ; if pending -> THROW pending
	int save = fs->free_reg;
	int err_reg = reg_alloc(s);     // receives the thrown error at LAND
	int pending_reg = reg_alloc(s); // the error to re-raise after finally (null if handled)

	intptr_t jpush = emit_jump(Opcode::PUSHTRY, err_reg, ln(s)); // -> LAND (patched)

	// The handler is active during the body; `return`/`break`/`continue` in the body
	// run this finally and POPTRY the handler on the way out.
	m_finally_stack.push_back(FinallyCtx{s->finally_body.get(), true});
	compile_block(s->body->as<StatementList>());

	emit(encode_ABC(Opcode::POPTRY, 0, 0, 0), ln(s));
	emit_ABC(Opcode::LOADNULL, pending_reg, 0, 0, ln(s));
	intptr_t jnormal = emit_jump(Opcode::JMP, 0, ln(s)); // -> FINALLY

	// LAND: the catch-dispatch block. The handler is no longer on the stack here, so
	// an early exit from a catch clause runs the finally but must not POPTRY it.
	patch_jump(jpush);
	m_finally_stack.back().handler_active = false;
	Vector<intptr_t> to_finally;
	for (auto &cl : s->catches)
	{
		auto *cc = cl->as<CatchClause>();
		intptr_t jnext = -1;
		if (cc->type)
		{
			// if not (err is-a T) -> next clause
			int csave = fs->free_reg;
			int treg = reg_alloc(cc);
			expr_to(cc->type.get(), treg); // the exception class object
			int cond = reg_alloc(cc);
			emit_ABC(Opcode::IS, cond, err_reg, treg, ln(cc));
			jnext = emit_jump(Opcode::JMPF, cond, ln(cc));
			reg_free_to(csave);
		}
		// matched: bind the catch variable (if any) and run the handler in a scope.
		enter_block();
		if (cc->name != NO_SYMBOL)
		{
			int ereg = declare_local(cc->name, false, cc);
			emit_ABC(Opcode::MOVE, ereg, err_reg, 0, ln(cc));
		}
		compile_block(cc->body->as<StatementList>());
		exit_block(static_cast<uint32_t>(cc->line));
		emit_ABC(Opcode::LOADNULL, pending_reg, 0, 0, ln(cc)); // handled: nothing pending
		to_finally.push_back(emit_jump(Opcode::JMP, 0, ln(cc)));
		if (jnext >= 0)
			patch_jump(jnext);
	}
	// No clause matched: carry the error through finally, then re-raise.
	emit_ABC(Opcode::MOVE, pending_reg, err_reg, 0, ln(s));

	// The try is exited via one of the paths below; its context leaves the stack so
	// the shared finally is emitted directly, not through the unwind machinery.
	m_finally_stack.pop_back();

	// FINALLY: reached from normal completion, a handled catch, or the unmatched path.
	patch_jump(jnormal);
	for (intptr_t j : to_finally)
		patch_jump(j);
	if (s->finally_body)
		compile_block(s->finally_body->as<StatementList>());
	intptr_t jend = emit_jump(Opcode::JMPF, pending_reg, ln(s)); // pending null -> done
	emit_ABC(Opcode::THROW, pending_reg, 0, 0, ln(s));
	patch_jump(jend);

	reg_free_to(save);
}

void Lowerer::compile_named_function(FunctionDefinition *f)
{
	// Nested named function: a local binding holding a closure. Declaring the local
	// before compiling the body lets the function recurse (it captures itself).
	int L = declare_local(f->name, false, f);
	int idx = compile_function(f);
	emit_ABx(Opcode::CLOSURE, L, static_cast<uint32_t>(idx), ln(f));
}

Symbol Lowerer::this_symbol() { return intern("this"); }

TypeRef Lowerer::type_ref(Ast *type_node)
{
	TypeRef t;
	if (!type_node)
	{
		t.kind = TypeRef::Concrete;
		t.value = CID_OBJECT; // unannotated is Object (design §12)
		return t;
	}
	if (auto *v = type_node->as<Variable>())
	{
		if (Class *c = class_by_name(v->name)) // builtins only
		{
			t.kind = TypeRef::Concrete;
			t.value = c->id;
			return t;
		}
		if (m_module_classes.find(v->name.id) != m_module_classes.end())
		{
			t.kind = TypeRef::ModuleSlot;
			t.value = static_cast<uint32_t>(module_lookup(v->name));
			return t;
		}
		error(type_node, "[Type error] unknown type '" + std::string(symbol_name(v->name)) + "'");
	}
	error(type_node, "[Type error] unsupported type annotation");
}

int Lowerer::add_method_def(FunctionDefinition *f, TypeRef self, bool have_self)
{
	MethodDef md;
	md.name = f->name;
	md.ref_mask = 0;
	// The implicit `this` (when present) is never `ref`; explicit `ref` parameters set
	// their bit so dispatch requires a reference argument at that position (§6/§7).
	int pos = 0;
	if (have_self)
	{
		md.sig.push_back(self);
		pos = 1;
	}
	for (auto &p : f->params)
	{
		auto *param = p->as<Parameter>();
		if (param->default_value)
			continue; // keyword-only option: not a dispatch position (design §6)
		md.sig.push_back(type_ref(param->type.get()));
		if (param->variadic)
			md.is_vararg = true; // this type is the vararg element type (the last sig entry)
		else if (param->by_ref)
			md.ref_mask |= (uint64_t(1) << pos);
		++pos;
	}
	int idx = static_cast<int>(fs->proto.method_defs.size());
	fs->proto.method_defs.push_back(std::move(md));
	return idx;
}

// --- classes (design §5.6/§6) -------------------------------------------------

void Lowerer::compile_cast(CastExpression *c, int dest)
{
	// `cast x as T` lowers to the generic call cast(x, T) (design §7).
	int save = fs->free_reg;
	int base = reg_alloc(c);
	emit_ABx(Opcode::LOADK, base, static_cast<uint32_t>(k_symbol(intern("cast"))), ln(c));
	int xr = reg_alloc(c);
	expr_to(c->expr.get(), xr);
	int tr = reg_alloc(c);
	expr_to(c->type.get(), tr); // the class object (Variable naming a class)
	emit_ABC(Opcode::CALLG, base, 2, 0, ln(c));
	emit(encode_Ax(Opcode::EXTRA_ARG, static_cast<uint32_t>(P().num_ic++)), ln(c));
	reg_free_to(save);
	if (dest != base)
		emit_ABC(Opcode::MOVE, dest, base, 0, ln(c));
}

void Lowerer::emit_full_defaults(ClassDeclaration *cls, int thisreg)
{
	// Base defaults first (so a subclass's own initializers can, in principle,
	// override), then this class's own — applied to the fresh, unique instance in
	// `thisreg`, which is uniquely owned so SETFIELD mutates it in place.
	if (cls->parent)
		if (auto *pv = cls->parent->as<Variable>())
		{
			auto it = m_module_classes.find(pv->name.id);
			if (it != m_module_classes.end())
				emit_full_defaults(it->second, thisreg);
		}
	for (auto &f : cls->fields)
	{
		auto *fd = f->as<FieldDeclaration>();
		if (!fd->default_value)
			continue; // no initializer -> stays null (design: default is null)
		int save = fs->free_reg;
		int sreg = reg_alloc(fd);
		emit_ABx(Opcode::LOADK, sreg, static_cast<uint32_t>(k_symbol(fd->name)), ln(fd));
		int vreg = reg_alloc(fd);
		expr_to(fd->default_value.get(), vreg);
		// Raw: field defaults set the slot directly, bypassing any setter and the
		// privacy check (construction runs outside the class).
		emit_ABC(Opcode::SETFIELDRAW, thisreg, sreg, vreg, ln(fd));
		reg_free_to(save);
	}
}

int Lowerer::compile_method(FunctionDefinition *m, bool is_init, ClassDeclaration *cls)
{
	int num_fixed = 0;
	bool is_vararg = false;
	for (auto &p : m->params)
	{
		auto *param = p->as<Parameter>();
		if (param->default_value)
			continue; // keyword-only option: a trailing slot, not a fixed positional
		if (param->variadic)
			is_vararg = true;
		else
			++num_fixed;
	}

	auto child = std::make_unique<Proto>();
	child->name = m->name;
	child->num_params = 1 + num_fixed; // implicit `this` + fixed explicit (vararg/option slots are locals)
	child->is_vararg = is_vararg;

	FuncState cfs(*child, fs, false);
	FuncState *saved = fs;
	fs = &cfs;
	cfs.finally_base = static_cast<int>(m_finally_stack.size());

	(void) cls;
	declare_local(this_symbol(), true, m); // reg 0 = this (const)
	for (auto &p : m->params)
		declare_param(p->as<Parameter>());
	emit_option_prologue(m->params); // bind keyword-option defaults before the body

	// Field defaults are applied at construction (emit_full_defaults), not here, so
	// an inherited `init` still sees a fully-defaulted instance.
	compile_block(m->body->as<StatementList>());

	// A constructor implicitly returns the (mutated) instance; other methods fall off
	// the end returning null.
	if (is_init)
		emit_ABC(Opcode::RET, 0, 1, 0, ln(m));
	else
		emit_ABC(Opcode::RET, 0, 0, 0, ln(m));
	child->num_regs = cfs.max_reg > child->num_params ? cfs.max_reg : child->num_params;

	fs = saved;
	int idx = static_cast<int>(fs->proto.children.size());
	fs->proto.children.push_back(std::move(child));
	return idx;
}

int Lowerer::compile_accessor(FieldDeclaration *fd, bool is_setter)
{
	// A get/set body compiles like a method: reg 0 is `this`, and a setter also
	// binds its value parameter at reg 1. Inside, `this.<this field>` reaches the
	// raw slot (m_accessor_field), so an accessor never re-enters itself.
	auto child = std::make_unique<Proto>();
	child->name = fd->name;
	child->num_params = is_setter ? 2 : 1;

	FuncState cfs(*child, fs, false);
	FuncState *saved = fs;
	fs = &cfs;
	cfs.finally_base = static_cast<int>(m_finally_stack.size());

	declare_local(this_symbol(), true, fd);
	if (is_setter)
		declare_local(fd->setter_param, false, fd);

	Symbol prev = m_accessor_field;
	m_accessor_field = fd->name;
	compile_block((is_setter ? fd->setter : fd->getter)->as<StatementList>());
	m_accessor_field = prev;

	// A setter returns the (possibly detached) instance so `obj.field = v` can write
	// it back to its binding; a getter falling off the end yields null.
	emit_ABC(Opcode::RET, 0, is_setter ? 1 : 0, 0, ln(fd));
	child->num_regs = cfs.max_reg > child->num_params ? cfs.max_reg : child->num_params;

	fs = saved;
	int idx = static_cast<int>(fs->proto.children.size());
	fs->proto.children.push_back(std::move(child));
	return idx;
}

void Lowerer::compile_class(ClassDeclaration *c)
{
	int slot = module_lookup(c->name); // reserved in pass 1
	TypeRef self{TypeRef::ModuleSlot, static_cast<uint32_t>(slot)};

	// Private fields reachable via `this` in this class's methods/accessors: its own
	// `local field`s and (protected-style) those of its in-module base chain.
	m_local_fields = FlatHashSet<uint32_t>();
	for (ClassDeclaration *k = c; k;)
	{
		for (auto &f : k->fields)
		{
			auto *fd = f->as<FieldDeclaration>();
			if (fd->is_private)
				m_local_fields.insert(fd->name.id);
		}
		ClassDeclaration *next = nullptr;
		if (k->parent)
			if (auto *pv = k->parent->as<Variable>())
			{
				auto it = m_module_classes.find(pv->name.id);
				if (it != m_module_classes.end())
					next = it->second;
			}
		k = next;
	}

	// Build and record the ClassDef consumed by DEFCLASS.
	ClassDef cd;
	cd.name = c->name;
	cd.is_ref = c->is_ref;
	cd.is_open = c->is_open;
	cd.base = c->parent ? type_ref(c->parent.get()) : TypeRef{TypeRef::Concrete, CID_OBJECT};
	for (auto &f : c->fields)
	{
		auto *fd = f->as<FieldDeclaration>();
		FieldDef def{fd->name, type_ref(fd->type.get()), -1, -1, fd->is_private};
		if (fd->getter)
			def.getter_proto = compile_accessor(fd, /*is_setter*/ false);
		if (fd->setter)
			def.setter_proto = compile_accessor(fd, /*is_setter*/ true);
		cd.fields.push_back(def);
	}
	int cdidx = static_cast<int>(fs->proto.class_defs.size());
	fs->proto.class_defs.push_back(std::move(cd));

	// Register the class and bind its class object.
	int t = reg_alloc(c);
	emit_ABx(Opcode::DEFCLASS, t, static_cast<uint32_t>(cdidx), ln(c));
	emit_ABx(Opcode::SETMODULE, t, static_cast<uint32_t>(slot), ln(c));
	reg_free_to(t);

	// Register the methods (keyed on this class via `self`), and note whether the
	// user supplied a constructor.
	Symbol init_name = intern("init");
	bool has_init = false;
	for (auto &m : c->methods)
	{
		auto *md = m->as<FunctionDefinition>();
		bool is_init = (md->name == init_name);
		has_init = has_init || is_init;
		int idx = compile_method(md, is_init, c);
		int r = reg_alloc(m.get());
		emit_ABx(Opcode::CLOSURE, r, static_cast<uint32_t>(idx), ln(md));
		emit_ABx(Opcode::DEFMETHOD, r, static_cast<uint32_t>(add_method_def(md, self, true)), ln(md));
		reg_free_to(r);
	}

	// Synthesize a default constructor `init()` if none was written, so every class
	// applies its field defaults and is constructible with no arguments.
	if (!has_init)
	{
		auto child = std::make_unique<Proto>();
		child->name = init_name;
		child->num_params = 1; // this
		FuncState cfs(*child, fs, false);
		FuncState *saved = fs;
		fs = &cfs;
	cfs.finally_base = static_cast<int>(m_finally_stack.size());
		declare_local(this_symbol(), true, c);
		emit_ABC(Opcode::RET, 0, 1, 0, ln(c)); // empty body: defaults ran at construction; return this
		child->num_regs = cfs.max_reg > 1 ? cfs.max_reg : 1;
		fs = saved;
		int idx = static_cast<int>(fs->proto.children.size());
		fs->proto.children.push_back(std::move(child));

		MethodDef md;
		md.name = init_name;
		md.sig.push_back(self);
		int mdidx = static_cast<int>(fs->proto.method_defs.size());
		fs->proto.method_defs.push_back(std::move(md));

		int r = reg_alloc(c);
		emit_ABx(Opcode::CLOSURE, r, static_cast<uint32_t>(idx), ln(c));
		emit_ABx(Opcode::DEFMETHOD, r, static_cast<uint32_t>(mdidx), ln(c));
		reg_free_to(r);
	}
}

void Lowerer::compile_spawn(SpawnStatement *s)
{
	// `spawn f(args…)` (design §13): the parser guarantees the operand is a call. Only a
	// named-function (generic) target is supported; a bound closure/local would need its
	// captured state shared across threads. Emit the callee symbol + evaluated args, then
	// SPAWN — which resolves the method, transfers the args, and launches the worker.
	auto *call = s->call->as<CallExpression>();
	auto *v = call->callee->as<Variable>();
	if (!v)
		error(call->callee.get(), "[Compile error] 'spawn' target must be a named function");
	NameRef nr = resolve(v->name);
	if (nr.kind != NameKind::Generic)
		error(call->callee.get(), "[Compile error] 'spawn' target must be a named function");
	if (!call->options.empty())
		error(call, "[Compile error] 'spawn' does not take keyword options");
	for (auto &a : call->args)
		if (a->is<SplatExpression>() || a->is<RefExpression>())
			error(a.get(), "[Compile error] 'spawn' arguments cannot use splat or ref");

	int save = fs->free_reg;
	int base = reg_alloc(call);
	emit_ABx(Opcode::LOADK, base, static_cast<uint32_t>(k_symbol(v->name)), ln(call));
	int nargs = static_cast<int>(call->args.size());
	for (auto &a : call->args)
	{
		int r = reg_alloc(a.get());
		expr_to(a.get(), r);
	}
	emit_ABC(Opcode::SPAWN, base, nargs, 0, ln(call));
	reg_free_to(save);
}

void Lowerer::construct(ClassDeclaration *cls, int class_slot, Class *builtin, CallExpression *call,
                        int dest)
{
	if (!call->options.empty())
		error(call, "[Compile error] named constructor options arrive later in M5");
	for (auto &a : call->args)
		if (a->is<SplatExpression>() || a->is<RefExpression>())
			error(a.get(), "[Compile error] ref/splat arguments arrive later in M5");

	// NEW a fresh instance directly into the init call's `this` slot, apply the full
	// (base→derived) field defaults, then dispatch init(this, args...). init returns
	// the (in-place mutated) instance. The class object comes from the class's module
	// binding (user class) or a constant (builtin Error subtree).
	int save = fs->free_reg;
	int base = reg_alloc(call); // holds :init, then the result
	emit_ABx(Opcode::LOADK, base, static_cast<uint32_t>(k_symbol(intern("init"))), ln(call));
	int thisreg = reg_alloc(call); // base + 1
	if (builtin)
		emit_ABx(Opcode::LOADK, thisreg, static_cast<uint32_t>(k_class(builtin)), ln(call));
	else
		emit_ABx(Opcode::GETMODULE, thisreg, static_cast<uint32_t>(class_slot), ln(call));
	emit_ABC(Opcode::NEW, thisreg, thisreg, 0, ln(call)); // this = fresh instance (rc 1)
	if (cls)
		emit_full_defaults(cls, thisreg);
	int nargs = static_cast<int>(call->args.size());
	for (auto &a : call->args)
	{
		int r = reg_alloc(a.get());
		expr_to(a.get(), r);
	}
	emit_ABC(Opcode::CALLG, base, nargs + 1, 0, ln(call));
	emit(encode_Ax(Opcode::EXTRA_ARG, static_cast<uint32_t>(P().num_ic++)), ln(call));
	reg_free_to(save);
	if (dest != base)
		emit_ABC(Opcode::MOVE, dest, base, 0, ln(call));
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
	mfs.finally_base = static_cast<int>(m_finally_stack.size());

	// Pass 1: reserve module slots for top-level bindings, and collect the names of
	// top-level `function`s so forward and mutually-recursive references resolve
	// (design §11, two-pass top level). A non-`local` `function` becomes a method on
	// a generic (design §6); a `local function` stays a module-private binding, since
	// a private method on a shared generic is not a coherent concept (design §11).
	for (auto &s : list->statements)
	{
		if (auto *d = s->as<Declaration>())
			module_define(d->name);
		else if (auto *f = s->as<FunctionDefinition>(); f && !f->is_anonymous())
		{
			if (f->modifier == DeclModifier::Local)
				module_define(f->name);
			else
			{
				m_module_generics.insert(f->name.id);
				m_generic_ref_mask.insert(f->name.id, compute_ref_mask(f, false));
			}
		}
		else if (auto *cl = s->as<ClassDeclaration>())
		{
			module_define(cl->name); // the class object lives in a module binding
			m_module_classes.insert(cl->name.id, cl);
			// A `method` is a generic method (design §6), so its name must resolve to
			// a generic call even though it only registers at load.
			for (auto &m : cl->methods)
			{
				auto *mf = m->as<FunctionDefinition>();
				m_module_generics.insert(mf->name.id);
				m_generic_ref_mask.insert(mf->name.id, compute_ref_mask(mf, true));
			}
		}
	}

	// Pass 2a-classes: register user classes (DEFCLASS + their methods) before
	// functions, so a class's `this`/base type references resolve at load. Classes
	// are emitted in declaration order (a base must precede its use).
	for (auto &s : list->statements)
		if (auto *cl = s->as<ClassDeclaration>())
			compile_class(cl);

	// Pass 2a-functions: register/hoist top-level functions. Generic methods (the
	// default) are compiled to a closure and installed via DEFMETHOD; `local`
	// functions keep the module-slot binding.
	for (auto &s : list->statements)
	{
		auto *f = s->as<FunctionDefinition>();
		if (!f || f->is_anonymous())
			continue;
		int idx = compile_function(f);
		int t = reg_alloc(f);
		emit_ABx(Opcode::CLOSURE, t, static_cast<uint32_t>(idx), ln(f));
		if (f->modifier == DeclModifier::Local)
			emit_ABx(Opcode::SETMODULE, t, static_cast<uint32_t>(module_lookup(f->name)), ln(f));
		else
			emit_ABx(Opcode::DEFMETHOD, t,
			         static_cast<uint32_t>(add_method_def(f, TypeRef{}, false)), ln(f));
		reg_free_to(t);
	}

	// Pass 2b: execute statements top to bottom. The value of a trailing expression
	// statement becomes the module result (so do_string returns it, REPL-style).
	intptr_t count = static_cast<intptr_t>(list->statements.size());
	for (intptr_t i = 0; i < count; ++i)
	{
		Ast *s = list->statements[i].get();
		if (auto *f = s->as<FunctionDefinition>(); f && !f->is_anonymous())
			continue; // already hoisted
		if (s->is<ClassDeclaration>())
			continue; // already registered
		if (i == count - 1 && s->is<ExpressionStatement>())
		{
			int r = reg_alloc(s);
			expr_to(s->as<ExpressionStatement>()->expr.get(), r);
			emit_ABC(Opcode::HALT, r, 1, 0, ln(s));
			main.num_regs = mfs.max_reg;
			out.num_slots = m_ns.num_slots;
			return;
		}
		compile_stmt(s);
	}
	emit_ABC(Opcode::HALT, 0, 0, 0, 0);
	main.num_regs = mfs.max_reg;
	out.num_slots = m_ns.num_slots;
}

} // namespace

void compile_module(Ast *module_ast, ModuleNamespace &ns, CompiledModule &out)
{
	Lowerer lw(ns);
	lw.compile_module(module_ast, out);
}

void compile_module(Ast *module_ast, CompiledModule &out)
{
	ModuleNamespace throwaway;
	compile_module(module_ast, throwaway, out);
}

} // namespace phonometrica
