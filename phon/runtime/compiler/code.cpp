/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 24/05/2020                                                                                                 *
 *                                                                                                                     *
 * purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <limits>
#include "phon/error.hpp"
#include "code.hpp"

namespace phonometrica {

const char *opcode_names[] = {
	"Assert",
	"Add",
	"Call",
	"ClearLocal",
	"Compare",
	"Concat",
	"DecrementLocal",
	"DefineLocal",
	"GetField",
	"GetFieldArg",
	"GetFieldRef",
	"GetGlobal",
	"GetGlobalArg",
	"GetGlobalRef",
	"GetIndex",
	"GetIndexArg",
	"GetIndexRef",
	"GetLocal",
	"GetLocalArg",
	"GetLocalRef",
	"GetUniqueGlobal",
	"GetUniqueLocal",
	"GetUniqueUpvalue",
	"GetUpvalue",
	"GetUpvalueArg",
	"GetUpvalueRef",
	"Divide",
	"Equal",
	"Greater",
	"GreaterEqual",
	"IncrementLocal",
	"Jump",
	"JumpFalse",
	"JumpFalseAnd",
	"JumpTrue",
	"JumpTrueOr",
	"Less",
	"LessEqual",
	"Modulus",
	"Multiply",
	"Negate",
	"NewArray",
	"NewClass",
	"NewClosure",
	"NewField",
	"NewFrame",
	"NewInstance",
	"NewIterator",
	"NewList",
	"NewMethod",
	"NewSet",
	"NewTable",
	"NextKey",
	"NextValue",
	"Not",
	"NotEqual",
	"Pop",
	"Power",
	"Precall",
	"Print",
	"PrintLine",
	"PushFalse",
	"PushFloat",
	"PushInteger",
	"PushNan",
	"PushNull",
	"PushSmallInt",
	"PushString",
	"PushTrue",
	"Return",
	"SetField",
	"SetGlobal",
	"SetIndex",
	"SetLocal",
	"SetUpvalue",
	"Subtract",
	"TestIterator",
	"Throw"
};

void Code::add_line(intptr_t line_no)
{
	constexpr auto max_lines = (std::numeric_limits<uint16_t>::max)();

	if (unlikely(line_no > max_lines)) {
		throw error("Source file too long: a file can contain at most % lines", max_lines);
	}

	if (lines.empty() || lines.back().first != line_no)
	{
		lines.emplace_back(uint16_t(line_no), 1);
	}
	else
	{
		lines.back().second++;
	}
}

int Code::get_line(int offset) const
{
	int count = 0;

	for (auto ln : lines)
	{
		count += ln.second;

		if (offset < count) {
			return ln.first;
		}
	}

	throw error("[Internal error] Cannot determine line number: invalid offset %", offset);
}

void Code::append_return()
{
	intptr_t index = lines.empty() ? intptr_t(0) : intptr_t(lines.back().first);
	append(index, Opcode::Return);
}

void Code::backpatch(int at)
{
	backpatch(at, get_current_offset());
}

void Code::backpatch(int at, int value)
{
	if (value > std::numeric_limits<uint16_t>::max()) [[unlikely]] {
		throw error("[Compilation error] Jump offset too large");
	}
	auto offset = serialize_short_int(value);
	code[at] = offset[0];
	code[at + 1] = offset[1];
}

int Code::read_integer(const Instruction *&ip)
{
	std::array<Instruction,2> instr = { *ip++, *ip++ };
	return deserialize_short_int(instr);
}

int Code::append_jump(intptr_t line_no, Opcode jmp)
{
	return append_jump(line_no, jmp, 0);
}

void Code::backpatch_instruction(int at, Instruction value)
{
	code[at] = value;
}

int Code::append_jump(intptr_t line_no, Opcode jmp, int addr)
{
	append(line_no, jmp);
	auto a = serialize_short_int(addr);
	auto offset = get_current_offset();
	append(line_no, a[0]);
	append(line_no, a[1]);

	return offset;
}

const char *Code::get_opcode_name(Instruction op)
{
	return opcode_names[op];
}

bool Code::has_return() const
{
	return static_cast<Opcode>(code.back()) == Opcode::Return;
}

} // namespace phonometrica
