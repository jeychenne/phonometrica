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
 * Created: 26/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: The thread pool queries run on, kept separate from the engine's ThreadPool (which serves NumArray          *
 * kernels) because the two want opposite things. A numeric kernel is uniform, so the engine's pool splits the range   *
 * into equal contiguous pieces and lets the submitting thread run one of them. A query is neither: annotations differ  *
 * in size by orders of magnitude, so work has to be handed out dynamically, and the submitting thread is the GUI      *
 * thread, which must stay free to repaint and to notice a click on Cancel instead of disappearing into a work item.    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_QUERY_POOL_HPP
#define PHONOMETRICA_QUERY_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <phon/definitions.hpp>

namespace phonometrica {

class QueryPool final
{
public:

	// The process-wide query pool, sized from the "query" settings on first use and joined at
	// process exit.
	static QueryPool &get();

	explicit QueryPool(int workers);

	~QueryPool();

	PHON_DISABLE_COPY(QueryPool)

	int worker_count() const { return (int) m_workers.size(); }

	// Resize the pool, joining the current workers first. Must not be called while a job is
	// running.
	void resize(int workers);

	// Resize to whatever the "query" settings currently ask for. Must not be called while a job
	// is running.
	void apply_settings();

	// Run `body(i)` for every i in [0, n) and return once all of them have finished.
	//
	// The calling thread runs no item. It waits, calling `on_tick(finished)` roughly every
	// `tick_ms` with the number of items completed so far — that callback is where a GUI front
	// end repaints and watches for Cancel, which it could not do from inside a work item. This
	// costs one core's worth of scanning and buys an interface that keeps responding.
	//
	// Indices are claimed one at a time rather than split into blocks up front, so one
	// annotation that takes a hundred times longer than its neighbours delays only the worker
	// that drew it.
	//
	// `body` runs on several threads at once: everything it touches must be either confined to
	// its own index or immutable for the duration.
	//
	// If `body` throws, the remaining items still run, and the exception belonging to the
	// *lowest* index is rethrown here afterwards, so which error a user sees does not depend on
	// how the work happened to be scheduled.
	//
	// With no workers (worker_count() == 0), everything runs on the calling thread, still
	// reporting through `on_tick`.
	void run(intptr_t n,
	         const std::function<void(intptr_t)> &body,
	         const std::function<void(intptr_t)> &on_tick,
	         int tick_ms = 40);

private:

	void worker_loop();

	// Claim and run indices until the current job is exhausted.
	void drain();

	std::vector<std::thread> m_workers;

	std::mutex m_mtx;
	std::condition_variable m_cv_go;   // a job was published, or the pool is stopping
	std::condition_variable m_cv_done; // the last worker on a job finished

	// Current job. Guarded by m_mtx except for the two atomics, which workers touch freely.
	const std::function<void(intptr_t)> *m_body = nullptr;
	intptr_t m_n = 0;
	std::atomic<intptr_t> m_next{0};     // next index to claim
	std::atomic<intptr_t> m_finished{0}; // items completed (for on_tick)
	uint64_t m_round = 0;                // bumped per job; workers compare with their last seen
	int m_running = 0;                   // workers still draining the current job
	bool m_stop = false;

	// The lowest-indexed failure of the current job.
	intptr_t m_error_index = -1;
	std::exception_ptr m_error;
};

} // namespace phonometrica

#endif // PHONOMETRICA_QUERY_POOL_HPP
