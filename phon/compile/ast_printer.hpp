// Phonometrica engine — AST pretty-printer (structural dump).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Renders an AST to a stable, readable, indented tree. This is the front end's
// primary debugging aid and the basis of the golden-dump regression tests
// (architecture §14). The dump is *structural*: it shows node kinds and their
// salient attributes (names, operators, literal values, flags) but not source
// positions, so it is robust to reformatting of the input while still pinning
// down the parse. Position handling is covered separately by the parser's
// error-position tests.

#ifndef PHON_COMPILE_AST_PRINTER_HPP
#define PHON_COMPILE_AST_PRINTER_HPP

#include <phon/compile/ast.hpp>

#include <string>

namespace phonometrica {

// Render `root` (typically the module StatementList from Parser::parse()) to a
// multi-line dump ending in a trailing newline.
std::string dump_ast(Ast *root);

} // namespace phonometrica

#endif // PHON_COMPILE_AST_PRINTER_HPP
