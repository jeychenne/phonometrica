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
#include <phon/vm/proto.hpp>

#include <memory>

namespace phonometrica {

// The result of compiling one module: the top-level Proto (owns its nested
// prototypes) and the size of the module namespace the Isolate must allocate.
struct CompiledModule
{
	std::unique_ptr<Proto> main;
	int num_slots = 0;
};

// Compile a parsed module (the top-level StatementList from Parser::parse()).
void compile_module(Ast *module_ast, CompiledModule &out);

} // namespace phonometrica

#endif // PHON_COMPILE_LOWER_HPP
