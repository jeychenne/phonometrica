// Phonometrica engine — the cross-thread transfer walk (architecture §8.3).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/concurrency/transfer.hpp>

#include <phon/core/cell.hpp>
#include <phon/core/flat_hash_map.hpp>
#include <phon/core/handle.hpp>
#include <phon/memory/cycle_collector.hpp>
#include <phon/object/class.hpp>
#include <phon/object/instance.hpp>
#include <phon/types/array.hpp>
#include <phon/types/list.hpp>
#include <phon/types/set.hpp>
#include <phon/types/string.hpp>
#include <phon/types/table.hpp>
#include <phon/vm/isolate.hpp>

namespace phonometrica {

namespace {

// src cell -> the +1-in-graph copy already made for it. Preserves in-graph sharing (a
// value DAG copies each node once) and bounds pathological duplication. Reference types
// — the only things that could form a real cycle — are rejected before we recurse, so
// the walked subgraph is always acyclic.
using SeenMap = FlatHashMap<Cell *, Value>;

Value do_transfer(Isolate &iso, Value v, SeenMap &seen);

// Return a cell Value carrying a fresh +1 (used to share a frozen cell zero-copy).
PHON_FORCE_INLINE Value share_cell(Cell *c) noexcept
{
	retain(c);
	return Value::make_cell(c);
}

[[noreturn]] void reject(Isolate &iso, const Class *k)
{
	iso.raise(String("[Type error] cannot send a value of type '") + String(k->name).view() +
	              "' across threads (reference types are not sendable)",
	          0);
}

// Unfrozen String: an independent fresh cell. Frozen String: shared zero-copy.
Value transfer_string(Value v)
{
	Cell *c = v.as_cell();
	if (c->is_shared_buffer())
		return share_cell(c);
	String src = String::from_value(v);
	String copy(src.data(), src.size());
	Value out = copy.to_value();
	retain(out.as_cell());
	return out;
}

Value transfer_list(Isolate &iso, Value v, SeenMap &seen)
{
	List src = List::from_value(v);
	intptr_t n = src.size();
	List out(n); // n null slots
	Value *slots = out.writable_slots();
	Value outv = out.to_value();
	seen.insert(v.as_cell(), outv); // record before filling (borrowed)
	const ListCell *sc = src.cell();
	for (intptr_t i = 0; i < n; ++i)
		slots[i] = do_transfer(iso, sc->data[i], seen); // +1 into the (null) slot
	retain(outv.as_cell());
	return outv;
}

Value transfer_set(Isolate &iso, Value v, SeenMap &seen)
{
	Set src = Set::from_value(v);
	List elems = src.to_list();
	Set out;
	Value outv = out.to_value();
	seen.insert(v.as_cell(), outv);
	intptr_t n = elems.size();
	for (intptr_t i = 1; i <= n; ++i)
	{
		Variant ev = elems.get(i);
		Value t = do_transfer(iso, ev.value(), seen);
		out.add(Variant(t));
		if (t.is_cell())
			release(t.as_cell());
	}
	retain(outv.as_cell());
	return outv;
}

Value transfer_table(Isolate &iso, Value v, SeenMap &seen)
{
	Table src = Table::from_value(v);
	List ks = src.keys();
	List vs = src.values();
	Table out;
	Value outv = out.to_value();
	seen.insert(v.as_cell(), outv);
	intptr_t n = ks.size();
	for (intptr_t i = 1; i <= n; ++i)
	{
		Variant kv = ks.get(i);
		Variant vv = vs.get(i);
		Value tk = do_transfer(iso, kv.value(), seen);
		Value tvv = do_transfer(iso, vv.value(), seen);
		out.set(Variant(tk), Variant(tvv));
		if (tk.is_cell())
			release(tk.as_cell());
		if (tvv.is_cell())
			release(tvv.as_cell());
	}
	retain(outv.as_cell());
	return outv;
}

Value transfer_instance(Isolate &iso, Value v, SeenMap &seen)
{
	Cell *src = v.as_cell();
	Class *k = get_class(src->class_id());
	// RAII: if a field transfer raises, dst is released (and its filled fields with it).
	Handle<Cell> dst = Handle<Cell>::adopt(make_instance(k));
	seen.insert(src, Value::make_cell(dst.get()));
	Value *sf = instance_fields(src);
	Value *df = instance_fields(dst.get());
	for (int32_t i = 0; i < k->field_count; ++i)
		df[i] = do_transfer(iso, sf[i], seen); // +1 into the (null) field slot
	Cell *out = dst.get();
	retain(out);
	return Value::make_cell(out);
}

Value do_transfer(Isolate &iso, Value v, SeenMap &seen)
{
	if (!v.is_cell())
		return v; // Int/Float/Bool/Null/Symbol: a bit copy, no cell
	Cell *c = v.as_cell();

	if (auto it = seen.find(c); it != seen.end())
	{
		Value d = it->second;
		if (d.is_cell())
			retain(d.as_cell());
		return d; // shared subgraph: one copy, referenced again
	}

	switch (c->class_id())
	{
	case CID_STRING:
	{
		Value out = transfer_string(v);
		seen.insert(c, out);
		return out;
	}
	case CID_ARRAY:
	{
		// Keep the copy alive in a named local: a temporary Array would release its
		// cell at the end of the statement, before we retain the borrowed to_value().
		Array copy = Array::from_value(v).transfer_to_thread();
		Value out = copy.to_value();
		retain(out.as_cell());
		seen.insert(c, out);
		return out;
	}
	case CID_LIST: return transfer_list(iso, v, seen);
	case CID_SET: return transfer_set(iso, v, seen);
	case CID_TABLE: return transfer_table(iso, v, seen);
	default: break;
	}

	Class *k = get_class(c->class_id());
	// Reference types (functions, class objects, ref-class instances) have identity and
	// cannot be reconstructed by copy — not sendable. Value-class instances (Error, user
	// value classes) deep-copy their fields.
	if (k->is_ref())
		reject(iso, k);
	return transfer_instance(iso, v, seen);
}

} // namespace

Value transfer_across_threads(Isolate &iso, Value v)
{
	// Detach this thread's cycle collector for the walk. Building each copy briefly drops
	// a wrapper's reference (rc 2 -> 1), which would otherwise enroll the copy as a cycle
	// candidate on the *sender's* collector — but the copy is about to be handed to
	// another thread, so the sender's collector must never reference it (it would race the
	// receiver's release and, worse, the receiver frees it). Skipping candidate tracking
	// is safe: the transferable subgraph is all value types, which are acyclic, so
	// refcounting alone reclaims it. A RAII guard restores the collector even if the walk
	// raises on a non-sendable payload.
	struct CollectorDetach
	{
		CycleCollector *prev;
		CollectorDetach() : prev(current_collector()) { set_current_collector(nullptr); }
		~CollectorDetach() { set_current_collector(prev); }
	} detach;

	SeenMap seen;
	return do_transfer(iso, v, seen);
}

} // namespace phonometrica
