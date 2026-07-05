// Phonometrica engine — cell allocation backend and finalizer dispatch.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// M1 uses the FOREIGN allocation path exclusively (design/architecture.md §11.5):
// cells are sys_alloc'd and carry size-class byte SC_FOREIGN, so growth is a
// realloc and deallocation is a sys_free that can happen on any thread. The
// Isolate-arena path (real size classes, arena free lists) replaces this in M4;
// only this file changes.

#include <phon/core/cell.hpp>

#include <phon/base/alloc.hpp>
#include <phon/object/class.hpp>

namespace phonometrica {

Cell *cell_alloc(uint32_t class_id, intptr_t total_size)
{
	PHON_ASSERT(total_size >= static_cast<intptr_t>(sizeof(Cell)));
	Cell *c = static_cast<Cell *>(sys_alloc(total_size));
	c->set_header(class_id, Cell::SC_FOREIGN);
	c->rc_bits = 1; // refcount 1, no flags
	return c;
}

Cell *cell_realloc(Cell *cell, intptr_t new_total_size)
{
	PHON_ASSERT(cell != nullptr);
	PHON_ASSERT_MSG(cell->size_class() == Cell::SC_FOREIGN,
	                "cell_realloc: only FOREIGN cells realloc in place (M1)");
	PHON_ASSERT_MSG(cell->is_unique(), "cell_realloc: cell must be uniquely owned");
	PHON_ASSERT(new_total_size >= static_cast<intptr_t>(sizeof(Cell)));
	return static_cast<Cell *>(sys_realloc(cell, new_total_size));
}

void cell_free(Cell *cell) noexcept
{
	if (cell == nullptr)
		return;
	switch (cell->size_class())
	{
	case Cell::SC_FOREIGN:
		sys_free(cell);
		break;
	default:
		// The Isolate-arena and large paths land here in M4.
		PHON_UNREACHABLE_MSG("cell_free: non-FOREIGN size class not supported until M4");
	}
}

void cell_dispose(Cell *cell) noexcept
{
	Class *k = get_class(cell->class_id());
	if (k->finalize)
		k->finalize(cell);
	cell_free(cell);
}

} // namespace phonometrica
