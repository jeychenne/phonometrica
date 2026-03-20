/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 24/05/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: bytecode object, which represents a chunk of compiled code.                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CODE_HPP
#define PHONOMETRICA_CODE_HPP

#include <algorithm>
#include <utility>
#include <vector>
#include "phon/runtime/definitions.hpp"

namespace phonometrica {

using Instruction = uint8_t;



enum class Opcode : Instruction
{
	Assert,
	Add,
	Call,
	ClearLocal,
	Compare,
	Concat,
	DecrementLocal,
	DefineLocal,
	GetField,			// Get field by value
	GetFieldArg,		// Get field by value or by reference
	GetFieldRef,		// Get field by reference
	GetGlobal,			// Get global by value
	GetGlobalArg,		// Get global either by value or by reference
	GetGlobalRef,		// Get global by reference
	GetIndex,			// Get index by value
	GetIndexArg,		// Get index by value or by reference
	GetIndexRef,		// Get index by reference
	GetLocal,			// Get global by value
	GetLocalArg,		// Get global either by value or by reference
	GetLocalRef,		// Get global by reference
	GetUniqueGlobal,	// Unshare and push a global
	GetUniqueLocal,		// Unshare and push a local
	GetUniqueUpvalue,	// Unshare and push a non-local variable
	GetUpvalue,			// Get non-local variable by value
	GetUpvalueArg,		// Get non-local variable by value or by reference
	GetUpvalueRef,		// Get non-local variable by reference
	Divide,
	Equal,
	Greater,
	GreaterEqual,
	IncrementLocal,
	Jump,
	JumpFalse,
	JumpFalseAnd,
	JumpTrue,
	JumpTrueOr,
	Less,
	LessEqual,
	Modulus,
	Multiply,
	Negate,
	NewArray,
	NewClass,
	NewClosure,
	NewField,
	NewFrame,
	NewInstance,
	NewIterator,
	NewList,
	NewMethod,
	NewSet,
	NewTable,
	NextKey,
	NextValue,
	Not,
	NotEqual,
	Pop,
	Power,
	Precall,
	Print,
	PrintLine,
	PushFalse,
	PushFloat,
	PushInteger,
	PushNan,
	PushNull,
	PushSmallInt,
	PushString,
	PushTrue,
	Return,
	SetField,
	SetGlobal,
	SetIndex,
	SetLocal,
	SetUpvalue,
	Subtract,
	TestIterator,
	Throw
};


class Code final
{
	using Storage = std::vector<Instruction>;

	// For error reporting.
	using LineNo = uint16_t;

public:

	static constexpr int IntSize = sizeof(uint16_t) / sizeof(Instruction);

	Code() = default;

	Code(const Code &) = delete;

	Code(Code &&) = default;

	~Code() = default;

	void append(intptr_t line_no, Instruction i) { add_line(line_no); code.push_back(i); }

	void append(intptr_t line_no, Opcode op) { append(line_no, static_cast<Instruction>(op)); }

	void append(intptr_t line_no, Opcode op, Instruction i) { append(line_no, op); append(line_no, i); }

	void append(intptr_t line_no, Opcode op, Instruction i1, Instruction i2) { append(line_no, op); append(line_no, i1); append(line_no, i2); }

	static int read_integer(const Instruction *&ip);

	void append_return();

	bool has_return() const;

	const Instruction *data() const { return code.data(); }

	const Instruction *end() const { return code.data() + code.size(); }

	const Instruction &operator[](size_t i) const { return code[i]; }

	size_t size() const { return code.size(); }

	int get_line(int offset) const;

	void backpatch_instruction(int at, Instruction value);

	void backpatch(int at);

	void backpatch(int at, int value);

	int append_jump(intptr_t line_no, Opcode jmp);

	int append_jump(intptr_t line_no, Opcode jmp, int addr);

	int get_current_offset() const { return int(code.size()); }

	static const char *get_opcode_name(Instruction op);

private:

	// Big endian serialization
	static std::array<uint8_t, 2> serialize_short_int(int value)
	{
		auto v = uint16_t(value);
		return {
				static_cast<uint8_t>((v >> 8) & 0xFF), // high byte
				static_cast<uint8_t>(v & 0xFF)         // low byte
		};
	}

	static uint16_t deserialize_short_int(const std::array<uint8_t, 2> bytes) {
		return (static_cast<uint16_t>(bytes[0]) << 8) |
			   static_cast<uint16_t>(bytes[1]);
	}


	void add_line(intptr_t line_no);

	// Byte codes.
	Storage code;

	// Line numbers on which byte codes are found, for error reporting.
	// first = line number; second = number of instructions on that line
	std::vector<std::pair<LineNo,LineNo>> lines;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CODE_HPP
