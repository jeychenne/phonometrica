// Phonometrica engine — script execution façade + builtin library (M4).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/runtime/runtime.hpp>

#include <phon/engine/compile/disassembler.hpp>
#include <phon/engine/concurrency/channel.hpp>
#include <phon/engine/concurrency/spawn.hpp>
#include <phon/engine/compile/lower.hpp>
#include <phon/engine/compile/parser.hpp>
#include <phon/engine/compile/source.hpp>
#include <phon/engine/core/handle.hpp>
#include <phon/engine/core/small_vector.hpp>
#include <phon/engine/core/vector.hpp>
#include <phon/engine/object/generic.hpp>
#include <phon/engine/lib/lib.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/object/instance.hpp>
#include <phon/engine/runtime/bootstrap.hpp>
#include <phon/engine/runtime/native_traits.hpp>
#include <phon/engine/types/array.hpp>
#include <phon/engine/types/atom.hpp>
#include <phon/engine/types/list.hpp>
#include <phon/engine/types/set.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/types/table.hpp>
#include <phon/engine/vm/function.hpp>
#include <phon/engine/vm/interpreter.hpp>

#include <phon/engine/compile/diagnostic.hpp>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace phonometrica {

namespace {

// --- builtin natives ----------------------------------------------------------

Value builtin_print(Isolate &iso, NativeCell *, Value *args, int argc)
{
	String out;
	for (int i = 0; i < argc; ++i)
	{
		if (i > 0)
			out.append(" ");
		out.append(stringify(iso, args[i]));
	}
	out.append("\n");
	iso.write_output(std::string_view(out.data(), static_cast<size_t>(out.size())));
	return Value::make_null();
}

Value builtin_to_string(Isolate &iso, NativeCell *, Value *args, int argc)
{
	// The builtin `to_string` methods (one per arity-1 call) stringify any value the
	// way `&`/print do; a user `method to_string()` overrides for its own class.
	(void) argc;
	// A native returns a value carrying +1. `stringify` yields a temporary String, so
	// retain its cell before the temporary drops (otherwise a *freshly built* string —
	// from a NumArray/List/Table — would be freed out from under the returned value).
	String s = stringify(iso, args[0]);
	Value v = s.to_value();
	retain(v.as_cell());
	return v;
}

Value builtin_assert(Isolate &iso, NativeCell *, Value *args, int argc)
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

Value builtin_len(Isolate &iso, NativeCell *, Value *args, int argc)
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
		case CID_ARRAY: return Value::make_int(NumArray::from_value(v).size());
		default: break;
		}
	}
	iso.raise(String("[Type error] 'len' expects a List, String, Table, or Set"), 0);
}

Value builtin_cast(Isolate &iso, NativeCell *, Value *args, int argc)
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

Value error_init(Isolate &, NativeCell *, Value *args, int argc)
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

Value builtin_freeze(Isolate &iso, NativeCell *, Value *args, int argc)
{
	(void) iso;
	(void) argc;
	// freeze(x): make x an immutable, cross-thread-shareable value (§8.3). Strings and
	// NumArray buffers flip to the frozen/shared regime (zero-copy on send); other values
	// are copied on send anyway, so freezing them is a harmless no-op. Returns x.
	Value v = args[0];
	if (v.is_cell())
	{
		switch (class_of(v))
		{
		case CID_STRING: String::from_value(v).make_frozen(); break;
		case CID_ARRAY: NumArray::from_value(v).make_frozen(); break;
		default: break;
		}
		retain(v.as_cell()); // native returns a value carrying +1
	}
	return v;
}

Value builtin_collect_garbage(Isolate &iso, NativeCell *, Value *args, int argc)
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
		add_method(g, sig, 0, false, nf);
	}
}

} // namespace

