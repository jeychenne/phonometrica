// Phonometrica engine — lowering: AST -> bytecode (architecture §9.2).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A single pass over the AST that resolves lexical scope, allocates registers for
// locals, analyses upvalue captures for closures, folds a few constants, and emits
// specialized opcodes. The module top level and every function become a Proto; the
// module also gets a namespace (design §11): top-level var/const/function bindings
// are slot-indexed (GET_MODULE/SET_MODULE), function locals are registers.
//
// Semantic errors (unresolved names, features not yet implemented) throw
// SyntaxError with a `[…]` message and the offending node's position, so the
// embedding layer renders them exactly like scanner/parser errors.

#ifndef PHON_COMPILE_LOWER_HPP
#define PHON_COMPILE_LOWER_HPP

#include <phon/compile/ast.hpp>
#include <phon/core/flat_hash_map.hpp>
#include <phon/core/flat_hash_set.hpp>
#include <phon/core/vector.hpp>
#include <phon/vm/proto.hpp>

#include <memory>
#include <string>

namespace phonometrica {

// A module's persistent namespace: the name->slot map plus the slot count (design
// §11). A REPL/editor session keeps one of these across runs so that a binding
// declared in one chunk resolves to the same slot in the next; new declarations
// append slots and existing indices never move.
//
// With multiple modules (design §11 imports) the slot indices are **session-global**:
// every module draws its slots from one shared allocator, so `M.x` from an importer is
// just a GET_MODULE of x's slot in the isolate's single, shared slot vector. `exported`
// records which top-level names are public (everything not declared `local`), so an
// importer can reach `M.x` but not `M`'s privates.
struct ModuleNamespace
{
	FlatHashMap<uint32_t, int> name_to_slot; // Symbol id -> session-global slot index
	FlatHashSet<uint32_t> exported;          // Symbol ids visible to importers
	int num_slots = 0;                       // high-water slot count (session-global)
};

// The abstract seam the lowerer uses to pull in `import`ed modules during compilation:
// the Runtime implements it (resolve the file, compile it once, cache the result) and
// hands out the session-global module slots so bindings never collide across modules.
struct LoadedModule;
struct ModuleLoader
{
	virtual ~ModuleLoader() = default;

	// Resolve + compile (once, cached) the module named `name` as seen from the importing
	// file's directory `from_dir`. Returns the cached module. Throws SyntaxError if the
	// module cannot be found or fails to compile.
	virtual LoadedModule *load(Symbol name, const std::string &from_dir) = 0;

	// Hand out the next session-global module slot (shared across all modules).
	virtual int alloc_slot() = 0;
};

// The result of compiling one chunk: the top-level Proto (owns its nested
// prototypes). `num_slots` is the total namespace size after this chunk (so the
// Isolate can size/grow its module-slot vector to match).
struct CompiledModule
{
	std::unique_ptr<Proto> main;
	int num_slots = 0;
};

// A module the loader has compiled and cached: its namespace (public vars/consts/classes
// as session-global slots), the public generic-function names it contributes (so an
// importer can call them bare — functions live in the shared dispatch table, design §6),
// its top-level Proto, and whether that top-level has run yet this session.
struct LoadedModule
{
	std::string path;                // canonical path (the cache key)
	std::string dir;                 // directory, for resolving this module's own imports
	ModuleNamespace ns;           // vars/consts/classes: name -> session-global slot
	Vector<uint32_t> functions;   // public function/method names (global generics)
	std::unique_ptr<Proto> main;  // top-level code
	bool has_run = false;         // its top-level statements have executed
};

// Compile a parsed chunk (the top-level StatementList from Parser::parse()) against
// the persistent namespace `ns`, appending any new bindings to it. `loader` (may be null
// for one-shot compiles that forbid `import`) resolves imported modules and allocates the
// session-global slots; `dir` is the importing file's directory. On return, `imports`
// lists the modules this chunk imported (dependency order) and `public_functions` the
// non-`local` top-level function/method names it defines.
struct CompileEnv
{
	ModuleLoader *loader = nullptr;
	std::string dir;                    // importing module's directory ("" for <string>)
	Vector<LoadedModule *> imports;     // out: modules imported (in encounter order)
	Vector<uint32_t> public_functions;  // out: this module's public generic names
};

void compile_module(Ast *module_ast, ModuleNamespace &ns, CompiledModule &out,
                    CompileEnv *env = nullptr);

// Convenience for one-shot compilation against a fresh (throwaway) namespace.
void compile_module(Ast *module_ast, CompiledModule &out);

} // namespace phonometrica

#endif // PHON_COMPILE_LOWER_HPP
