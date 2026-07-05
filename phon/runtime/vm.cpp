// Phonometrica engine — script execution façade + builtin library (M4).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/runtime/vm.hpp>

#include <phon/compile/disassembler.hpp>
#include <phon/compile/lower.hpp>
#include <phon/compile/parser.hpp>
#include <phon/compile/source.hpp>
#include <phon/core/handle.hpp>
#include <phon/core/small_vector.hpp>
#include <phon/dispatch/generic.hpp>
#include <phon/object/class.hpp>
#include <phon/runtime/bootstrap.hpp>
#include <phon/types/atom.hpp>
#include <phon/types/list.hpp>
#include <phon/types/set.hpp>
#include <phon/types/string.hpp>
#include <phon/types/table.hpp>
#include <phon/vm/function.hpp>
#include <phon/vm/interpreter.hpp>

#include <cstdio>
#include <vector>

namespace phonometrica {

namespace {

// --- builtin natives ----------------------------------------------------------

Value builtin_print(Isolate &, Value *args, int argc)
{
	String out;
	for (int i = 0; i < argc; ++i)
	{
		if (i > 0)
			out.append(" ");
		out.append(vm_to_string(args[i]));
	}
	out.append("\n");
	std::fwrite(out.data(), 1, static_cast<size_t>(out.size()), stdout);
	return Value::make_null();
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
			msg.append(vm_to_string(args[1]));
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

// Keep native cells alive for the process lifetime (they back builtin generics).
// A function-local static releases them at process exit (matching the class/atom
// registries), so leak checkers stay clean.
struct NativeKeepalive
{
	std::vector<NativeCell *> cells;
	~NativeKeepalive()
	{
		for (NativeCell *nf : cells)
			release(reinterpret_cast<Cell *>(nf));
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
}

bool g_booted = false;

// Compile `src` into a module; the returned CompiledModule owns the Proto tree.
void compile_source(const std::string &src, CompiledModule &cm)
{
	Source source = Source::from_string(src);
	Parser parser(source);
	AutoAst ast = parser.parse();
	compile_module(ast.get(), cm);
}

} // namespace

void vm_boot()
{
	if (g_booted)
		return;
	g_booted = true;
	bootstrap();
	vm_register_function_classes();
	register_builtins();
}

Variant do_string(const std::string &src)
{
	vm_boot();

	CompiledModule cm;
	compile_source(src, cm);

	Isolate iso;
	iso.module_slots = Vector<Variant>(cm.num_slots);

	Isolate *prev = current_isolate();
	set_current_isolate(&iso);

	// RAII owns the module closure (adopt the +1 from make_closure); it releases on
	// return or exception.
	Handle<ClosureCell> main = Handle<ClosureCell>::adopt(make_closure(cm.main.get()));
	Variant out;
	try
	{
		Value result = vm_execute(iso, main.get());
		out = Variant(result);         // retains
		if (result.is_cell())
			release(result.as_cell()); // drop vm_execute's +1
	}
	catch (...)
	{
		set_current_isolate(prev);
		throw;
	}
	set_current_isolate(prev);
	return out;
}

std::string disassemble_source(const std::string &src)
{
	vm_boot();
	CompiledModule cm;
	compile_source(src, cm);
	return disassemble(*cm.main);
}

} // namespace phonometrica
