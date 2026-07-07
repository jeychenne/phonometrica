// Phonometrica engine — Cell: the mandatory 8-byte heap-object header.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// [INVARIANT — layout] Every heap object begins with a Cell (design/architecture.md
// §3.2). The arena is a raw byte source; allocation computes header + payload,
// obtains the bytes, and constructs the object whose first field is the Cell.
//
//   hdr:     bits 0..23 class_id (index into the class registry)
//            bits 24..31 size-class byte (0xFF large, 0xFE FOREIGN, else heap class)
//   rc_bits: bits 0..25 refcount (saturating), bits 26..28 GC color, bit29 BUFFERED,
//            bit30 FROZEN, bit31 SHARED_BUFFER
//
// The 3-bit GC color drives the Bacon–Rajan cycle collector (design/architecture
// §8.2, memory/cycle_collector.*). GREEN = acyclic (set at allocation for classes
// that can never form a cycle: String, Function-native, class objects); it makes
// `release` skip candidate bookkeeping. The other colors (BLACK/GRAY/WHITE/PURPLE)
// are the collector's working states. This narrows the refcount from 29 to 26 bits
// versus the original §3.2 sketch (67M refs, still saturating) to make room — see
// DEVIATIONS "M5 — Cycle collector".
//
// retain/release are non-atomic here (thread-confined heaps, §3). The SHARED_BUFFER
// atomic regime is wired in M7; the flag bit is reserved now. The finalizer, the
// allocation backend, and the cycle-collector seams (cc_possible_root /
// cc_collect_deferred) are defined one layer up (memory/): this header declares the
// seams so Handle and the VM can retain/release without a cyclic include.

#ifndef PHON_CORE_CELL_HPP
#define PHON_CORE_CELL_HPP

#include <phon/base/definitions.hpp>

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
	static constexpr uint32_t RC_MASK = 0x03FF'FFFFu; // bits 0..25 (26-bit refcount)
	static constexpr uint32_t RC_MAX = RC_MASK;       // saturates: object leaks
	static constexpr uint32_t COLOR_SHIFT = 26;       // bits 26..28 (3-bit GC color)
	static constexpr uint32_t COLOR_MASK = 0x7u;
	static constexpr uint32_t COLOR_FIELD = COLOR_MASK << COLOR_SHIFT;
	static constexpr uint32_t FLAG_BUFFERED = 1u << 29;
	static constexpr uint32_t FLAG_FROZEN = 1u << 30;
	static constexpr uint32_t FLAG_SHARED_BUFFER = 1u << 31;

	// GC colors (Bacon–Rajan, architecture §8.2). GREEN is the persistent acyclic
	// state; BLACK is "assumed live"; GRAY/WHITE/PURPLE are the collector's working
	// colors (possible cycle member / possibly dead / root candidate).
	static constexpr uint32_t COLOR_GREEN = 0;
	static constexpr uint32_t COLOR_BLACK = 1;
	static constexpr uint32_t COLOR_GRAY = 2;
	static constexpr uint32_t COLOR_WHITE = 3;
	static constexpr uint32_t COLOR_PURPLE = 4;

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

	// GC color access (used by the cycle collector; §8.2).
	PHON_FORCE_INLINE uint32_t gc_color() const noexcept
	{
		return (rc_bits >> COLOR_SHIFT) & COLOR_MASK;
	}
	PHON_FORCE_INLINE void set_gc_color(uint32_t color) noexcept
	{
		rc_bits = (rc_bits & ~COLOR_FIELD) | ((color & COLOR_MASK) << COLOR_SHIFT);
	}
	PHON_FORCE_INLINE void set_buffered(bool on) noexcept
	{
		rc_bits = on ? (rc_bits | FLAG_BUFFERED) : (rc_bits & ~FLAG_BUFFERED);
	}
	// Overwrite just the refcount subfield, preserving color and flags. Used by the
	// collector's trial deletion (raw ±1 that must not touch the color/flag bits).
	PHON_FORCE_INLINE void set_refcount(uint32_t rc) noexcept
	{
		rc_bits = (rc_bits & ~RC_MASK) | (rc & RC_MASK);
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

// --- cycle-collector seams (defined in memory/cycle_collector.cpp) ---

// A non-acyclic cell was just decremented but is still live: enroll it as a
// cycle-collection root candidate (Bacon–Rajan PossibleRoot, architecture §8.2).
// A no-op when no collector is active on this thread.
void cc_possible_root(Cell *cell) noexcept;

// A *buffered* cell's refcount just reached zero. It cannot be freed synchronously
// (its slot in the candidate buffer would dangle), so its destruction is deferred:
// this parks it (refcount 0, color BLACK) for the collector to free at its next
// pass. Defined in memory/cycle_collector.cpp.
void cc_collect_deferred(Cell *cell) noexcept;

// A *buffered* cell was moved by a reallocation (CoW growth of a FOREIGN cell):
// repoint its slot in the candidate buffer so it does not dangle. A no-op when no
// collector is active. Defined in memory/cycle_collector.cpp.
void cc_cell_moved(Cell *old_ptr, Cell *new_ptr) noexcept;

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
		// Last reference gone. A buffered cell (a live cycle-collection candidate)
		// cannot be freed here — its slot in the candidate buffer would dangle — so
		// its destruction is deferred to the collector (§8.2).
		if (PHON_UNLIKELY(c->is_buffered()))
		{
			cc_collect_deferred(c);
			return;
		}
		cell_dispose(c);
		return;
	}
	--c->rc_bits; // rc > 1 guarantees no borrow into the color/flag bits
	// PossibleRoot: a decremented but still-live, potentially-cyclic cell may be the
	// root of a garbage cycle. Acyclic (GREEN) cells and already-buffered candidates
	// skip this (architecture §8.2).
	if (PHON_UNLIKELY(c->gc_color() != Cell::COLOR_GREEN && !c->is_buffered()))
		cc_possible_root(c);
}

} // namespace phonometrica

#endif // PHON_CORE_CELL_HPP
