// Phonometrica engine — the Bacon–Rajan synchronous cycle collector (§8.2).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/core/cycle_collector.hpp>

#include <phon/engine/object/class.hpp>

#include <utility>

namespace phonometrica {

// Each script thread collects its own heap (architecture §8.2): the current
// collector is thread-local, so a spawned Isolate's release()/possible_root traffic
// feeds its own candidate buffer, never another thread's.
namespace {
thread_local CycleCollector *g_collector = nullptr;
} // namespace

CycleCollector *current_collector() noexcept { return g_collector; }
void set_current_collector(CycleCollector *cc) noexcept { g_collector = cc; }

// --- refcount helpers used by trial deletion --------------------------------------
//
// Trial deletion adjusts refcounts *without* the release()/retain() side effects
// (no disposal, no re-buffering): a raw ±1 on the refcount subfield only.

namespace {

PHON_FORCE_INLINE uint32_t cc_rc(Cell *c) noexcept { return c->refcount(); }

PHON_FORCE_INLINE void cc_dec(Cell *c) noexcept { c->set_refcount(c->refcount() - 1); }

PHON_FORCE_INLINE void cc_inc(Cell *c) noexcept { c->set_refcount(c->refcount() + 1); }

// --- the four Bacon–Rajan phases (recursive over the object graph) -----------------

// GREEN (acyclic) children stay out of the cycle machinery entirely: their
// refcounts are never trial-adjusted and their color is never disturbed. They are
// ordinary reference-counted objects that a garbage cycle merely points at, so
// CollectWhite releases them the normal way (running their finalizers). Everything
// below therefore guards on GREEN before touching a child.

void mark_gray(Cell *s)
{
	if (s->gc_color() == Cell::COLOR_GRAY)
		return;
	s->set_gc_color(Cell::COLOR_GRAY);
	Class *k = get_class(s->class_id());
	if (k->trace)
		k->trace(s, [](Cell *child) {
			if (child->gc_color() == Cell::COLOR_GREEN)
				return;
			cc_dec(child); // subtract the internal (parent→child) reference
			mark_gray(child);
		});
}

void scan_black(Cell *s)
{
	// A live object was reached: repair the refcounts trial deletion subtracted.
	s->set_gc_color(Cell::COLOR_BLACK);
	Class *k = get_class(s->class_id());
	if (k->trace)
		k->trace(s, [](Cell *child) {
			if (child->gc_color() == Cell::COLOR_GREEN)
				return;
			cc_inc(child);
			if (child->gc_color() != Cell::COLOR_BLACK)
				scan_black(child);
		});
}

void scan(Cell *s)
{
	if (s->gc_color() != Cell::COLOR_GRAY)
		return;
	if (cc_rc(s) > 0)
	{
		// An external reference survived: this object and all it reaches are live.
		scan_black(s);
	}
	else
	{
		// No external reference: provisionally garbage.
		s->set_gc_color(Cell::COLOR_WHITE);
		Class *k = get_class(s->class_id());
		if (k->trace)
			k->trace(s, [](Cell *child) { scan(child); });
	}
}

void collect_white(Cell *s)
{
	// Free a confirmed-garbage cell. A cell still in the candidate buffer is a fresh
	// root added mid-collection — leave it. CollectWhite recurses into the cyclic
	// (non-green) white children, releases green children the ordinary way (their
	// edges were never trial-deleted), then frees this cell's own memory — bypassing
	// the finalizer (child edges are already accounted for) and using the shallow
	// gc_free hook for any auxiliary buffer.
	if (s->gc_color() != Cell::COLOR_WHITE || s->is_buffered())
		return;
	s->set_gc_color(Cell::COLOR_BLACK);
	Class *k = get_class(s->class_id());
	if (k->trace)
		k->trace(s, [](Cell *child) {
			if (child->gc_color() == Cell::COLOR_GREEN)
				release(child); // acyclic child: drop the dying cell's reference normally
			else
				collect_white(child); // black (live) children are skipped inside
		});
	if (k->gc_free)
		k->gc_free(s);
	cell_free(s);
}

} // namespace

// --- CycleCollector ---------------------------------------------------------------

void CycleCollector::possible_root(Cell *c) noexcept
{
	if (c->gc_color() == Cell::COLOR_PURPLE)
		return; // already a buffered root candidate
	c->set_gc_color(Cell::COLOR_PURPLE);
	if (!c->is_buffered())
	{
		c->set_buffered(true);
		m_candidates.push_back(c);
	}
}

void CycleCollector::collect_deferred(Cell *c) noexcept
{
	// The last reference to a buffered candidate is gone. We cannot free it here (it
	// still sits in the candidate vector), so park it: refcount 0, color BLACK. The
	// next MarkRoots sees a black, rc-0 candidate and disposes it.
	c->set_refcount(0);
	c->set_gc_color(Cell::COLOR_BLACK);
}

void CycleCollector::collect()
{
	if (paused)
		return;

	// Snapshot the current roots. New candidates buffered during this collection
	// (e.g. a finalizer releasing a shared cell) accumulate in a fresh buffer and
	// are handled next time — keeping this pass's working set stable.
	Vector<Cell *> roots = std::move(m_candidates);
	m_candidates.clear();

	// MarkRoots: keep the live purple roots (painting their subgraphs gray via trial
	// deletion); drop the rest, freeing any parked (black, rc-0) candidate.
	Vector<Cell *> kept;
	Vector<Cell *> deferred;
	for (intptr_t i = 0; i < roots.size(); ++i)
	{
		Cell *s = roots[i];
		s->set_buffered(false); // no longer pending; it is being processed now
		if (s->gc_color() == Cell::COLOR_PURPLE && cc_rc(s) > 0)
		{
			mark_gray(s);
			kept.push_back(s);
		}
		else if (s->gc_color() == Cell::COLOR_BLACK && cc_rc(s) == 0)
		{
			deferred.push_back(s); // parked by collect_deferred; free below
		}
	}

	// Scan: split the gray subgraphs into live (black) and garbage (white).
	for (intptr_t i = 0; i < kept.size(); ++i)
		scan(kept[i]);

	// CollectWhite: reclaim the garbage.
	for (intptr_t i = 0; i < kept.size(); ++i)
		collect_white(kept[i]);

	// Finally dispose the parked cells. This runs their finalizers (releasing their
	// children the ordinary way — they were never part of a traced cycle), which may
	// buffer fresh candidates; done last so it cannot disturb the phases above.
	for (intptr_t i = 0; i < deferred.size(); ++i)
		cell_dispose(deferred[i]);
}

void CycleCollector::collect_until_stable()
{
	// Garbage is finite and each pass frees some of it; a live purple root is painted
	// black and removed each pass and only re-buffered by a fresh decrement, so this
	// converges. The bound is a backstop against pathological finalizer churn.
	for (int i = 0; i < 32 && !m_candidates.empty(); ++i)
	{
		intptr_t before = m_candidates.size();
		collect();
		if (m_candidates.size() >= before)
			break; // no progress
	}
}

CycleCollector::~CycleCollector()
{
	// Any cells still buffered are unreachable-but-uncollected roots (e.g. teardown
	// order); clear the BUFFERED flag so a later stray release doesn't try to defer
	// against a dead collector. Their memory, if leaked, is reported by ASan.
	for (intptr_t i = 0; i < m_candidates.size(); ++i)
		m_candidates[i]->set_buffered(false);
}

void CycleCollector::cell_moved(Cell *old_ptr, Cell *new_ptr) noexcept
{
	// A buffered cell keeps at most one slot in the candidate buffer; repoint it.
	for (intptr_t i = m_candidates.size() - 1; i >= 0; --i)
		if (m_candidates[i] == old_ptr)
		{
			m_candidates[i] = new_ptr;
			return;
		}
}

// --- seams called from core/cell.hpp release() ------------------------------------

void cc_possible_root(Cell *c) noexcept
{
	if (g_collector)
		g_collector->possible_root(c);
}

void cc_cell_moved(Cell *old_ptr, Cell *new_ptr) noexcept
{
	if (g_collector)
		g_collector->cell_moved(old_ptr, new_ptr);
}

void cc_collect_deferred(Cell *c) noexcept
{
	if (g_collector)
	{
		g_collector->collect_deferred(c);
		return;
	}
	// No collector: honour the drop immediately (a buffered flag with no owning
	// collector is stale). Dispose as an ordinary last release.
	c->set_buffered(false);
	cell_dispose(c);
}

} // namespace phonometrica
