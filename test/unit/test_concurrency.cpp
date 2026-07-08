// Phonometrica engine — concurrency (M7): thread-local plumbing, safepoints,
// cooperative interruption. Copyright (C) 2019-2026 Julien Eychenne. GPLv3.
//
// The acceptance bar for M7 is TSan-clean (architecture §15); run the suite under a
// TSan build (-DPHON_TSAN=ON) as well as the normal one.

#include "test_framework.hpp"

#include <phon/core/cell.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/types/array.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/isolate.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace phonometrica;

namespace {

// A Runtime driven entirely from a *worker* thread must work: current_isolate and
// current_collector are thread-local, so the worker installs its own on entry and
// nothing leaks onto (or from) the main thread's slots.
TEST_CASE("concurrency: a Runtime runs on a spawned OS thread")
{
	int result = 0;
	std::thread worker([&] {
		Runtime rt;
		rt.do_string("var acc = 0\nfor i = 1 to 1000 do acc += i end");
		result = static_cast<int>(rt.do_string("acc").as_int());
	});
	worker.join();
	CHECK(result == 500500);

	// The main thread's isolate slot is untouched by the worker's run.
	CHECK(current_isolate() == nullptr);
}

// Two Runtimes on two threads run concurrently without stepping on each other:
// separate isolates, separate heaps-of-record, separate collectors.
TEST_CASE("concurrency: two Runtimes run concurrently")
{
	std::atomic<int> a{0}, b{0};
	std::thread ta([&] {
		Runtime rt;
		a = static_cast<int>(rt.do_string("var s = 0\nfor i = 1 to 100000 do s += 1 end\ns").as_int());
	});
	std::thread tb([&] {
		Runtime rt;
		b = static_cast<int>(rt.do_string("var s = 0\nfor i = 1 to 100000 do s += 2 end\ns").as_int());
	});
	ta.join();
	tb.join();
	CHECK(a.load() == 100000);
	CHECK(b.load() == 200000);
}

// request_interrupt() from another thread aborts a running loop at its next
// back-edge safepoint, surfacing as a RuntimeError whose message is the interrupt.
TEST_CASE("concurrency: request_interrupt aborts a running loop")
{
	Runtime rt;
	std::atomic<bool> running{false};
	std::atomic<bool> interrupted{false};
	String message;

	std::thread worker([&] {
		try
		{
			running = true;
			// An unbounded loop: only a cooperative interrupt breaks it.
			rt.do_string("var n = 0\nwhile true do n += 1 end");
		}
		catch (const RuntimeError &e)
		{
			interrupted = true;
			message = e.message;
		}
	});

	// Wait until the worker is in its loop, then interrupt it.
	while (!running.load())
		std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	rt.request_interrupt();
	worker.join();

	CHECK(interrupted.load());
	CHECK(message.starts_with("[Interrupt]"));
}

// A stale interrupt request must not abort a later, unrelated run.
TEST_CASE("concurrency: a consumed interrupt does not leak into the next run")
{
	Runtime rt;
	rt.request_interrupt();       // targets whatever runs next
	bool threw = false;
	int sum = 0;
	try
	{
		// A counted loop hits back-edge safepoints; if the stale request were not
		// cleared at run start, the first back-edge would abort this run.
		sum = static_cast<int>(
		    rt.do_string("var n = 0\nfor i = 1 to 1000 do n += 1 end\nn").as_int());
	}
	catch (const RuntimeError &)
	{
		threw = true;
	}
	CHECK(!threw);
	CHECK(sum == 1000);
	// And the session is still usable afterwards.
	CHECK(rt.do_string("40 + 2").as_int() == 42);
}

// --- Stage 2: freeze() + atomic shared-buffer refcounts ------------------------

// Freezing a String flips it into the frozen / shared-buffer regime, eagerly building
// its lazy caches, and leaves it fully readable.
TEST_CASE("concurrency: freeze() makes a String immutable + shared, still readable")
{
	String s("héllo wörld"); // multi-byte: exercises grapheme + breadcrumb caches
	CHECK(!s.cell()->header.is_frozen());
	CHECK(!s.cell()->header.is_shared_buffer());

	s.make_frozen();
	CHECK(s.cell()->header.is_frozen());
	CHECK(s.cell()->header.is_shared_buffer());

	s.make_frozen(); // idempotent
	CHECK(s.cell()->header.is_shared_buffer());

	CHECK(s == "héllo wörld");
	CHECK(s.grapheme_count() == 11);
}

// Freezing an Array freezes its buffer; a value copy shares that buffer zero-copy, and
// any mutation copies-on-write off the frozen original.
TEST_CASE("concurrency: freeze() on an Array shares the buffer, mutation copies")
{
	Array a = Array::make_1d(4);
	double *d = a.detach();
	for (int i = 0; i < 4; ++i)
		d[i] = i + 1;

	a.make_frozen();
	auto *ac = reinterpret_cast<ArrayCell *>(a.to_value().as_cell());
	CHECK(ac->buf->header.is_frozen());
	CHECK(ac->buf->header.is_shared_buffer());

	const double *base = a.data();

	// A value copy shares the frozen buffer — zero copy, pointer identity.
	Array b = a;
	CHECK(b.data() == base);

	// Mutating b copies-on-write off the frozen buffer; a stays put and stays frozen.
	b.detach()[0] = 99.0;
	CHECK(b.data() != base);
	intptr_t i0 = 0;
	CHECK(a.get(&i0) == 1.0);
	CHECK(b.get(&i0) == 99.0);
	CHECK(ac->buf->header.is_frozen());
}

// The atomic path: many threads hammer retain/release on one shared frozen cell. The
// count returns to its start and the payload is intact. TSan is the real assertion here.
TEST_CASE("concurrency: concurrent retain/release on a frozen cell is race-free")
{
	String s("shared frozen payload");
	s.make_frozen();
	Cell *c = &s.cell()->header;
	CHECK(c->refcount() == 1);

	constexpr int kThreads = 8;
	constexpr int kIters = 200000;
	std::vector<std::thread> ts;
	for (int t = 0; t < kThreads; ++t)
		ts.emplace_back([c] {
			for (int k = 0; k < kIters; ++k)
			{
				retain(c);
				release(c);
			}
		});
	for (auto &th : ts)
		th.join();

	CHECK(c->refcount() == 1); // s still holds the sole reference
	CHECK(s == "shared frozen payload");
}

// The last release of a cross-thread-shared frozen cell must dispose it exactly once —
// under ASan a double free or leak fails the run, under TSan a race does.
TEST_CASE("concurrency: last release of a shared frozen cell disposes once")
{
	String tmp("disposable frozen payload");
	tmp.make_frozen();
	Cell *c = &tmp.cell()->header; // rc == 1 (tmp)

	constexpr int kThreads = 8;
	for (int i = 0; i < kThreads; ++i)
		retain(c); // rc == 1 + kThreads

	std::vector<std::thread> ts;
	for (int i = 0; i < kThreads; ++i)
		ts.emplace_back([c] { release(c); }); // kThreads releases -> rc == 1
	for (auto &th : ts)
		th.join();

	CHECK(c->refcount() == 1);
	// tmp owns the final reference; its destructor disposes exactly once on scope exit.
}

} // namespace
