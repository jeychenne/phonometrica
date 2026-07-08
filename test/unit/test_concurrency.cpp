// Phonometrica engine — concurrency (M7): thread-local plumbing, safepoints,
// cooperative interruption. Copyright (C) 2019-2026 Julien Eychenne. GPLv3.
//
// The acceptance bar for M7 is TSan-clean (architecture §15); run the suite under a
// TSan build (-DPHON_TSAN=ON) as well as the normal one.

#include "test_framework.hpp"

#include <phon/concurrency/transfer.hpp>
#include <phon/core/cell.hpp>
#include <phon/core/variant.hpp>
#include <phon/object/class.hpp>
#include <phon/runtime/runtime.hpp>
#include <phon/types/array.hpp>
#include <phon/types/list.hpp>
#include <phon/types/set.hpp>
#include <phon/types/string.hpp>
#include <phon/types/table.hpp>
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

// --- Stage 3: the transfer walk ------------------------------------------------

namespace {

// A transferred cell Value carries +1; drop it.
void drop(Value v)
{
	if (v.is_cell())
		release(v.as_cell());
}

Cell *cell_of_string(const String &s) { return &s.cell()->header; }

} // namespace

TEST_CASE("concurrency: transfer copies immediates trivially")
{
	Isolate iso;
	Value r = transfer_across_threads(iso, Value::make_int(42));
	CHECK(!r.is_cell());
	CHECK(r.as_int() == 42);
}

TEST_CASE("concurrency: transfer deep-copies an unfrozen String")
{
	Isolate iso;
	String s("hello");
	Value r = transfer_across_threads(iso, s.to_value());
	CHECK(r.is_cell());
	CHECK(r.as_cell() != cell_of_string(s)); // a fresh, independent cell
	CHECK(String::from_value(r) == "hello");
	drop(r);
}

TEST_CASE("concurrency: transfer shares a frozen String zero-copy")
{
	Isolate iso;
	String s("frozen payload");
	s.make_frozen();
	Cell *orig = cell_of_string(s);
	uint32_t rc0 = orig->refcount();

	Value r = transfer_across_threads(iso, s.to_value());
	CHECK(r.as_cell() == orig);              // same cell — shared, not copied
	CHECK(orig->refcount() == rc0 + 1);      // and retained (atomically)
	drop(r);
	CHECK(orig->refcount() == rc0);
}

TEST_CASE("concurrency: transfer deep-copies a List")
{
	Isolate iso;
	List l{Variant::from_int(1), Variant::from_int(2), Variant::from_int(3)};
	Value r = transfer_across_threads(iso, l.to_value());
	CHECK(r.as_cell() != reinterpret_cast<Cell *>(l.cell())); // independent cell
	List rl = List::from_value(r);
	CHECK(rl.size() == 3);
	CHECK(rl.get(1).value().as_int() == 1);
	CHECK(rl.get(3).value().as_int() == 3);
	drop(r);
}

TEST_CASE("concurrency: transfer preserves in-graph sharing (seen-map)")
{
	Isolate iso;
	List inner{Variant::from_int(7), Variant::from_int(8)};
	Value innerv = inner.to_value();
	List outer{Variant(innerv), Variant(innerv)}; // the same inner list twice

	Value r = transfer_across_threads(iso, outer.to_value());
	List ro = List::from_value(r);
	Value a = ro.get(1).value();
	Value b = ro.get(2).value();
	CHECK(a.is_cell());
	CHECK(b.is_cell());
	CHECK(a.as_cell() == b.as_cell());          // one copy, referenced twice
	CHECK(a.as_cell() != innerv.as_cell());     // but a copy, not the original
	drop(r);
}

TEST_CASE("concurrency: transfer deep-copies a Table and a Set")
{
	Isolate iso;
	Table t;
	t.set(Variant(String("k").to_value()), Variant::from_int(10));
	Value tr = transfer_across_threads(iso, t.to_value());
	CHECK(tr.as_cell() != reinterpret_cast<Cell *>(t.cell()));
	Table rt = Table::from_value(tr);
	CHECK(rt.get(Variant(String("k").to_value())).value().as_int() == 10);
	drop(tr);

	Set s;
	s.add(Variant::from_int(1));
	s.add(Variant::from_int(2));
	Value sr = transfer_across_threads(iso, s.to_value());
	Set rs = Set::from_value(sr);
	CHECK(rs.size() == 2);
	CHECK(rs.contains(Variant::from_int(1)));
	drop(sr);
}

