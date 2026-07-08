// Phonometrica engine — concurrency (M7): thread-local plumbing, safepoints,
// cooperative interruption. Copyright (C) 2019-2026 Julien Eychenne. GPLv3.
//
// The acceptance bar for M7 is TSan-clean (architecture §15); run the suite under a
// TSan build (-DPHON_TSAN=ON) as well as the normal one.

#include "test_framework.hpp"

#include <phon/runtime/runtime.hpp>
#include <phon/types/string.hpp>
#include <phon/vm/isolate.hpp>

#include <atomic>
#include <chrono>
#include <thread>

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

} // namespace
