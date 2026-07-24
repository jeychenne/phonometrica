# Phonometrica Engine — Implementation Architecture

*Blueprint for implementation. Companion to the design document (language semantics
and syntax). This document specifies the C++ architecture: module layout, concrete
data-structure designs, and an ordered milestone plan with acceptance criteria.
Instructions marked **[INVARIANT]** must not be violated by implementing agents
without explicit sign-off from the project owner.*

---

## 0. Relationship to the Current Codebase

The engine is a **rewrite**, developed as a standalone, embeddable library with no
dependency on the Phonometrica application, Qt, or the GUI. It replaces
`phon/runtime/` in the current tree (https://github.com/jeychenne/phonometrica).

Preserved from the current high-level API (spirit, not ABI):

- A `Runtime` facade with `do_string(code)`, `do_file(path)`, `compile_*`,
  `import_module`, module search paths.
- `Handle<T>` intrusive smart pointers as the C++-side view of engine objects.
- Native-function registration with an explicit class signature and by-ref
  parameter mask (current `add_global(name, cb, {classes}, ParamBitset)`),
  upgraded with a typed template front-end (§11).
- The `.phon` script test-suite pattern (`test/engine/run_all.phon`).

Deliberately **replaced**:

- `Variant` tagged union → 64-bit NaN-boxed representation (§3). The public
  type keeps the name `Variant` (now an RAII wrapper over the internal POD
  `Value`), minimizing churn when porting the application.
- Heap-allocated `Alias` reference mechanism → second-class register references
  (design doc §7); no `Alias` type exists in the new engine.
- Stack-machine-style push/pop VM interface → register-based bytecode; a thin
  push/pop compatibility shim may be added last, only if the application port
  needs it.
- Per-type `func_*.hpp` free-function tables → same idea, but bound through the
  typed registration API and organized as stdlib modules (§12).
- STL containers in the runtime core → custom containers (§4).

Directory layout (new tree, self-contained):

```
engine/
  CMakeLists.txt
  include/phon/          # public headers only (§11 embedding API)
    runtime.hpp  value.hpp  handle.hpp  string.hpp  list.hpp  map.hpp
    array.hpp  clazz.hpp  channel.hpp  error.hpp
  src/
    base/        # definitions.hpp, assert, bits, allocator, arenas
    core/        # Value, Cell, Handle, containers (Vector, FlatHashMap, SmallVector)
    types/       # String, List, Table, Set, Array, File, Regex, Channel
    object/      # Class, registry, subtype intervals, instances, fields
    dispatch/    # GenericFunction, method tables, inline caches
    memory/      # refcount ops, cycle collector, freeze/transfer
    compile/     # scanner, parser, AST, lowering, bytecode, disassembler
    vm/          # interpreter loop, frames, opcodes, safepoints
    concurrency/ # Isolate, spawn, channels, thread pool, parallel kernels
    lib/         # stdlib modules (string, list, math, array, system, ...)
  test/
    unit/        # C++ unit tests (doctest or Catch2 — single-header, no gtest dep)
    scripts/     # .phon acceptance tests + run_all driver
    bench/       # micro/macro benchmarks (§14)
```

**[INVARIANT]** C++20. No exceptions across the VM dispatch loop hot path (script
errors use an unwinding mechanism described in §10.5); exceptions are permitted in
the compiler and embedding layers. No RTTI reliance in the core (class IDs replace
it). No third-party runtime dependencies (test framework and benchmark harness
only). `namespace phonometrica`.

**[INVARIANT]** STL usage policy: `<atomic>`, `<bit>`, `<type_traits>`,
`<utility>`, `<span>`, `<string_view>`, `<mutex>`, `<condition_variable>`,
`<thread>`, `<memory>` are allowed. Ownership rules: `std::unique_ptr`
(zero-overhead) is used **everywhere ownership is singular and the owned object
is not a Cell** — e.g. the Isolate owns its arena set, the Runtime owns the
thread pool and module cache, a Chunk owns its debug tables. Anything
cell-headed uses `Handle<T>` (the intrusive refcount *is* the ownership
mechanism). Raw pointers appear only as non-owning borrows.
`std::vector`, `std::string`, `std::unordered_map`, `std::function`,
`std::variant`, `std::any` are **forbidden in `core/`, `types/`, `object/`,
`dispatch/`, `memory/`, `vm/`** — use the custom containers in §4.
They are tolerated in `compile/` cold paths and tests, but prefer the custom
containers there too.

---

## 1. Layering

Strict one-directional dependencies; a layer may include headers only from layers
above it in this list:

1. `base` — platform definitions, assertions, bit utilities, raw allocation.
2. `core` — Value, Cell, Handle, containers. No knowledge of specific types.
3. `types` + `object` — concrete built-in types; class registry.
4. `dispatch` + `memory` — generic functions; cycle collector.
5. `compile` + `vm` — bytecode pipeline and interpreter.
6. `concurrency` — threads, channels, kernels (uses vm for thread entry).
7. `lib` — stdlib, built on the public embedding API where possible (dogfooding).
8. `include/phon` — the embedding facade.

---

## 2. Base Layer

- `definitions.hpp`: `intptr_t`-based sizes everywhere (the current codebase
  already does this — keep it). `PHON_ASSERT` (debug), `PHON_CHECK` (always-on
  invariant with message), `PHON_UNREACHABLE`, `likely/unlikely` wrappers,
  `PHON_NOINLINE`, force-inline macro.
- `bits.hpp`: `std::bit_cast` helpers, count-leading-zero wrappers, alignment
  helpers (`align_up(n, a)`).
- `alloc.hpp`: `sys_alloc/sys_free` (malloc-backed with OOM check),
  `aligned_alloc64` for array buffers, and the **arena API** used by the
  per-thread heap (§8.1).

---

## 3. Value Representation

### 3.1 NaN-boxing — concrete encoding **[INVARIANT]**

Two layers share this encoding:

- **`Value`** — `class Value { uint64_t bits; }`, trivially copyable, 8 bytes.
  VM-internal only (never in public headers). Refcounting is *manual*: the
  interpreter manages retain/release explicitly through opcode semantics, which
  keeps register moves at memcpy speed.
- **`Variant`** — the public embedding type (keeping the current API's name and
  role): an RAII wrapper over the same 8 bytes whose copy constructor retains
  and destructor releases. This is the only value type embedders touch; the
  JSC analogy is `JSValue` vs `Strong<>`.

A `Value` is a **double** unless `(bits & MASK_BOX) == MASK_BOX`:

```
MASK_BOX  = 0x7FFC'0000'0000'0000   // quiet-NaN space, bit 50 set to avoid
                                    // colliding with hardware-generated qNaNs
```

For boxed values, the tag is 3 bits — `{bit 63 (sign), bit 49, bit 48}` — and the
payload is bits 47..0:

```
tag 001  IMMEDIATE  payload 0 = null, 1 = false, 2 = true
tag 010  INTEGER    48-bit two's-complement payload (range ±2^47)
tag 011  SYMBOL     32-bit index into the global atom table (design doc §4)
tag 100  CELL       pointer to a heap Cell (see 3.3 pointer policy)
tag 101  REF        pointer to a Value slot (second-class reference; VM-internal,
                    never stored in user-visible containers) [INVARIANT]
tag 110  reserved
tag 111  reserved
```

Canonical accessors (all force-inlined, all going through single choke points so
the encoding can evolve):
`is_double/is_integer/is_cell/…`, `as_double`, `as_integer` (sign-extend from bit
47), `cell()`, `make(double)`, `make_int(int64_t)` (range-checked in debug),
`make_cell(Cell*)`.

Arithmetic fast path: `both_double(a, b)` = one AND + one compare on
`(a.bits | b.bits) & MASK_BOX` (with care) or two independent tests — implementer
may pick whichever benchmarks faster, behind the choke-point function.

**Integer overflow** in `+ - *` on the integer fast path raises a script error
(design doc §4); use compiler builtins (`__builtin_add_overflow`).

### 3.2 Cell header **[INVARIANT — layout]**

`Cell` is not a container or an entity of its own: it is the mandatory first
8 bytes of every heap object (the "object header", in JVM terms). The arena (§8.1)
is a raw byte source; allocation computes the full object size (header + payload,
including any inline data), obtains that many bytes from the arena, and
placement-constructs the object whose first field is the Cell.

```cpp
struct Cell {
    uint32_t hdr;        // bits 0..23: class_id (index into the class registry);
                         // bits 24..31: size class of the allocation
                         //   (0xFF = large allocation: byte size stored in a
                         //    word immediately preceding the Cell)
    uint32_t rc_bits;    // bits 0..28: refcount (saturating at max: object
                         // leaks rather than misbehaves on overflow);
                         // bit 29: BUFFERED (in cycle-collector candidate set);
                         // bit 30: FROZEN;
                         // bit 31: SHARED_BUFFER (atomic-RC regime, §8.3)
};
static_assert(sizeof(Cell) == 8);
```

The size-class byte exists so `release` can return memory to the correct free
list for variable-size objects (e.g. a String with inline data), whose size is
not derivable from the class alone. 24-bit class ids allow 16M classes.
Two sentinel values: `0xFF` = large allocation (byte size stored in a word
immediately preceding the Cell); `0xFE` = **FOREIGN** — allocated via
`sys_alloc` by C++ code running outside any Isolate (§11.5), freed via
`sys_free`. Because deallocation dispatches on this byte, objects may be born
on any thread and die on any other.

Refcount ops are **non-atomic** (thread-confined heaps) except for objects in the
SHARED_BUFFER regime. `retain/release` are free functions on `Cell*`; `release`
dispatches to a per-class finalizer through the registry when rc hits 0, and
enrolls potentially-cyclic classes into the candidate set on rc decrement (§8.2).
Classes are marked *acyclic* in the registry when their instances can never
participate in cycles (String, Array buffer, File, …) — acyclic classes skip
candidate bookkeeping entirely.

### 3.3 Pointer policy

Cells are allocated from per-thread arenas that request memory with `mmap`
hints below 2^47 (`MAP_32BIT` is too small; use address hints and verify).
`make_cell` asserts the pointer fits in 48 bits. A compile-time switch
`PHON_HEAP_BASE_OFFSETS` changes the CELL payload to a 47-bit offset from a
per-process heap base for platforms where low addresses cannot be guaranteed.
All code goes through `Value::cell()` / `make_cell()`, so this switch touches
exactly one header. **[INVARIANT]** No code outside `value.hpp` may extract
pointers from `Value::bits` directly.

### 3.4 Handle<T>

Intrusive non-atomic RC smart pointer over `Cell`-derived types; same role as
today's `Handle<T>`. `Handle` is the C++-side owner type; `Value` is the
script-side representation. Conversions both ways are explicit and cheap.
Move-aware; `Handle<T>::make(args...)` allocates via the arena and constructs.

---

## 4. Core Containers (replacing STL in the runtime)

All containers use `intptr_t` sizes, are move-only-friendly, and support a
`TriviallyRelocatable<T>` trait (default: `std::is_trivially_copyable`) enabling
`memcpy`-based growth. Specialize the trait for engine types that are safely
relocatable despite non-trivial copies (String, Handle, Value are all trivially
relocatable).

- **`Vector<T>`** — contiguous growable buffer. Growth factor 1.5, initial
  capacity 8 on first push. `memcpy` relocation for trivially-relocatable T.
  No iterator invalidation guarantees beyond the obvious. This is the workhorse;
  `List` wraps a `Vector<Value>`.
- **`SmallVector<T, N>`** — inline storage for N elements, spills to heap.
  Used pervasively in the compiler (child lists, register lists) and dispatch
  (argument type tuples).
- **`FlatHashMap<K, V, Hash>`** — open-addressing, Swiss-table style:
  a separate metadata byte array (7-bit hash tag + control states
  empty/deleted/full), group probing over 16-byte metadata groups with SSE2
  where available and a portable scalar fallback; max load factor 7/8;
  power-of-two capacities. Keys: integers, `Symbol`, `String`, `Value`
  (hash via a per-class `hash` hook). This single implementation backs:
  the script `Table` type, the atom table shards, module namespaces, dispatch
  memo tables, and the compiler's scopes.
- **`FlatHashSet<K>`** — thin wrapper over the map.
- **Function references** — no `std::function`. Native callbacks are
  `Value (*)(Isolate&, ArgSpan)` plus an optional `void*`/`Handle<Cell>`
  environment slot stored in the `NativeFunction` object (§6). Capturing C++
  lambdas are supported by templated registration that materializes the lambda
  into a cell-managed environment (§11.3).

Unit-test each container exhaustively (including deletion patterns and
tombstone reuse in the map) **before** anything is built on top — Milestone M0.

---

## 5. Built-in Types (`types/`)

Each built-in is a `Cell`-headed struct with a payload; value classes implement
copy-on-write through the uniqueness check `rc == 1` (design doc §7).

### 5.0 Single-allocation policy and the slot-rewrite discipline **[INVARIANT]**

String and List store their payload **inline after the header** (flexible array
member): one allocation per object, and for small objects the header and data
share a cache line. The consequence: growing a unique object (in-place append,
the hot path) may reallocate **the cell itself**. This is safe under one
discipline, which the language design guarantees is always available: *every
mutation site has an lvalue slot* (a register, a field slot, an element slot, or
a `ref` — which is a pointer to a slot). Therefore:

- Growable mutating primitives never mutate through a bare `Cell*`. They either
  take the owning slot and write the (possibly moved) cell pointer back, or they
  consume and return (`String append(String&&, …)`). VM append opcodes do the
  write-back; the embedding API exposes the consume-and-return form.
- CoW covers the aliased case automatically: at rc > 1, mutation copies, so any
  other holder (including a C++ `Handle`) keeps seeing the old object — correct
  value semantics. Cell movement can only occur at rc == 1, where no other
  reference exists by definition.

Table and Set instead keep a **stable cell plus one combined side block**
(control bytes + entries in a single auxiliary allocation): rehashing moves only
the block, never the cell, so no slot rewriting is needed where it would be
error-prone. Array keeps its view/buffer split (§5.3) — that separation is
load-bearing for slicing and freeze-and-share.

### 5.1 String

Single-allocation layout: `Cell | byte_size | capacity | cp_length | flags |
Breadcrumbs* | char data[]` (data inline, UTF-8, always NUL-terminated for cheap
C interop). Growth follows §5.0. `cp_length` (code-point count) computed on
construction (SIMD-friendly scalar loop is fine initially).

Breadcrumbs solve random access into variable-width UTF-8 (naive `s[i]` is an
O(n) scan): a lazily allocated side array records the byte offset of every 32nd
code point; indexing jumps to `crumbs[i >> 5]` and scans at most 31 code points
forward. Built on first random access — comparison, search, concatenation, and
iteration never pay for it; invalidated on mutation. The stride 32 is a
shift-not-divide with ~4/32 bytes overhead; treat it as an M8 tuning knob. Interned strings used as identifiers live in the **atom table** and are
referenced by `Symbol` (32-bit id): a global, sharded structure — 16 shards, each
`mutex + FlatHashMap<string-view, Symbol>` for interning; id→string lookup is a
lock-free read from an append-only chunked array. Grapheme iteration ports the
existing implementation's logic as library functions.

### 5.2 List, Table, Set

`List` = `Cell | size | capacity | Value data[]` — inline payload, growth per
§5.0 (`LIST_APPEND` and `push(ref …)` write the possibly-moved cell back through
the owning slot). `Table` (the script-level associative type; the C++ container
remains `FlatHashMap`) = stable cell + one combined side block holding the
Swiss-table control bytes and a `Vector<Entry>`-style entry region, giving
insertion-order iteration (compact "indexed hash", like Python's dict). `Set`
mirrors Table. All three are value classes (CoW).

### 5.3 Array (numeric)

Two objects: the **view** and the **buffer**.

```cpp
struct ArrayBuffer {              // acyclic class
    Cell   header;                // rc atomic iff SHARED_BUFFER flag set
    intptr_t byte_size;
    double  data[];               // 64-byte aligned allocation
};
struct Array {                    // the script-visible value class
    Cell        header;
    ArrayBuffer* buf;             // owned reference
    intptr_t    offset;           // element offset into buffer
    int32_t     rank;             // 1..PHON_MAX_RANK (8)
    uint32_t    flags;            // CONTIGUOUS, ...
    intptr_t    dim[PHON_MAX_RANK];
    intptr_t    stride[PHON_MAX_RANK];   // in elements, column-major
};
```

Slicing produces a new `Array` view sharing the buffer. CoW uniqueness for
mutation checks **both** view rc and buffer rc. Element type is `double` only,
plus a separate `BoolArray` for masks (bit-packed, same view/buffer split).
1-based → 0-based adjustment happens in index opcodes and in the `Array` C++ API
boundary — internal code is 0-based. **[INVARIANT]** All elementwise kernels
live in `lib/array_kernels.*` as free functions over raw
`(double* out, const double* a, const double* b, intptr_t n)` spans so the
thread pool and future SIMD work target one file.

### 5.4 Regex and Match — value types

`Regex` is a **value class** using the Array-style view/buffer split: the cell
holds the pattern `String` and a pointer to a separately refcounted, **immutable**
compiled-program blob (PCRE2 `pcre2_code`). CoW clone = refcount bump on the
blob; freezing and cross-isolate transfer are cheap for the same reason. The
mutable per-match scratch (`pcre2_match_data`) never lives in the Regex — it is
per call (optionally cached per-isolate). `Match` is a small value class holding
a reference to the subject string plus captured-group byte offsets; group text
and 1-based positions are computed on demand.

### 5.5 Reference classes

`File`, `Channel`, `Module`, plus user `ref class` instances. Reference classes
skip CoW entirely; `retain/release` only.

### 5.6 Instances of user classes

`Instance = Cell | Value fields[]` — fixed-size, field count and name→slot map
live in the `Class`. Field access compiles to slot loads when the static type is
known (annotated), otherwise goes through `get_field/set_field` generic dispatch
on symbol.

---

## 6. Type System (`object/`)

```cpp
struct Class {
    Cell      header;
    Symbol    name;
    Class*    base;
    uint32_t  id;            // pre-order index (see renumbering)
    uint32_t  max_subclass;  // interval end
    uint32_t  meta_id;       // class-of-this-class, for dispatch on class objects
    uint16_t  flags;         // VALUE|REF|SEALED|ACYCLIC|BUILTIN
    intptr_t  instance_size; // or field count for script classes
    // hooks:
    void    (*finalize)(Cell*);
    void    (*clone)(Cell* dst, const Cell* src);   // CoW copy for value classes
    // field table for script classes, native vtable-ish hooks for builtins
    FlatHashMap<Symbol, int32_t> field_slots;
};
```

**Registry**: a process-global, append-only `Vector<Class*>` indexed by id, owned
by `Runtime`. Reads (the common case: `registry[id]`) are plain loads.
Class creation (module load, `create_dynamic_type` equivalent) takes the registry
mutex, appends, and triggers **interval renumbering**: recompute `id/max_subclass`
by pre-order walk, then bump a global `type_epoch` (atomic). Inline caches and
memoized dispatch tables record the epoch they were filled at and self-invalidate
on mismatch. Renumbering happens only at safepoints with all script threads
parked (§9.4); it is rare (load time), so simplicity beats cleverness.

Subtype test: `sub.id >= sup.id && sub.id <= sup.max_subclass` — two loads, two
compares. `Value`-level `is_a(Value, Class*)` maps doubles/ints/immediates to
their builtin class ids without touching memory.

Builtin class ids for core types (Object=0, then the primitives) are compile-time
constants — the numbering scheme must place builtins first and keep them stable
across renumbering so the VM can hard-code them in specialized opcodes.
**[INVARIANT]** Builtin classes are never re-parented; renumbering preserves
their ids.

---

## 7. Generic Functions and Dispatch (`dispatch/`)

```cpp
struct Method {
    SmallVector<Class*, 4> sig;    // declared parameter classes
    uint64_t ref_mask;             // which params are `ref`
    Handle<Callable> code;         // shared, not unique: a Routine is also
                                   // referenced by Closures over it, and a
                                   // NativeFunction may back several generics
    // defaults/named-options metadata (not dispatched on)
};
struct GenericFunction {
    Cell header;
    Symbol name;
    Vector<Method> methods;
    uint32_t epoch;                // bumped when a method is added (open generics)
    DispatchTable* table;          // arity-specialized memo (below)
    uint8_t min_arity, max_arity;
    bool sealed;
};
```

Resolution order (design doc §6):

1. **Inline cache at the call site**: the `CALL_G` opcode carries an index into a
   per-thread IC table; each entry stores up to 2 (monomorphic + one) tuples of
   argument class ids, the resolved `Callable*`, and `(type_epoch, generic_epoch)`.
2. **Memo table** on the generic: arity 1 → `FlatHashMap<uint32_t, Method*>`;
   arity 2 → hash on `(id_a << 32) | id_b`; arity ≥3 → hash of the id tuple.
3. **Full resolution**: filter applicable methods (subtype interval checks +
   ref-mask applicability: a `ref` param requires the argument to be a REF value;
   a non-ref param requires it not to be), then select the most specific by
   pointwise comparison. With single inheritance, applicable methods are totally
   ordered per position; ambiguity (incomparable pair) is **detected when a
   method is added**, not here — resolution asserts this. Memoize the result
   keyed by the concrete id tuple.

Ambiguity check at `add_method`: compare the new signature pairwise against
existing ones; if two signatures are incomparable and no dominating disambiguator
exists, raise an error at definition time.

Sealed generics (the default after module load) with a **single** method, or with
a statically-known unique applicable method at a call site whose argument classes
are known (annotations), are devirtualized by the compiler to `CALL` (direct).

Arithmetic/comparison/indexing generics exist as normal generics, but the
compiler emits specialized opcodes (`ADD`, `LT`, `GET_INDEX`, …) whose fast paths
handle double/double, int/int, and the common builtin cases inline, falling back
to `CALL_G` machinery on miss (§10.3).

---

## 8. Memory Management (`memory/`)

### 8.1 Per-thread heap

Each `Isolate` owns an arena set: size-segregated free lists over 64 KiB
blocks obtained from a page allocator (chunks are `mmap`ed with the low-address
policy of §3.3). Size classes: 16-byte steps to 256 bytes, then powers of two to
8 KiB; larger allocations (array buffers, big strings) go straight to
`aligned_alloc64` and are tracked individually. No compaction — RC + free lists.
Blocks are owned by the thread; on thread exit, after the heap is drained,
blocks return to a global block pool (mutex-protected; touched only at thread
birth/death).

### 8.2 Cycle collector

Bacon–Rajan synchronous variant, per-thread (each thread collects its own heap):
`release` on a non-acyclic object that doesn't hit zero marks it BUFFERED and
appends to the thread's candidate vector; collection runs at **safepoints**
(function-call and loop-back-edge checks, §9.4) when the candidate count or
allocated bytes exceed thresholds. Port the algorithmic structure from the
current `add_candidate/remove_candidate` implementation, adapted to the new Cell
header. Per-class `trace(Cell*, visitor)` hooks enumerate child Values.

### 8.3 Freeze and cross-thread sharing

`freeze(x)` (library function) sets FROZEN on an Array's buffer (and on Strings)
after asserting view uniqueness; frozen buffers flip to SHARED_BUFFER, and their
refcount ops become atomic (checked via the flag — a predictable branch).
`send` on a channel: immediates/doubles copy trivially; frozen buffers share;
everything else is **deep-copied by a transfer walk** (per-class `transfer` hook
that reconstructs the object graph into the receiving thread's heap, preserving
sharing via a seen-map). Reference-class instances are **not sendable** (script
error) unless the class is `Channel` itself or provides a transfer hook (e.g.
none initially). **[INVARIANT]** No Value may ever be reachable from two script
threads unless its cell is in the SHARED_BUFFER regime.

---

## 9. Compiler (`compile/`)

Pipeline: `Scanner → Parser → AST → Lowering → Chunk (bytecode)`.

### 9.1 Front end

- Scanner: hand-written, UTF-8 aware, produces tokens with source spans.
  Implements the newline/continuation rule (line continues iff it ends with an
  operator, comma, or opening bracket) by having the scanner suppress NEWLINE
  tokens in those states and inside brackets.
- Parser: recursive descent with Pratt expression parsing (precedence climbing).
  Grammar per the design document §11. Produces an arena-allocated AST
  (bump allocator per compilation; nodes are freed wholesale).
- All identifiers are interned to `Symbol` at scan time.

### 9.2 Lowering (single pass over AST)

- Lexical scope resolution; locals get register slots; upvalue analysis for
  closures (port the existing upvalue semantics — the current test suite has
  `test_upvalues.phon`).
- Constant folding (numeric, string concat of literals) and simple peepholes.
- Pattern lowerings from the design doc: interpolation → `CONCAT n`;
  `for x in a..b [by s]` → counted-loop opcodes with the 1-based adjustment
  folded; `lst += [e]` → `LIST_APPEND`; compound assignment → in-place ops;
  `ref` arguments → `MAKE_REF reg`.
- Register allocation: simple stack-discipline allocation over a max-256-register
  frame (compile error on overflow, like Lua).
- Emission of specialized opcodes when operand classes are statically known from
  annotations; `CALL` devirtualization for sealed single-method generics.

### 9.3 Bytecode format **[INVARIANT]**

Fixed 32-bit instructions: `op:8 A:8 B:8 C:8`, with `ABx` (`op:8 A:8 Bx:16`) and
`AsBx` (signed) variants. Constants per-chunk in a `Vector<Value>` pool (KBx
addressing via the Bx field; constants beyond 65535 use an `EXTRA_ARG` prefix
word). A `Chunk` owns: code vector, constant pool, debug line table
(run-length encoded), nested prototypes, upvalue descriptors.
Include the disassembler from day one — it is the primary debugging tool and the
milestone acceptance tests reference its output.

### 9.4 Safepoints

The compiler inserts a safepoint check at function entry and loop back-edges:
one load + compare of a per-thread `poll` word. Safepoints service: cycle
collection, class-registry renumbering rendezvous, and cooperative interruption
(the GUI's "stop script" button — the current runtime has this need).

---

## 10. Interpreter (`vm/`)

### 10.1 Threading structure

- `Runtime` (one per process): class registry, atom table, generic-function
  table for globals, module cache, compiler options, block pool. Immutable or
  internally synchronized; never touched per-instruction.
- `Isolate` (one per script thread): heap/arenas, register stack, call
  frames, IC table, candidate buffer, RNG, current error. The main thread's
  `Isolate` is created by `Runtime` and services the embedding API.

### 10.2 Frames and stack

One contiguous `Value` stack per thread (grown geometrically; frames use
base-relative register indices so growth is a realloc + base fixup). Call frame:
`{Closure*, ip, base, nret}` in a parallel frame vector. Native calls receive
`ArgSpan = std::span<Value>` over the frame registers — same convention as the
current `NativeCallback`, so stdlib porting is mechanical.

### 10.3 Dispatch loop

Computed-goto threaded dispatch (macro fallback to switch for MSVC). Each opcode
body is written as a small self-contained inline function invoked from the
dispatch site — the discipline that keeps a later copy-and-patch JIT possible.
Core opcode families (~60 opcodes; exact list finalized in M4):

- moves/constants: `MOVE, LOADK, LOADI, LOAD_NULL/TRUE/FALSE`
- arithmetic/compare with inline double/int fast paths: `ADD, SUB, MUL, DIV,
  POW, IDIV, MOD, NEG, EQ, NE, LT, LE, NOT, CONCAT n`
- control: `JMP, TEST, FORPREP/FORLOOP (counted), ITER_INIT/ITER_NEXT`
- calls: `CALL (direct), CALL_G (generic, carries IC index), RET, TAILCALL`
- closures/upvalues: `CLOSURE, GETUPVAL, SETUPVAL, CLOSE`
- data: `GET_INDEX, SET_INDEX, GET_FIELD, SET_FIELD, NEW_LIST/MAP/ARRAY,
  LIST_APPEND, GET_SLICE, SET_SLICE`
- refs: `MAKE_REF, DEREF, SET_REF`
- misc: `IS, CAST, THROW, SAFEPOINT`(folded into back-edges), `SPAWN`

`GET_INDEX` fast path: base is List and index is int → bounds-check, load;
base is Array rank-1 contiguous and index int → load; else `CALL_G get_index`.

### 10.4 Inline caches

Per-thread `Vector<ICEntry>`; the chunk records how many IC slots it needs and
the thread lazily sizes its table per loaded chunk (chunks get an IC base
offset at load time per thread). This keeps chunks shareable across threads
(bytecode is immutable after compilation) while ICs stay thread-local.
**[INVARIANT]** Compiled `Chunk`s are immutable and may be shared across
threads; all mutable execution state lives in `Isolate`.

### 10.5 Errors

Script errors: `Error` instances thrown via `THROW` or raised by opcodes/native
code. Implementation: the VM uses an explicit handler stack per thread
(`try` pushes a handler record {frame index, ip, register base}); raising walks
handlers, running `finally` blocks, unwinding frames (closing upvalues,
releasing registers). Native frames use a C++ exception (`ScriptError`) only to
cross native boundaries, caught at the VM/native seam. Error objects carry a
backtrace built from the frame vector + line tables (the current engine's
`test_error_trace.phon` / `test_nested_error_lines.phon` define expected
behavior).

---

## 11. Embedding API (`include/phon/`)

The public facade, modern C++, no engine internals leaking.

### 11.1 Runtime lifecycle

```cpp
phon::Runtime rt(options);                 // creates main Isolate
rt.do_file("script.phon");
phon::Variant v = rt.do_string("mean_f0(snd, i)");
rt.add_import_path(dir);
```

`Runtime` methods that execute code must be called from the thread that owns the
target `Isolate` (main thread by default); debug builds assert thread
identity. GUI integration: a `rt.request_interrupt()` (thread-safe) flips the
safepoint poll word.

### 11.2 Class registration for C++ types

```cpp
auto cls = rt.add_class<Sound>("Sound", rt.get_class("Object"),
                               phon::ClassKind::Reference);
```

`add_class<T>` records `sizeof`, a finalizer calling `~T`, and binds
`T::phon_class` (a static slot the current codebase's `CLS(T)` macro pattern can
map onto) so `Handle<T>` and dispatch interoperate. Value-class registration
additionally requires a clone hook.

### 11.3 Function registration — typed front end

Keep the raw form (unchanged in spirit from today):

```cpp
rt.add_function("bind_to_sound", callback,
                { cls_annotation, cls_string }, /*ref_mask*/ 0b00);
```

Add the typed template form as the primary style:

```cpp
rt.add_function("duration", [](Handle<Interval> i) -> double {
    return i->xmax - i->xmin;
});
rt.add_function("normalize", [](phon::Ref<Array> x) { /* in-place */ });
```

Implementation: a `traits<T>` unboxing layer maps C++ parameter types to
(class, ref-ness) pairs and generates a thunk `Value(*)(Isolate&, ArgSpan)`
that checks/unboxes arguments and boxes the return. `phon::Ref<T>` marks `ref`
parameters (writes through to the caller's slot). Capturing lambdas: the lambda
is moved into a cell-managed environment object owned by the `NativeFunction`.
Each `add_function` with an existing name adds a method to that generic —
overload registration from C++ and script are the same mechanism.

### 11.4 Values from C++

`phon::Variant` conversion helpers (`to<double>()`, `to<Handle<Sound>>()`,
`make(…)`), plus typed views for String/List/Table/Array with the natural modern
API (`ArrayView::dim(i)`, `data()`, iteration). Channels are exposed so the GUI
can `receive` results from worker script threads (with a timeout variant for
event-loop polling).

### 11.5 C++-side object model: stack wrappers and the foreign allocation path

Engine value types are usable from application C++ as ordinary stack objects,
without any Isolate and on any thread:

- The public types `phon::String`, `phon::Regex`, `phon::List`, `phon::Table`,
  `phon::Array` are **stack-value RAII wrappers** over their cells (the same
  shape as the current engine's `String` over its `Data`). Construct on the
  stack, automatic lifetime, value semantics via CoW:
  `phon::Regex re("^[aeiou]+"); if (auto m = re.match(s)) { … }`.
- **Boundary crossing is zero-copy in both directions.** A wrapper already holds
  the cell, so converting to/from `Variant` is a refcount bump — there is no
  boxing step (the current engine's `Variant(T&&)` → `TObject<T>` move+allocate
  disappears). Object identity is preserved across the boundary.
- **Foreign allocation.** Cell allocation normally uses the current Isolate's
  arenas, located via a thread-local pointer. When no Isolate exists on the
  current thread (GUI startup, Qt worker threads), cells are allocated via
  `sys_alloc` with size-class sentinel FOREIGN (§3.2); deallocation dispatches
  on the size-class byte, so foreign-born objects may flow into scripts, be
  retained by an Isolate, and be freed anywhere.
- **Thread contract (unchanged in spirit from the current engine's non-atomic
  `Countable`):** refcounts are non-atomic, so an object is used by one thread
  at a time; handing an object to another thread requires a happens-before edge
  (queue, mutex, signal/slot), after which the source thread must not touch it.
  For genuinely concurrent read access from C++ worker threads, `freeze` an
  Array/String (atomic buffer refcounts, §8.3) — same mechanism as script-side
  sharing.
- Registered application classes (`Sound`, `Annotation`, …) live in cells and
  are owned via `Handle<T>`; `Handle<T>::make(args…)` replaces `rt.create<T>`.
  Fully heap-free payloads are not supported for cell types (a `Value` can only
  reference the heap); the raw utility layers (PCRE2 wrappers, array kernels)
  remain callable on plain C++ types if a profile ever demands it.

---

## 12. Standard Library (`lib/`)

One registration unit per module mirroring today's `func_*.hpp` split:
`lib/string.cpp`, `lib/list.cpp`, `lib/math.cpp`, `lib/array.cpp`,
`lib/system.cpp`, `lib/file.cpp`, `lib/regex.cpp`, `lib/json.cpp`,
`lib/channel.cpp`. Port function-by-function from the current
`phon/runtime/func_*.hpp` (the semantics are already field-tested), rewritten
against the typed registration API. `print` is a generic with named options
`end`/`sep` (design doc §11). Array module includes `freeze`, `parallel_map`,
and the elementwise kernels.

---

## 13. Concurrency (`concurrency/`)

- `spawn f(args…)`: compiles to `SPAWN` which packages the callable + transferred
  arguments, creates a `Isolate` (fresh heap) on an OS thread. Thread
  handles are reference-class values (joinable via `wait`).
- `Channel`: reference class; internally `mutex + condvar + Vector<Value> ring`,
  optional capacity (0 = rendezvous is *not* supported initially; default
  unbounded, `Channel(n)` bounded). `send` performs the transfer walk (§8.3)
  **before** enqueueing so the receiving thread never touches the sender's heap.
- Runtime-internal pool: `ThreadPool` sized to hardware concurrency − 1, used by
  array kernels above an element-count threshold (start: 32768; tune in M8) and
  by `parallel_map`. Pool workers execute only raw-buffer kernels or spawn-style
  confined script functions (for `parallel_map` with a script lambda, each worker
  gets a scratch `Isolate` and inputs are transferred/frozen — this is the
  one subtle piece; implement kernels-only first, script-lambda `parallel_map`
  last).

---

## 14. Testing and Benchmarks

- **Unit tests** (C++): containers, Value encoding round-trips, RC/cycle
  collector scenarios, subtype intervals under renumbering, dispatch resolution
  incl. ambiguity detection, transfer walk. Every milestone lands with tests.
- **Script acceptance tests**: adopt the existing `test/engine/*.phon` pattern;
  port the current tests (they encode subtle semantics: upvalues, error line
  numbers, try/catch in loops, compound assignment) and grow the suite with each
  language feature. A `run_all` driver returns a nonzero exit code on failure —
  CI-friendly.
- **Golden tests** for scanner/parser (AST dumps) and compiler (disassembly
  dumps) so regressions in codegen are visible in review.
- **Benchmarks** (tracked from M4 onward, results recorded per commit):
  fib(30) recursive (call overhead), nbody (double arithmetic), string-builder
  loop (& / CONCAT), map insert/lookup churn, dispatch microbench
  (mono/poly/megamorphic call sites), array elementwise ops at 1e3/1e6/1e8
  elements (single vs pooled), spectral-norm. Compare against the current engine
  and against Lua 5.4 where the benchmark is portable — the goal is to beat the
  current engine everywhere and be within striking distance of Lua on scalar
  code while far exceeding it on array code.

---

## 15. Milestones

Each milestone must compile warning-clean (`-Wall -Wextra`), pass all prior
tests, and add its own. Do not start milestone N+1 with failing tests in N.

- **M0 — Foundations.** Build system, `base/`, `Vector`, `SmallVector`,
  `FlatHashMap/Set`, arena allocator + size-class free lists.
  *Accept:* container unit tests incl. fuzz-style randomized ops vs. a reference
  model; allocator stress test.
- **M1 — Values and core types.** `Value` encoding, `Cell`, `Handle`,
  retain/release, `String` (+ atom table), `List`, `Table`, `Set`, CoW machinery.
  *Accept:* encoding round-trip property tests (every tag, integer boundary
  ±2^47, NaN payloads survive); CoW uniqueness semantics; string breadcrumbs
  correctness on multi-byte text (use IPA samples).
- **M2 — Type system + dispatch.** Class registry, intervals + renumbering +
  epochs, `GenericFunction`, resolution, ambiguity-at-definition, memo tables.
  All exercised from C++ (no VM yet).
  *Accept:* dispatch unit tests incl. ref-mask applicability, metaclass dispatch
  (`cast(x, Float)` pattern), epoch invalidation after adding classes/methods.
- **M3 — Front end.** Scanner, parser, AST for the full grammar in the design
  doc, incl. continuation rules, both string literal forms, interpolation.
  *Accept:* golden AST dumps for a corpus covering every construct; error
  messages with correct line/column; fuzz the scanner (no crashes on arbitrary
  bytes).
- **M4 — Core VM.** Lowering, bytecode, disassembler, interpreter loop:
  expressions, control flow, functions, closures/upvalues, `print`, direct and
  generic calls with ICs.
  *Accept:* ported subset of the `.phon` suite (upvalues, compound assignment,
  scientific notation, interpolation); fib/nbody benchmarks run; disassembly
  goldens.
- **M5 — Objects and errors.** Script classes (`class`/`ref class`, fields,
  constructors), `is`, `cast`, `ref` params end-to-end, `try/catch/finally`,
  `throw`, `Error` hierarchy, backtraces, iterators (`for x in`), safepoints +
  cycle collector wired in.
  *Accept:* full port of the existing `.phon` engine suite incl. error-trace
  tests; cycle-collector leak tests (ASan + instrumented counts).
- **M6 — Array.** View/buffer, slicing, logical masks, kernels, 1-based
  boundary handling, array literals/printing, `lib/array` + `lib/math`.
  *Accept:* array acceptance scripts (slices, strided assignment, masks,
  negative indices); kernel correctness vs. naive reference; array benchmarks.
- **M7 — Concurrency.** `Isolate` spawning, channels, transfer walk,
  freeze/share, thread pool + pooled kernels; `request_interrupt`.
  *Accept:* multi-threaded stress tests under TSan (no races — TSan-clean is
  the acceptance bar); producer/consumer scripts; frozen-array zero-copy
  verified (pointer identity across threads).
- **M8 — Embedding + stdlib + performance.** Typed registration API, class
  registration for C++ types, port remaining stdlib modules, `parallel_map`
  with script lambdas, performance pass (IC tuning, refcount elision on
  borrowed locals in the compiler, opcode fast-path audit vs. benchmarks).
  *Accept:* benchmark targets met and recorded; an example host app embedding
  the engine (CLI REPL + a minimal worker-thread demo standing in for the GUI).
- **M9 — Application port (separate effort).** Swap the engine into
  Phonometrica behind the compat layer; adapt `Handle<Sound>`-style bindings.

Sequencing note for implementing agents: M0–M2 are pure C++ with no language
front end — resist the temptation to start with the parser. The dispatch and
memory substrate is where the performance lives and where design errors are
costliest to discover late.

---

## 16. Cross-cutting Conventions **[INVARIANT]**

1. Every performance-relevant representation detail (Value bits, Cell layout,
   instruction encoding) is accessed through one inline function in one header.
2. `intptr_t` for all sizes/indices; 1-based indices exist only at the
   script/API boundary and are converted immediately.
3. Debug builds: full assertion coverage, poisoned freed memory (0xDD fill),
   refcount sanity checks (release on rc==0 aborts). CI runs ASan+UBSan on unit
   and script tests, TSan on concurrency tests.
4. No allocation in opcode fast paths except where the operation is an
   allocation (constructors, CONCAT, list growth).
5. Error messages follow the current engine's format conventions
   (`[Index error] …`) — user-facing continuity matters.
6. Any deviation from this document or the design document is recorded in
   `DEVIATIONS.md` with rationale, and flagged to the project owner.
