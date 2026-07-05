// Phonometrica engine — bytecode disassembler (architecture §9.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// The primary debugging tool: renders a Proto (and its nested prototypes) to a
// stable, human-readable listing. Milestone acceptance tests diff this output
// against golden files, so the format is deliberately fixed and position-stable
// (no addresses, no source columns — only ip indices, mnemonics, operands, and
// decoded constant/jump annotations).

#ifndef PHON_COMPILE_DISASSEMBLER_HPP
#define PHON_COMPILE_DISASSEMBLER_HPP

#include <string>

namespace phonometrica {

struct Proto;

// Full recursive disassembly of `proto` and every nested prototype.
std::string disassemble(const Proto &proto);

} // namespace phonometrica

#endif // PHON_COMPILE_DISASSEMBLER_HPP
