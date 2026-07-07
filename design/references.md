# First-Class References (design)

Status: **proposal** (2026-07-06). Supersedes design.md §7's "second-class references"
paragraph and reverses the second-class `ref` implementation landed in M5
(DEVIATIONS "M5 — References"). No code yet; this is the spec to build to.

## 1. Decision and rationale

References become **first-class**: a reference is a heap-allocated, reference-counted
box that transparently stands in for the value it wraps, exactly like PHP 7+'s
`zend_reference` and Phonometrica's `Alias`. This was chosen over the second-class
register-pointer model for two reasons:

1. **Migration.** Phonometrica's entire standard library and its reference semantics
   are built on the `Alias`/`resolve()` model. Adopting the same model lets the
   mutator natives (`trim`, `append`, `insert`, …) and script-level reference
   behaviour port with minimal change, which is a stated project goal.
2. **Flexibility.** A boxed reference can alias a local, a container element, or an
   object field uniformly, can be shared by several referrers, and survives
   reallocation of any container it points into (the box is stable; only pointers to
   it move).

Two ergonomic decisions come with it:

- **No `ref` at the call site.** `trim(str)`, not `trim(ref str)`. The visual noise of
  call-site `ref` is not worth its explicitness. Whether an argument is passed by
  reference is determined by the callee's signature (§4).
- **`ref` is a uniform property of a generic, not a per-overload one** (§4). It is
  therefore *not* a dispatch dimension — this reverses the M5 "`f(ref x)` and `f(x)`
  are distinct methods" design.

The cost we knowingly accept: a single, well-predicted dereference branch on reads
that might be references, and the copy-on-write/reference interaction (§7). The cost
we avoid versus Phonometrica: `resolve()`'s per-access chain-chasing — we use PHP's
single-branch `DEREF` instead.

## 2. The reference model (borrowed from PHP 7+)

- Ordinary values are stored inline (NaN-boxed `Value`); refcounted heap types
  (String, List, …) are cells the `Value` points at. Unchanged.
- A **reference** is a box cell owning one `Value`. A slot "holds a reference" when it
  contains a `Value` tagged as a reference pointing at that box. `$b = &$a`'s analogue
  makes *both* the source slot and the new referrer hold the same box, so a write
  through either is seen by both.
- **Promotion** (`make_reference` / PHP `make_alias`): converting a slot to hold a
  reference moves the slot's current value *into* a fresh box and replaces the slot
  with a reference to it. Promoting the **source** slot is what gives write-back for
  free — the argument and the caller's variable share one box; nothing is copied back.
- **Auto-collapse**: reading through a reference whose box refcount has fallen to 1
  (no other referrer) moves the value back out and drops the box (Phonometrica's
  `resolve()` optimisation). This is what keeps a temporary borrow from *permanently*
  turning a variable or element into a reference — it avoids PHP's "reference leakage"
  wart. We keep it.

## 3. The box cell — unified with `UpvalueCell`

There is **one** cell type for boxes and upvalues; an upvalue *is* a reference to a
captured variable. The existing `UpvalueCell { Cell header; Value* slot; Value closed;
UpvalueCell* next; }` generalises to the box:

- **Open**: `slot` points at a live location (a caller's stack register). Reads/writes
  go through `slot`. This is the Lua-style optimisation for a reference to a *local*
  that has not escaped its frame: no value is moved while the local lives on the stack.
- **Closed**: `slot == &closed`; the box owns the value. A reference to a **heap
  element or field** is born closed (the value moves into the box, the container slot
  holds a reference to it) so it survives container reallocation. An open box also
  closes when its stack frame returns — the existing upvalue-closing path.

Properties:
- Reference-counted like any cell; born non-acyclic (a box can point back at a cell
  that points at it → a cycle).
- **Cycle-collected with no collector changes**: the box gets a `trace` hook over its
  inner `Value`. The Bacon–Rajan machinery (memory/cycle_collector) reclaims reference
  cycles through boxes as-is.
- Allocation is the ordinary cell allocator today; a pooled/free-list allocator for
  boxes is a later optimisation (boxes are high-churn), not a correctness concern.

Consequence to note: once a local is boxed (captured or referenced), the defining
function's own reads of it must `DEREF` too. The compiler knows which locals are boxed
and emits `DEREF` only for those (§5); this replaces the current raw open-upvalue
access for the defining function. This is the deliberate simplification that buys the
box/upvalue unification.

## 4. Signatures: ref-ness is uniform per generic

- Each generic carries a `ref_mask` (which parameter positions are `ref`). `add_method`
  **rejects** any overload whose ref-mask disagrees with the generic's established one
  (Phonometrica `function.cpp` does exactly this). So all overloads of a name agree on
  ref-ness.
- Because ref-ness is uniform, it is **removed from the dispatch/memo key**. Overloads
  are selected by argument *types* only; ref-ness is then applied uniformly. (This
  reverses the M5 ref-dispatch code.)
- `ref` remains meaningful only for value types; a `ref` on a reference-class parameter
  is rejected (it would be a silent no-op — reference classes already have identity).

## 5. `Value` encoding and `DEREF`

- Add a **reference tag** to `Value` using one of the reserved NaN tags (110/111), so
  `is_reference()` is a pure in-register bit test (no memory load) — cheaper than PHP's
  `type_info` read.
- `DEREF(v)` = `v.is_reference() ? box(v)->value() : v` — one predicted branch, one
  indirection only for actual references (PHP `ZVAL_DEREF`).
- The compiler emits `DEREF`:
  - **Never** for a slot it can prove is not a reference (ordinary locals, most
    temporaries) — zero overhead.
  - **Unconditionally** (no branch) for a slot it knows *is* a reference (a `ref`
    parameter, a local it has boxed).
  - **As a branch** only where it cannot prove either way — principally reading a
    **container element** (`GET_INDEX`), since any element may have been promoted. This
    is the one residual hot-path cost and it mirrors PHP's array-fetch deref.

## 6. Call protocol: promotion driven by the callee's ref-mask

The core mechanism: **the source lvalue of a `ref` argument is promoted to a box at
argument-load time**, driven by the callee's (uniform) ref-mask. The caller's slot and
the callee's parameter then share the box, so mutation writes back automatically — no
post-call copy-back, and no `SETREF` dance.

Only **lvalue** arguments (a variable, `c[i]`, `o.field`) can be promoted. A non-lvalue
argument (a literal or computed expression) passed to a `ref` parameter is boxed
without a write-back target — it behaves as an ordinary local inside the callee. (Open
question: allow silently, or warn? §9.)

### 6.1 Direct calls (named generic) — compile-time

The callee name, and therefore its ref-mask, is known when the call compiles (we
**require forward declaration**: the generic must be defined before the call site —
accepted as a small price). The compiler emits, per argument position:

- ref position + lvalue argument → a **promote-and-load**: box the source, place the
  reference in the argument register;
- otherwise → a plain load.

No runtime ref-mask lookup; the fast path.

### 6.2 Indirect calls (through a variable / first-class function) — runtime

We **must** support calling a callable held in a variable, where the ref-mask is not
known at compile time. The register calling convention copies arguments into a fresh
window, which would sever the source link needed for write-back — so the promotion
cannot be deferred to *after* the arguments are staged. The resolution, following
Phonometrica's ordering (evaluate callee first, then load arguments ref-aware):

1. **First-class callables carry their generic's `ref_mask`** (closures and natives
   already are cells; the mask is a field on the callable).
