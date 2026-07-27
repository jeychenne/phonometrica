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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/application/conc/query_pool.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Worker count when the "threads" setting is 0 ("as many as the machine allows"). One less than
// hardware concurrency: the submitting thread is busy keeping the interface alive, so leaving it
// a core to do that on is the point.
static int default_worker_count()
{
	int hw = (int) std::thread::hardware_concurrency();
	if (hw <= 1) {
		return 0;
	}

	return hw - 1;
}

// The "query.threads" setting is the number of threads that scan, the submitting thread not being
// one of them. 0 means "decide from the machine".
static int configured_worker_count()
{
	int requested = 0;
	try
	{
		requested = Settings::get_int("query", "threads");
	}
	catch (...)
	{
		// Settings not initialized (a headless tool, or a profile written by an older version):
		// fall back to the machine's own answer rather than refusing to run in parallel.
		return default_worker_count();
	}
	if (requested <= 0) {
		return default_worker_count();
	}

	// Cap at what the machine actually has, so a stale or hand-edited setting cannot oversubscribe.
	int hw = (int) std::thread::hardware_concurrency();
	if (hw > 0 && requested > hw) {
		requested = hw;
	}

	return requested;
}

QueryPool &QueryPool::get()
{
	static QueryPool pool(configured_worker_count());

	return pool;
}

void QueryPool::apply_settings()
{
	// Called before a run rather than only at construction, so changing the setting takes effect
	// on the next query instead of the next restart.
	resize(configured_worker_count());
}

QueryPool::QueryPool(int workers)
{
	resize(workers);
}

QueryPool::~QueryPool()
{
	{
		std::unique_lock<std::mutex> lock(m_mtx);
		m_stop = true;
		m_cv_go.notify_all();
	}
	for (auto &t : m_workers)
	{
		if (t.joinable()) t.join();
	}
}

void QueryPool::resize(int workers)
{
	if (workers < 0) workers = 0;
	if (workers == worker_count()) {
		return;
	}

	// Stop the current workers, then start over. `m_stop` is cleared before the new threads are
	// created so they don't exit immediately.
	{
		std::unique_lock<std::mutex> lock(m_mtx);
		m_stop = true;
		m_cv_go.notify_all();
	}
	for (auto &t : m_workers)
	{
		if (t.joinable()) t.join();
	}
	m_workers.clear();

	{
		std::unique_lock<std::mutex> lock(m_mtx);
		m_stop = false;
	}
	m_workers.reserve((size_t) workers);
	for (int i = 0; i < workers; i++)
	{
		m_workers.emplace_back(&QueryPool::worker_loop, this);
	}
}

void QueryPool::drain()
{
	for (;;)
	{
		intptr_t i = m_next.fetch_add(1, std::memory_order_relaxed);
		if (i >= m_n) {
			return;
		}
		try
		{
			(*m_body)(i);
		}
		catch (...)
		{
			// Keep the lowest-indexed failure, so the reported error is the same whichever
			// worker happened to draw which annotation.
			std::unique_lock<std::mutex> lock(m_mtx);
			if (m_error_index < 0 || i < m_error_index)
			{
				m_error_index = i;
				m_error = std::current_exception();
			}
		}
		m_finished.fetch_add(1, std::memory_order_relaxed);
	}
}

void QueryPool::worker_loop()
{
	uint64_t seen = 0;

	for (;;)
	{
		{
			std::unique_lock<std::mutex> lock(m_mtx);
			m_cv_go.wait(lock, [&] { return m_stop || m_round != seen; });
			if (m_stop) {
				return;
			}
			seen = m_round;
		}

		drain();

		{
			std::unique_lock<std::mutex> lock(m_mtx);
			if (--m_running == 0) {
				m_cv_done.notify_one();
			}
		}
	}
}

void QueryPool::run(intptr_t n,
                    const std::function<void(intptr_t)> &body,
                    const std::function<void(intptr_t)> &on_tick,
                    int tick_ms)
{
	if (n <= 0) {
		on_tick(0);
		return;
	}

	if (m_workers.empty())
	{
		// No workers: run everything here, ticking as we go so the caller's progress reporting
		// behaves the same as it would with a pool.
		for (intptr_t i = 0; i < n; i++)
		{
			on_tick(i);
			body(i);
		}
		on_tick(n);
		return;
	}

	m_body = &body;
	m_n = n;
	m_next.store(0, std::memory_order_relaxed);
	m_finished.store(0, std::memory_order_relaxed);
	m_error_index = -1;
	m_error = nullptr;

	{
		std::unique_lock<std::mutex> lock(m_mtx);
		m_running = (int) m_workers.size();
		++m_round;
		m_cv_go.notify_all();
	}

	// Wait for the workers, reporting progress on the way. This thread deliberately does not
	// claim any index: it is the GUI thread, and on_tick is the only place the interface gets to
	// repaint and see a click on Cancel.
	{
		std::unique_lock<std::mutex> lock(m_mtx);
		while (m_running > 0)
		{
			m_cv_done.wait_for(lock, std::chrono::milliseconds(tick_ms), [&] { return m_running == 0; });

			// on_tick runs outside the lock: it reaches into Qt, which may run a nested event
			// loop, and holding the pool's mutex across that would block every worker that
			// finishes its share.
			lock.unlock();
			on_tick(m_finished.load(std::memory_order_relaxed));
			lock.lock();
		}
	}

	on_tick(m_finished.load(std::memory_order_relaxed));

	m_body = nullptr;
	m_n = 0;

	if (m_error) {
		auto error = m_error;
		m_error = nullptr;
		std::rethrow_exception(error);
	}
}

} // namespace phonometrica
