// Phonometrica engine — the runtime-internal thread pool (architecture §13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Fine-grained data parallelism lives *inside* the runtime, not in script-visible
// threads (architecture §2): a fixed pool of `hardware_concurrency - 1` workers runs
// raw-buffer kernels (elementwise NumArray ops, and later parallel_map) above a size
// threshold. `parallel_for` partitions [0, n) into `worker_count() + 1` contiguous
// ranges — one per worker plus the calling thread — and blocks until all complete. Each
// participant writes a DISJOINT output range, so kernels need no locking on the data.
//
// Type erasure is a plain function pointer + context (no std::function), so the pool has
// no per-round allocation and stays usable from every layer. A caller lock serializes
// concurrent parallel_for calls (a spawned script thread may trigger one too), keeping
// the single job slot race-free; workers only ever run the numeric callback, never
// re-enter the pool, so this cannot deadlock.

#ifndef PHON_CONCURRENCY_THREAD_POOL_HPP
#define PHON_CONCURRENCY_THREAD_POOL_HPP

#include <phon/engine/base/definitions.hpp>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace phonometrica {

class ThreadPool final
{
public:
	// `nthreads` worker threads (0 => everything runs on the calling thread).
	explicit ThreadPool(int nthreads);
	~ThreadPool();

	PHON_DISABLE_COPY(ThreadPool)

	int worker_count() const noexcept { return m_nthreads; }

	// Run `task(ctx, lo, hi)` over [0, n) split into worker_count()+1 contiguous ranges
	// (workers take the first ranges, the caller runs the last), blocking until every
	// range is done. Runs serially when there are no workers or n is 0. Thread-safe:
	// concurrent callers are serialized.
	void parallel_for(intptr_t n, void (*task)(void *ctx, intptr_t lo, intptr_t hi), void *ctx);

private:
	void worker_loop(int participant);

	int m_nthreads;
	std::thread *m_threads = nullptr;

	std::mutex m_caller_mtx; // serializes parallel_for invocations (one job slot)
	std::mutex m_mtx;
	std::condition_variable m_cv_go;
	std::condition_variable m_cv_done;

	// Current job (guarded by m_mtx).
	void (*m_task)(void *, intptr_t, intptr_t) = nullptr;
	void *m_ctx = nullptr;
	intptr_t m_n = 0;
	intptr_t m_chunk = 0;
	uint64_t m_round = 0; // bumped per job; workers compare against their last-seen round
	int m_pending = 0;    // workers still running the current job
	bool m_stop = false;
};

// The process-wide pool, sized hardware_concurrency() - 1 (lazily created, joined at
// process exit). NumArray kernels reach it through here.
ThreadPool &global_thread_pool();

// Element-count threshold above which elementwise kernels use the pool (architecture §13
// start value; an M8 tuning knob).
inline constexpr intptr_t PHON_PARALLEL_THRESHOLD = 32768;

} // namespace phonometrica

#endif // PHON_CONCURRENCY_THREAD_POOL_HPP
