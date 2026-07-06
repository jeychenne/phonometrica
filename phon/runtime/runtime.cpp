// Phonometrica engine — script execution façade + builtin library (M4).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/runtime/runtime.hpp>

#include <phon/compile/disassembler.hpp>
#include <phon/compile/lower.hpp>
#include <phon/compile/parser.hpp>
#include <phon/compile/source.hpp>
#include <phon/core/handle.hpp>
#include <phon/core/small_vector.hpp>
#include <phon/core/vector.hpp>
#include <phon/dispatch/generic.hpp>
#include <phon/object/class.hpp>
#include <phon/object/instance.hpp>
#include <phon/runtime/bootstrap.hpp>
#include <phon/types/atom.hpp>
#include <phon/types/list.hpp>
#include <phon/types/set.hpp>
#include <phon/types/string.hpp>
#include <phon/types/table.hpp>
#include <phon/vm/function.hpp>
#include <phon/vm/interpreter.hpp>

#include <cstdio>

namespace phonometrica {

namespace {

// --- builtin natives ----------------------------------------------------------

Value builtin_print(Isolate &iso, Value *args, int argc)
{
	String out;
	for (int i = 0; i < argc; ++i)
	{
		if (i > 0)
			out.append(" ");
		out.append(stringify(iso, args[i]));
	}
	out.append("\n");
	std::fwrite(out.data(), 1, static_cast<size_t>(out.size()), stdout);
	return Value::make_null();
}

Value builtin_to_string(Isolate &iso, Value *args, int argc)
{
	// The builtin `to_string` methods (one per arity-1 call) stringify any value the
	// way `&`/print do; a user `method to_string()` overrides for its own class.
	(void) argc;
	return stringify(iso, args[0]).to_value();
}

Value builtin_assert(Isolate &iso, Value *args, int argc)
{
	Value cond = args[0];
	if (cond.is_null() || cond.is_false())
	{
		String msg("[Assertion failed]");
		if (argc >= 2)
		{
			msg = String("[Assertion failed] ");
			msg.append(stringify(args[1]));
		}
		iso.raise(msg, 0);
	}
	return Value::make_null();
}

Value builtin_len(Isolate &iso, Value *args, int argc)
{
	(void) argc;
	Value v = args[0];
	if (v.is_cell())
	{
		switch (class_of(v))
		{
		case CID_LIST: return Value::make_int(List::from_value(v).size());
		case CID_STRING: return Value::make_int(String::from_value(v).length());
		case CID_TABLE: return Value::make_int(Table::from_value(v).size());
		case CID_SET: return Value::make_int(Set::from_value(v).size());
		default: break;
		}
	}
	iso.raise(String("[Type error] 'len' expects a List, String, Table, or Set"), 0);
}

Value builtin_cast(Isolate &iso, Value *args, int argc)
{
	// `cast x as T` -> cast(x, T) (design §7). The default is a checked,
	// identity-preserving downcast; type-specific conversions are library overloads.
	(void) argc;
	Value x = args[0];
	Class *target = class_denoted_by(args[1]);
	if (!target)
		iso.raise(String("[Type error] 'cast' target is not a class"), 0);
	if (value_is_a(x, target))
	{
		if (x.is_cell())
			retain(x.as_cell());
		return x;
	}
	iso.raise(String("[Type error] cannot cast a ") + String(class_of_desc(x)->name).view() +
	              " to " + String(target->name).view(),
	          0);
}

Value error_init(Isolate &, Value *args, int argc)
{
	// init(this as Error, message): set the message field (slot 0) and return `this`.
	(void) argc;
	Cell *self = args[0].as_cell();
	Value msg = args[1];
	if (msg.is_cell())
		retain(msg.as_cell());
	Value *f = instance_fields(self);
	if (f[0].is_cell())
		release(f[0].as_cell());
	f[0] = msg;
	retain(self); // native returns a value carrying +1
	return args[0];
}

Value builtin_collect_garbage(Isolate &iso, Value *args, int argc)
{
	(void) args;
	(void) argc;
	// Force a cycle-collection pass (design §8.2). Live values are held in counted
	// registers, so a mid-run collection only reclaims genuine garbage.
	if (CycleCollector *cc = current_collector())
		cc->collect();
	(void) iso;
	return Value::make_null();
}

// Keep native cells alive for the process lifetime (they back builtin generics).
// A function-local static releases them at process exit (matching the class/atom
// registries), so leak checkers stay clean.
struct NativeKeepalive
{
	Vector<NativeCell *> cells;
	~NativeKeepalive()
	{
		for (intptr_t i = 0; i < cells.size(); ++i)
			release(reinterpret_cast<Cell *>(cells[i]));
	}
};

NativeKeepalive &native_keepalive()
{
	static NativeKeepalive k;
	return k;
}

void register_native(const char *name, NativeFn fn, int min_arity, int max_arity)
{
	Symbol s = intern(name);
	GenericFunction *g = get_or_create_generic(s);
	NativeCell *nf = make_native(fn, s, min_arity, max_arity);
	native_keepalive().cells.push_back(nf); // retains via the +1 from make_native

	Class *obj = get_class(CID_OBJECT);
	int lo = min_arity;
	int hi = max_arity < 0 ? min_arity : max_arity;
	// One method per supported arity (variadic dispatch is folded into the fixed
	// arities the native accepts — full variadic method support arrives in M5).
	for (int arity = lo; arity <= hi; ++arity)
	{
		SmallVector<Class *, 4> sig;
		for (int k = 0; k < arity; ++k)
			sig.push_back(obj);
		add_method(g, sig, 0, nf);
	}
}

void register_builtins()
{
	register_native("print", builtin_print, 0, 8);
	register_native("len", builtin_len, 1, 1);
	register_native("assert", builtin_assert, 1, 2);
	register_native("cast", builtin_cast, 2, 2);
	register_native("to_string", builtin_to_string, 1, 1);
	register_native("collect_garbage", builtin_collect_garbage, 0, 0);

	// Error construction: init(this as Error, message). Registered with a typed
	// signature so subclasses inherit it (constructor inheritance).
	{
		Symbol s = intern("init");
		GenericFunction *g = get_or_create_generic(s);
		NativeCell *nf = make_native(error_init, s, 2, 2);
		native_keepalive().cells.push_back(nf);
		SmallVector<Class *, 4> sig;
		sig.push_back(error_class());
		sig.push_back(get_class(CID_OBJECT));
		add_method(g, sig, 0, nf);
	}
}

bool g_booted = false;

// Parse `src` and compile it against `ns` (the persistent namespace). `Source` is
// the compiler-layer text buffer: it holds the script as a std::string because it
// reads files via std::ifstream and scans with std::string_view, so the engine
// String is converted once here at the boundary (cold path).
void compile_source(const String &src, ModuleNamespace &ns, CompiledModule &cm)
{
	Source source = Source::from_string(std::string(src.data(), static_cast<size_t>(src.size())));
	Parser parser(source);
	AutoAst ast = parser.parse();
	compile_module(ast.get(), ns, cm);
}

} // namespace

void init_runtime()
{
	if (g_booted)
		return;
	g_booted = true;
	bootstrap();
	register_function_classes();
	register_builtins();
}

// The session's mutable state: the long-lived Isolate, the persistent module
// namespace, and the compiled-chunk history that keeps every Proto tree alive for
// closures stored in module slots across chunks (a growing session cost; the
// registration journal that unloads them is design §11, deferred to M5).
struct Runtime::State
{
	Isolate isolate;
	ModuleNamespace shell;
	Vector<std::unique_ptr<CompiledModule>> history;
};

Runtime::Runtime() : m_state(std::make_unique<State>()) { init_runtime(); }
Runtime::~Runtime() = default;

Variant Runtime::do_string(const String &code)
{
	State &st = *m_state;

	auto cm = std::make_unique<CompiledModule>();
	compile_source(code, st.shell, *cm);

	// Grow the Isolate's module-slot vector to the (possibly larger) namespace,
	// preserving the values of existing slots — indices never move (design §11).
	if (st.isolate.module_slots.size() < st.shell.num_slots)
		st.isolate.module_slots.resize(st.shell.num_slots);

	// Keep the Proto tree alive: module-slot closures created by this chunk outlive
	// the call and borrow their Proto.
	Proto *main_proto = cm->main.get();
	st.history.push_back(std::move(cm));

	Isolate *prev = current_isolate();
	CycleCollector *prev_cc = current_collector();
	set_current_isolate(&st.isolate);
	set_current_collector(&st.isolate.collector());
	Handle<ClosureCell> main = Handle<ClosureCell>::adopt(make_closure(main_proto));
	Variant out;
	try
	{
		Value result = execute(st.isolate, main.get());
		out = Variant(result);         // retains
		if (result.is_cell())
			release(result.as_cell()); // drop execute's +1
	}
	catch (RuntimeError &e)
	{
		// An uncaught script error reaches the embedding boundary: release the live
		// register stack and the in-flight error value, keeping the message/line.
		st.isolate.unwind_on_error();
		if (e.error.is_cell())
			release(e.error.as_cell());
		e.error = Value::make_null();
		set_current_isolate(prev);
		set_current_collector(prev_cc);
		throw;
	}
	catch (...)
	{
		st.isolate.unwind_on_error();
		set_current_isolate(prev);
		set_current_collector(prev_cc);
		throw;
	}
	set_current_isolate(prev);
	set_current_collector(prev_cc);
	return out;
}

void Runtime::collect_garbage() { m_state->isolate.collector().collect(); }

intptr_t Runtime::gc_candidate_count() const
{
	return m_state->isolate.collector().candidate_count();
}

Variant do_string(const String &src)
{
	Runtime rt; // one-shot: a fresh session, discarded on return
	return rt.do_string(src);
}

std::string disassemble_source(const String &src)
{
	init_runtime();
	ModuleNamespace ns;
	CompiledModule cm;
	compile_source(src, ns, cm);
	return disassemble(*cm.main);
}

} // namespace phonometrica
