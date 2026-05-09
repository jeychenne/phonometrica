/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
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
	"PopHandler",
	"Power",
	"Precall",
	"Print",
	"PrintLine",
	"PushFalse",
	"PushFloat",
	"PushHandler",
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
	code[at] = serialize_short_int(value);
}

int Code::read_integer(const Instruction *&ip)
{
	return deserialize_short_int(*ip++);
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
	if (addr > std::numeric_limits<uint16_t>::max()) [[unlikely]] {
		throw error("[Compilation error] Jump offset too large");
	}
	append(line_no, jmp);
	auto offset = get_current_offset();
	append(line_no, serialize_short_int(addr));

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