// The typed registration front end's installation core (native_traits.hpp): install a
// generated thunk as one method on the generic `name`, dispatched on `sig` (the C++
// parameter classes). Unlike register_native (Object-typed, arity-ranged), this adds a
// single precisely-typed overload — C++ registration and script method definition go
// through the same add_method path.
void register_typed_native(const char *name, NativeFn fn, Cell *env,
                           const SmallVector<Class *, 4> &sig, int arity, uint64_t ref_mask)
{
	Symbol s = intern(name);
	GenericFunction *g = get_or_create_generic(s);
	NativeCell *nf = make_native(fn, s, arity, arity, env); // adopts env's +1
	native_keepalive().cells.push_back(nf);                 // retains via make_native's +1
	AddMethod res = add_method(g, sig, ref_mask, false, nf);
	if (res == AddMethod::RefMaskConflict)
		throw std::runtime_error(std::string("[Registration error] method '") + name +
		                         "' has `ref` parameters that disagree with an existing overload");
	if (res != AddMethod::Ok)
		throw std::runtime_error(std::string("[Registration error] method '") + name +
		                         "' is ambiguous against an existing overload");
}

// Install a read-only field on a registered foreign class (add_field<T>): the getter
// thunk becomes a keepalive-owned NativeCell that GETFIELD invokes with the object as
// its single argument. Duplicate names are an embedding bug, surfaced eagerly.
void register_foreign_field(Class *cls, const char *name, NativeFn fn, Cell *env)
{
	PHON_ASSERT_MSG(cls != nullptr, "register_foreign_field: null class");
	Symbol s = intern(name);
	if (find_foreign_field(cls, s))
	{
		// The env cell's +1 has not been adopted yet (make_native below) — release it
		// so the error path doesn't leak the captured callable.
		if (env)
			release(env);
		throw std::runtime_error(std::string("[Registration error] class '") + cls->name +
		                         "' already has a field '" + name + "'");
	}
	NativeCell *nf = make_native(fn, s, 1, 1, env); // adopts env's +1
	native_keepalive().cells.push_back(nf);         // retains via make_native's +1
	add_foreign_field(cls, s, reinterpret_cast<Cell *>(nf));
}

// Register a C++ type as a phon class (native_traits.hpp add_class<T>). Marked
// CLASS_BUILTIN so the compiler's name resolver finds it (usable in `is`/annotations and
// dispatch), which also makes it non-script-constructible — instances come from C++.
Class *register_foreign_class(const char *name, Class *base, bool is_reference,
                              intptr_t instance_size, FinalizeHook finalize, CloneHook clone,
                              TraceHook trace, GcFreeHook gc_free, bool acyclic)
{
	uint16_t flags = CLASS_BUILTIN | CLASS_FOREIGN | (is_reference ? CLASS_REF : CLASS_VALUE);
	if (acyclic)
		flags |= CLASS_ACYCLIC; // no traceable cells: born GREEN, never a cycle candidate
	Class *c = add_class(name, base, flags, instance_size);
	c->finalize = finalize; // ordinary refcount-0 free path (runs ~T)
	c->clone = clone;
	c->trace = trace;     // lets the collector see cells held inside the boxed value
	c->gc_free = gc_free; // cyclic-free path: non-cell cleanup with ~T bypassed (optional)
	return c;
}

namespace {

void register_builtins()
{
	register_native("print", builtin_print, 0, 8);
	register_native("len", builtin_len, 1, 1);
	register_native("assert", builtin_assert, 1, 2);
	// Load-bearing despite looking unreachable: `cast` is a reserved word, so no script
	// can call it by name. It is reached only from the compiler, which lowers
	// `cast x as T` to a CALLG of this global (Lowerer::compile_cast, lower.cpp).
	register_native("cast", builtin_cast, 2, 2);
	register_native("to_string", builtin_to_string, 1, 1);
	register_native("collect_garbage", builtin_collect_garbage, 0, 0);
	register_native("freeze", builtin_freeze, 1, 1);
	register_native("Channel", builtin_channel, 0, 1);
	register_native("send", builtin_send, 2, 2);
	register_native("receive", builtin_receive, 1, 1);
	register_native("wait", builtin_wait, 1, 1);

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
		add_method(g, sig, 0, false, nf);
	}
}

std::once_flag g_init_once;

// Parse an already-built Source and compile it against `ns` (the persistent
// namespace). `Source` is the compiler-layer text buffer: it holds the script as a
// std::string because it reads files via stdio and scans with std::string_view, so
// engine Strings are converted once at this boundary (cold path).
void compile_source_parsed(Source source, ModuleNamespace &ns, CompiledModule &cm,
                           CompileEnv *env = nullptr)
{
	Parser parser(source);
	AutoAst ast = parser.parse();
	compile_module(ast.get(), ns, cm, env);
}

