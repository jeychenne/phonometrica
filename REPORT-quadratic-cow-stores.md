# Element stores into a shared container are O(n) each

**Status: open. Investigated 2026-07-27, not fixed.** Everything below was measured or read
against the working tree at that date, not recalled. Orthogonal to the query-parallelization
work it was found during — nothing in that work depends on this, and fixing this does not
touch it.

---

## Summary

Storing one element into a copy-on-write container costs a **full copy of the container**
whenever the container is reached through anything other than a plain local register. A loop
that fills a container element by element is therefore O(n²).

This affects **all three CoW containers — `Table`, `List` and `NumArray`.** It is not a
`Table` bug; `Table` is merely where it hurts most, because its clone rehashes every entry
while the other two memcpy.

**It is not a regression.** The fast path that used to avoid this was disabled deliberately,
for correctness, and the reason is recorded in `phon/engine/DEVIATIONS.md` entry 5. The
performance number still sitting in `bench/BASELINE.md` predates that decision and is stale
(see [Loose end](#loose-end-a-stale-benchmark-baseline)).

---

## Reproduction

Build a Release engine (no GUI, no docs — about a minute):

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DWITH_GUI=OFF -DWITH_APPLICATION=OFF -DPHON_BUILD_DOCS=OFF -DWITH_WHISPER=OFF && cmake --build build-perf --target phon_repl -j"$(nproc)"
```

The smallest case that shows it — a module-scope `Table`:

```
var t = {}
for i = 1 to 20000 do t["k-" & i] = i end
print(len(t))
```

Run it with `build-perf/phon/engine/phon_repl <file>`. Double `n` and the time roughly
quadruples. The same loop wrapped in a `function` returns in milliseconds.

`bench/scripts/maps.phon` is this loop at n = 400 000. It is recorded at 157 ms and now does
not finish: extrapolating the measured curve puts it around **30 minutes**. If you want the
benchmark suite usable before this is fixed, run everything except `maps`
(`phon_bench fib loops strings dispatch arrays`).

---

## Mechanism

The same statement compiles differently depending on how the container is bound. Disassembly
of `var t = {}` / `t["k"] = 1` at module scope, against the same code inside a function:

```
module-scope binding              local binding
0003  GETMODULE  0 0              0000  NEWTABLE   1 0 0
0004  LOADK      1 0   ; "k"      0001  MOVE       0 1 0
0005  LOADI      2 1              0002  LOADK      1 0   ; "k"
0006  SETINDEX   0 1 2            0003  LOADI      2 1
0007  SETMODULE  0 0              0004  SETINDEX   0 1 2
```

`GETMODULE` creates a **second owner** of the container: the module slot still holds its
reference, and now a register does too, so the cell's refcount is 2 when `SETINDEX` runs.

The VM already does what it can. `SETINDEX` nulls the object register and releases that
reference before storing (`phon/engine/vm/interpreter.cpp:1912`, and the same shape in the
`Table` arm just below). But that only disposes of *one* of the two references. The binding
slot's reference survives, `Table::set` sees a shared cell, and copy-on-write clones the whole
thing. `SETMODULE` then writes the clone back.

So: **one surviving reference turns each element store into a full container copy.** The local
case has no second owner and mutates in place.

`emit_index_unshare` (`phon/engine/compile/lower.cpp:1733`) is the fast path that used to null
the binding slot around the store so the register was the sole owner. Its body is now empty —
`(void) node;` — and the comment there explains why. Call sites that still invoke it and would
come back to life: `lower.cpp:1557`, `1574`, `1583`, `1884`.

### Why it was disabled

From `DEVIATIONS.md` entry 5: nulling the slot is not exception-safe. Once a store could raise
a **catchable** error (an out-of-range index, a throwing field setter), the write-back that
restores the binding may never run, and after `catch` the variable reads `null` — silent state
corruption. That included a pre-existing corruption for `Array` out-of-range writes. Given the
choice between "slow" and "quietly wrong", slow was the right call.

`DEVIATIONS.md` entry 35 (nested writes, 2026-07-26) pays the same cost for the same reason and
points at the same remedy.

---

## What is affected

Measured on an AMD Ryzen 7 8745HS (16 threads), Debian, GCC 14.2, `-O3 -DNDEBUG`. Each cell is
wall-clock for the whole script. "4× per 2× n" is the signature of quadratic behaviour.

| container | reached through | n=10 000 | n=20 000 | n=40 000 | scaling |
|---|---|---:|---:|---:|---|
| `Table` | module binding | 1102 ms | 4399 ms | 19304 ms | 4.4× |
| `Table` | upvalue (closure capture) | 1113 ms | 4772 ms | 19722 ms | 4.1× |
| `Table` | instance field, *inside a function* | 1115 ms | 4509 ms | — | 4.0× |
| `Table` | nested (`o["in"][k] = v`), *inside a function* | 1115 ms | 4503 ms | — | 4.0× |
| `List` | module binding | 146 ms | 553 ms | 1773 ms | 3.2× |
| `NumArray` | module binding | 69 ms | 266 ms | 1054 ms | 4.0× |
| `Table` | plain local | 6 ms | 9 ms | 15 ms | **linear** |
| `List` | `append(t, x)` (reference parameter) | — | 5 ms | — | **linear** |

Reading this table:

- **What decides whether you pay is how the container is reached, not which container it is.**
  Module binding, upvalue, instance field, or any level of nesting: you pay. A plain local
  storing into its own register: you don't.
- **The field and nested rows are quadratic even inside a function.** Being in a local doesn't
  save you once there's a level of indirection. This is the case most likely to bite real
  application code, which keeps data in object fields rather than module globals.
- **Mutation through a reference parameter is free.** `append(t, x)` is registered as taking
  `List &` (`phon/engine/lib/list.cpp:110`) and mutates in place — it never reaches `SETINDEX`.
  Where a generic already exists, it is the fast way to fill a container today.
- **Only the constant differs between containers.** `Table` clone = rehash every entry;
  `List` clone = memcpy `Value`s; `NumArray` clone = memcpy doubles. Hence `Table` ≈ 7× worse
  than `List` at equal n. All three are equally quadratic.

Store opcodes on the affected path: `SETINDEX`, `SETIDXN`, `SETSLICE`, `SETFIELD`
(`phon/engine/vm/interpreter.cpp:1892`, `1959`, `2092`, `1641`).

---

## The fix both DEVIATIONS entries point at

Entries 5 and 35 independently name the same remedy: **a pending-write-back journal on the
`Isolate`, replayed by the error-landing path during unwinding.** With that in place, nulling
the binding slot becomes safe, because an unwind restores it.

This is a **VM change, not a lowering change** — the unwinder has to replay the journal. Entry
35 is explicit that if nested stores ever show up in a profile, the journal is the fix, not a
special case for nesting.

Sketch, to be validated rather than trusted:

1. The `Isolate` grows a small stack of pending write-backs: (destination slot, saved value).
2. Lowering re-enables `emit_index_unshare`, but instead of only nulling the slot, it records
   the saved reference in the journal.
3. The normal path pops the entry when the write-back executes.
4. The error-landing path replays every entry above the landing frame's watermark, restoring
   the bindings, then discards them.

Open questions the sketch does not answer, and which should be settled before writing code:

- **What is the journal's frame discipline?** Entries must be scoped to a frame so `catch` in
  an outer frame restores everything the inner frames left pending.
- **Does it interact with `for ref x in xs`?** Entry 34 notes that by-reference loops lose
  their write-back on unwind "deliberately for now", and calls out the journal as the shared
  machinery. Whether that is fixed at the same time is a scope decision, not a technical one.
- **Cost when nothing throws.** The journal is pushed and popped on every optimized store. It
  has to be cheap enough that the fast path is still worth having — a bump allocator on the
  Isolate, presumably, not a container with allocation.

---

## Scope options

Two separable jobs, smaller first:

1. **Re-enable the module/upvalue fast path.** Restores `emit_index_unshare` behind the journal.
   Fixes the `maps` benchmark and every module/upvalue-held container. Smaller blast radius:
   the affected lowering already exists and is exercised by a dedicated test.
2. **Extend the journal to nested and field stores.** Same machinery, more surface. Removes the
   per-level copy that entry 35 records. This is where real application code most likely
   benefits, and it is what makes `h.items[k] = v` in a loop usable.

Doing (1) alone is a coherent, shippable step. Doing (2) without (1) does not make sense.

---

## Acceptance criteria

- `phon/engine/test/scripts/test_cow_binding.phon` stays green **unchanged**. It already pins
  the semantics the fast path must not break: a genuine second owner still forces a clone, and
  expressions that read the same binding mid-statement still see it. Read its header comment
  first — it describes the optimization as if it were active, which is how the intent was
  recorded.
- **New exception-safety cases are the real bar**, since that is what the disabling protected.
  At minimum: a store that raises mid-loop, caught by an outer `catch`, with the binding
  observably intact afterwards, for each of a module binding, an upvalue, a field and a nested
  target. An `Array` out-of-range write is a ready-made trigger — entry 5 notes it corrupted the
  binding even before the store-failure change, so it is a regression test for a bug that
  predates the fast path.
- Golden disassembly is expected to change. Entry 5 records that `arrays.dis` and `slices.dis`
  were regenerated when the path was disabled, so they will move back. Regenerate with
  `PHON_UPDATE_GOLDEN=1` and **review the diff** rather than accepting it — the point of the
  corpus is that codegen changes are visible.
- The full bar: `ctest` (unit + examples), `phonometrica -r test/engine/run_all.phon`, both GUI
  smokes, and an ASan run over the unit suite.
- `bench/scripts/maps.phon` finishes, and `BASELINE.md` is corrected (below).

---

## Loose end: a stale benchmark baseline

`phon/engine/bench/BASELINE.md:24` records `maps` at 157 ms best / 163 ms median, and its notes
describe the O(n²) behaviour as fixed by `emit_index_unshare` in the M8 pass. That number was
recorded before the path was disabled and is no longer achievable.

Worth correcting regardless of when the fix lands, because in its present state the file reads
as though the current tree is fast. Either annotate the row as historical, or re-record the
suite with `maps` excluded and say why.

---

## Files to start from

| path | what |
|---|---|
| `phon/engine/compile/lower.cpp:1733` | `emit_index_unshare`, disabled, with the rationale |
| `phon/engine/compile/lower.cpp:1557,1574,1583,1884` | its four call sites |
| `phon/engine/vm/interpreter.cpp:1892` | `SETINDEX`; the null-and-release that drops the register's reference but not the binding's |
| `phon/engine/DEVIATIONS.md` entry 5 | why it was disabled |
| `phon/engine/DEVIATIONS.md` entry 35 | nested writes paying the same cost, same proposed fix |
| `phon/engine/test/scripts/test_cow_binding.phon` | the semantics any fix must preserve |
| `phon/engine/bench/scripts/maps.phon` | the benchmark this makes unrunnable |
