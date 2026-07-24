// Phonometrica engine — opcode mnemonics.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/vm/opcode.hpp>

namespace phonometrica {

const char *opcode_name(Opcode op) noexcept
{
	switch (op)
	{
#define PHON_OPCODE_CASE(name)                                                  \
	case Opcode::name:                                                          \
		return #name;
		PHON_OPCODE_LIST(PHON_OPCODE_CASE)
#undef PHON_OPCODE_CASE
	default:
		return "?";
	}
}

} // namespace phonometrica