2. The compiler evaluates the callee into a register **before** loading arguments, then
   for each **lvalue** argument emits a **maybe-promote load** that consults the
   callable's `ref_mask` bit for that position at runtime: set → promote the source and
   pass the box; clear → plain load. Non-lvalue arguments always load plainly.

So the compiler always supplies the source location for lvalue arguments; whether
promotion happens is a compile-time constant for direct calls and a one-bit runtime
check for indirect calls. This keeps first-class function values fully general while
still promoting the correct source.

### 6.3 What the callee sees

A `ref` parameter's register holds a reference (box). Inside the callee, reads `DEREF`
it and writes go through it (into the shared box). Because the box is shared with the
caller's source, the caller observes every mutation. On return, if the callee's box
reference was the last one, the source auto-collapses (§2) back to a plain value.

## 7. Copy-on-write × references

A boxed slot must be **excluded from CoW separation**: when a container holding a
reference element is copied, the copy shares the *same* box (both elements alias one
mutable value), and a write through a reference must not silently clone it apart. This
is threaded through every container's write and detach path. PHP has a mature
implementation of exactly this (the `SEPARATE_ZVAL*` / reference-aware copy paths); the
implementation should verify the details against `php-src` before coding. Auto-collapse
(§2) bounds how long an element stays boxed, limiting the blast radius.

## 8. Cycle collector

No changes. A box is a traceable, non-acyclic cell; its `trace` hook visits the inner
`Value`, so reference cycles (a box in a list that the box's value points back at) are
reclaimed by the existing collector. The M5 collector work stands unchanged under this
design.

## 9. Open questions

- **Non-lvalue argument to a `ref` parameter** — silently box (no write-back) or emit a
  diagnostic? Phonometrica silently boxes; a warning may be friendlier.
- **Boxed-local deref elision** — the precise compiler analysis for which reads of a
  boxed local need `DEREF` (a local is "boxed" from its first capture/reference;
  conservative whole-variable treatment is the simple start).
- **CoW-separation details** — verify against `php-src` (`zend_reference`, `ZVAL_DEREF`,
  the reference-aware copy paths) before implementing §7.
- **Escaping references** — the box model *allows* a reference to escape its frame
  (stored in a list, returned, captured); we do not need it, but it is free and matches
  Phonometrica, so it is permitted rather than forbidden. Confirm this is acceptable.

## 10. Impact on existing code

- **Reverse** the second-class ref implementation (DEVIATIONS "M5 — References"):
  `MAKEREF`/`DEREF`/`SETREF`-as-register-pointer and the `x[i]=`/`x.field=` writeback
  paths; drop `ref` from the dispatch/memo key; remove the call-site `ref` requirement
  from the parser.
- **Reuse**: the `ref_mask` plumbing on signatures, the parameter `by_ref` flags, and
  the `UpvalueCell` open/closed machinery (which becomes the box).
- **Add**: the reference `Value` tag + `DEREF`; `make_reference`/auto-collapse; the
  ref-mask on first-class callables; the promote-and-load / maybe-promote-load
  argument lowering; the CoW-separation exclusion; the box `trace` hook.
- Update design.md §7 to describe first-class references and mark the register-pointer
  form as retired.