TEST_CASE("concurrency: transfer of an unfrozen Array is an independent copy")
{
	Isolate iso;
	Array a = Array::make_1d(3);
	double *d = a.detach();
	d[0] = 1, d[1] = 2, d[2] = 3;

	Value r = transfer_across_threads(iso, a.to_value());
	Array ra = Array::from_value(r);
	CHECK(ra.data() != a.data()); // a fresh, independent buffer
	intptr_t i0 = 0, i2 = 2;
	CHECK(ra.get(&i0) == 1.0);
	CHECK(ra.get(&i2) == 3.0);
	drop(r);
}

TEST_CASE("concurrency: transfer of a frozen Array shares the buffer zero-copy")
{
	Isolate iso;
	Array f = Array::make_1d(3);
	double *fd = f.detach();
	fd[0] = 5, fd[1] = 6, fd[2] = 7;
	f.make_frozen();
	const double *base = f.data();

	Value r = transfer_across_threads(iso, f.to_value());
	Array rf = Array::from_value(r);
	CHECK(rf.data() == base); // same buffer — zero copy
	drop(r);
}

TEST_CASE("concurrency: transfer rejects a reference-type value")
{
	Isolate iso;
	Value cls = class_object(get_class(CID_STRING)); // a class object: a reference type
	bool raised = false;
	try
	{
		transfer_across_threads(iso, cls);
	}
	catch (RuntimeError &e)
	{
		raised = true;
		if (e.error.is_cell())
			release(e.error.as_cell());
	}
	CHECK(raised);
}

// The integration shape channels/spawn will rely on: build a value on this thread,
// transfer it, and hand the copy to a worker that owns it exclusively. No CHECK runs off
// the main thread (the test harness is single-threaded); results come back via atomics.
TEST_CASE("concurrency: a transferred copy is owned exclusively by the receiver")
{
	Isolate iso;
	List l{Variant::from_int(10), Variant::from_int(20), Variant::from_int(30)};
	Value copy = transfer_across_threads(iso, l.to_value()); // +1, allocated here

	std::atomic<int> worker_sum{0};
	std::thread worker([&] {
		List wl = List::from_value(copy); // the worker now owns the graph
		int sum = 0;
		for (intptr_t i = 1; i <= wl.size(); ++i)
			sum += static_cast<int>(wl.get(i).value().as_int());
		worker_sum = sum;
		drop(copy); // release the transfer's +1 on the worker thread
	});
	worker.join();

	CHECK(worker_sum.load() == 60);
	// The original is untouched and independent.
	CHECK(l.get(1).value().as_int() == 10);
	CHECK(l.size() == 3);
}

// The zero-copy acceptance case: a frozen Array buffer is shared across two threads by
// pointer identity, read concurrently, with atomic refcounts. TSan is the assertion.
TEST_CASE("concurrency: a frozen Array buffer is shared across threads by identity")
{
	Isolate iso;
	constexpr intptr_t N = 4096;
	Array shared = Array::make_1d(N);
	double *sd = shared.detach();
	for (intptr_t i = 0; i < N; ++i)
		sd[i] = static_cast<double>(i);
	shared.make_frozen();
	const double *base = shared.data();

	Value copy = transfer_across_threads(iso, shared.to_value()); // shares the buffer

	std::atomic<bool> same_buffer{false};
	std::atomic<double> worker_sum{0};
	std::thread worker([&] {
		Array wa = Array::from_value(copy);
		same_buffer = (wa.data() == base);
		double s = 0;
		for (intptr_t i = 0; i < N; ++i)
			s += wa.get(&i);
		worker_sum = s;
		drop(copy);
	});

	// The main thread reads the same frozen buffer concurrently.
	double ms = 0;
	for (intptr_t i = 0; i < N; ++i)
		ms += shared.get(&i);
	worker.join();

	CHECK(same_buffer.load());         // pointer identity across threads: zero copy
	CHECK(worker_sum.load() == ms);    // both threads saw the same data
}

} // namespace
