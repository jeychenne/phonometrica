// Phonometrica engine — thread pool implementation (architecture §13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/concurrency/thread_pool.hpp>

namespace phonometrica {

ThreadPool::ThreadPool(int nthreads) : m_nthreads(nthreads < 0 ? 0 : nthreads)
{
	if (m_nthreads == 0)
		return;
	m_threads = new std::thread[static_cast<size_t>(m_nthreads)];
	for (int i = 0; i < m_nthreads; ++i)
		m_threads[i] = std::thread(&ThreadPool::worker_loop, this, i + 1); // participant 1..n
}

ThreadPool::~ThreadPool()
{
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		m_stop = true;
		m_cv_go.notify_all();
	}
	for (int i = 0; i < m_nthreads; ++i)
		if (m_threads[i].joinable())
			m_threads[i].join();
	delete[] m_threads;
}

void ThreadPool::worker_loop(int participant)
{
	uint64_t seen = 0;
	for (;;)
	{
		void (*task)(void *, intptr_t, intptr_t);
		void *ctx;
		intptr_t lo, hi;
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			m_cv_go.wait(lk, [&] { return m_stop || m_round != seen; });
			if (m_stop)
				return;
			seen = m_round;
			task = m_task;
			ctx = m_ctx;
			lo = static_cast<intptr_t>(participant) * m_chunk;
			hi = lo + m_chunk;
			if (hi > m_n)
				hi = m_n;
		}
		if (lo < hi)
			task(ctx, lo, hi);
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			if (--m_pending == 0)
				m_cv_done.notify_one();
		}
	}
}

void ThreadPool::parallel_for(intptr_t n, void (*task)(void *, intptr_t, intptr_t), void *ctx)
{
	if (m_nthreads == 0 || n <= 0)
	{
		if (n > 0)
			task(ctx, 0, n);
		return;
	}

	// One job in flight at a time: the pool has a single job slot.
	std::unique_lock<std::mutex> caller(m_caller_mtx);

	int participants = m_nthreads + 1;
	intptr_t chunk = (n + participants - 1) / participants;
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		m_task = task;
		m_ctx = ctx;
		m_n = n;
		m_chunk = chunk;
		m_pending = m_nthreads;
		++m_round;
		m_cv_go.notify_all();
	}

	// The caller runs participant 0's range: [0, chunk).
	intptr_t caller_hi = chunk < n ? chunk : n;
	task(ctx, 0, caller_hi);

	std::unique_lock<std::mutex> lk(m_mtx);
	m_cv_done.wait(lk, [&] { return m_pending == 0; });
}

ThreadPool &global_thread_pool()
{
	// hardware_concurrency() - 1: leave one core for the calling thread. Unknown or
	// single-core reports 0 workers (everything runs serially on the caller).
	static ThreadPool pool([] {
		unsigned hc = std::thread::hardware_concurrency();
		return hc > 1 ? static_cast<int>(hc - 1) : 0;
	}());
	return pool;
}

} // namespace phonometrica
