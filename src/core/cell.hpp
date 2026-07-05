// Phonometrica engine — Cell: the mandatory 8-byte heap-object header.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// [INVARIANT — layout] Every heap object begins with a Cell (design/architecture.md
// §3.2). The arena is a raw byte source; allocation computes header + payload,
// obtains the bytes, and constructs the object whose first field is the Cell.
//
//   hdr:     bits 0..23 class_id (index into the class registry)
//            bits 24..31 size-class byte (0xFF large, 0xFE FOREIGN, else heap class)
//   rc_bits: bits 0..28 refcount (saturating), bit29 BUFFERED, bit30 FROZEN,
//            bit31 SHARED_BUFFER
//
// retain/release are non-atomic here (thread-confined heaps, §3). The SHARED_BUFFER
// atomic regime is wired in M7; the flag bit is reserved now. The finalizer and
// the allocation backend are defined one layer up (memory/): this header declares
// the seam so Handle and the VM can retain/release without a cyclic include.

#ifndef PHON_CORE_CELL_HPP
#define PHON_CORE_CELL_HPP

#include "base/definitions.hpp"

namespace phonometrica {

struct Cell
{
	uint32_t hdr;
	uint32_t rc_bits;

	// hdr fields.
	static constexpr uint32_t CLASS_ID_MASK = 0x00FF'FFFFu; // 24 bits: 16M classes
	static constexpr uint32_t SIZE_CLASS_SHIFT = 24;
	static constexpr uint32_t SIZE_CLASS_MASK = 0xFFu;

	// Size-class sentinels (design §3.2).
	static constexpr uint32_t SC_LARGE = 0xFFu;   // byte size stored before the Cell
	static constexpr uint32_t SC_FOREIGN = 0xFEu; // sys_alloc'd outside any Isolate

	// rc_bits fields.
	static constexpr uint32_t RC_MASK = 0x1FFF'FFFFu; // bits 0..28
	static constexpr uint32_t RC_MAX = RC_MASK;       // saturates: object leaks
	static constexpr uint32_t FLAG_BUFFERED = 1u << 29;
	static constexpr uint32_t FLAG_FROZEN = 1u << 30;
	static constexpr uint32_t FLAG_SHARED_BUFFER = 1u << 31;

	PHON_FORCE_INLINE uint32_t class_id() const noexcept { return hdr & CLASS_ID_MASK; }
	PHON_FORCE_INLINE uint32_t size_class() const noexcept
	{
		return (hdr >> SIZE_CLASS_SHIFT) & SIZE_CLASS_MASK;
	}
	PHON_FORCE_INLINE uint32_t refcount() const noexcept { return rc_bits & RC_MASK; }

	PHON_FORCE_INLINE bool is_frozen() const noexcept { return rc_bits & FLAG_FROZEN; }
	PHON_FORCE_INLINE bool is_buffered() const noexcept { return rc_bits & FLAG_BUFFERED; }
	PHON_FORCE_INLINE bool is_shared_buffer() const noexcept
	{
		return rc_bits & FLAG_SHARED_BUFFER;
	}

	// Uniqueness witness for copy-on-write (§5.0/§7). Meaningful only while the
	// caller itself holds a reference (the count cannot drop below 1 underneath).
	PHON_FORCE_INLINE bool is_unique() const noexcept { return refcount() == 1; }
	PHON_FORCE_INLINE bool is_shared() const noexcept { return refcount() > 1; }

	PHON_FORCE_INLINE void set_header(uint32_t class_id, uint32_t size_class) noexcept
	{
		hdr = (class_id & CLASS_ID_MASK) | ((size_class & SIZE_CLASS_MASK) << SIZE_CLASS_SHIFT);
	}
};

static_assert(sizeof(Cell) == 8, "Cell must be exactly 8 bytes");

// --- allocation seam (defined in memory/cell_memory.cpp) ---

// Allocate a cell of `total_size` bytes (header included). For M1 this uses the
// FOREIGN path (sys_alloc); the Isolate-arena path arrives in M4. The returned
// cell has refcount 1 and its header set to (class_id, SC_FOREIGN).
Cell *cell_alloc(uint32_t class_id, intptr_t total_size);

// Grow/shrink a *uniquely-owned* FOREIGN cell in place (single-allocation policy,
// §5.0). Returns the possibly-moved cell; callers must write it back to the
// owning slot. Preserves the header and refcount.
Cell *cell_realloc(Cell *cell, intptr_t new_total_size);

// Free a cell's memory (dispatches on the size-class byte). Does NOT run the
// finalizer — cell_dispose does that.
void cell_free(Cell *cell) noexcept;

// Run the finalizer for cell's class (if any) and free it. Called by release()
// when the refcount reaches zero.
void cell_dispose(Cell *cell) noexcept;

// --- refcount operations ---

// Add a reference. Saturates at RC_MAX (the object then leaks rather than
// misbehaving on overflow, §3.2).
PHON_FORCE_INLINE void retain(Cell *c) noexcept
{
	uint32_t rc = c->rc_bits & Cell::RC_MASK;
	if (PHON_LIKELY(rc != Cell::RC_MAX))
		++c->rc_bits; // rc < RC_MAX guarantees no carry into the flag bits
}

// Drop a reference; dispose the cell when the last one goes away.
PHON_FORCE_INLINE void release(Cell *c) noexcept
{
	uint32_t rc = c->rc_bits & Cell::RC_MASK;
	PHON_ASSERT_MSG(rc != 0, "release on a cell with refcount 0");
	if (rc == Cell::RC_MAX)
		return; // saturated: intentionally leaked
	if (rc == 1)
	{
		cell_dispose(c);
		return;
	}
	--c->rc_bits; // rc > 1 guarantees no borrow from the flag bits
}

} // namespace phonometrica

#endif // PHON_CORE_CELL_HPP
