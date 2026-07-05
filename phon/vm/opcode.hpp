// Phonometrica engine — bytecode instruction set and encoding.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// [INVARIANT] Fixed 32-bit instructions (design/architecture.md §9.3): the single
// choke point for the encoding. Layout is `op:8 A:8 B:8 C:8`, with two derived
// views over the B/C fields:
//
//   ABx  — op:8 A:8 Bx:16      (Bx unsigned: constant/proto/slot indices)
//   AsBx — op:8 A:8 sBx:16     (sBx signed, bias-encoded: jump offsets)
//   Ax   — op:8 Ax:24          (EXTRA_ARG payload: wide operands)
//
// Registers are 8-bit frame-relative indices (max 256 per frame, Lua-style; the
// register allocator errors on overflow, architecture §9.2). The opcode list is
// an X-macro so the disassembler and the dispatch table stay in lockstep.

#ifndef PHON_VM_OPCODE_HPP
#define PHON_VM_OPCODE_HPP

#include <phon/base/definitions.hpp>

namespace phonometrica {

using Instruction = uint32_t;

// X-macro: one entry per opcode. The comment documents the operand roles.
// clang-format off
#define PHON_OPCODE_LIST(_)                                                     \
	/* moves & constants */                                                     \
	_(MOVE)        /* A B    : R[A] = R[B]                                    */ \
	_(LOADK)       /* A Bx   : R[A] = K[Bx]                                   */ \
	_(LOADI)       /* A sBx  : R[A] = Integer(sBx)                            */ \
	_(LOADBOOL)    /* A B    : R[A] = (B != 0)                               */ \
	_(LOADNULL)    /* A B    : R[A..A+B] = null  (B+1 slots)                 */ \
	/* upvalues & module bindings */                                            \
	_(GETUPVAL)    /* A B    : R[A] = Upval[B]                                */ \
	_(SETUPVAL)    /* A B    : Upval[B] = R[A]                                */ \
	_(GETMODULE)   /* A Bx   : R[A] = Module[Bx]                              */ \
	_(SETMODULE)   /* A Bx   : Module[Bx] = R[A]                              */ \
	/* arithmetic (specialized int/double fast paths, else CALL_G fallback) */   \
	_(ADD)         /* A B C  : R[A] = R[B] + R[C]                             */ \
	_(SUB)         /* A B C  : R[A] = R[B] - R[C]                             */ \
	_(MUL)         /* A B C  : R[A] = R[B] * R[C]                             */ \
	_(DIV)         /* A B C  : R[A] = R[B] / R[C]  (always Float)             */ \
	_(POW)         /* A B C  : R[A] = R[B] ^ R[C]                             */ \
	_(IDIV)        /* A B C  : R[A] = R[B] div R[C]                           */ \
	_(MOD)         /* A B C  : R[A] = R[B] mod R[C]                           */ \
	_(NEG)         /* A B    : R[A] = -R[B]                                   */ \
	/* comparison & logic (produce a Boolean) */                                \
	_(EQ)          /* A B C  : R[A] = (R[B] == R[C])                          */ \
	_(NE)          /* A B C  : R[A] = (R[B] != R[C])                          */ \
	_(LT)          /* A B C  : R[A] = (R[B] < R[C])                           */ \
	_(LE)          /* A B C  : R[A] = (R[B] <= R[C])                          */ \
	_(NOT)         /* A B    : R[A] = not R[B]                                */ \
	_(CONCAT)      /* A B C  : R[A] = stringify-join R[B..C]                  */ \
	/* control flow */                                                          \
	_(JMP)         /* sBx    : ip += sBx                                      */ \
	_(JMPF)        /* A sBx  : if not truthy(R[A]) ip += sBx                  */ \
	_(JMPT)        /* A sBx  : if truthy(R[A]) ip += sBx                      */ \
	_(FORPREP)     /* A sBx  : counted-loop setup, jump to FORLOOP           */ \
	_(FORLOOP)     /* A sBx  : counted-loop step/test, jump back if running  */ \
	/* iteration protocol (design §12): builtin collections, state at R[A..] */   \
	_(ITER_INIT)   /* A      : init for-in over R[A]; state in R[A],R[A+1..] */ \
	_(ITER_NEXT)   /* A B C  : advance R[A]; R[B]=exhausted; C=var count     */ \
	/* error handling (design §12) */                                           \
	_(PUSHTRY)     /* A sBx  : push handler; on throw R[A]=error, ip->A+sBx  */ \
	_(POPTRY)      /* (none) : pop the innermost handler (try body finished) */ \
	_(THROW)       /* A      : throw R[A] (an Error) to the handler stack    */ \
	/* calls */                                                                 \
	_(CALL)        /* A B    : R[A] = R[A](R[A+1..A+B])  direct, 1 result    */ \
	_(CALLG)       /* A B    : generic call; IC index in following EXTRA_ARG */ \
	_(EXTRA_ARG)   /* Ax     : wide operand for the preceding instruction    */ \
	_(RET)         /* A B    : return R[A] (B=1) or null (B=0)               */ \
	/* closures & upvalue scope */                                              \
	_(CLOSURE)     /* A Bx   : R[A] = closure over proto Bx                  */ \
	_(CLOSE)       /* A      : close open upvalues for slots >= A            */ \
	_(DEFMETHOD)   /* A Bx   : register R[A] (closure) as method-def Bx      */ \
	/* second-class references (`ref` params, design §7) */                     \
	_(MAKEREF)     /* A B    : R[A] = reference to register B                */ \
	_(DEREF)       /* A B    : R[A] = R[B] dereferenced (identity if not ref)*/ \
	_(SETREF)      /* A B    : *R[A] = R[B]  (write through a reference)     */ \
	/* aggregate construction & indexing */                                     \
	_(NEWLIST)     /* A B    : R[A] = [R[A+1..A+B]]                          */ \
	_(NEWTABLE)    /* A B    : R[A] = { B pairs from R[A+1..A+2B] }          */ \
	_(NEWSET)      /* A B    : R[A] = { R[A+1..A+B] }                        */ \
	_(LISTAPPEND)  /* A B    : R[A].append(R[B])  (CoW, slot rewrite)        */ \
	_(GETINDEX)    /* A B C  : R[A] = R[B][R[C]]                             */ \
	_(SETINDEX)    /* A B C  : R[A][R[B]] = R[C]                             */ \
	/* user classes: registration, instances, fields */                         \
	_(DEFCLASS)    /* A Bx   : register class-def Bx; R[A] = its class obj   */ \
	_(NEW)         /* A B    : R[A] = fresh instance of class object R[B]    */ \
	_(GETFIELD)    /* A B C  : R[A] = R[B].field R[C]   (routes get accessor) */ \
	_(SETFIELD)    /* A B C  : R[A].field R[B] = R[C]   (routes set accessor) */ \
	_(GETFIELDRAW) /* A B C  : R[A] = R[B].field R[C]   (raw slot, no getter) */ \
	_(SETFIELDRAW) /* A B C  : R[A].field R[B] = R[C]   (raw slot, no setter) */ \
	/* type test */                                                             \
	_(IS)          /* A B C  : R[A] = R[B] is class R[C]                     */ \
	/* terminator */                                                            \
	_(HALT)        /* A B    : stop; module result is R[A] (B=1) or null    */
// clang-format on

enum class Opcode : uint8_t
{
#define PHON_OPCODE_ENUM(name) name,
	PHON_OPCODE_LIST(PHON_OPCODE_ENUM)
#undef PHON_OPCODE_ENUM
	    Count
};

// Human-readable mnemonic (for the disassembler and error messages).
const char *opcode_name(Opcode op) noexcept;

// --- encoding [INVARIANT: the only place instruction bits are assembled] ---

inline constexpr int kMaxRegisters = 256;
inline constexpr uint32_t kBxBias = 0x8000u; // sBx = Bx - bias; range [-32768, 32767]
inline constexpr uint32_t kMaxBx = 0xFFFFu;
inline constexpr uint32_t kMaxAx = 0x00FF'FFFFu;

PHON_FORCE_INLINE Instruction encode_ABC(Opcode op, int a, int b, int c) noexcept
{
	return static_cast<uint32_t>(op) | (uint32_t(a & 0xFF) << 8) | (uint32_t(b & 0xFF) << 16) |
	       (uint32_t(c & 0xFF) << 24);
}

PHON_FORCE_INLINE Instruction encode_ABx(Opcode op, int a, uint32_t bx) noexcept
{
	return static_cast<uint32_t>(op) | (uint32_t(a & 0xFF) << 8) | ((bx & 0xFFFF) << 16);
}

PHON_FORCE_INLINE Instruction encode_AsBx(Opcode op, int a, int sbx) noexcept
{
	uint32_t bx = static_cast<uint32_t>(sbx + static_cast<int>(kBxBias));
	return encode_ABx(op, a, bx);
}

PHON_FORCE_INLINE Instruction encode_Ax(Opcode op, uint32_t ax) noexcept
{
	return static_cast<uint32_t>(op) | ((ax & kMaxAx) << 8);
}

// --- decoding ---

PHON_FORCE_INLINE Opcode op_of(Instruction i) noexcept { return static_cast<Opcode>(i & 0xFF); }
PHON_FORCE_INLINE int op_a(Instruction i) noexcept { return static_cast<int>((i >> 8) & 0xFF); }
PHON_FORCE_INLINE int op_b(Instruction i) noexcept { return static_cast<int>((i >> 16) & 0xFF); }
PHON_FORCE_INLINE int op_c(Instruction i) noexcept { return static_cast<int>((i >> 24) & 0xFF); }
PHON_FORCE_INLINE uint32_t op_bx(Instruction i) noexcept { return (i >> 16) & 0xFFFF; }
PHON_FORCE_INLINE int op_sbx(Instruction i) noexcept
{
	return static_cast<int>(op_bx(i)) - static_cast<int>(kBxBias);
}
PHON_FORCE_INLINE uint32_t op_ax(Instruction i) noexcept { return (i >> 8) & kMaxAx; }

} // namespace phonometrica

#endif // PHON_VM_OPCODE_HPP