void compile_source(const String &src, ModuleNamespace &ns, CompiledModule &cm,
                    CompileEnv *env = nullptr)
{
	Source source = Source::from_string(std::string(src.data(), static_cast<size_t>(src.size())));
	compile_source_parsed(std::move(source), ns, cm, env);
}

// Stamp the source file over a compiled Proto tree, so error traces/frames can name
// the file each frame belongs to.
void stamp_source_path(Proto *p, const std::string &path)
{
	p->source_path = path;
	for (auto &child : p->children)
		stamp_source_path(child.get(), path);
}

// The session's module loader (design §11): resolves `import M` to a file, compiles it
// once, caches it, and hands out session-global slots. Modules are compiled here; their
// top-level code is *run* by do_string (which owns the Isolate) before the main chunk.
namespace fs = std::filesystem;

class ModuleManager final : public ModuleLoader
{
public:
	void add_import_path(const std::string &dir) { m_paths.push_back(dir); }
	void remove_import_path(const std::string &dir)
	{
		m_paths.erase(std::remove(m_paths.begin(), m_paths.end(), dir), m_paths.end());
	}

	int alloc_slot() override { return m_next_slot++; }
	int total_slots() const noexcept { return m_next_slot; }
	const std::vector<LoadedModule *> &load_order() const noexcept { return m_order; }

	// The isolate-global namespace (design §11): session-wide name → shared-slot map,
	// fed by `global var` declarations and Runtime::add_global.
	int find_global(Symbol name) override
	{
		auto it = m_globals.find(name.id);
		return it == m_globals.end() ? -1 : it->second;
	}
	int declare_global(Symbol name) override
	{
		auto it = m_globals.find(name.id);
		if (it != m_globals.end())
			return it->second;
		int slot = alloc_slot();
		m_globals[name.id] = slot;
		return slot;
	}

	LoadedModule *load(Symbol name, const std::string &from_dir) override
	{
		std::string path = resolve(symbol_name(name), from_dir);
		if (path.empty())
			return nullptr; // compile_import reports "module not found" at the import node

		auto it = m_cache.find(path);
		if (it != m_cache.end())
		{
			if (it->second == nullptr) // still compiling: a cyclic import
				throw SyntaxError("[Compile error] import cycle through module '" +
				                      std::string(symbol_name(name)) + "'",
				                  0, 0, 1);
			return it->second;
		}

		m_cache[path] = nullptr; // in-progress marker (cycle guard)
		Source source = Source::from_file(path);
		Parser parser(source);
		AutoAst ast = parser.parse();

		auto lm = std::make_unique<LoadedModule>();
		lm->path = path;
		lm->dir = fs::path(path).parent_path().string();
		CompileEnv env;
		env.loader = this;
		env.dir = lm->dir;
		CompiledModule cm;
		compile_module(ast.get(), lm->ns, cm, &env); // recursively loads this module's imports
		lm->functions = std::move(env.public_functions);
		for (intptr_t i = 0; i < env.public_classes.size(); ++i)
			lm->classes.insert(env.public_classes[i]);
		lm->main = std::move(cm.main);
		stamp_source_path(lm->main.get(), path);

		LoadedModule *raw = lm.get();
		m_cache[path] = raw;             // replace the in-progress marker
		m_owned.push_back(std::move(lm));
		m_order.push_back(raw);          // post-order: this module's deps are already listed
		return raw;
	}

private:
	std::string resolve(std::string_view name, const std::string &from_dir) const
	{
		std::vector<std::string> dirs;
		if (!from_dir.empty())
			dirs.push_back(from_dir);
		for (const auto &p : m_paths)
			dirs.push_back(p);
		if (const char *env = std::getenv("PHON_MODULE_PATH"))
		{
			std::string s(env), cur;
			for (char c : s)
			{
				if (c == ':')
				{
					if (!cur.empty())
						dirs.push_back(cur);
					cur.clear();
				}
				else
					cur += c;
			}
			if (!cur.empty())
				dirs.push_back(cur);
		}
		for (const auto &d : dirs)
		{
			fs::path base(d);
			fs::path single = base / (std::string(name) + ".phon");
			if (fs::is_regular_file(single))
				return fs::weakly_canonical(single).string();
			fs::path dirmod = base / std::string(name) / "initialize.phon";
			if (fs::is_regular_file(dirmod))
				return fs::weakly_canonical(dirmod).string();
		}
		return "";
	}

