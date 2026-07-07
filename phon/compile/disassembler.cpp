// Phonometrica engine — bytecode disassembler implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/compile/disassembler.hpp>

#include <phon/object/class.hpp>
#include <phon/types/atom.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/opcode.hpp>
#include <phon/vm/proto.hpp>

#include <cinttypes>
#include <cstdio>

namespace phonometrica {

namespace {

enum class Form
{
	ABC,
	ABx,
	AsBx,
	Ax
};

Form form_of(Opcode op)
{
	switch (op)
	{
	case Opcode::LOADK:
	case Opcode::GETMODULE:
	case Opcode::SETMODULE:
	case Opcode::PROMOTEMODULE:
	case Opcode::CLOSURE:
	case Opcode::DEFMETHOD:
	case Opcode::DEFCLASS:
		return Form::ABx;
	case Opcode::LOADI:
	case Opcode::JMP:
	case Opcode::JMPF:
	case Opcode::JMPT:
	case Opcode::JMPSET:
	case Opcode::FORPREP:
	case Opcode::FORLOOP:
	case Opcode::PUSHTRY:
		return Form::AsBx;
	case Opcode::EXTRA_ARG:
		return Form::Ax;
	default:
		return Form::ABC;
	}
}

// Render a constant-pool Value for the annotation column.
std::string render_const(Value v)
{
	char buf[64];
	if (v.is_null())
		return "null";
	if (v.is_true())
		return "true";
	if (v.is_false())
		return "false";
	if (v.is_int())
	{
		std::snprintf(buf, sizeof buf, "%" PRId64, v.as_int());
		return buf;
	}
	if (v.is_double())
	{
		std::snprintf(buf, sizeof buf, "%g", v.as_double());
		return buf;
	}
	if (v.is_symbol())
	{
		std::string s = ":";
		s += std::string(symbol_name(v.as_symbol()));
		return s;
	}
	if (v.is_cell() && class_of(v) == CID_STRING)
	{
		String s = String::from_value(v);
		std::string out = "\"";
		out.append(s.data(), static_cast<size_t>(s.size()));
		out += "\"";
		return out;
	}
	return "<value>";
}

void append_line(std::string &out, intptr_t ip, const char *mnemonic, const std::string &operands,
                 const std::string &annotation)
{
	char head[32];
	std::snprintf(head, sizeof head, "%04td  ", ip);
	out += head;
	// Mnemonic padded to a fixed width for column alignment.
	std::string m = mnemonic;
	while (m.size() < 11)
		m += ' ';
	out += m;
	out += operands;
	if (!annotation.empty())
	{
		// Pad operands to a fixed column before the comment.
		std::string pad = operands;
		while (pad.size() < 16)
			pad += ' ';
		out.resize(out.size() - operands.size());
		out += pad;
		out += "; ";
		out += annotation;
	}
	out += '\n';
}

void disassemble_proto(const Proto &p, std::string &out, int index)
{
	char header[128];
	const char *name = (p.name == NO_SYMBOL) ? "<anon>" : nullptr;
	std::string title;
	if (name)
		title = name;
	else
		title = std::string(symbol_name(p.name));

	std::snprintf(header, sizeof header, "proto #%d %s  params=%d regs=%d upvals=%td consts=%td%s\n",
	              index, title.c_str(), p.num_params, p.num_regs, p.upvals.size(),
	              p.constants.size(), p.is_vararg ? " vararg" : "");
	out += header;

	for (intptr_t ip = 0; ip < p.code.size(); ++ip)
	{
		Instruction ins = p.code[ip];
		Opcode op = op_of(ins);
		const char *mn = opcode_name(op);
		Form form = form_of(op);
		char ops[64] = {0};
		std::string annotation;

		switch (form)
		{
		case Form::ABC:
			std::snprintf(ops, sizeof ops, "%d %d %d", op_a(ins), op_b(ins), op_c(ins));
			break;
		case Form::ABx:
			std::snprintf(ops, sizeof ops, "%d %u", op_a(ins), op_bx(ins));
			if (op == Opcode::LOADK && op_bx(ins) < static_cast<uint32_t>(p.constants.size()))
				annotation = render_const(p.constants[op_bx(ins)].value());
			else if (op == Opcode::DEFMETHOD && op_bx(ins) < static_cast<uint32_t>(p.method_defs.size()))
				annotation = std::string(symbol_name(p.method_defs[op_bx(ins)].name));
			else if (op == Opcode::DEFCLASS && op_bx(ins) < static_cast<uint32_t>(p.class_defs.size()))
				annotation = std::string(symbol_name(p.class_defs[op_bx(ins)].name));
			break;
		case Form::AsBx:
			std::snprintf(ops, sizeof ops, "%d %d", op_a(ins), op_sbx(ins));
			if (op == Opcode::JMP || op == Opcode::JMPF || op == Opcode::JMPT ||
			    op == Opcode::JMPSET || op == Opcode::FORPREP || op == Opcode::FORLOOP ||
			    op == Opcode::PUSHTRY)
			{
				char tgt[32];
				std::snprintf(tgt, sizeof tgt, "-> %04td", ip + 1 + op_sbx(ins));
				annotation = tgt;
			}
			break;
		case Form::Ax:
			std::snprintf(ops, sizeof ops, "%u", op_ax(ins));
			break;
		}
		append_line(out, ip, mn, ops, annotation);
	}
	out += '\n';

	// Recurse into nested prototypes.
	for (intptr_t i = 0; i < p.children.size(); ++i)
		disassemble_proto(*p.children[i], out, static_cast<int>(i));
}

} // namespace

std::string disassemble(const Proto &proto)
{
	std::string out;
	disassemble_proto(proto, out, 0);
	return out;
}

} // namespace phonometrica
