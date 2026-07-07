// Phonometrica engine — the bytecode interpreter implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/vm/interpreter.hpp>

#include <phon/dispatch/generic.hpp>
#include <phon/object/class.hpp>
#include <phon/object/instance.hpp>
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
#include <string>
#include <utility>

namespace phonometrica {

namespace {

PHON_FORCE_INLINE void retain_value(Value v) noexcept
{
	if (v.owns_cell())
		retain(v.cell_ptr());
}
PHON_FORCE_INLINE void release_value(Value v) noexcept
{
	if (v.owns_cell())
		release(v.cell_ptr());
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

// Read a stored slot (a container element or field), auto-collapsing a spent
// reference: when the slot holds a *closed* box that no one else references, move the
// value back out and drop the box, so a temporary borrow does not leave the element
// permanently boxed (design/references.md §2 — Phonometrica's resolve()). Returns the
// value the slot now holds (borrowed; the caller reg_copy's it). Open boxes and shared
// boxes are read through but left in place.
PHON_FORCE_INLINE Value deref_collapse(Value *slot) noexcept
{
	Value v = *slot;
	if (!v.is_reference())
		return v;
	UpvalueCell *box = reference_box(v);
	Value inner = *box->slot;
	if (!box->is_open() && reinterpret_cast<Cell *>(box)->refcount() == 1)
	{
		retain_value(inner);                     // the slot takes over the value
		*slot = inner;                           // replace the reference in place
		release(reinterpret_cast<Cell *>(box));  // rc 1 -> 0: drops the box and its copy
	}
	return inner;
}

PHON_FORCE_INLINE bool truthy(Value v) noexcept { return !(v.is_null() || v.is_false()); }

bool is_string(Value v) { return v.is_cell() && class_of(v) == CID_STRING; }
bool is_list(Value v) { return v.is_cell() && class_of(v) == CID_LIST; }
bool is_table(Value v) { return v.is_cell() && class_of(v) == CID_TABLE; }
bool is_set(Value v) { return v.is_cell() && class_of(v) == CID_SET; }

// A field-bearing instance (design §5.6): a cell laid out by the instance machinery
// — every user class and the builtin Error hierarchy — identified by its finalizer.
bool is_instance(Value v) noexcept
{
	return v.is_cell() && get_class(class_of(v))->finalize == &instance_finalize;
}

// Resolve a compile-time type annotation to its Class* at module-load time. A
// builtin/`Object` is a concrete id; a user class is read from the module slot
// holding its class object (registered by the preceding DEFCLASS).
Class *resolve_type_ref(const TypeRef &t, Isolate &iso)
{
	if (t.kind == TypeRef::Concrete)
		return get_class(t.value);
	Class *c = class_denoted_by(iso.module_slots[t.value].value());
	PHON_ASSERT_MSG(c != nullptr, "TypeRef module slot does not hold a class object");
	return c;
}

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
	// A user-class instance. Auto-dispatching a user `to_string` from `&`/print is a
	// follow-up (it needs CONCAT to lower through the generic); explicit
	// `to_string(x)` already works. For now show the class name.
	if (v.is_cell())
	{
		Class *c = get_class(class_of(v));
		if (!(c->flags & CLASS_BUILTIN))
			return String("<") + String(c->name).view() + ">";
	}
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

// Re-entrant C++→script calls (used by CONCAT/print to dispatch the to_string
// generic to a user method).
Value run(Isolate &iso);
Value vm_call(Isolate &iso, Value callee, Value *args, int argc);
String stringify_dispatch(Isolate &iso, Value v);

// Adapt the arguments already staged in a callee's register window to the closure's
// parameter layout, then null its non-parameter locals. Shared by the inline `invoke`
// path and the re-entrant `vm_call`. Layout of the staged window: `npos` positional
// args at new_base[0..npos), then `nnamed` keyword (Symbol,value) pairs. A variadic
// callee packs its trailing positional args into a List at the vararg slot; keyword
// options are matched by name into their slots (unmatched → the missing sentinel, so
// the prologue evaluates the default) — design §6. Raises on an arity or unknown-option
// error. `new_base` addresses register 0 of the callee frame.
void setup_callee_frame(Isolate &iso, Proto *cp, Value *new_base, int npos, int nnamed, int line)
{
	const int fixed = cp->num_params; // fixed positional slots (this + fixed explicit)
	if (cp->is_vararg ? npos < fixed : npos != fixed)
		iso.raise(String("[Argument error] function called with the wrong number of arguments"),
		          line);

	// Snapshot the keyword pairs out of the window first: the option slots we fill
	// below overlap the region where the caller staged these pairs. Symbols are
	// immediates; each value carries the caller's +1, borrowed into the snapshot, and
	// its source slots are raw-cleared (their ownership now lives in `named`).
	struct NamedPair { Symbol name; Value val; };
	SmallVector<NamedPair, 8> named;
	named.reserve(nnamed);
	for (int j = 0; j < nnamed; ++j)
	{
		named.push_back({new_base[npos + 2 * j].as_symbol(), new_base[npos + 2 * j + 1]});
		new_base[npos + 2 * j] = Value::make_null();
		new_base[npos + 2 * j + 1] = Value::make_null();
	}

	int option_base = fixed;
	if (cp->is_vararg)
	{
		// Pack the trailing positional args new_base[fixed..npos) into a List at the
		// vararg slot new_base[fixed]. Each staged arg carries the caller's +1;
		// Variant(e) retains it into the List, so the staged +1 is then dropped.
		const int vcount = npos - fixed;
		List xs(0);
		xs.reserve(vcount);
		for (int i = 0; i < vcount; ++i)
		{
			Value e = new_base[fixed + i];
			xs.append(Variant(e));
			release_value(e);
			new_base[fixed + i] = Value::make_null();
		}
		release_value(new_base[fixed]); // vcount==0: a stale slot; else already null
		Value lv = xs.to_value();
		retain_value(lv); // the slot owns the List; xs releases its handle on scope exit
		new_base[fixed] = lv;
		option_base = fixed + 1;
	}

	const int nopt = static_cast<int>(cp->option_names.size());
	// Initialise every option slot to the missing sentinel (releasing any stale value
	// the slot held), then bind each supplied keyword into its slot.
	for (int k = 0; k < nopt; ++k)
	{
		release_value(new_base[option_base + k]);
		new_base[option_base + k] = Value::make_missing();
	}
	for (int j = 0; j < static_cast<int>(named.size()); ++j)
	{
		int slot = -1;
		for (int k = 0; k < nopt; ++k)
			if (cp->option_names[k] == named[j].name)
			{
				slot = option_base + k;
				break;
			}
		if (slot < 0)
		{
			// Unknown option: release every value still held only by the snapshot
			// (the already-bound ones live in option slots and unwind with the frame),
			// then raise.
			for (int r = j; r < static_cast<int>(named.size()); ++r)
				release_value(named[r].val);
			iso.raise(String("[Argument error] no such option '") +
			              String(symbol_name(named[j].name)).view() + "'",
			          line);
		}
		new_base[slot] = named[j].val; // transfer the snapshot's +1 (slot held MISSING)
	}

	for (int i = option_base + nopt; i < cp->num_regs; ++i)
	{
		release_value(new_base[i]);
		new_base[i] = Value::make_null();
	}
}

// The interpreter loop. Executes from the current top frame until the frame stack
// unwinds to the depth it had on entry, then returns that frame's result (+1).
// execute() drives the module root; vm_call() drives a nested re-entrant call.
Value run(Isolate &iso)
{
	Value *stack_end = iso.stack() + iso.stack_capacity();
	intptr_t stop_depth = iso.frames.size() - 1;

	CallFrame &top = iso.frames.back();
	ClosureCell *cl = top.cl;
	Proto *proto = cl->proto;
	const Instruction *ip = proto->code.data();
	Value *base = top.base;
	const Variant *K = proto->constants.data();
	int ic_base_cur = iso.ic_base(proto);

	auto cur_line = [&]() -> int {
		intptr_t idx = (ip - proto->code.data()) - 1;
		return static_cast<int>(proto->line_at(idx));
	};

	// Invoke a callable with `nargs` arguments at base[a+1..], result -> base[a].
	// For a closure this pushes a frame and rebinds the execution locals; for a
	// native it runs inline. `pushed` reports whether a new frame was entered.
	auto invoke = [&](Value callee, int a, int nargs, int nnamed) {
		if (is_native(callee))
		{
			auto *nf = reinterpret_cast<NativeCell *>(callee.as_cell());
			if (nnamed != 0)
			{
				// A native generic takes no keyword options in M6: drop the staged pairs
				// and report it (rather than silently ignoring them).
				for (int i = 0; i < 2 * nnamed; ++i)
				{
					release_value(base[a + 1 + nargs + i]);
					base[a + 1 + nargs + i] = Value::make_null();
				}
				iso.raise(String("[Argument error] '") + String(symbol_name(nf->name)).view() +
				              "' does not accept keyword options",
				          cur_line());
			}
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
			Value *new_base = &base[a + 1];
			// The window must hold the positional args, the staged keyword pairs, and
			// the frame's registers — whichever reaches highest.
			int span = nargs + 2 * nnamed;
			if (span < cp->num_regs)
				span = cp->num_regs;
			if (new_base + span > stack_end)
				iso.raise(String("[Runtime error] stack overflow"), cur_line());
			setup_callee_frame(iso, cp, new_base, nargs, nnamed, cur_line());
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

	// Resolve the three slice parts (a null part = default, direction-dependent) to
	// concrete 1-based bounds. Validates Integer parts and a non-zero step; raises on
	// error. Used by GETSLICE/SETSLICE; the caller generates and bounds-checks the
	// stepped positions (design §9: inclusive both ends, negatives from the end).
	struct SliceSpec { int64_t start, stop, step; };
	auto slice_spec = [&](Value vstart, Value vstop, Value vstep, int64_t n) -> SliceSpec {
		int64_t step = 1;
		if (!vstep.is_null())
		{
			if (!vstep.is_int())
				iso.raise(String("[Index error] slice step must be an Integer"), cur_line());
			step = vstep.as_int();
			if (step == 0)
				iso.raise(String("[Index error] slice step cannot be zero"), cur_line());
		}
		auto norm = [&](Value v) -> int64_t {
			if (!v.is_int())
				iso.raise(String("[Index error] slice bound must be an Integer"), cur_line());
			int64_t i = v.as_int();
			return i < 0 ? n + i + 1 : i; // 1-based, negatives count from the end
		};
		int64_t start = vstart.is_null() ? (step > 0 ? 1 : n) : norm(vstart);
		int64_t stop = vstop.is_null() ? (step > 0 ? n : 1) : norm(vstop);
		return {start, stop, step};
	};
	// Invoke `body(pos0)` for each selected 0-based position, bounds-checking each
	// generated index against [1, n] (an empty range visits nothing).
	auto for_each_slice_pos = [&](const SliceSpec &s, int64_t n, auto &&body) {
		if (s.step > 0)
			for (int64_t i = s.start; i <= s.stop; i += s.step)
			{
				if (i < 1 || i > n)
					iso.raise(String("[Index error] List slice index out of range"), cur_line());
				body(i - 1);
			}
		else
			for (int64_t i = s.start; i >= s.stop; i += s.step)
			{
				if (i < 1 || i > n)
					iso.raise(String("[Index error] List slice index out of range"), cur_line());
				body(i - 1);
			}
	};

	// Outer retry loop: the inner interpreter loop runs until it returns (RET/HALT at
	// this run's root) or throws. A RuntimeError is caught here; if an active handler
	// belongs to this run we unwind to it and re-enter the inner loop at its catch
	// dispatch, otherwise it propagates (architecture §10.5).
	for (;;)
	{
		try
		{

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
			// The slot may hold a reference (a module variable passed by `ref`); deref,
			// auto-collapsing a spent one (§2). A Variant aliases a Value slot.
			reg_copy(base, a,
			         deref_collapse(reinterpret_cast<Value *>(&iso.module_slots[op_bx(ins)])));
			break;
		case Opcode::SETMODULE:
		{
			Value *slot = reinterpret_cast<Value *>(&iso.module_slots[op_bx(ins)]);
			if (slot->is_reference())
			{
				UpvalueCell *box = reference_box(*slot);
				retain_value(base[a]);
				release_value(*box->slot);
				*box->slot = base[a];
			}
			else
				iso.module_slots[op_bx(ins)] = Variant(base[a]);
			break;
		}

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
				out.append(stringify_dispatch(iso, base[r]));
			reg_copy(base, a, out.to_value());
			break;
		}

		case Opcode::JMP:
		{
			int32_t off = op_sbx(ins);
			ip += off;
			// Loop back-edges are safepoints: service the cycle collector here so a
			// long-running loop that produces garbage cycles reclaims them mid-run
			// (architecture §8.2/§9.4). All live values sit in counted registers.
			if (off < 0)
				iso.collector().collect_if_needed();
			break;
		}
		case Opcode::JMPF:
			if (!truthy(base[a]))
				ip += op_sbx(ins);
			break;
		case Opcode::JMPT:
			if (truthy(base[a]))
				ip += op_sbx(ins);
			break;
		case Opcode::JMPSET:
			// Option prologue: skip the default-value evaluation when the slot was
			// supplied (i.e. it is not the missing sentinel).
			if (!base[a].is_missing())
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

		case Opcode::ITER_INIT:
		{
			// Normalize the collection to an index-walkable form (design §12). A Set
			// is materialized to a List; a Table keeps its cell and stashes its key
			// List in R[A+2]; a List walks in place. Cursor (R[A+1]) starts at 0.
			Value coll = base[a];
			if (is_list(coll))
				reg_move(base, a + 2, Value::make_null());
			else if (is_set(coll))
			{
				reg_copy(base, a, Set::from_value(coll).to_list().to_value());
				reg_move(base, a + 2, Value::make_null());
			}
			else if (is_table(coll))
				reg_copy(base, a + 2, Table::from_value(coll).keys().to_value());
			else
				iso.raise(String("[Type error] value is not iterable"), cur_line());
			// reg_move: R[A+1] may still hold a temporary from the collection expression.
			reg_move(base, a + 1, Value::make_int(0));
			break;
		}

		case Opcode::ITER_NEXT:
		{
			// Advance one element. R[B] receives whether the sequence is exhausted;
			// otherwise R[A+3] = value and (when C == 2) R[A+4] = key/index.
			int ib = a, donereg = op_b(ins), arity = op_c(ins);
			Value src = base[ib];
			int64_t cursor = base[ib + 1].as_int();
			bool exhausted = false;
			if (is_table(src))
			{
				List ks = List::from_value(base[ib + 2]);
				if (cursor >= ks.size())
					exhausted = true;
				else
				{
					Variant kv = ks.get(cursor + 1);
					Variant vv = Table::from_value(src).get(kv);
					reg_copy(base, ib + 3, vv.value());
					if (arity == 2)
						reg_copy(base, ib + 4, kv.value());
				}
			}
			else // List (a materialized Set is a List)
			{
				List l = List::from_value(src);
				if (cursor >= l.size())
					exhausted = true;
				else
				{
					Variant ev = l.get(cursor + 1);
					reg_copy(base, ib + 3, ev.value());
					if (arity == 2)
						reg_move(base, ib + 4, Value::make_int(cursor + 1)); // 1-based index
				}
			}
			if (exhausted)
			{
				reg_move(base, donereg, Value::make_bool(true));
				break;
			}
			base[ib + 1] = Value::make_int(cursor + 1); // advance
			reg_move(base, donereg, Value::make_bool(false));
			break;
		}

		case Opcode::ITER_INITREF:
		{
			// By-reference iteration (design §12): a List (value by index) or a Table
			// (value by key), made uniquely owned so boxing a slot (and mutation through
			// it) does not disturb any alias. The lowerer writes the collection back to
			// its binding after the loop. Table R[A+2] stashes the key List.
			Value coll = base[a];
			if (is_list(coll))
			{
				base[a] = Value::make_null();
				List l = List::from_value(coll);
				release(coll.as_cell());
				l.reserve(l.size());             // force a private copy if shared
				reg_copy(base, a, l.to_value()); // base[A] = the unique list
				reg_move(base, a + 2, Value::make_null());
			}
			else if (is_table(coll))
			{
				base[a] = Value::make_null();
				Table t = Table::from_value(coll);
				release(coll.as_cell());
				t.make_unique();                          // private copy if shared
				reg_copy(base, a + 2, t.keys().to_value()); // iterate a snapshot of keys
				reg_copy(base, a, t.to_value());
			}
			else
				iso.raise(String("[Type error] by-reference iteration requires a List or Table"),
				          cur_line());
			reg_move(base, a + 1, Value::make_int(0));
			break;
		}

		case Opcode::ITER_NEXTREF:
		{
			// Advance and bind R[A+3] to a *reference* to the current slot (its box), so
			// the loop body mutates it in place; the index/key (C == 2) is by value. The
			// container in R[A] is uniquely owned (ITER_INITREF).
			int ib = a, donereg = op_b(ins), arity = op_c(ins);
			int64_t cursor = base[ib + 1].as_int();
			Value *slot;
			Value keyval;             // the index/key to expose when arity == 2
			if (is_table(base[ib]))
			{
				List ks = List::from_value(base[ib + 2]);
				if (cursor >= ks.size())
				{
					reg_move(base, donereg, Value::make_bool(true));
					break;
				}
				Variant kv = ks.get(cursor + 1);
				auto *tc = reinterpret_cast<TableCell *>(base[ib].as_cell());
				auto it = tc->table.find(kv.value());
				slot = &it->second; // the key came from keys(), so it exists
				keyval = kv.value();
			}
			else // List
			{
				auto *l = reinterpret_cast<ListCell *>(base[ib].as_cell());
				if (cursor >= l->size)
				{
					reg_move(base, donereg, Value::make_bool(true));
					break;
				}
				slot = &l->data[cursor];
				keyval = Value::make_int(cursor + 1); // 1-based index
			}
			Cell *box;
			if (slot->is_reference())
			{
				box = slot->as_reference_box();
			}
			else
			{
				box = reinterpret_cast<Cell *>(make_reference_box(*slot));
				release_value(*slot);
				*slot = Value::make_reference(box);
			}
			retain(box);
			reg_move(base, ib + 3, Value::make_reference(box));
			if (arity == 2)
				reg_copy(base, ib + 4, keyval);
			base[ib + 1] = Value::make_int(cursor + 1);
			reg_move(base, donereg, Value::make_bool(false));
			break;
		}

		case Opcode::CALL:
			invoke(base[a], a, op_b(ins), op_c(ins));
			break;
		case Opcode::CALLG:
		{
			int nargs = op_b(ins);
			int nnamed = op_c(ins);
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
			// The inline cache keys on argument classes read directly; a reference
			// argument dispatches on its referent, so those calls take the full-resolve
			// path (which derefs).
			bool has_ref = argv[0].is_reference() || (nargs == 2 && argv[1].is_reference());
			if ((nargs == 1 || nargs == 2) && !has_ref)
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
			invoke(Value::make_cell(reinterpret_cast<Cell *>(callable)), a, nargs, nnamed);
			break;
		}
		case Opcode::CALLD:
		{
			// Splat call: R[B] is a List of all positional arguments (design §6). Unpack
			// it into base[a+1..] to give the call its dynamic arity, then dispatch (via
			// the generic memo, never the inline cache) or invoke directly.
			int listreg = op_b(ins);
			Value listv = base[listreg];
			if (!is_list(listv))
				iso.raise(String("[Type error] a splat argument ('...') must be a List"), cur_line());
			auto *lc = reinterpret_cast<ListCell *>(listv.as_cell());
			int64_t len = lc->size;
			if (&base[a + 1 + len] > stack_end)
				iso.raise(String("[Runtime error] stack overflow"), cur_line());
			retain_value(listv); // keep the List alive while we overwrite its source slot
			for (int64_t k = 0; k < len; ++k)
			{
				Value e = deref(lc->data[k]); // splat passes values
				retain_value(e);
				release_value(base[a + 1 + k]); // release stale (k==0: the List's own slot +1)
				base[a + 1 + k] = e;
			}
			if (len == 0)
			{
				release_value(base[listreg]); // no element overwrote the source slot
				base[listreg] = Value::make_null();
			}
			release_value(listv);
			int npos = static_cast<int>(len);

			Value callee = base[a];
			if (callee.is_symbol())
			{
				GenericFunction *g = find_generic(callee.as_symbol());
				if (!g)
					iso.raise(String("[Name error] no function named '") +
					              String(symbol_name(callee.as_symbol())).view() + "'",
					          cur_line());
				Method *m = resolve(g, &base[a + 1], npos);
				if (!m)
					iso.raise(String("[Dispatch error] no applicable method for '") +
					              String(symbol_name(callee.as_symbol())).view() + "'",
					          cur_line());
				invoke(Value::make_cell(reinterpret_cast<Cell *>(m->code)), a, npos, 0);
			}
			else
			{
				invoke(callee, a, npos, 0);
			}
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

		case Opcode::MAKEREF:
		{
			// Promote the caller's local R[B] to a first-class boxed reference
			// (design/references.md §2/§6). The box shares the register while the
			// frame lives (open upvalue), so the caller and callee alias one value —
			// mutation writes back with no copy. The same cell serves upvalue capture,
			// so a captured-and-referenced local shares a single box.
			UpvalueCell *box = iso.find_or_make_open_upvalue(&base[op_b(ins)]);
			retain(reinterpret_cast<Cell *>(box)); // R[A] owns a reference to the box
			reg_move(base, a, Value::make_reference(reinterpret_cast<Cell *>(box)));
			break;
		}
		case Opcode::DEREF:
			// Read through a reference (identity if R[B] is not one).
			reg_copy(base, a, deref(base[op_b(ins)]));
			break;
		case Opcode::SETREF:
		{
			// Write through the reference in R[A] into the box's slot.
			UpvalueCell *box = reference_box(base[a]);
			Value nv = base[op_b(ins)];
			retain_value(nv);
			release_value(*box->slot);
			*box->slot = nv;
			break;
		}
		case Opcode::MAYBEPROMOTE:
		{
			// Indirect-call argument load (design/references.md §6.2): the source local
			// R[B] feeds argument position A-C-1 (args sit right after the callee R[C]).
			// If the callee marks that position `ref`, pass a reference — forwarding an
			// existing box, or promoting a plain local into a fresh one; otherwise pass
			// the value (dereferencing the source if it is itself a reference).
			int c_reg = op_c(ins);
			int argpos = a - c_reg - 1;
			Value src = base[op_b(ins)];
			if ((callable_ref_mask(base[c_reg]) >> argpos) & 1u)
			{
				if (src.is_reference())
				{
					reg_copy(base, a, src); // forward the existing box
				}
				else
				{
					UpvalueCell *box = iso.find_or_make_open_upvalue(&base[op_b(ins)]);
					retain(reinterpret_cast<Cell *>(box));
					reg_move(base, a, Value::make_reference(reinterpret_cast<Cell *>(box)));
				}
			}
			else
			{
				reg_copy(base, a, deref(src));
			}
			break;
		}
		case Opcode::MAYBEBOX:
		{
			// A non-lvalue argument already computed into R[A]: if the callee R[C]
			// marks this position `ref`, box the value (closed, no write-back) so the
			// callee still receives a reference; else leave it a plain value.
			int c_reg = op_c(ins);
			int argpos = a - c_reg - 1;
			if ((callable_ref_mask(base[c_reg]) >> argpos) & 1u)
			{
				UpvalueCell *box = make_reference_box(base[a]);
				reg_move(base, a, Value::make_reference(reinterpret_cast<Cell *>(box)));
			}
			break;
		}
		case Opcode::PROMOTEINDEX:
		{
			// Promote a List element or Table value to a first-class reference
			// (design/references.md §7): detach the (value-semantic) container so aliases
			// keep the plain value, then box the slot in place. R[A] and the slot share the
			// box, so mutation through the reference is visible via the container. R[B]
			// receives the (possibly detached) container for write-back to its binding.
			// A Table *key* can never be referenced (owner invariant), so only its value is.
			Value obj = base[op_b(ins)], idx = base[op_c(ins)];
			if (is_list(obj))
			{
				if (!idx.is_int())
					iso.raise(String("[Index error] List index must be an Integer"), cur_line());
				base[op_b(ins)] = Value::make_null();
				List lst = List::from_value(obj);
				release(obj.as_cell());
				Value *slot = reinterpret_cast<Value *>(&lst.ref(idx.as_int())); // detaches
				Cell *box;
				if (slot->is_reference())
					box = slot->as_reference_box();
				else
				{
					box = reinterpret_cast<Cell *>(make_reference_box(*slot));
					release_value(*slot);
					*slot = Value::make_reference(box);
				}
				retain(box);
				reg_move(base, a, Value::make_reference(box));
				reg_copy(base, op_b(ins), lst.to_value());
				break;
			}
			if (is_table(obj))
			{
				auto *tc = reinterpret_cast<TableCell *>(obj.as_cell());
				if (!tc->table.contains(idx))
					iso.raise(String("[Key error] cannot take a reference to a missing Table key"),
					          cur_line());
				base[op_b(ins)] = Value::make_null();
				Table t = Table::from_value(obj);
				release(obj.as_cell());
				Value *slot = t.detached_value_slot(Variant(idx)); // exists (checked); detaches
				Cell *box;
				if (slot->is_reference())
					box = slot->as_reference_box();
				else
				{
					box = reinterpret_cast<Cell *>(make_reference_box(*slot));
					release_value(*slot);
					*slot = Value::make_reference(box);
				}
				retain(box);
				reg_move(base, a, Value::make_reference(box));
				reg_copy(base, op_b(ins), t.to_value());
				break;
			}
			iso.raise(String("[Type error] only a List element or Table value can be passed by "
			                 "reference"),
			          cur_line());
			break;
		}
		case Opcode::PROMOTEFIELD:
		{
			// Promote an instance field to a reference (as PROMOTEINDEX, for a field slot).
			Value obj = base[op_b(ins)];
			if (!is_instance(obj))
				iso.raise(String("[Type error] value has no fields"), cur_line());
			Symbol fname = base[op_c(ins)].as_symbol();
			Cell *inst = obj.as_cell();
			Class *c = get_class(inst->class_id());
			int32_t slotno = field_slot(c, fname);
			if (slotno < 0)
				iso.raise(String("[Name error] '") + String(c->name).view() + "' has no field '" +
				              String(symbol_name(fname)).view() + "'",
				          cur_line());
			const FieldInfo *fi = field_at(c, slotno);
			if (fi->getter || fi->setter)
				iso.raise(String("[Type error] a computed field cannot be passed by reference"),
				          cur_line());
			// Detach a shared value-class instance so aliases keep the plain field value.
			base[op_b(ins)] = Value::make_null();
			if (c->is_value() && inst->refcount() > 1)
			{
				Cell *copy = instance_clone(inst);
				release(inst);
				inst = copy;
			}
			Value *slot = &instance_fields(inst)[slotno];
			Cell *box;
			if (slot->is_reference())
			{
				box = slot->as_reference_box();
			}
			else
			{
				box = reinterpret_cast<Cell *>(make_reference_box(*slot));
				release_value(*slot);
				*slot = Value::make_reference(box);
			}
			retain(box);
			reg_move(base, a, Value::make_reference(box));
			reg_copy(base, op_b(ins), Value::make_cell(inst)); // (possibly detached) instance back
			release(inst); // reg_copy retained; drop our transient reference
			break;
		}
		case Opcode::PROMOTEUPVAL:
		{
			// An upvalue *is* a reference box (design/references.md §3), so a reference
			// to a captured variable is simply a reference to that box — writes through
			// it reach the captured variable's storage.
			Cell *box = reinterpret_cast<Cell *>(cl->upvals[op_b(ins)]);
			retain(box);
			reg_move(base, a, Value::make_reference(box));
			break;
		}
		case Opcode::PROMOTEMODULE:
		{
			// Box the module slot in place (a persistent slot needs no write-back); a
			// later GETMODULE derefs and eventually auto-collapses it.
			Value *slot = reinterpret_cast<Value *>(&iso.module_slots[op_bx(ins)]);
			Cell *box;
			if (slot->is_reference())
			{
				box = slot->as_reference_box();
			}
			else
			{
				box = reinterpret_cast<Cell *>(make_reference_box(*slot));
				release_value(*slot);
				*slot = Value::make_reference(box);
			}
			retain(box);
			reg_move(base, a, Value::make_reference(box));
			break;
		}

		case Opcode::DEFMETHOD:
		{
			// Register R[A] (a closure) as a method on the generic named by the
			// method-def, then journal it so the Isolate owns the closure and can
			// retract the whole run's registrations on teardown (design §11).
			const MethodDef &md = proto->method_defs[op_bx(ins)];
			GenericFunction *g = get_or_create_generic(md.name);
			SmallVector<Class *, 4> sig;
			for (intptr_t i = 0; i < md.sig.size(); ++i)
				sig.push_back(resolve_type_ref(md.sig[i], iso));
			// `ref` is meaningful only for value types; a `ref` on a reference-class
			// parameter would be a silent no-op, so it is rejected (§4).
			for (intptr_t i = 0; i < sig.size(); ++i)
				if ((md.ref_mask & (uint64_t(1) << i)) && sig[i]->is_ref())
				{
					release_value(base[a]);
					base[a] = Value::make_null();
					iso.raise(String("[Type error] 'ref' is not allowed on the reference-class "
					                 "parameter of '") +
					              String(symbol_name(md.name)).view() + "'",
					          cur_line());
				}
			Cell *clo = base[a].as_cell(); // the closure holds this register's +1
			AddMethod res = add_method(g, sig, md.ref_mask, md.is_vararg, clo);
			if (res != AddMethod::Ok)
			{
				release_value(base[a]);
				base[a] = Value::make_null();
				const char *why = res == AddMethod::RefMaskConflict
				    ? "[Type error] overloads must agree on 'ref' parameters: '"
				    : "[Type error] ambiguous definition of '";
				iso.raise(String(why) + String(symbol_name(md.name)).view() + "'", cur_line());
			}
			iso.record_method(g, std::move(sig), md.is_vararg, clo); // takes the +1
			base[a] = Value::make_null();              // ownership moved
			break;
		}

		case Opcode::DEFCLASS:
		{
			// Register a user class at module load: resolve its base, build the field
			// layout, and place the class object in R[A] (the caller SETMODULEs it into
			// the class's binding). Its methods register via the following DEFMETHODs.
			const ClassDef &cd = proto->class_defs[op_bx(ins)];
			Class *base_cls = resolve_type_ref(cd.base, iso);
			SmallVector<FieldInfo, 4> fields;
			for (intptr_t i = 0; i < cd.fields.size(); ++i)
			{
				const FieldDef &fdef = cd.fields[i];
				FieldInfo fi;
				fi.name = fdef.name;
				fi.type = resolve_type_ref(fdef.type, iso);
				fi.is_private = fdef.is_private;
				// Accessor closures capture nothing (top-level protos), so make_closure
				// is complete; the Isolate owns them for its lifetime.
				if (fdef.getter_proto >= 0)
				{
					Cell *g = reinterpret_cast<Cell *>(
					    make_closure(proto->children[fdef.getter_proto].get()));
					iso.keep_alive(g);
					fi.getter = g;
				}
				if (fdef.setter_proto >= 0)
				{
					Cell *s = reinterpret_cast<Cell *>(
					    make_closure(proto->children[fdef.setter_proto].get()));
					iso.keep_alive(s);
					fi.setter = s;
				}
				fields.push_back(fi);
			}
			std::string name(symbol_name(cd.name));
			Class *c = add_user_class(name.c_str(), base_cls, cd.is_ref, cd.is_open,
			                          fields.data(), static_cast<int32_t>(fields.size()));
			reg_copy(base, a, class_object(c));
			break;
		}

		case Opcode::NEW:
		{
			Class *c = class_denoted_by(base[op_b(ins)]);
			// User classes and the Error hierarchy are constructible; other builtins
			// (List, Float, …) are not — their `T(x)` is conversion, not construction.
			if (!c || ((c->flags & CLASS_BUILTIN) && !is_a(c, error_class())))
				iso.raise(String("[Type error] value is not a constructible class"), cur_line());
			reg_move(base, a, Value::make_cell(make_instance(c))); // fresh, rc==1
			break;
		}

		case Opcode::GETFIELD:
		case Opcode::GETFIELDRAW:
		{
			Value obj = base[op_b(ins)];
			if (!is_instance(obj))
				iso.raise(String("[Type error] value has no fields"), cur_line());
			Symbol fname = base[op_c(ins)].as_symbol();
			Class *c = get_class(class_of(obj));
			int32_t slot = field_slot(c, fname);
			if (slot < 0)
				iso.raise(String("[Name error] '") + String(c->name).view() +
				              "' has no field '" + String(symbol_name(fname)).view() + "'",
				          cur_line());
			const FieldInfo *fi = field_at(c, slot);
			if (op == Opcode::GETFIELD && fi->is_private)
				iso.raise(String("[Name error] field '") + String(symbol_name(fname)).view() +
				              "' of '" + String(c->name).view() + "' is private",
				          cur_line());
			if (op == Opcode::GETFIELD && fi->getter)
			{
				// Route through the getter: get(obj) -> value.
				Value r = vm_call(iso, Value::make_cell(fi->getter), &obj, 1);
				reg_move(base, a, r); // r carries +1
			}
			else
				reg_copy(base, a, deref_collapse(&instance_fields(obj.as_cell())[slot])); // may be a ref
			break;
		}

		case Opcode::SETFIELD:
		case Opcode::SETFIELDRAW:
		{
			// R[A].field(R[B]) = R[C]. GETFIELD/SETFIELD route through accessors; the
			// RAW variants (used inside a field's own accessor) reach the slot directly.
			if (!is_instance(base[a]))
				iso.raise(String("[Type error] value has no fields"), cur_line());
			Symbol fname = base[op_b(ins)].as_symbol();
			Cell *obj = base[a].as_cell();
			Class *c = get_class(obj->class_id());
			int32_t slot = field_slot(c, fname);
			if (slot < 0)
				iso.raise(String("[Name error] '") + String(c->name).view() +
				              "' has no field '" + String(symbol_name(fname)).view() + "'",
				          cur_line());
			const FieldInfo *fi = field_at(c, slot);
			if (op == Opcode::SETFIELD && fi->is_private)
				iso.raise(String("[Name error] field '") + String(symbol_name(fname)).view() +
				              "' of '" + String(c->name).view() + "' is private",
				          cur_line());
			if (op == Opcode::SETFIELD && fi->setter)
			{
				// Route through the setter, which returns the (mutated, possibly
				// detached) instance so the caller writes it back to its binding.
				Value args2[2] = {base[a], base[op_c(ins)]};
				Value r = vm_call(iso, Value::make_cell(fi->setter), args2, 2);
				reg_move(base, a, r);
				break;
			}
			if (op == Opcode::SETFIELD && fi->getter)
				iso.raise(String("[Name error] field '") + String(symbol_name(fname)).view() +
				              "' of '" + String(c->name).view() + "' is read-only",
				          cur_line());
			// If the field is a reference, write through its box (the shared mutable
			// identity); the instance is not detached (design/references.md §7).
			if (instance_fields(obj)[slot].is_reference())
			{
				UpvalueCell *box = reference_box(instance_fields(obj)[slot]);
				Value v = base[op_c(ins)];
				retain_value(v);
				release_value(*box->slot);
				*box->slot = v;
				break;
			}
			// Raw slot store: a shared value class detaches a private copy first (the
			// detached cell is left in R[A] for write-back); a ref class mutates in place.
			if (c->is_value() && obj->refcount() > 1)
			{
				Cell *copy = instance_clone(obj); // rc==1
				release(obj);                      // drop this register's share
				base[a] = Value::make_cell(copy);
				obj = copy;
			}
			Value *f = instance_fields(obj);
			Value v = base[op_c(ins)];
			retain_value(v);
			release_value(f[slot]);
			f[slot] = v;
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
			base[a] = Value::make_null();   // hand ownership to the wrapper (adopt: no buffering)
			List lst = List::adopt(lv);
			lst.append(Variant(base[op_b(ins)]));
			reg_copy(base, a, lst.to_value());
			break;
		}
		case Opcode::LISTEXTEND:
		{
			// Append every element of List R[B] to List R[A] (the splat expansion that
			// builds a call's positional-argument list). R[B] is a distinct List.
			Value lv = base[a];
			if (!is_list(lv))
				iso.raise(String("[Type error] splat target is not a List"), cur_line());
			Value src = base[op_b(ins)];
			if (!is_list(src))
				iso.raise(String("[Type error] a splat argument ('...') must be a List"), cur_line());
			auto *sc = reinterpret_cast<ListCell *>(src.as_cell());
			int64_t n = sc->size;
			base[a] = Value::make_null(); // hand ownership to the wrapper (adopt: no buffering)
			List lst = List::adopt(lv);
			lst.reserve(lst.size() + n);
			for (int64_t k = 0; k < n; ++k)
				lst.append(Variant(deref(sc->data[k]))); // splat passes values, not references
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
				reg_copy(base, a, deref_collapse(&l->data[k])); // element may be a reference (§7)
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
				// If the target element is a reference, write through its box (the shared
				// mutable identity) — the list is not detached (design/references.md §7).
				auto *l = reinterpret_cast<ListCell *>(obj.as_cell());
				int64_t i = idx.as_int();
				int64_t k = i < 0 ? l->size + i : i - 1;
				if (k >= 0 && k < l->size && l->data[k].is_reference())
				{
					UpvalueCell *box = reference_box(l->data[k]);
					retain_value(val);
					release_value(*box->slot);
					*box->slot = val;
					break;
				}
				base[a] = Value::make_null();
				List lst = List::from_value(obj);
				release(obj.as_cell());
				lst.set(idx.as_int(), Variant(val));
				reg_copy(base, a, lst.to_value());
			}
			else if (is_table(obj))
			{
				// A reference value is written through its box (no detach), like a list
				// element (design/references.md §7).
				auto *tc = reinterpret_cast<TableCell *>(obj.as_cell());
				auto it = tc->table.find(idx);
				if (it != tc->table.end() && it->second.is_reference())
				{
					UpvalueCell *box = reference_box(it->second);
					retain_value(val);
					release_value(*box->slot);
					*box->slot = val;
					break;
				}
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
		case Opcode::GETSLICE:
		{
			// R[A] = R[B][ slice(R[C], R[C+1], R[C+2]) ]  (start, stop, step; null = default)
			Value obj = base[op_b(ins)];
			int c = op_c(ins);
			if (is_list(obj))
			{
				auto *l = reinterpret_cast<ListCell *>(obj.as_cell());
				SliceSpec s = slice_spec(base[c], base[c + 1], base[c + 2], l->size);
				List out(0);
				for_each_slice_pos(s, l->size, [&](int64_t k) {
					out.append(Variant(deref(l->data[k]))); // element may be a reference (§7)
				});
				reg_copy(base, a, out.to_value());
			}
			else if (is_string(obj))
			{
				String str = String::from_value(obj);
				SliceSpec s = slice_spec(base[c], base[c + 1], base[c + 2], str.length());
				String out;
				for_each_slice_pos(s, str.length(), [&](int64_t k) { out.append(str.at(k + 1)); });
				reg_copy(base, a, out.to_value());
			}
			else
				iso.raise(String("[Type error] value is not sliceable"), cur_line());
			break;
		}
		case Opcode::SETSLICE:
		{
			// R[A][ slice(R[B], R[B+1], R[B+2]) ] = R[C]
			Value obj = base[a];
			int bslice = op_b(ins);
			Value val = base[op_c(ins)];
			if (is_string(obj))
				iso.raise(String("[Type error] a String slice is read-only"), cur_line());
			if (!is_list(obj))
				iso.raise(String("[Type error] value does not support sliced assignment"), cur_line());
			int64_t n = reinterpret_cast<ListCell *>(obj.as_cell())->size;
			SliceSpec s = slice_spec(base[bslice], base[bslice + 1], base[bslice + 2], n);
			// Pass 1: validate every position and count the selected slots.
			int64_t count = 0;
			for_each_slice_pos(s, n, [&](int64_t) { ++count; });
			// The right-hand side is either a same-length List (element-wise replace) or a
			// scalar broadcast to every selected position (owner decision, DEVIATIONS M6).
			bool rhs_list = is_list(val);
			ListCell *rl = rhs_list ? reinterpret_cast<ListCell *>(val.as_cell()) : nullptr;
			if (rhs_list && rl->size != count)
				iso.raise(String("[Index error] slice-assignment length mismatch"), cur_line());
			// Detach once (CoW), then write in place. Reference-valued targets write through
			// their box (the shared mutable identity), mirroring SETINDEX (§7).
			base[a] = Value::make_null();
			List lst = List::from_value(obj);
			release(obj.as_cell());
			Value *slots = lst.writable_slots();
			int64_t j = 0;
			for_each_slice_pos(s, n, [&](int64_t k) {
				Value nv = rhs_list ? deref(rl->data[j]) : val;
				if (slots[k].is_reference())
				{
					UpvalueCell *box = reference_box(slots[k]);
					retain_value(nv);
					release_value(*box->slot);
					*box->slot = nv;
				}
				else
				{
					retain_value(nv);
					release_value(slots[k]);
					slots[k] = nv;
				}
				++j;
			});
			reg_copy(base, a, lst.to_value());
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
			// Discard any handlers belonging to the frame that just returned (a return
			// from within a try emits POPTRY, but this keeps the stack sound regardless).
			while (!iso.handlers.empty() &&
			       iso.handlers.back().frame_depth > static_cast<intptr_t>(iso.frames.size()))
				iso.handlers.pop_back();
			if (iso.frames.size() == stop_depth)
				return result; // this run's top frame returned; caller adopts the +1
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

		case Opcode::PUSHTRY:
			iso.handlers.push_back(Isolate::Handler{static_cast<intptr_t>(iso.frames.size()), base,
			                                        ip + op_sbx(ins), a});
			break;
		case Opcode::POPTRY:
			iso.handlers.pop_back();
			break;
		case Opcode::THROW:
		{
			Value ev = base[a];
			Class *ec = ev.is_cell() ? get_class(class_of(ev)) : nullptr;
			if (!ec || !is_a(ec, error_class()))
				iso.raise(String("[Type error] only an Error can be thrown"), cur_line());
			Value msgv = instance_fields(ev.as_cell())[0]; // slot 0 == message
			String msg = is_string(msgv) ? String::from_value(msgv) : String(ec->name);
			capture_error_trace(iso, ev.as_cell(), cur_line()); // origin, if not already set
			retain_value(ev);                                    // the in-flight error owns a reference
			throw RuntimeError{msg, cur_line(), ev};
		}

		default:
			PHON_UNREACHABLE_MSG("unknown opcode");
		}
	}

		}
		catch (RuntimeError &err)
		{
			// No handler belongs to this run -> propagate to the caller / do_string.
			if (iso.handlers.empty() || iso.handlers.back().frame_depth <= stop_depth)
				throw;

			Isolate::Handler h = iso.handlers.back();
			iso.handlers.pop_back();

			// Unwind the frames above the try's frame, releasing their registers.
			while (static_cast<intptr_t>(iso.frames.size()) > h.frame_depth)
			{
				CallFrame f = iso.frames.back();
				iso.close_upvalues(f.base);
				for (int i = 0; i < f.cl->proto->num_regs; ++i)
				{
					release_value(f.base[i]);
					f.base[i] = Value::make_null();
				}
				iso.frames.pop_back();
			}

			// Resume in the try's frame at its catch dispatch, with the error bound.
			CallFrame &top = iso.frames.back();
			cl = top.cl;
			proto = cl->proto;
			base = top.base;
			K = proto->constants.data();
			ic_base_cur = iso.ic_base(proto);
			ip = h.land_ip;
			release_value(base[h.err_reg]);
			base[h.err_reg] = err.error; // adopt the in-flight error's reference
		}
	}
}

// Call `callee` with `argc` borrowed args and run it to completion, returning its
// result (+1). A native runs inline; a closure gets a fresh frame placed above the
// current top frame's registers, then run() executes it re-entrantly. This is the
// C++→script seam used for to_string dispatch (and any native→generic callback).
Value vm_call(Isolate &iso, Value callee, Value *args, int argc)
{
	if (is_native(callee))
	{
		auto *nf = reinterpret_cast<NativeCell *>(callee.as_cell());
		return nf->fn(iso, args, argc);
	}
	PHON_ASSERT_MSG(is_closure(callee), "vm_call on a non-callable");
	auto *cl = reinterpret_cast<ClosureCell *>(callee.as_cell());
	Proto *cp = cl->proto;

	CallFrame &top = iso.frames.back();
	Value *nb = top.base + top.cl->proto->num_regs; // above the caller's live registers
	Value *stack_end = iso.stack() + iso.stack_capacity();
	// The frame needs room for its registers *and* the staged args (a variadic callee
	// stages more positional args than its packed frame holds).
	int span = argc > cp->num_regs ? argc : cp->num_regs;
	if (nb + span > stack_end)
		iso.raise(String("[Runtime error] stack overflow"), 0);
	for (int i = 0; i < argc; ++i)
	{
		nb[i] = args[i];
		if (args[i].is_cell())
			retain(args[i].as_cell());
	}
	// This window is fresh (uninitialized) stack, so raw-null the non-arg slots before
	// setup_callee_frame — its release loop assumes every slot holds a releasable value.
	for (int i = argc; i < cp->num_regs; ++i)
		nb[i] = Value::make_null();
	setup_callee_frame(iso, cp, nb, argc, /*nnamed=*/0, 0);
	iso.frames.push_back(CallFrame{cl, nb, nullptr, nullptr}); // ret slot unused: run() stops here
	return run(iso);
}

String stringify_dispatch(Isolate &iso, Value v)
{
	// A user class with a `to_string` method stringifies through it (design §12);
	// everything else uses the builtin representation. Only a *script* (closure)
	// method is invoked — the builtin `to_string` native forwards back here, so
	// calling it would recurse.
	if (is_instance(v))
	{
		if (GenericFunction *g = find_generic(intern("to_string")))
		{
			Method *m = resolve(g, &v, 1);
			if (m)
			{
				Value code = Value::make_cell(reinterpret_cast<Cell *>(m->code));
				if (is_closure(code))
				{
					Value r = vm_call(iso, code, &v, 1);
					String s = is_string(r) ? String::from_value(r) : to_string_value(r);
					release_value(r);
					return s;
				}
			}
		}
	}
	return to_string_value(v);
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
	return run(iso);
}

// Stringify dispatching a user to_string (for print and the string library).
String stringify(Isolate &iso, Value v) { return stringify_dispatch(iso, v); }

} // namespace phonometrica