	std::unordered_map<std::string, LoadedModule *> m_cache; // path -> module (null=compiling)
	std::vector<std::unique_ptr<LoadedModule>> m_owned;
	std::vector<LoadedModule *> m_order; // dependency order (deps before dependents)
	std::vector<std::string> m_paths;
	std::unordered_map<uint32_t, int> m_globals; // isolate globals: symbol id -> slot
	int m_next_slot = 0;
};

// Keeps Isolate::script_paths balanced around one chunk/module top-level execution,
// so get_script_path() reports the file whose code is currently running.
struct ScriptPathScope
{
	Isolate &iso;
	ScriptPathScope(Isolate &i, const std::string &p) : iso(i) { iso.script_paths.push_back(p); }
	~ScriptPathScope() { iso.script_paths.pop_back(); }
};

} // namespace

void init_runtime()
{
	// Idempotent and thread-safe: multiple Runtimes (even on different threads) share
	// one process-global registry, atom table, and builtin generics, initialized once.
	std::call_once(g_init_once, [] {
		bootstrap();
		register_function_classes();
		register_channel_class();
		register_thread_class();
		register_builtins();
		register_math_lib();
		register_string_lib();
		register_list_lib();
		register_table_lib();
		register_array_lib();
		register_system_lib();
		register_file_lib();
		register_regex_lib();
		register_json_lib();
	});
}

// The session's mutable state: the long-lived Isolate, the persistent module
// namespace, and the compiled-chunk history that keeps every Proto tree alive for
// closures stored in module slots across chunks (a growing session cost; the
// registration journal that unloads them is design §11, deferred to M5).
//
// Declaration order is load-bearing for teardown. Members destruct in reverse, so
// `isolate` (declared last) is destroyed FIRST: ~Isolate joins every outstanding
// spawned worker before anything else is freed (structured concurrency, architecture
// §13). A worker can still be executing bytecode over a Proto owned by `history` right
// up until it is joined, so `history` MUST outlive the Isolate — declare it first so it
// is torn down LAST. Reordering these (e.g. putting `history` last) resurrects a
// shutdown data race where the Proto trees are freed while a worker still reads them.
struct Runtime::State
{
	Vector<std::unique_ptr<CompiledModule>> history;
	ModuleNamespace shell;
	ModuleManager modules;
	bool interactive = false;
	Isolate isolate;

	// Compile `source` against the persistent namespace and run it: the common body
	// behind do_string (`dir`/`path` empty) and do_file (the file's directory feeds
	// import resolution; its path feeds get_script_path()).
	Variant run(Source source, const std::string &dir, const std::string &path);
};

Runtime::Runtime() : m_state(std::make_unique<State>()) { init_runtime(); }
Runtime::~Runtime() = default;

Variant Runtime::do_string(const String &code)
{
	Source source =
	    Source::from_string(std::string(code.data(), static_cast<size_t>(code.size())));
	return m_state->run(std::move(source), "", "");
}

Variant Runtime::do_string(const String &code, const String &path)
{
	if (path.empty())
		return do_string(code);
	std::string p(path.data(), static_cast<size_t>(path.size()));
	Source source =
	    Source::from_string(std::string(code.data(), static_cast<size_t>(code.size())), p);
	return m_state->run(std::move(source), fs::path(p).parent_path().string(), p);
}

std::string Runtime::disassemble(const String &code)
{
	State &st = *m_state;
	Source source =
	    Source::from_string(std::string(code.data(), static_cast<size_t>(code.size())));
	auto cm = std::make_unique<CompiledModule>();
	CompileEnv env;
	env.loader = &st.modules;
	env.dir = "";
	env.interactive = st.interactive;
	compile_source_parsed(std::move(source), st.shell, *cm, &env);
	return phonometrica::disassemble(*cm->main);
}

Variant Runtime::do_file(const String &path)
{
	std::string p(path.data(), static_cast<size_t>(path.size()));
	std::error_code ec;
	fs::path canon = fs::weakly_canonical(fs::path(p), ec);
	std::string cpath = (ec || canon.empty()) ? p : canon.string();
	Source source = Source::from_file(cpath); // throws std::runtime_error if unreadable
	return m_state->run(std::move(source), fs::path(cpath).parent_path().string(), cpath);
}

