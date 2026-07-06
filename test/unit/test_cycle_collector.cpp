// Phonometrica engine — cycle collector unit tests (architecture §8.2).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// These exercise the Bacon–Rajan collector directly on a tiny instrumented cell
// type, so reclamation is observable without the whole VM. A garbage cycle is
// reclaimed by CollectWhite, which frees memory *without* running the finalizer
// (the child edges were already accounted for), so the node counts its own death
// in BOTH the finalizer (ordinary RC free) and the gc_free hook (cycle free).

#include "test_framework.hpp"

#include <phon/core/cell.hpp>
#include <phon/core/value.hpp>
#include <phon/memory/cycle_collector.hpp>
#include <phon/object/class.hpp>
#include <phon/runtime/bootstrap.hpp>

using namespace phonometrica;

namespace {

int g_live = 0; // number of live GcNode cells

// A cell with two child slots — enough to build cycles that also point at a
// separate live node.
struct NodeCell
{
	Cell header;
	Value child[2];
};

void node_finalize(Cell *c)
{
	--g_live;
	auto *n = reinterpret_cast<NodeCell *>(c);
	for (Value &v : n->child)
		if (v.is_cell())
			release(v.as_cell());
}

void node_trace(Cell *c, void (*visit)(Cell *))
{
	auto *n = reinterpret_cast<NodeCell *>(c);
	for (Value &v : n->child)
		if (v.is_cell())
			visit(v.as_cell());
}

// Cycle-collector free: account for this node's death but do NOT release the child
// (the collector already balanced the child edges).
void node_gc_free(Cell *c)
{
	(void) c;
	--g_live;
}

// An acyclic (GREEN) leaf with a real finalizer — models String/File. It must be
// reclaimed *through its finalizer* even when its only referrer is a garbage cycle.
int g_leaf_live = 0;

struct LeafCell
{
	Cell header;
};

void leaf_finalize(Cell *)
{
	--g_leaf_live;
}

uint32_t leaf_cid()
{
	static uint32_t cid = [] {
		bootstrap();
		Class *k = add_class("GcLeaf", get_class(CID_OBJECT), CLASS_REF | CLASS_ACYCLIC);
		k->instance_size = static_cast<intptr_t>(sizeof(LeafCell));
		k->finalize = &leaf_finalize; // no trace / gc_free: it is acyclic
		return k->id;
	}();
	return cid;
}

Cell *make_leaf()
{
	Cell *c = cell_alloc(leaf_cid(), static_cast<intptr_t>(sizeof(LeafCell)));
	++g_leaf_live;
	return c;
}

uint32_t node_cid()
{
	static uint32_t cid = [] {
		bootstrap(); // ensure the base classes exist
		Class *k = add_class("GcNode", get_class(CID_OBJECT), CLASS_REF);
		k->instance_size = static_cast<intptr_t>(sizeof(NodeCell));
		k->finalize = &node_finalize;
		k->trace = &node_trace;
		k->gc_free = &node_gc_free;
		return k->id;
	}();
	return cid;
}

Cell *make_node()
{
	Cell *c = cell_alloc(node_cid(), static_cast<intptr_t>(sizeof(NodeCell)));
	auto *n = reinterpret_cast<NodeCell *>(c);
	n->child[0] = Value::make_null();
	n->child[1] = Value::make_null();
	++g_live;
	return c;
}

// Point `parent`'s child slot `i` at `val` (retaining it, releasing the previous).
void set_child(Cell *parent, Cell *val, int i = 0)
{
	auto *n = reinterpret_cast<NodeCell *>(parent);
	if (val)
		retain(val);
	if (n->child[i].is_cell())
		release(n->child[i].as_cell());
	n->child[i] = val ? Value::make_cell(val) : Value::make_null();
}

// RAII: install a fresh collector for a test and detach it afterwards, so no
// dangling collector pointer leaks into later test files that run scripts.
struct CollectorScope
{
	CycleCollector cc;
	CycleCollector *prev;
	CollectorScope() : prev(current_collector())
	{
		g_live = 0;
		g_leaf_live = 0;
		set_current_collector(&cc);
	}
	~CollectorScope() { set_current_collector(prev); }
};

} // namespace

TEST_CASE("gc: a self-referential node is reclaimed")
{
	CollectorScope scope;
	Cell *n = make_node();
	set_child(n, n);   // n->child = n, rc == 2 (external + self)
	release(n);        // drop the external ref: rc 1, buffered as a purple root
	CHECK(g_live == 1);
	CHECK(scope.cc.candidate_count() == 1);

	scope.cc.collect();
	CHECK(g_live == 0); // the cycle was broken and reclaimed
	CHECK(scope.cc.candidate_count() == 0);
}

