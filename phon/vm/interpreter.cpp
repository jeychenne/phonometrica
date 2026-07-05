// Phonometrica engine — the bytecode interpreter implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/vm/interpreter.hpp>

#include <phon/dispatch/generic.hpp>
#include <phon/object/class.hpp>
#include <phon/object/value_ops.hpp>
#include <phon/types/atom.hpp>
#include <phon/types/list.hpp>
#include <phon/types/set.hpp>
#include <phon/types/string.hpp>
#include <phon/types/table.hpp>
#include <phon/vm/function.hpp>
#include <phon/vm/isolate.hpp>
#include <phon/vm/opcode.hpp>

#include <cmath>
#include <utility>

namespace phonometrica {

namespace {

PHON_FORCE_INLINE void retain_value(Value v) noexcept
{
	if (v.is_cell())
		retain(v.as_cell());
}
PHON_FORCE_INLINE void release_value(Value v) noexcept
{
	if (v.is_cell())
		release(v.as_cell());
}

// Store into a register by *copying* a borrowed value (retain new, release old).
PHON_FORCE_INLINE void reg_copy(Value *base, int a, Value v) noexcept
{
	retain_value(v);
	release_value(base[a]);
	base[a] = v;
}
// Store into a register by *moving* an already-owned value (release old only).
PHON_FORCE_INLINE void reg_move(Value *base, int a, Value v) noexcept
{
	release_value(base[a]);
	base[a] = v;
}

PHON_FORCE_INLINE bool truthy(Value v) noexcept { return !(v.is_null() || v.is_false()); }

bool is_string(Value v) { return v.is_cell() && class_of(v) == CID_STRING; }
bool is_list(Value v) { return v.is_cell() && class_of(v) == CID_LIST; }
bool is_table(Value v) { return v.is_cell() && class_of(v) == CID_TABLE; }

// --- stringification (the to_string generic; user overloads arrive in M5) ------

String to_string_value(Value v)
{
	if (v.is_null())
		return String("null");
	if (v.is_true())
		return String("true");
	if (v.is_false())
		return String("false");
	if (v.is_int())
		return String::convert(static_cast<intptr_t>(v.as_int()));
	if (v.is_double())
		return String::convert(v.as_double());
	if (v.is_symbol())
		return String(symbol_name(v.as_symbol()));
	if (is_string(v))
		return String::from_value(v);
	if (is_list(v))
	{
		List l = List::from_value(v);
		String out("[");
		for (intptr_t i = 1; i <= l.size(); ++i)
		{
			if (i > 1)
				out.append(", ");
			out.append(to_string_value(l.get(i).value()));
		}
		out.append("]");
		return out;
	}
	if (is_table(v))
	{
		Table t = Table::from_value(v);
		List keys = t.keys();
		String out("{");
		for (intptr_t i = 1; i <= keys.size(); ++i)
		{
			if (i > 1)
				out.append(", ");
			Variant k = keys.get(i);
			out.append(to_string_value(k.value()));
			out.append(": ");
			out.append(to_string_value(t.get(k).value()));
		}
		out.append("}");
		return out;
	}
	if (v.is_cell() && class_of(v) == CID_SET)
	{
		Set s = Set::from_value(v);
		List items = s.to_list();
		String out("{");
		for (intptr_t i = 1; i <= items.size(); ++i)
		{
			if (i > 1)
				out.append(", ");
			out.append(to_string_value(items.get(i).value()));
		}
		out.append("}");
		return out;
	}
	if (is_callable(v))
		return String("<function>");
	if (Class *c = class_denoted_by(v))
		return String(c->name ? c->name : "<class>");
	return String("<object>");
}

} // namespace

// Exposed for the print/string library (lib/) without pulling in the whole VM.
String stringify(Value v) { return to_string_value(v); }

namespace {

// --- comparisons --------------------------------------------------------------

bool values_equal(Value a, Value b)
{
	if (a.is_number() && b.is_number())
		return a.to_double() == b.to_double();
	return value_equals(a, b);
}

// -1 / 0 / 1; raises on incomparable operands.
int compare_ordered(Isolate &iso, int line, Value a, Value b)
{
	if (a.is_number() && b.is_number())
	{
		double x = a.to_double(), y = b.to_double();
		return x < y ? -1 : (x > y ? 1 : 0);
	}
	if (is_string(a) && is_string(b))
	{
		String sa = String::from_value(a);
		String sb = String::from_value(b);
		int c = sa.compare(sb.view());
		return c < 0 ? -1 : (c > 0 ? 1 : 0);
	}
	iso.raise(String("[Type error] values are not ordered/comparable"), line);
}

// --- arithmetic slow paths ----------------------------------------------------

Value list_concat(Value a, Value b)
{
	List la = List::from_value(a);
	List lb = List::from_value(b);
	List r(0);
	r.reserve(la.size() + lb.size());
	for (intptr_t i = 1; i <= la.size(); ++i)
		r.append(la.get(i));
	for (intptr_t i = 1; i <= lb.size(); ++i)
		r.append(lb.get(i));
	Value v = r.to_value();
	retain_value(v);
	return v; // carries +1 (caller reg_move's it)
}

} // namespace

Value execute(Isolate &iso, ClosureCell *main)
{
	Value *stack = iso.stack();
	Value *stack_end = stack + iso.stack_capacity();

	// Root frame: leave stack[0] as the result sink; registers start at stack[1].
	Value *root_base = stack + 1;
	Proto *mp = main->proto;
	PHON_CHECK(root_base + mp->num_regs <= stack_end, "[Runtime error] stack overflow");
	for (int i = 0; i < mp->num_regs; ++i)
		root_base[i] = Value::make_null();
	iso.frames.push_back(CallFrame{main, root_base, nullptr, &stack[0]});

	ClosureCell *cl = main;
	Proto *proto = cl->proto;
	const Instruction *ip = proto->code.data();
	Value *base = root_base;
	const Variant *K = proto->constants.data();
	int ic_base_cur = iso.ic_base(proto);

	auto cur_line = [&]() -> int {
		intptr_t idx = (ip - proto->code.data()) - 1;
		return static_cast<int>(proto->line_at(idx));
	};

	// Invoke a callable with `nargs` arguments at base[a+1..], result -> base[a].
	// For a closure this pushes a frame and rebinds the execution locals; for a
	// native it runs inline. `pushed` reports whether a new frame was entered.
	auto invoke = [&](Value callee, int a, int nargs) {
		if (is_native(callee))
		{
			auto *nf = reinterpret_cast<NativeCell *>(callee.as_cell());
			if (nargs < nf->min_arity || (nf->max_arity >= 0 && nargs > nf->max_arity))
				iso.raise(String("[Argument error] '") + String(symbol_name(nf->name)).view() +
				              "' called with the wrong number of arguments",
				          cur_line());
			Value result = nf->fn(iso, &base[a + 1], nargs);
			for (int i = 0; i <= nargs; ++i)
			{
				release_value(base[a + i]);
				base[a + i] = Value::make_null();
			}
			base[a] = result; // native returns a value carrying +1
		}
		else if (is_closure(callee))
		{
			auto *callee_cl = reinterpret_cast<ClosureCell *>(callee.as_cell());
			Proto *cp = callee_cl->proto;
			if (nargs != cp->num_params)
				iso.raise(String("[Argument error] function called with the wrong number of arguments"),
				          cur_line());
			Value *new_base = &base[a + 1];
			if (new_base + cp->num_regs > stack_end)
				iso.raise(String("[Runtime error] stack overflow"), cur_line());
			for (int i = cp->num_params; i < cp->num_regs; ++i)
			{
				release_value(new_base[i]);
				new_base[i] = Value::make_null();
			}
			iso.frames.push_back(CallFrame{callee_cl, new_base, ip, &base[a]});
			cl = callee_cl;
			proto = cp;
			base = new_base;
			K = proto->constants.data();
			ip = proto->code.data();
			ic_base_cur = iso.ic_base(proto);
		}
		else
		{
			iso.raise(String("[Type error] value is not callable"), cur_line());
		}
	};

	for (;;)
	{
		Instruction ins = *ip++;
		Opcode op = op_of(ins);
		int a = op_a(ins);

		switch (op)
		{
		case Opcode::MOVE:
			reg_copy(base, a, base[op_b(ins)]);
			break;
		case Opcode::LOADK:
			reg_copy(base, a, K[op_bx(ins)].value());
			break;
		case Opcode::LOADI:
			reg_move(base, a, Value::make_int(op_sbx(ins)));
			break;
		case Opcode::LOADBOOL:
			reg_move(base, a, Value::make_bool(op_b(ins) != 0));
			break;
		case Opcode::LOADNULL:
		{
			int n = op_b(ins);
			for (int i = 0; i <= n; ++i)
				reg_move(base, a + i, Value::make_null());
			break;
		}
		case Opcode::GETUPVAL:
			reg_copy(base, a, *cl->upvals[op_b(ins)]->slot);
			break;
		case Opcode::SETUPVAL:
		{
			Value *slot = cl->upvals[op_b(ins)]->slot;
			retain_value(base[a]);
			release_value(*slot);
			*slot = base[a];
			break;
		}
		case Opcode::GETMODULE:
			reg_copy(base, a, iso.module_slots[op_bx(ins)].value());
			break;
		case Opcode::SETMODULE:
			iso.module_slots[op_bx(ins)] = Variant(base[a]);
			break;

		case Opcode::ADD:
		{
			Value x = base[op_b(ins)], y = base[op_c(ins)];
			if (x.is_int() && y.is_int())
			{
				int64_t r;
				if (__builtin_add_overflow(x.as_int(), y.as_int(), &r) || r < Value::INT_MIN_VALUE ||
				    r > Value::INT_MAX_VALUE)
					iso.raise(String("[Math error] integer overflow"), cur_line());
				reg_move(base, a, Value::make_int(r));
			}
			else if (x.is_number() && y.is_number())
				reg_move(base, a, Value::make(x.to_double() + y.to_double()));
			else if (is_list(x) && is_list(y))
				reg_move(base, a, list_concat(x, y));
			else
				iso.raise(String("[Type error] '+' expects two numbers or two lists"), cur_line());
			break;
		}
		case Opcode::SUB:
		{
			Value x = base[op_b(ins)], y = base[op_c(ins)];
			if (x.is_int() && y.is_int())
			{
				int64_t r;
				if (__builtin_sub_overflow(x.as_int(), y.as_int(), &r) || r < Value::INT_MIN_VALUE ||
				    r > Value::INT_MAX_VALUE)
					iso.raise(String("[Math error] integer overflow"), cur_line());
				reg_move(base, a, Value::make_int(r));
			}
			else if (x.is_number() && y.is_number())
				reg_move(base, a, Value::make(x.to_double() - y.to_double()));
			else
				iso.raise(String("[Type error] '-' expects numbers"), cur_line());
			break;
		}
		case Opcode::MUL:
		{
			Value x = base[op_b(ins)], y = base[op_c(ins)];
			if (x.is_int() && y.is_int())
			{
				int64_t r;
				if (__builtin_mul_overflow(x.as_int(), y.as_int(), &r) || r < Value::INT_MIN_VALUE ||
				    r > Value::INT_MAX_VALUE)
					iso.raise(String("[Math error] integer overflow"), cur_line());
				reg_move(base, a, Value::make_int(r));
			}
			else if (x.is_number() && y.is_number())
				reg_move(base, a, Value::make(x.to_double() * y.to_double()));
			else
				iso.raise(String("[Type error] '*' expects numbers"), cur_line());
			break;
		}
		case Opcode::DIV:
		{
			Value x = base[op_b(ins)], y = base[op_c(ins)];
			if (x.is_number() && y.is_number())
				reg_move(base, a, Value::make(x.to_double() / y.to_double())); // '/' always Float
			else
				iso.raise(String("[Type error] '/' expects numbers"), cur_line());
			break;
		}
		case Opcode::POW:
		{
			Value x = base[op_b(ins)], y = base[op_c(ins)];
			if (x.is_number() && y.is_number())
				reg_move(base, a, Value::make(std::pow(x.to_double(), y.to_double())));
			else
				iso.raise(String("[Type error] '^' expects numbers"), cur_line());
			break;
		}
		case Opcode::IDIV:
		{
			Value x = base[op_b(ins)], y = base[op_c(ins)];
			if (x.is_int() && y.is_int())
			{
				if (y.as_int() == 0)
					iso.raise(String("[Math error] division by zero"), cur_line());
				reg_move(base, a, Value::make_int(x.as_int() / y.as_int()));
			}
			else
				iso.raise(String("[Type error] 'div' expects integers"), cur_line());
			break;
		}
		case Opcode::MOD:
		{
			Value x = base[op_b(ins)], y = base[op_c(ins)];
			if (x.is_int() && y.is_int())
			{
				if (y.as_int() == 0)
					iso.raise(String("[Math error] division by zero"), cur_line());
				reg_move(base, a, Value::make_int(x.as_int() % y.as_int()));
			}
			else
				iso.raise(String("[Type error] 'mod' expects integers"), cur_line());
			break;
		}
		case Opcode::NEG:
		{
			Value x = base[op_b(ins)];
			if (x.is_int())
			{
				if (x.as_int() == Value::INT_MIN_VALUE)
					iso.raise(String("[Math error] integer overflow"), cur_line());
				reg_move(base, a, Value::make_int(-x.as_int()));
			}
			else if (x.is_double())
				reg_move(base, a, Value::make(-x.as_double()));
			else
				iso.raise(String("[Type error] unary '-' expects a number"), cur_line());
			break;
		}
		case Opcode::EQ:
			reg_move(base, a, Value::make_bool(values_equal(base[op_b(ins)], base[op_c(ins)])));
			break;
		case Opcode::NE:
			reg_move(base, a, Value::make_bool(!values_equal(base[op_b(ins)], base[op_c(ins)])));
			break;
		case Opcode::LT:
			reg_move(base, a,
			         Value::make_bool(compare_ordered(iso, cur_line(), base[op_b(ins)],
			                                           base[op_c(ins)]) < 0));
			break;
		case Opcode::LE:
			reg_move(base, a,
			         Value::make_bool(compare_ordered(iso, cur_line(), base[op_b(ins)],
			                                           base[op_c(ins)]) <= 0));
			break;
		case Opcode::NOT:
			reg_move(base, a, Value::make_bool(!truthy(base[op_b(ins)])));
			break;
		case Opcode::CONCAT:
		{
			String out;
			for (int r = op_b(ins); r <= op_c(ins); ++r)
				out.append(to_string_value(base[r]));
			reg_copy(base, a, out.to_value());
			break;
		}

		case Opcode::JMP:
			ip += op_sbx(ins);
			break;
		case Opcode::JMPF:
			if (!truthy(base[a]))
				ip += op_sbx(ins);
			break;
		case Opcode::JMPT:
			if (truthy(base[a]))
				ip += op_sbx(ins);
			break;

		case Opcode::FORPREP:
		{
			Value start = base[a], limit = base[a + 1], step = base[a + 2];
			if (!start.is_number() || !limit.is_number() || !step.is_number())
				iso.raise(String("[Type error] numeric 'for' bounds must be numbers"), cur_line());
			bool all_int = start.is_int() && limit.is_int() && step.is_int();
			if (all_int)
			{
				if (step.as_int() == 0)
					iso.raise(String("[Value error] 'for' step must not be zero"), cur_line());
				reg_move(base, a, Value::make_int(start.as_int() - step.as_int()));
			}
			else
			{
				if (step.to_double() == 0.0)
					iso.raise(String("[Value error] 'for' step must not be zero"), cur_line());
				reg_move(base, a, Value::make(start.to_double() - step.to_double()));
				reg_move(base, a + 1, Value::make(limit.to_double()));
				reg_move(base, a + 2, Value::make(step.to_double()));
			}
			ip += op_sbx(ins);
			break;
		}
		case Opcode::FORLOOP:
		{
			Value idx = base[a], limit = base[a + 1], step = base[a + 2];
			bool cont;
			Value next;
			if (idx.is_int() && step.is_int())
			{
				int64_t ni;
				if (__builtin_add_overflow(idx.as_int(), step.as_int(), &ni))
					cont = false, next = idx;
				else
				{
					next = Value::make_int(ni);
					cont = step.as_int() > 0 ? ni <= limit.to_double() : ni >= limit.to_double();
				}
			}
			else
			{
				double ni = idx.to_double() + step.to_double();
				next = Value::make(ni);
				cont = step.to_double() > 0 ? ni <= limit.to_double() : ni >= limit.to_double();
			}
			if (cont)
			{
				reg_move(base, a, next);        // internal index
				reg_copy(base, a + 3, next);    // user-visible loop variable
				ip += op_sbx(ins);
			}
			break;
		}

		case Opcode::CALL:
			invoke(base[a], a, op_b(ins));
			break;
		case Opcode::CALLG:
		{
			int nargs = op_b(ins);
			Instruction extra = *ip++; // EXTRA_ARG carries the IC index
			int slot = ic_base_cur + static_cast<int>(op_ax(extra));
			Symbol sym = base[a].as_symbol();
			Value *argv = &base[a + 1];
			GenericFunction *g = find_generic(sym);
			if (!g)
				iso.raise(String("[Name error] no function named '") +
				              String(symbol_name(sym)).view() + "'",
				          cur_line());
			void *callable = nullptr;
			if (nargs == 1 || nargs == 2)
			{
				ICEntry &ic = iso.ics[slot];
				uint64_t c0 = class_of(argv[0]);
				uint64_t key = (c0 << 1);
				if (nargs == 2)
					key = ((c0 << 1) << 25) | (static_cast<uint64_t>(class_of(argv[1])) << 1);
				if (ic.key == key && ic.type_epoch == type_epoch() &&
				    ic.generic_epoch == g->generic_epoch)
				{
					callable = ic.callable;
				}
				else
				{
					Method *m = resolve(g, argv, nargs);
					if (!m)
						iso.raise(String("[Dispatch error] no applicable method for '") +
						              String(symbol_name(sym)).view() + "'",
						          cur_line());
					callable = m->code;
					ic.key = key;
					ic.callable = callable;
					ic.type_epoch = type_epoch();
					ic.generic_epoch = g->generic_epoch;
				}
			}
			else
			{
				Method *m = resolve(g, argv, nargs);
				if (!m)
					iso.raise(String("[Dispatch error] no applicable method for '") +
					              String(symbol_name(sym)).view() + "'",
					          cur_line());
				callable = m->code;
			}
			invoke(Value::make_cell(reinterpret_cast<Cell *>(callable)), a, nargs);
			break;
		}
		case Opcode::EXTRA_ARG:
			PHON_UNREACHABLE_MSG("EXTRA_ARG executed on its own");
			break;

		case Opcode::CLOSURE:
		{
			Proto *child = proto->children[op_bx(ins)].get();
			ClosureCell *nc = make_closure(child);
			for (intptr_t i = 0; i < child->upvals.size(); ++i)
			{
				const UpvalDesc &d = child->upvals[i];
				UpvalueCell *uv = d.in_stack ? iso.find_or_make_open_upvalue(&base[d.index])
				                             : cl->upvals[d.index];
				retain(reinterpret_cast<Cell *>(uv));
				nc->upvals[i] = uv;
			}
			reg_move(base, a, Value::make_cell(reinterpret_cast<Cell *>(nc)));
			break;
		}
		case Opcode::CLOSE:
			iso.close_upvalues(&base[a]);
			break;

		case Opcode::DEFMETHOD:
		{
			// Register R[A] (a closure) as a method on the generic named by the
			// method-def, then journal it so the Isolate owns the closure and can
			// retract the whole run's registrations on teardown (design §11).
			const MethodDef &md = proto->method_defs[op_bx(ins)];
			GenericFunction *g = get_or_create_generic(md.name);
			SmallVector<Class *, 4> sig;
			for (intptr_t i = 0; i < md.class_ids.size(); ++i)
				sig.push_back(get_class(md.class_ids[i]));
			Cell *clo = base[a].as_cell(); // the closure holds this register's +1
			if (add_method(g, sig, md.ref_mask, clo) == AddMethod::Ambiguous)
			{
				release_value(base[a]);
				base[a] = Value::make_null();
				iso.raise(String("[Type error] ambiguous definition of '") +
				              String(symbol_name(md.name)).view() + "'",
				          cur_line());
			}
			iso.record_method(g, std::move(sig), md.ref_mask, clo); // takes the +1
			base[a] = Value::make_null();                           // ownership moved
			break;
		}

		case Opcode::NEWLIST:
		{
			int n = op_b(ins);
			List lst(0);
			lst.reserve(n);
			for (int i = 1; i <= n; ++i)
				lst.append(Variant(base[a + i]));
			reg_copy(base, a, lst.to_value());
			break;
		}
		case Opcode::NEWTABLE:
		{
			int n = op_b(ins);
			Table t;
			for (int i = 0; i < n; ++i)
				t.set(Variant(base[a + 1 + 2 * i]), Variant(base[a + 2 + 2 * i]));
			reg_copy(base, a, t.to_value());
			break;
		}
		case Opcode::NEWSET:
		{
			int n = op_b(ins);
			Set s;
			for (int i = 1; i <= n; ++i)
				s.add(Variant(base[a + i]));
			reg_copy(base, a, s.to_value());
			break;
		}
		case Opcode::LISTAPPEND:
		{
			Value lv = base[a];
			if (!is_list(lv))
				iso.raise(String("[Type error] '+=' target is not a List"), cur_line());
			base[a] = Value::make_null(); // hand ownership to the wrapper
			List lst = List::from_value(lv);
			release(lv.as_cell());
			lst.append(Variant(base[op_b(ins)]));
			reg_copy(base, a, lst.to_value());
			break;
		}
		case Opcode::GETINDEX:
		{
			Value obj = base[op_b(ins)], idx = base[op_c(ins)];
			if (is_list(obj))
			{
				if (!idx.is_int())
					iso.raise(String("[Index error] List index must be an Integer"), cur_line());
				auto *l = reinterpret_cast<ListCell *>(obj.as_cell());
				int64_t i = idx.as_int();
				int64_t k = i < 0 ? l->size + i : i - 1; // 1-based, negatives from end
				if (k < 0 || k >= l->size)
					iso.raise(String("[Index error] List index out of range"), cur_line());
				reg_copy(base, a, l->data[k]);
			}
			else if (is_table(obj))
			{
				Table t = Table::from_value(obj);
				reg_copy(base, a, t.get(Variant(idx)).value());
			}
			else if (is_string(obj))
			{
				if (!idx.is_int())
					iso.raise(String("[Index error] String index must be an Integer"), cur_line());
				String s = String::from_value(obj);
				Substring g = s.at(idx.as_int());
				reg_copy(base, a, String(g).to_value());
			}
			else
				iso.raise(String("[Type error] value is not indexable"), cur_line());
			break;
		}
		case Opcode::SETINDEX:
		{
			Value obj = base[a], idx = base[op_b(ins)], val = base[op_c(ins)];
			if (is_list(obj))
			{
				if (!idx.is_int())
					iso.raise(String("[Index error] List index must be an Integer"), cur_line());
				base[a] = Value::make_null();
				List lst = List::from_value(obj);
				release(obj.as_cell());
				lst.set(idx.as_int(), Variant(val));
				reg_copy(base, a, lst.to_value());
			}
			else if (is_table(obj))
			{
				base[a] = Value::make_null();
				Table t = Table::from_value(obj);
				release(obj.as_cell());
				t.set(Variant(idx), Variant(val));
				reg_copy(base, a, t.to_value());
			}
			else
				iso.raise(String("[Type error] value does not support indexed assignment"), cur_line());
			break;
		}
		case Opcode::IS:
		{
			Class *c = class_denoted_by(base[op_c(ins)]);
			bool r = c && value_is_a(base[op_b(ins)], c);
			reg_move(base, a, Value::make_bool(r));
			break;
		}

		case Opcode::RET:
		case Opcode::HALT:
		{
			int b = op_b(ins);
			Value result = (b == 1) ? base[a] : Value::make_null();
			retain_value(result);
			iso.close_upvalues(base);
			for (int i = 0; i < proto->num_regs; ++i)
			{
				release_value(base[i]);
				base[i] = Value::make_null();
			}
			CallFrame f = iso.frames.back();
			iso.frames.pop_back();
			if (iso.frames.empty())
				return result; // root: caller adopts the +1
			release_value(*f.ret_slot);
			*f.ret_slot = result; // adopt the retain into the caller slot
			CallFrame &caller = iso.frames.back();
			cl = caller.cl;
			proto = cl->proto;
			base = caller.base;
			K = proto->constants.data();
			ic_base_cur = iso.ic_base(proto);
			ip = f.ret_ip;
			break;
		}

		default:
			PHON_UNREACHABLE_MSG("unknown opcode");
		}
	}
}

} // namespace phonometrica
