# Deviations

Per architecture §16.6, any deviation from the design/architecture documents is
recorded here with rationale, for the project owner's review.

## M0 — Foundations

1. **Size-class heap lives in `base/`, not `memory/`.**
   The architecture (§8.1) discusses the per-thread heap under `memory/`, but the
   directory-layout note (§0) lists "allocator, arenas" under `base/`. The heap
   (`base/heap.hpp`) and the 64 KiB block allocator (`base/alloc.hpp`) depend only
   on layer 1 primitives and are consumed by every layer above, so they are placed
   in `base/`. `memory/` remains reserved for refcount ops, the cycle collector,
   and freeze/transfer (M1+).

2. **Custom minimal test harness instead of doctest/Catch2.**
   §0/§14 permit "doctest or Catch2 (single-header, no gtest dep)". Rather than
   vendor a large third-party header, `test/unit/test_framework.hpp` provides a
   ~150-line harness (`TEST_CASE`/`CHECK`/`REQUIRE`, an intrusive registry, and a
   runner). Zero third-party dependencies; swappable for doctest later if desired.

3. **Erase always writes a tombstone (`kDeleted`) in `FlatHashMap`.**
   Abseil reclaims a deleted slot straight to `kEmpty` when its probe group still
   contains an empty slot, sparing a rehash. That optimization is deferred (an M8
   tuning knob). Correctness does not depend on it: tombstones are reused on
   insert and cleared on rehash, so capacity stays bounded under churn (covered by
   `test_flat_hash_map`'s 50k-cycle churn test).

4. **`raw_alloc`/`aligned_alloc64` use `std::aligned_alloc` + `std::free`.**
   Correct on POSIX/glibc (the current target). Windows lacks a `free`-compatible
   `aligned_alloc`; a `_aligned_malloc`/`_aligned_free` path is a TODO for the
   Windows port. The block allocator already has an mmap/`aligned_alloc` split
   behind `PHON_HAVE_MMAP`.

5. **Low-address `mmap` hint is best-effort, not asserted.**
   `BlockAllocator` maps blocks with a low-address hint (§3.3) but does not yet
   assert the result fits in 48 bits — there are no `Cell` pointers to box in M0.
   The 48-bit assertion belongs in `value.hpp` (M1), where `make_cell` is defined.

## M1 — Values and core types

1. **String indexing is by grapheme cluster, reversing design §8.**
   Design §8 specified codepoint indexing; the project owner reversed this to
   grapheme clusters (UAX #29 "user-perceived characters") on 2026-07-04. For the
   phonetics/IPA audience a "character" is the grapheme, and this keeps behavioral
   parity with Phonometrica's String. Breadcrumbs index grapheme boundaries; a
   codepoint/byte-level API (`next_codepoint`, byte iterators, `utf8_length`)
   remains underneath. Positions in `s[i]`/`at`/`find`/`index_to_iter`/`advance`/
   `distance`/`mid`/`left`/`right`/`length` are graphemes.

2. **All M1 cells use the FOREIGN allocation path (`sys_alloc`).**
   `cell_alloc`/`cell_realloc`/`cell_free` (memory/cell_memory.cpp) allocate every
   cell via `sys_alloc` with size-class byte `SC_FOREIGN` (§11.5). Single-allocation
   growth is therefore a `sys_realloc`. The Isolate-arena path (real size classes,
   arena free lists, copy-on-grow) arrives in M4 and only that file changes.

3. **retain/release are non-atomic; the SHARED_BUFFER atomic regime is stubbed.**
   The `FLAG_SHARED_BUFFER` bit is reserved but unused in M1 (single-threaded).
   Atomic refcounting for frozen/shared buffers is wired in M7 (§8.3).

4. **Class registry is the M1 seed, not the full §6 type system.**
   `object/class.hpp` provides a flat id→Class table with finalize/clone/hash/
   equals hooks — exactly what retain/release, CoW, and hashing need. Subtype
   intervals, renumbering, epochs, metaclasses, and generic dispatch are M2.

5. **Atom table uses a single mutex, not 16 shards.**
   §5.1 specifies 16 sharded `{mutex, FlatHashMap}` interning tables; M1 uses one
   mutex + one map + one append-only name vector (single-threaded is the only
   client so far). Sharding is a concurrency optimization deferred to M7.

6. **String API is a dependency-free subset (per owner sign-off).**
   Implemented: construction, byte/codepoint/grapheme access + breadcrumbs, CoW,
   comparison/hash, append/prepend/insert/concat, clear/reserve/chop/shrink,
   replace/remove/trim, to_upper/to_lower/reverse/repeat/left/right/mid, find/
   rfind/contains/starts_with/ends_with/count, convert/to_int/to_float/to_bool/
   format, and UTF-16/UTF-32 conversion. Deferred to the milestone that supplies
   the dependency: `split`/`join` (Array<String>, M6), `replace(Regex,…)` (M5),
   positional `arg`, Qt/wxWidgets conversions, and `wstring`/`wchar_t` (Windows).

7. **The script dictionary is named `Table` (design docs' "Map").** Renamed for
   source compatibility with Phonometrica, whose dictionary type is `Table`
   (`CID_TABLE`, `types/table.*`). The design docs still call it "Map"; the engine
   type is `Table`. It and **`Set` are unordered, reusing the one hash table
   (FlatHashMap).**
   The script `Map`/`Set` are a stable cell wrapping `FlatHashMap<Value,Value>` /
   `FlatHashSet<Value>` (keyed by `value_hash`/`value_equals`), with copy-on-write
   in the wrapper. This follows architecture §4 ("this single implementation backs
   … the script Map type") and drops the **insertion-ordered iteration** that
   §5.2 called for — a decision the owner made on 2026-07-04 after confirming that
   neither Phonometrica (`Table` wraps one unordered `Hashmap`; `Set` is a sorted
   `std::set`) nor calao (one cell-headed open-addressing table) provides ordered
   iteration. One hash algorithm serves both internal use (atom table, future
   dispatch memo/compiler scopes) and the script types; the cell stays put on
   growth (only the table's buffer moves), so no slot rewriting is needed (§5.0).
   `keys()`/`values()`/`to_list()` iterate in unspecified order.

## Source layout and includes

**Sources live under `phon/`, included as `<phon/subdir/file.hpp>`.** Architecture
§0 sketches an `include/phon/` (public headers) + `src/` (implementation) split.
Instead the engine follows Phonometrica's own convention: everything under a
top-level `phon/` directory with `.hpp`/`.cpp` together, included via
`#include <phon/base/…>` etc. (the repo root is the include path). This makes the
eventual drop-in replacement of Phonometrica's `phon/runtime/` mechanical and
keeps include style identical across the two codebases. The architecture's layer
names (`base`, `core`, `object`, `dispatch`, `memory`, `types`, `runtime`) are
preserved as `phon/<layer>/`. A public-facade split (`phon/string.hpp` →
`<phon/types/string.hpp>`, as Phonometrica does) can be added at the embedding
milestone (M8).

## M2 — Type system + dispatch

1. **Two id spaces: a stable id in cells, a separate interval id for subtyping.**
   §6 uses one `id` for both the cell-header class tag and the subtype interval,
   but renumbering the interval would invalidate every existing instance's header.
   The engine splits them: `Class::id` is the stable registration index (in Cell
   headers, hard-coded in opcodes, never changes); `Class::interval_lo/hi` is the
   pre-order interval, renumbered on each class addition. `is_a` uses the interval;
   `class_of` returns the stable id. Builtins register first and sealed, so their
   intervals equal their stable ids (0..10) and never shift.

2. **Class descriptors are plain structs; class-objects are separate cells.**
   §6 shows `struct Class { Cell header; … }`. To avoid migrating every static
   builtin registration to cell allocation, `Class` stays a plain descriptor and a
   `ClassObjectCell { Cell; Class* desc; }` wraps it when a class is used as a
   Value (`class_object(C)`). Each class has a lazily-created **leaf metaclass**
   under the root metaclass `Class` (CID_CLASS); the class-object's dispatch class
   is that metaclass, which is what makes `cast(x, Float)` resolve to a
   Float-specific method. Full cell-headed `Class` can come later if needed.

3. **Ambiguity is detected at definition (Julia-style meet rule), returned not thrown.**
   `add_method` compares the new signature pairwise against existing ones (same
   arity + ref-mask). Two incomparable signatures conflict only if they *overlap*
   (comparable at every position); the overlap needs a method equal to the
   pointwise meet, else `add_method` returns `AddMethod::Ambiguous`. This follows
   §7 (definition-time detection) rather than calao's resolution-time tie-break,
   and returns a status (the script-level error surface is M5).

4. **Memo tables cover arity 1–2 with exact keys; arity 0 and ≥3 resolve fully.**
   Arity-1/2 keys pack `(class_id<<1 | ref_bit)` per position into a uint64
   losslessly. §7's "hash of the id tuple" for arity ≥3 is deferred (a hashed key
   risks collision→misdispatch); those arities re-resolve each call. Memo
   self-invalidates on `generic_epoch` (method-set change) or `type_epoch` change.

5. **Inline caches and Callable binding are deferred to later milestones.**
   The per-call-site inline cache (§7.1) needs the VM (M4) — M2 has no call sites.
   A `Method`'s `code` is an opaque `void*` in M2; binding it to a Routine /
   NativeFunction (and actually invoking) lands with the VM (M4) and the typed
   embedding API (M8). M2 resolves methods but does not call them.

## M3 — Front end

### Step 1 — Scanner

1. **Front end lives in `phon/compile/`.** Architecture §0 lists the pipeline under
   `compile/`; the tree places it at `phon/compile/` per the `phon/<layer>/`
   convention already adopted (see "Source layout and includes"). Files:
   `token.*`, `source.*`, `diagnostic.hpp`, `scanner.*`.

2. **Grammar follows design/design.md, not old Phonometrica.** The scanner targets
   the *new* language, so its lexeme set diverges from Phonometrica's
   `token.hpp`: added `var/const/local/global`, `is`, `cast`, `div/mod`,
   `method/field`, `spawn`, `import`, `finally`, `open`; dropped `foreach`,
   `let`, `downto`, `option`, `print` (now an ordinary function), `assert`,
   `debug`, `pass`, `super`, `inherits`, `nan`, and the `%`/`~`/`|`/`@` operators
   (bitwise ops are library functions per design §12). `->` is the lambda arrow.
   `break`/`continue` are kept as keywords though design §12 does not list them
   (every loop construct needs an exit; the reference AST has `LoopExitStatement`).

3. **Two string families, single-line and triple-quoted.** `"…"` / `"""…"""`
   process escapes and interpolate with `{expr}`; `'…'` / `'''…'''` are raw (no
   escapes, no interpolation) for regexes/Windows paths. The single-line forms may
   not span a physical line; the triple-quoted forms may (owner requested the
   Phonometrica-style multi-line strings on 2026-07-05, so design §12's "two string
   literal forms" is read as two *families*). A literal `{`/`}` in a double-quoted
   string is written `\{` / `\}`. Unknown escape sequences are a **hard error**
   (design principle "start strict"), unlike Phonometrica which passed them
   through verbatim.

   The `...` token (`Lexeme::Ellipsis`) lexes the variadic-parameter suffix
   (`values as Object...`, design §6/§12) and the call-site splat (`f(xs...)`).
   `..` remains unspent (design §15 defers a Range type) and is a scan error.

4. **Interpolation lexes to `InterpStart`/`InterpMid`/`InterpEnd` markers**
   (PEP-701 style) rather than desugaring into a synthetic `( "…" & (expr) & … )`
   token stream in the scanner (Phonometrica's approach). The embedded
   expressions are ordinary token runs delimited by the markers; the parser
   (step 3) assembles a dedicated `StringInterpolation` AST node, which keeps
   golden AST dumps clean and readable. Nesting (a string literal inside `{…}`) is
   supported via a per-interpolation brace-depth stack in the scanner.

5. **Newline/continuation handled in the scanner.** Insignificant newlines
   (line continuations, blank lines, leading newlines, and any newline inside
   brackets or an interpolation expression) are suppressed, so every `Newline`
   token the parser sees is a real statement separator (architecture §9.1). A line
   continues iff it ends with an operator, a comma, a colon/dot/arrow, or an
   opening bracket (design §12). `;` is also a separator.

6. **`Source` scans the whole UTF-8 buffer in one pass** (via `unicode::decode`),
   tracking line starts for diagnostics, rather than Phonometrica's line-by-line
   `SourceCode`/`Array<String>` model (Array is an M6 type). Errors throw
   `SyntaxError` (compiler-layer exceptions are permitted, architecture §0)
   carrying a formatted `[Syntax error] …` message plus (line, column, length).

### Step 2 — AST

1. **`unique_ptr` tree, not an arena.** Architecture §9.1 calls for a bump-allocated
   AST freed wholesale. Step 2 uses `AutoAst = std::unique_ptr<Ast>` and
   `AstList = std::vector<AutoAst>` (Phonometrica's model), which is simplest and
   correct; the AST is transient (built → lowered → discarded). The arena is a
   compiler-throughput optimization deferred to a later pass — it can be dropped in
   behind the `AutoAst`/`AstList` aliases without touching node definitions.

2. **RTTI-free node tagging.** Every node carries a `NodeKind` set at construction;
   `is<T>()`/`as<T>()` compare `kind` against `T::KIND` instead of `dynamic_cast`
   (Phonometrica's AST used `dynamic_cast`). Cheaper, and keeps the door open to
   using the AST from lower layers. The visitor still drives traversal.

3. **Grammar-faithful node set for the new language.** Distinct nodes for the
   design-doc constructs that Phonometrica lacked or spelled differently:
   `IsExpression`, `CastExpression`, `SliceExpression`, `SplatExpression`,
   `RefExpression`, `NamedArgument`, `ConditionalExpression` (the `if…then…else…end`
   *expression* of §13), `ForNumeric` vs `ForEach`, `TryStatement` with multiple
   `CatchClause`s + `finally`, `SpawnStatement`, `ImportStatement`,
   `FieldDeclaration`. `[…]` is a `ListLiteral` and `{…}` a `TableLiteral`/
   `SetLiteral` (reversed from Phonometrica, where `[]`=array, `{}`=list); there is
   no array-literal node (arrays come from library functions until M6).

4. **No multiple declaration / multiple assignment.** The new design has no
   `x, y = a, b` or multi-`var` form (the only multi-binding construct is
   `for k, v in`), so `Declaration` and `Assignment` are single-target — unlike
   Phonometrica's `Declaration`/`MultiAssignment`.

5. **Lambdas and anonymous functions share `FunctionDefinition`** (`name ==
   NO_SYMBOL`). The thin-arrow `x -> e` is parsed to an anonymous definition whose
   body is `return e`, so there is one closure node rather than a separate lambda
   node.
