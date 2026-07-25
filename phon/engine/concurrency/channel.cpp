// Phonometrica engine — Channel implementation (§13).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/engine/concurrency/channel.hpp>

#include <phon/engine/concurrency/transfer.hpp>
#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/vector.hpp>
#include <phon/engine/object/class.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/vm/isolate.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace phonometrica {

namespace {

// The queue behind a Channel. `buf[head .. buf.size())` are the live items, each holding
// the +1 handed over by the transfer walk. A consumed prefix is compacted away once it
// grows past a small threshold, so the backing storage stays bounded (~2×capacity for a
// bounded channel) without a full ring-buffer index dance.
struct ChannelState
{
	std::mutex mtx;
	std::condition_variable not_empty;
	std::condition_variable not_full;
	Vector<Value> buf;
	intptr_t head = 0;
	intptr_t cap = 0; // 0 => unbounded

	intptr_t count() const noexcept { return buf.size() - head; }

	void push(Value v) { buf.push_back(v); }

	Value pop()
	{
		Value v = buf[head++];
		if (head > 16 && head * 2 >= buf.size())
		{
			intptr_t n = buf.size() - head;
			for (intptr_t i = 0; i < n; ++i)
				buf[i] = buf[head + i];
			while (buf.size() > n)
				buf.pop_back();
			head = 0;
		}
		return v;
	}
};

struct ChannelCell
{
	Cell header;
	ChannelState *state;
};

Class *g_channel = nullptr;

PHON_FORCE_INLINE ChannelState *state_of(Cell *c) noexcept
{
	return reinterpret_cast<ChannelCell *>(c)->state;
}

void channel_finalize(Cell *c)
{
	ChannelState *st = state_of(c);
	// The refcount reached zero, so no other thread references this channel: release the
	// values still queued (each carries +1) without locking.
	for (intptr_t i = st->head; i < st->buf.size(); ++i)
		if (st->buf[i].is_cell())
			release(st->buf[i].as_cell());
	delete st;
}

} // namespace

bool is_channel(Value v) noexcept
{
	return v.is_cell() && g_channel && v.as_cell()->class_id() == g_channel->id;
}

void register_channel_class()
{
	if (g_channel)
		return;
	// CLASS_BUILTIN, so the name resolver sees the class and `c is Channel` and `: Channel`
	// annotations work; `Channel(...)` still reaches the builtin factory generic, because
	// the lowerer redirects a call on a class object to a generic of the same name.
	// Acyclic: a channel never participates in a reference cycle (values are transferred
	// copies, and channels are not yet sendable through channels).
	g_channel = add_class("Channel", get_class(CID_OBJECT),
	                      CLASS_BUILTIN | CLASS_REF | CLASS_ACYCLIC);
	g_channel->finalize = &channel_finalize;
}

Class *channel_class() noexcept { return g_channel; }

Value builtin_channel(Isolate &iso, NativeCell *, Value *args, int argc)
{
	intptr_t cap = 0;
	if (argc >= 1)
	{
		if (!args[0].is_int() || args[0].as_int() < 0)
			iso.raise(String("[Type error] Channel capacity must be a non-negative Integer"), 0);
		cap = static_cast<intptr_t>(args[0].as_int());
	}
	Cell *c = cell_alloc(g_channel->id, static_cast<intptr_t>(sizeof(ChannelCell)));
	// A channel is shared across threads: switch its refcount to the atomic regime. Only
	// the SHARED_BUFFER bit — FROZEN governs copy-on-write, which a ref class never does.
	std::atomic_ref<uint32_t>(c->rc_bits).fetch_or(Cell::FLAG_SHARED_BUFFER, std::memory_order_relaxed);
	auto *ch = reinterpret_cast<ChannelCell *>(c);
	ch->state = new ChannelState();
	ch->state->cap = cap;
	return Value::make_cell(c); // +1
}

Value builtin_send(Isolate &iso, NativeCell *, Value *args, int argc)
{
	(void) argc;
	if (!is_channel(args[0]))
		iso.raise(String("[Type error] 'send' expects a Channel as its first argument"), 0);

	// Transfer before locking: the walk may raise (a reference-type payload), and keeping
	// it out of the critical section keeps the lock hold short.
	Value item = transfer_across_threads(iso, args[1]); // +1, safe to hand off

	ChannelState *st = state_of(args[0].as_cell());
	{
		std::unique_lock<std::mutex> lock(st->mtx);
		if (st->cap > 0)
			st->not_full.wait(lock, [st] { return st->count() < st->cap; });
		st->push(item); // the queue takes the +1
	}
	st->not_empty.notify_one();
	return Value::make_null();
}

Value builtin_receive(Isolate &iso, NativeCell *, Value *args, int argc)
{
	(void) argc;
	if (!is_channel(args[0]))
		iso.raise(String("[Type error] 'receive' expects a Channel"), 0);

	ChannelState *st = state_of(args[0].as_cell());
	Value v;
	bool was_bounded;
	{
		std::unique_lock<std::mutex> lock(st->mtx);
		st->not_empty.wait(lock, [st] { return st->count() > 0; });
		v = st->pop(); // moves the +1 out of the queue to us
		was_bounded = st->cap > 0;
	}
	if (was_bounded)
		st->not_full.notify_one();
	return v; // +1 handed to the caller
}

Variant channel_receive(Value channel)
{
	PHON_ASSERT(is_channel(channel));
	ChannelState *st = state_of(channel.as_cell());
	Value v;
	bool was_bounded;
	{
		std::unique_lock<std::mutex> lock(st->mtx);
		st->not_empty.wait(lock, [st] { return st->count() > 0; });
		v = st->pop();
		was_bounded = st->cap > 0;
	}
	if (was_bounded)
		st->not_full.notify_one();
	return Variant::adopt(v); // the Variant takes the +1 the queue held
}

bool channel_try_receive(Value channel, double timeout_seconds, Variant &out)
{
	PHON_ASSERT(is_channel(channel));
	ChannelState *st = state_of(channel.as_cell());
	Value v;
	bool got;
	bool was_bounded;
	{
		std::unique_lock<std::mutex> lock(st->mtx);
		auto has_item = [st] { return st->count() > 0; };
		if (timeout_seconds <= 0.0)
			got = has_item(); // poll without blocking
		else
		{
			auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
			    std::chrono::duration<double>(timeout_seconds));
			got = st->not_empty.wait_for(lock, ns, has_item);
		}
		if (got)
			v = st->pop();
		was_bounded = st->cap > 0;
	}
	if (got && was_bounded)
		st->not_full.notify_one();
	if (got)
		out = Variant::adopt(v);
	return got;
}

} // namespace phonometrica