void Runtime::set_interactive(bool on) noexcept
{
	m_state->interactive = on;
}

void Runtime::add_global(const char *name, const Variant &value)
{
	State &st = *m_state;
	int slot = st.modules.declare_global(intern(name));
	if (st.isolate.module_slots.size() <= slot)
		st.isolate.module_slots.resize(slot + 1);
	st.isolate.module_slots[slot] = value;
}

Variant Runtime::State::run(Source source, const std::string &dir, const std::string &path)
{
	State &st = *this;

	auto cm = std::make_unique<CompiledModule>();
	CompileEnv env;
	env.loader = &st.modules; // resolves `import`, allocates session-global slots
	env.dir = dir;            // "" for a <string> chunk: no file directory
	env.interactive = st.interactive;
	compile_source_parsed(std::move(source), st.shell, *cm, &env);

	// Grow the Isolate's module-slot vector to cover every module's session-global slots
	// (design §11: one shared vector; indices never move, so growth preserves values).
	int total = st.modules.total_slots();
	if (total < st.shell.num_slots)
		total = st.shell.num_slots;
	if (st.isolate.module_slots.size() < total)
		st.isolate.module_slots.resize(total);

	// Keep the Proto tree alive: module-slot closures created by this chunk outlive
	// the call and borrow their Proto.
	Proto *main_proto = cm->main.get();
	if (!path.empty())
		stamp_source_path(main_proto, path);
	st.history.push_back(std::move(cm));

	Isolate *prev = current_isolate();
	CycleCollector *prev_cc = current_collector();
	set_current_isolate(&st.isolate);
	set_current_collector(&st.isolate.collector());
	st.isolate.clear_interrupt(); // a stale request only affects the run it targeted

	Handle<ClosureCell> main = Handle<ClosureCell>::adopt(make_closure(main_proto));
	Variant out;
	try
	{
		// Run each imported module's top-level once, in dependency order, before the main
		// chunk — so their functions/classes are registered and their state initialized.
		for (LoadedModule *lm : st.modules.load_order())
		{
			if (lm->has_run)
				continue;
			Handle<ClosureCell> mc = Handle<ClosureCell>::adopt(make_closure(lm->main.get()));
			ScriptPathScope mscope(st.isolate, lm->path);
			Value r = execute(st.isolate, mc.get());
			if (r.is_cell())
				release(r.as_cell());
			lm->has_run = true;
		}

		ScriptPathScope scope(st.isolate, path);
		Value result = execute(st.isolate, main.get());
		out = Variant(result);         // retains
		if (result.is_cell())
			release(result.as_cell()); // drop execute's +1
	}
	catch (RuntimeError &e)
	{
		// An uncaught script error reaches the embedding boundary: release the live
		// register stack and the in-flight error value, keeping the message/line —
		// but first copy the error's structured backtrace into plain host data so
		// the embedder can render a trace (GUI console, script editor).
		if (e.frames.empty())
			e.frames = extract_error_frames(e.error);
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

void Runtime::add_import_path(const String &dir)
{
	m_state->modules.add_import_path(std::string(dir.data(), static_cast<size_t>(dir.size())));
}

void Runtime::remove_import_path(const String &dir)
{
	m_state->modules.remove_import_path(std::string(dir.data(), static_cast<size_t>(dir.size())));
}

void Runtime::request_interrupt() noexcept { m_state->isolate.request_interrupt(); }

void Runtime::collect_garbage() { m_state->isolate.collector().collect(); }

intptr_t Runtime::gc_candidate_count() const
{
	return m_state->isolate.collector().candidate_count();
}

// --- calling script functions from C++ (roadmap E1) ---------------------------

Variant Runtime::call(const Variant &fn, const Variant *args, int nargs)
{
	Isolate &iso = m_state->isolate;
	Value callee = fn.value();
	if (!is_closure(callee) && !is_native(callee))
	{
		// A guard at the embedding boundary (not a script-raised error): carry the
		// message only, with a null error value, so a catcher that inspects just
		// e.message needs to release nothing.
		throw RuntimeError{String("[Type error] value is not callable"), 0, Value::make_null()};
	}

	// Stage the positional arguments as raw Values (borrowed; the call retains as needed).
	std::vector<Value> argv;
	argv.reserve(static_cast<size_t>(nargs));
	for (int i = 0; i < nargs; ++i)
		argv.push_back(args[i].value());

	// If a script is already running on this session, re-enter it (vm_call stages above
	// the live frame); otherwise we own the run and drive it from the empty stack. The
	// isolate/collector context and interrupt reset are ours to manage only in the
	// latter case — touching them mid-run would disturb the in-flight script.
	const bool owns_run = iso.frames.size() == 0;
	Isolate *prev = nullptr;
	CycleCollector *prev_cc = nullptr;
	if (owns_run)
	{
		prev = current_isolate();
		prev_cc = current_collector();
		set_current_isolate(&iso);
		set_current_collector(&iso.collector());
		iso.clear_interrupt();
	}
	try
	{
		Value r = call_from_host(iso, callee, argv.data(), nargs);
		Variant out(r);
		if (r.is_cell())
			release(r.as_cell()); // drop the call's +1; `out` holds its own reference
		if (owns_run)
		{
			set_current_isolate(prev);
			set_current_collector(prev_cc);
		}
		return out;
	}
	catch (RuntimeError &e)
	{
		// When we own the run, clean up like State::run does (extract the structured
		// backtrace, then release the live stack and the in-flight error value, keeping
		// message/line) and restore the context. When re-entrant, leave everything for
		// the outer run()'s handler and just propagate.
		if (owns_run)
		{
			if (e.frames.empty())
				e.frames = extract_error_frames(e.error);
			iso.unwind_on_error();
			if (e.error.is_cell())
				release(e.error.as_cell());
			e.error = Value::make_null();
			set_current_isolate(prev);
			set_current_collector(prev_cc);
		}
		throw;
	}
	catch (...)
	{
		if (owns_run)
		{
			set_current_isolate(prev);
			set_current_collector(prev_cc);
		}
		throw;
	}
}

Variant Runtime::get_function(const char *name) const
{
	Symbol s = intern(name);
	GenericFunction *g = find_generic(s);
	if (!g || g->methods.size() == 0)
		return Variant(); // null: no such function
	return Variant(Value::make_cell(generic_function_value(s)));
}

Variant Runtime::get_global(const char *name) const
{
	State &st = *m_state;
	int slot = st.modules.find_global(intern(name));
	if (slot < 0 || static_cast<intptr_t>(slot) >= st.isolate.module_slots.size())
		return Variant(); // null: not a global
	return st.isolate.module_slots[slot];
}

// --- output redirection (roadmap E3) ------------------------------------------

void Runtime::set_output_hook(std::function<void(std::string_view)> hook)
{
	m_state->isolate.output_hook = std::move(hook);
}

void Runtime::set_error_output_hook(std::function<void(std::string_view)> hook)
{
	m_state->isolate.error_output_hook = std::move(hook);
}

void Runtime::set_clear_output_hook(std::function<void()> hook)
{
	m_state->isolate.clear_output_hook = std::move(hook);
}

std::function<void(std::string_view)> Runtime::output_hook() const
{
	return m_state->isolate.output_hook;
}

std::function<void(std::string_view)> Runtime::error_output_hook() const
{
	return m_state->isolate.error_output_hook;
}

std::function<void()> Runtime::clear_output_hook() const
{
	return m_state->isolate.clear_output_hook;
}

void Runtime::print(std::string_view text) { m_state->isolate.write_output(text); }

void Runtime::printf(const char *fmt, ...)
{
	char stack_buf[1024];
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int n = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
	va_end(ap);
	if (n < 0)
	{
		va_end(ap2);
		return;
	}
	if (static_cast<size_t>(n) < sizeof(stack_buf))
	{
		va_end(ap2);
		print(std::string_view(stack_buf, static_cast<size_t>(n)));
		return;
	}
	std::string big(static_cast<size_t>(n), '\0');
	std::vsnprintf(big.data(), big.size() + 1, fmt, ap2);
	va_end(ap2);
	print(big);
}
void Runtime::print_error(std::string_view text) { m_state->isolate.write_error_output(text); }
void Runtime::clear_output() { m_state->isolate.clear_output(); }

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
