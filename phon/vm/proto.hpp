// Phonometrica engine — Proto: a compiled function prototype (the "Chunk" of §9.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// A Proto is the immutable output of lowering (compile/lower.cpp): bytecode plus
// everything needed to execute and debug it. Both the module top level and every
// nested function compile to a Proto; nested Protos are owned by their parent in a
// unique_ptr-free tree (raw owning pointers, freed in the destructor), so a whole
// tree drops with its root.
//
// [INVARIANT] Compiled Protos are immutable and shareable across threads; all
// mutable execution state (registers, open upvalues, inline caches) lives in the
// Isolate (architecture §10.4). A Closure (vm/function.hpp) binds a Proto to its
// captured upvalues to become a callable Value.

#ifndef PHON_VM_PROTO_HPP
#define PHON_VM_PROTO_HPP

#include <phon/core/small_vector.hpp>
#include <phon/core/symbol.hpp>
#include <phon/core/variant.hpp>
#include <phon/core/vector.hpp>
#include <phon/vm/opcode.hpp>

#include <memory>

namespace phonometrica {

// How a closure captures one upvalue when it is created (CLOSURE opcode):
// from the enclosing frame's register `index` (in_stack) or from the enclosing
// closure's upvalue `index` (architecture §9.2, Lua upvalue analysis).
struct UpvalDesc
{
	bool in_stack = false; // true: capture parent register; false: parent upvalue
	uint8_t index = 0;
};

// How a compile-time type annotation names a class. A builtin (or `Object`) is a
// stable id known at compile time; a user class's id is only assigned when it
// registers at module load, so it is referenced indirectly through the module slot
// holding its class object (design §6/§11). Resolved to a Class* at load.
struct TypeRef
{
	enum Kind : uint8_t
	{
		Concrete,  // `value` is a stable class id
		ModuleSlot // `value` is a module slot holding the class object
	};
	Kind kind = Concrete;
	uint32_t value = 0;
};

// A generic-method registration emitted by DEFMETHOD (design §6: a named top-level
// function or a class `method` is a method on a generic). The closure supplying the
// code is produced at runtime by the preceding CLOSURE; the interpreter resolves the
// TypeRef signature to a Class* signature and calls add_method.
struct MethodDef
{
	Symbol name;                    // the generic this method joins
	SmallVector<TypeRef, 4> sig;    // per-parameter declared type
	uint64_t ref_mask = 0;          // bit i set => parameter i is `ref`
};

// One field of a class-def (name + declared type; the default initializer is
// compiled into the class's `init` prologue, not stored here).
struct FieldDef
{
	Symbol name;
	TypeRef type;
};

// A user-class registration emitted by DEFCLASS. The interpreter calls
// add_user_class (resolving `base`), stores the class object in the class's module
// slot, then the following DEFMETHODs register its methods keyed on the new class.
struct ClassDef
{
	Symbol name;
	TypeRef base;               // the parent class (Object if none written)
	bool is_ref = false;
	bool is_open = false;
	SmallVector<FieldDef, 4> fields;
};

struct Proto final
{
	Proto() = default;
	Proto(const Proto &) = delete;
	Proto &operator=(const Proto &) = delete;

	Vector<Instruction> code;                  // the bytecode
	Vector<Variant> constants;                 // constant pool (KBx addressing); retains cells
	Vector<std::unique_ptr<Proto>> children;   // nested prototypes (owned)
	Vector<uint32_t> lines;                    // source line per instruction (parallel to code)
	Vector<UpvalDesc> upvals;                  // upvalue capture descriptors
	Vector<MethodDef> method_defs;             // DEFMETHOD targets (generic registrations)
	Vector<ClassDef> class_defs;               // DEFCLASS targets (class registrations)

	Symbol name = NO_SYMBOL; // function name (NO_SYMBOL for anonymous / module)
	int num_params = 0;      // fixed positional parameter count
	int num_regs = 0;        // frame register count (stack slots to reserve)
	int num_ic = 0;          // inline-cache slots this proto needs (CALLG sites)
	bool is_vararg = false;  // trailing variadic parameter

	// --- emission helpers (used by lowering) ---

	intptr_t emit(Instruction ins, uint32_t line)
	{
		code.push_back(ins);
		lines.push_back(line);
		return code.size() - 1;
	}

	// Add a constant, returning its index. Deduplicates by bit identity so repeated
	// literals share one slot (keeps the pool small; distinct NaN payloads/-0.0 stay
	// separate, which is correct for constants).
	int add_constant(Variant v)
	{
		uint64_t bits = v.value().bits();
		for (intptr_t i = 0; i < constants.size(); ++i)
			if (constants[i].value().bits() == bits)
				return static_cast<int>(i);
		constants.push_back(std::move(v));
		return static_cast<int>(constants.size() - 1);
	}

	uint32_t line_at(intptr_t ip) const noexcept
	{
		return (ip >= 0 && ip < lines.size()) ? lines[ip] : 0;
	}
};

} // namespace phonometrica

#endif // PHON_VM_PROTO_HPP
