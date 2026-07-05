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
#include <phon/vm/proto.hpp>

#include <memory>

namespace phonometrica {

// A module's persistent namespace: the name->slot map plus the slot count (design
// §11). A REPL/editor session keeps one of these across runs so that a binding
// declared in one chunk resolves to the same slot in the next; new declarations
// append slots and existing indices never move.
struct ModuleNamespace
{
	FlatHashMap<uint32_t, int> name_to_slot; // Symbol id -> slot index
	int num_slots = 0;
};

// The result of compiling one chunk: the top-level Proto (owns its nested
// prototypes). `num_slots` is the total namespace size after this chunk (so the
// Isolate can size/grow its module-slot vector to match).
struct CompiledModule
{
	std::unique_ptr<Proto> main;
	int num_slots = 0;
};

// Compile a parsed chunk (the top-level StatementList from Parser::parse()) against
// the persistent namespace `ns`, appending any new bindings to it.
void compile_module(Ast *module_ast, ModuleNamespace &ns, CompiledModule &out);

// Convenience for one-shot compilation against a fresh (throwaway) namespace.
void compile_module(Ast *module_ast, CompiledModule &out);

} // namespace phonometrica

#endif // PHON_COMPILE_LOWER_HPP
