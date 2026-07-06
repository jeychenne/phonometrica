// Phonometrica engine — the Bacon–Rajan synchronous cycle collector (§8.2).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Reference counting reclaims everything except cycles of otherwise-dead objects
// (a list that contains itself, two instances that point at each other). This is
// the backup collector that reclaims those, ported from Phonometrica's synchronous
// Recycler (Bacon & Rajan 2001; Jones/Hosking/Moss, _The GC Handbook_, p. 66 ff.)
// and adapted to the new 8-byte Cell header.
//
// When `release` decrements a potentially-cyclic (non-GREEN) cell without freeing
// it, the cell may be the root of a garbage cycle: it is painted PURPLE and pushed
// onto the thread's candidate buffer (`cc_possible_root`). At a safepoint the
// collector runs trial deletion over the candidates: subtract each candidate's
// internal references (MarkGray), find which survive an external reference (Scan →
// black) and which do not (→ white), then free the white ones (CollectWhite).
//
// Colors live in the Cell header (core/cell.hpp). GREEN cells (acyclic classes)
// are never buffered. The candidate set is a plain Vector<Cell*> (the header has no
// room for the intrusive list Phonometrica used), so a candidate whose refcount
// reaches zero is not freed synchronously — it is parked (`cc_collect_deferred`)
// and reclaimed at the next MarkRoots.

#ifndef PHON_MEMORY_CYCLE_COLLECTOR_HPP
#define PHON_MEMORY_CYCLE_COLLECTOR_HPP

#include <phon/core/cell.hpp>
#include <phon/core/vector.hpp>

namespace phonometrica {

class CycleCollector final
{
public:
	CycleCollector() = default;
	~CycleCollector();

	CycleCollector(const CycleCollector &) = delete;
	CycleCollector &operator=(const CycleCollector &) = delete;

	// Bacon–Rajan PossibleRoot: buffer `c` as a cycle-collection candidate (paints
	// it PURPLE). Idempotent — an already-purple/buffered cell is ignored.
	void possible_root(Cell *c) noexcept;

	// A buffered candidate's refcount reached zero: park it (BLACK, rc 0) for the
	// next MarkRoots to free, instead of freeing it under the live candidate buffer.
	void collect_deferred(Cell *c) noexcept;

	// Run one collection cycle (MarkRoots → Scan → CollectWhite) over the current
	// candidate buffer. A no-op while `paused`.
	void collect();

	// Collect only once the candidate buffer has grown past `threshold`. Called at
	// interpreter safepoints (function calls, loop back-edges).
	void collect_if_needed()
	{
		if (!paused && m_candidates.size() >= threshold)
			collect();
	}

	// Reclaim everything reclaimable, iterating until the buffer stops shrinking.
	// Used at Isolate teardown once all roots have been released.
	void collect_until_stable();

	intptr_t candidate_count() const noexcept { return m_candidates.size(); }

	bool paused = false;
	intptr_t threshold = 1024;

private:
	Vector<Cell *> m_candidates;
};

// The collector serving allocations on this thread. `release` reaches it through
// the cc_* seams; the Isolate installs its collector for the duration of a run.
CycleCollector *current_collector() noexcept;
void set_current_collector(CycleCollector *cc) noexcept;

} // namespace phonometrica

#endif // PHON_MEMORY_CYCLE_COLLECTOR_HPP