TEST_CASE("gc: a two-node mutual cycle is reclaimed")
{
	CollectorScope scope;
	Cell *a = make_node();
	Cell *b = make_node();
	set_child(a, b); // a->b
	set_child(b, a); // b->a  (now a.rc==2, b.rc==2)
	release(a);
	release(b); // both unreachable, each rc 1, buffered
	CHECK(g_live == 2);

	scope.cc.collect();
	CHECK(g_live == 0);
}

TEST_CASE("gc: a live cycle is NOT reclaimed while externally referenced")
{
	CollectorScope scope;
	Cell *a = make_node();
	Cell *b = make_node();
	set_child(a, b);
	set_child(b, a);
	// Keep `a` externally reachable (do not release it). Only drop b's spare ref.
	release(b);
	CHECK(g_live == 2);

	scope.cc.collect();
	CHECK(g_live == 2); // both survive: `a` is a live root, `b` reachable from it

	// Now drop the external reference; the cycle becomes garbage.
	release(a);
	scope.cc.collect();
	CHECK(g_live == 0);
}

TEST_CASE("gc: a garbage cycle keeps an externally-shared live node alive")
{
	CollectorScope scope;
	Cell *a = make_node();
	Cell *b = make_node();
	Cell *live = make_node(); // reachable from the cycle AND externally

	set_child(a, b, 0);    // a->b
	set_child(b, a, 0);    // b->a  (the garbage cycle)
	set_child(a, live, 1); // a->live  (an edge out of the cycle to a live node)

	release(a);
	release(b);
	// `live` is now held by a's edge and by our external ref (rc 2).
	CHECK(g_live == 3);

	scope.cc.collect();
	CHECK(g_live == 1); // a and b reclaimed; `live` survived the trial deletion
	release(live);
	CHECK(g_live == 0);
}

TEST_CASE("gc: an acyclic child of a garbage cycle is finalized, not just freed")
{
	CollectorScope scope;
	Cell *a = make_node();
	Cell *b = make_node();
	Cell *leaf = make_leaf(); // GREEN, with a finalizer that must run

	set_child(a, b, 0);    // a<->b garbage cycle
	set_child(b, a, 0);
	set_child(a, leaf, 1); // the cycle's only edge to the leaf
	release(leaf);         // drop our external ref: leaf held only by the cycle now
	release(a);
	release(b);
	CHECK(g_live == 2);
	CHECK(g_leaf_live == 1);

	scope.cc.collect();
	CHECK(g_live == 0);      // the cycle is gone
	CHECK(g_leaf_live == 0); // and the acyclic leaf was finalized via normal release
}

TEST_CASE("gc: a garbage cycle keeps an externally-shared acyclic leaf alive")
{
	CollectorScope scope;
	Cell *a = make_node();
	Cell *b = make_node();
	Cell *leaf = make_leaf(); // shared: referenced by the cycle AND externally

	set_child(a, b, 0);
	set_child(b, a, 0);
	set_child(a, leaf, 1); // cycle -> leaf  (leaf.rc == 2: our ref + a's edge)
	release(a);
	release(b);
	CHECK(g_leaf_live == 1);

	scope.cc.collect();
	CHECK(g_live == 0);      // cycle reclaimed
	CHECK(g_leaf_live == 1); // leaf survives — still held by our external reference
	release(leaf);
	CHECK(g_leaf_live == 0);
}

TEST_CASE("gc: releasing a buffered node to zero defers its free to the collector")
{
	CollectorScope scope;
	Cell *a = make_node();
	Cell *b = make_node();
	set_child(a, b); // a->b, b.rc == 2 (a's edge + our ref)

	// Decrement `b` once so it becomes a buffered (purple) candidate, then drop the
	// last reference. Because it is buffered, release parks it for the collector.
	release(b);      // b.rc 2->1, buffered as purple candidate
	CHECK(scope.cc.candidate_count() == 1);
	set_child(a, nullptr); // a drops its edge: b.rc 1->0 while buffered -> deferred
	CHECK(g_live == 2);    // b not yet freed (deferred)

	scope.cc.collect();    // MarkRoots disposes the parked, black, rc-0 candidate
	CHECK(g_live == 1);    // b reclaimed; a still live (our ref)
	release(a);
	CHECK(g_live == 0);
}

TEST_CASE("gc: collect_if_needed respects the threshold")
{
	CollectorScope scope;
	scope.cc.threshold = 4;
	Cell *nodes[3];
	for (int i = 0; i < 3; ++i)
	{
		nodes[i] = make_node();
		set_child(nodes[i], nodes[i]);
		release(nodes[i]); // buffered
	}
	CHECK(scope.cc.candidate_count() == 3);
	scope.cc.collect_if_needed(); // below threshold (4): no collection
	CHECK(scope.cc.candidate_count() == 3);
	CHECK(g_live == 3);

	Cell *extra = make_node();
	set_child(extra, extra);
	release(extra); // 4th candidate reaches the threshold
	scope.cc.collect_if_needed();
	CHECK(g_live == 0);
}
