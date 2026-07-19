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
   *Update (Phonometrica-cutover pass):* `split`/`join`, `replace(Regex,…)`,
   `arg`, and the Qt conversions are now done — see "Embedding gaps for the
   Phonometrica cutover" (items 36–39); wxWidgets and `wstring` remain deferred.

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
`#include <phon/engine/base/…>` etc. (the repo root is the include path). This makes the
eventual drop-in replacement of Phonometrica's `phon/engine/runtime/` mechanical and
keeps include style identical across the two codebases. The architecture's layer
names (`base`, `core`, `object`, `dispatch`, `memory`, `types`, `runtime`) are
preserved as `phon/<layer>/`. A public-facade split (`phon/string.hpp` →
`<phon/engine/types/string.hpp>`, as Phonometrica does) can be added at the embedding
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

1. **Front end lives in `phon/engine/compile/`.** Architecture §0 lists the pipeline under
   `compile/`; the tree places it at `phon/engine/compile/` per the `phon/<layer>/`
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

### Step 3 — Parser

1. **Precedence-climbing recursive descent** (`parser.{hpp,cpp}`), single-token
   lookahead (`peek()`) over the Scanner. The ladder, lowest→highest:
   `or < and < not < comparison/is < & < additive < multiplicative < unary(± ) <
   ^ (right-assoc) < postfix(call/index/field) < primary`. `&` binds below
   arithmetic and above comparison (design §12: `"t: " & n + 1` → `"t: " & (n+1)`);
   `^` is right-associative and binds tighter than unary `-` (`-2^2` = `-(2^2)`).
   `cast`, the `if…then…else…end` expression, and anonymous `function…end` are
   primaries; `cast x as T` parses its operand greedily up to `as`.

2. **`this`, `break`/`continue` validity checked at parse time.** The parser
   tracks method-nesting and loop-nesting depth and rejects `this` outside a
   method and loop control outside a loop, with the position on the offending
   keyword. (Closures lexically inside a method still see `this` — a stricter
   check needs scope resolution, deferred to M4.)

3. **Parameter-order and call-argument rules enforced in the parser** (design §6):
   required params cannot follow options; only options may follow the variadic;
   at most one variadic; a variadic is neither `ref` nor defaulted; a positional
   argument cannot follow a keyword argument. The **closed method-name set**
   (`init`/`to_string`/…) and full name-resolution are semantic checks deferred to
   lowering (M4); the parser accepts any `method` name.

4. **Interpolation → `StringInterpolation`.** The parser consumes the scanner's
   `InterpStart`/`Mid`/`End` markers, parsing an ordinary expression between each,
   and drops empty literal chunks so `"{x}"` yields a single-part node.

5. **Numeric literals parsed to values at parse time** (`from_chars` after
   stripping `_`); an out-of-range integer or malformed float is a syntax error.
   The 47-bit inline-integer range check (design §4) is left to lowering/runtime.

6. **Operator overloading** parses (`function +(a, b) …`): a function name may be
   an identifier or an overloadable operator token, interned to a Symbol by its
   spelling.

### Step 4 — AST dumper + golden corpus

1. **Structural (position-free) AST dumps.** `ast_printer.{hpp,cpp}` renders a node
   tree showing kinds/attributes but not source positions, so goldens are stable
   under reformatting; positions are locked separately by the parser's
   error-position tests. `test/golden/ast/*.phon` (one per construct family) pair
   with checked-in `*.ast`; `test_golden_ast.cpp` compares them and regenerates
   with `PHON_UPDATE_GOLDEN=1`.

2. **Reserved words are never usable as identifiers** (owner decision, 2026-07-05).
   An earlier attempt to allow keywords as names in "unambiguous" positions was
   reverted. Consequently the `print` newline option is named **`end_line`**, not
   `end` (design §12 updated: `print(values as Object..., sep as String = " ",
   end_line as String = "\n")`).

3. **`...` marks both a variadic parameter and a call-site splat** (per design
   §6/§12). `name as T...` in a definition packs the remaining positional
   arguments; `f(xs...)` at a call site splats a List into positional arguments,
   parsed to a `SplatExpression`. No deviation.

## M4 — Core VM

### Scope

M4 delivers the register bytecode pipeline (opcodes, `Proto`/chunk, disassembler,
lowering, interpreter) for: literals, arithmetic/comparison/logic, `&`/concat and
interpolation, module/local variables, assignment and compound assignment, `if`/
`elsif`/`else`, the `if…then…else` expression, `while`, `repeat`, numeric `for`
(with `step`, `break`, `continue`), functions, recursion, closures/upvalues,
anonymous functions/lambdas, direct and generic (builtin) calls with per-call-site
inline caches, list/table/set literals, integer/key/grapheme indexing, `is`, and a
small builtin library (`print`, `len`, `assert`). Deferred **within** M4 to the
milestones that own them, each rejected at compile time with a `[…]` message:
classes/fields/`this`/constructors, `cast`, `try`/`throw`/`finally`, `for … in`
(iteration protocol), `ref`/splat arguments, named call options, script-defined
default/variadic parameters, `spawn`, and `import`.

1. **Script functions are module/local bindings, not open generics (yet).** Design
   §6 makes every named function a method on a generic. In M4 a top-level
   `function f` is a **module binding** holding a Closure (`GET_MODULE` + `CALL`);
   a nested named function is a **register local**. This gives calls, recursion,
   mutual recursion (via two-pass hoisting), and closures without the registration
   journal (§11) or class-based dispatch, and avoids cross-run pollution of the
   process-global generic tables in the unit tests. Registering *script* functions
   as generic methods (so user code can overload builtins and dispatch on
   annotated types) lands in M5 with classes and the journal. Consequently `CALL_G`
   is exercised only by the **builtin** generic library (`print`, `len`, `assert`)
   and is where inline caches live.

2. **Operator/`&`/comparison/index slow paths are handled in C++, not by
   dispatching to operator generics.** The specialized opcodes (`ADD`, `LT`,
   `CONCAT`, `GET_INDEX`, …) inline the builtin numeric/string/list/table cases and
   raise a `[Type error]` otherwise. Operator overloading via `function +(a as
   Fraction, …)` (design §12) needs user classes and is an M5 concern; until then
   no operator falls back to `CALL_G`.

3. **`to_string` is a C++ function, not yet the generic.** `&`, `print`, and
   interpolation stringify through `stringify` (builtin coverage: numbers,
   bool, null, symbol, String, List, Table, Set, functions, class objects). The
   `to_string` generic with user `method to_string()` overloads arrives in M5.

4. **Switch-dispatch interpreter loop, not computed-goto.** Architecture §10.3
   specifies computed-goto threading (switch fallback for MSVC). M4 uses a portable
   `switch` for correctness-first; each opcode body is still a self-contained unit,
   so the computed-goto conversion (and the copy-and-patch JIT door it keeps open)
   is a mechanical M8 performance change.

5. **Manual register refcounting is naïve (no elision).** Every register write
   releases the previous occupant and retains the new value; call frames release
   their registers on return (nulling as they go, so the overlapping caller/callee
   register windows never double-release). Design §3.1's refcount elision on
   borrowed locals is an M8 optimization. Verified leak-clean under ASan/UBSan.

6. **Fixed 64K-slot register stack, no growth.** Architecture §10.2 calls for a
   geometrically grown stack with base fixup. M4 pre-allocates 64K `Value`s and
   raises `[Runtime error] stack overflow` past it; growth + base fixup is deferred
   (rare in practice; fib/loop benchmarks fit easily).

7. **Inline caches are isolate-local, keyed by Proto pointer.** Per architecture
   §10.4 chunks stay immutable and shareable; the IC table lives in the `Isolate`
   (`ics` vector + a Proto→ic-base map assigned on first execution). Monomorphic in
   M4 (one cached arg-class tuple + resolved Callable, guarded by type/generic
   epochs); polymorphic (2–4 way) ICs are an M8 tuning knob.

8. **Line table is one entry per instruction, not run-length encoded.**
   Architecture §9.3 specifies an RLE debug line table; M4 stores a parallel
   `Vector<uint32_t>` of source lines (simplest; used for error positions). RLE
   compaction is deferred with the M5 backtrace work.

9. **Proto tree owned by `unique_ptr`, borrowed by closures.** A `Proto` owns its
   nested prototypes as `Vector<unique_ptr<Proto>> children`, and `CompiledModule`
   owns the module Proto as a `unique_ptr` (singular ownership of non-Cell objects
   uses `unique_ptr` per architecture §0). A `ClosureCell` holds a non-owning
   `Proto*` borrow; for M4 (a module runs to completion under one `do_string`) the
   tree outlives every closure. Refcounted or interned Protos (needed once closures
   can be stored across module reloads) are a later concern. The Isolate register
   stack is a `unique_ptr<Value[]>` and `do_string` owns the module closure via a
   `Handle<ClosureCell>` (RAII) — no owned pointer is held raw.

10. **Numeric `for` loop variable is one shared register across iterations.** A
    closure created inside the loop that captures the loop variable sees its final
    value (Lua ≤5.3 / JS `var` semantics), because M4 does not emit a per-iteration
    `CLOSE`. Per-iteration fresh bindings can be added compatibly if wanted.

11. **`div`/`mod` require Integer operands; `^` and `/` yield Float.** `div`/`mod`
    raise `[Type error]` on non-integers in M4 (float floor-div/`fmod` can be added
    later). `^` always produces a Float (`2 ^ 10 == 1024.0`); `/` always produces a
    Float per design §4. Integer `+ - *` overflow raises `[Math error] integer
    overflow` (design §4) via `__builtin_*_overflow`.

12. **Files placed under `phon/engine/vm/` and `phon/engine/compile/`.** New: `phon/engine/vm/`
    (`opcode`, `proto`, `function`, `isolate`, `interpreter`) and
    `phon/engine/compile/{lower,disassembler}`, plus the `phon/engine/runtime/runtime.*` façade
    (`Runtime`, `do_string`, `init_runtime`, builtins) standing in for the full
    `Runtime` (M8). The per-thread heap/arena allocation path (architecture §8.1) is
    still the M1 FOREIGN `sys_alloc` path; the Isolate-arena switch is orthogonal
    and unchanged by M4.

13. **`Runtime` is a persistent session; only the REPL/console surface is
    implemented.** Mirroring Phonometrica's old API, `Runtime` is a long-lived
    object owning one Isolate (§10.1) and a persistent `ModuleNamespace`; `do_string`
    compiles each chunk against that namespace (new bindings append slots, existing
    indices never move — design §11) and runs it on the session's Isolate, so
    module-level state persists across calls. This is the **REPL/console surface**
    (design §11). The other two surfaces are deferred: the editor's "run script"
    (fresh module instance per run) and the **registration journal** (retract a
    prior run's methods/classes/globals on reload). Consequently the compiled-chunk
    history grows unbounded within a session — every chunk's Proto tree is retained
    so closures stored in module slots keep a valid Proto; unloading it is the
    journal's job (M5+). The interactive leniencies (auto-declare on bare
    assignment, redeclaration-rebinds, bare-expression-prints) are not gated by mode
    yet: bare expressions always yield their value (used as the chunk result), and
    redeclaring a top-level `var` rebinds its slot rather than erroring. The free
    `do_string(src)` runs one-shot in a throwaway session; `Isolate` globals
    (`global var`) and `add_global` (embedder injection) arrive in M5/M8.

## M5 — Objects and errors

### Step 1 — Named functions become generic methods (function→generic migration)

This completes M4 deviation #1: a top-level `function` is now a method on a generic
(design §6), not a module binding. It reverses that deviation's interim model and
required a minimal registration journal (design §11), so the mechanism choices are
recorded here.

1. **A new `DEFMETHOD A Bx` opcode registers a closure as a generic method at load
   time.** Registration is a *runtime* effect (the closure only exists once `CLOSURE`
   runs), so pass 2a of the top level emits `CLOSURE` + `DEFMETHOD` instead of
   `CLOSURE` + `SETMODULE`. `Bx` indexes a per-`Proto` `method_defs` table
   (`{Symbol name, class-id signature, ref_mask}`); the interpreter rebuilds the
   `Class*` signature from stable ids and calls `add_method`. Signatures come from
   parameter annotations resolved to builtin classes (or `Object` when unannotated);
   `ref_mask` is 0 until `ref` parameters land (iteration/ref step). An ambiguous
   definition raises `[Type error] ambiguous definition of '…'` at load.

2. **The registration journal lives on the `Isolate` and retracts on teardown.**
   Because the generic registry is process-global, script methods would otherwise
   leak across independent runs (the exact pollution M4 #1 avoided). `DEFMETHOD`
   records each `{generic, sig, ref_mask, closure}` in an `Isolate` journal and takes
   the closure's +1; `~Isolate` (and an exposed `retract_journal()` for the editor
   reload surface) removes the methods and releases the closures, restoring the
   global generics to their builtin-only baseline. Since every `do_string` runs on a
   fresh `Runtime`/`Isolate` destroyed on return, unit-test isolation is preserved.
   Full per-run journaling of classes and globals (and the editor "fresh module per
   run" surface) is still deferred.

3. **`local function` stays a module-private binding, not a generic method.** A
   private method on a shared generic is not a coherent concept (design §11), so only
   non-`local` top-level functions migrate; `local function` keeps the M4
   `CLOSURE` + `SETMODULE` module-slot form. Nested named functions remain register
   locals and anonymous functions remain register closures — unchanged.

4. **An emptied non-builtin generic resolves as undefined.** Rather than erase
   generics from the global registry on retraction (which would dangle journal
   pointers), name resolution treats a generic with zero methods as not-a-name, so a
   retracted function reads as undeclared again on a later compile. Builtin generics
   always retain their natively-registered methods, so they are never emptied.

5. **`remove_method` uses swap-and-pop.** Method order does not affect resolution
   (most-specific is found by pairwise comparison), so retraction erases via
   `erase_unordered` and recomputes the generic's `min/max_arity`.

6. **A bare named-function reference is still not a first-class value.** `var g = f`
   where `f` is a generic errors `"…cannot be used as a value yet (M8)"` — the
   pre-existing policy for builtin generics, now covering script functions too.
   Functions-as-values are an M8 concern.

### Step 2 — User-defined classes and fields (checkpoint 1b)

Implements `class`/`ref class`, `field`s, instances, `this`, constructors, class
`method`s (as generic methods), and `cast`. Instance layout follows architecture
§5.6 (`Cell | Value[]`, `object/instance.*`); a `class` is a value type with
copy-on-write, a `ref class` has identity.

1. **A class is a module binding holding its class object; registration is a
   runtime `DEFCLASS`.** Pass 1 reserves a module slot for each top-level class and
   records it (with its method names) so forward references resolve; a class pass
   emits `DEFCLASS` (calls `add_user_class`, builds the field layout) then
   `SETMODULE`. User classes are therefore **module-scoped**, resolved through the
   namespace, never through `class_by_name` — which is now restricted to builtins.
   This keeps a stale user class in the process-global registry from being matched
   by name in a later run (test isolation), at the cost of the registry
   accumulating dead descriptors (class retraction/dead-marking, design §11, is
   deferred; user classes are not journaled yet).

2. **A type annotation is a `TypeRef`, resolved at load.** A builtin/`Object` is a
   `Concrete` stable id; a user class is a `ModuleSlot` reference resolved from the
   class object at `DEFCLASS`/`DEFMETHOD` time (a class's own id is only assigned at
   load). `MethodDef` signatures use `TypeRef`, so `function speak(d as Dog)` and
   `method init(this as Fraction)` both dispatch on user classes. Cross-module
   (imported) class types and forward references to a class declared **after** its
   use are deferred (declaration order is required for base/field/param class refs).

3. **Construction avoids `ref` via a return-`this` constructor.** `Foo(args)` lowers
   to `NEW` (a fresh, uniquely-owned instance placed directly in `init`'s `this`
   slot) then `CALLG init(this, args…)`; `init` mutates the unique instance in place
   and **implicitly returns `this`**, which is the construction result. So `this` is
   an ordinary by-value first parameter (register 0), not a second-class reference —
   general `ref` parameters and `this`-writeback for *mutating* methods on an
   existing shared instance (e.g. a user `set_item`) are deferred to the ref/
   iteration stage. Top-level `obj.field = v` still gets full value semantics:
   `SETFIELD` detaches a shared value-class instance (CoW) and the object is written
   back to its binding, mirroring index assignment.

4. **Field defaults are applied at construction, for the whole layout, before
   `init`.** `Foo(args)` lowers to `NEW` + `emit_full_defaults(Foo)` (walking the
   base→derived chain, emitting `this.<field> = <default>` for every field with an
   initializer) + `CALLG init`. Decoupling defaults from `init` makes **inherited
   defaults** and **constructor inheritance** both work: a subclass with no `init`
   dispatches an applicable base `init` (`this as Base` matches a subclass instance,
   design §6), yet the subclass's own fields are still defaulted. A synthesized
   `init()` is emitted only when the class writes none, so defining any `init`
   removes the implicit no-arg constructor. An uninitialized field is `null` (owner
   decision — annotations are advisory). Defaults are arbitrary runtime expressions
   evaluated at the construction site's scope; they may not reference `this` or
   other fields. Defaults for an **imported** base are still deferred with imports.

5. **`&`/`print`/interpolation dispatch a user `to_string` (M4 #3 follow-up done).**
   The interpreter loop was refactored into a re-entrant `run(iso)` (unwinds to the
   frame depth it started at) plus a `vm_call` seam that places a nested frame above
   the caller's registers and runs a closure to completion — the general C++→script
   callback path. `stringify_dispatch` uses it: for a user-class instance with a
   **script** `to_string` method it invokes that method; a builtin `to_string`
   native forwards back to `stringify` (so it is deliberately skipped to avoid
   recursion). A builtin `to_string(Object)` generic makes explicit `to_string(x)`
   work for every value.

6. **`cast x as T` lowers to the `cast(x, T)` generic (design §7).** The builtin
   `cast` is a checked, identity-preserving downcast (`value_is_a` → `x`, else a
   `[Type error]`); type-specific conversions (e.g. Integer→Float, `String(x)`) are
   library overloads added later. Construction of a **builtin** class as a
   conversion (`Float(x)`) is not handled here.

7. **`this` in a nested closure resolves as an upvalue.** `this` is a hidden
   `const` local at register 0 named by the reserved `this` keyword, so a closure
   lexically inside a method captures it as an upvalue automatically — closing the
   M3-parser gap that deferred this to scope resolution.

### Step 3 — Field accessors `get`/`set` (checkpoint 1c)

Implements the design's "Field accessors": a `field` may carry `get` and/or `set`
blocks that intercept reads and writes.

1. **`get`/`set` are contextual keywords.** They are lexed as ordinary identifiers;
   the parser recognizes them only at the head of an accessor block inside a field
   body (`parse_field` uses one-token lookahead over the newline), so `get`/`set`
   remain usable as names elsewhere. An accessor body is parsed with the method-depth
   bumped so `this` is valid inside it. `FieldDeclaration` gains `getter`/`setter`
   bodies + the setter's parameter.

2. **Accessors are closures stored in the field descriptor.** `compile_accessor`
   compiles each `get`/`set` body to a module child proto (getter: `(this)`; setter:
   `(this, v)`); `FieldDef` records their indices, and `DEFCLASS` builds the closures
   (`make_closure`, kept alive for the Isolate's lifetime via `keep_alive`) into the
   `FieldInfo`. Inheritance is automatic — `add_user_class` copies base `FieldInfo`s,
   accessor cells included.

3. **`GETFIELD`/`SETFIELD` route; `GETFIELDRAW`/`SETFIELDRAW` reach the slot.** A read
   with a getter invokes it via the re-entrant `vm_call`; a write with a setter
   invokes it. A write to a getter-only field is a `[Name error] … is read-only`. The
   **recursion-safety rule** — inside a field's own accessor, `this.<that field>` is
   the raw slot — is implemented by the lowerer emitting the RAW opcodes when the
   accessed field matches the accessor being compiled (`m_accessor_field`).

4. **A setter returns `this`.** Because a value-class receiver passed to the setter
   is refcount-bumped (so its `this.<backing> = …` write copy-on-write **detaches**),
   the setter implicitly returns the (possibly detached) instance, and `SETFIELD`
   writes that back to `R[A]` for the caller's binding write-back — mirroring the
   return-`this` constructor. One clone per setter call on a value class is accepted
   (optimizable once `this` can be a true reference).

5. **Uncaught errors now release the live register stack** (`Isolate::unwind_on_error`,
   called from `do_string`'s catch): a minimal stand-in for arch §10.5 handler-stack
   unwinding, so an aborted run (e.g. a read-only write) leaks no cells. Full
   `try`/`catch`/`finally` unwinding arrives with M5 errors (Stage 2).

### Step 4 — Private fields (`local field`)

A `local field` (owner request) is private: reachable only through `this`, and
**protected-style** — a subclass's methods reach an inherited private field through
`this`, but no external `obj.field` can (design "Private fields").

1. **Enforcement is a runtime check bypassed by the raw opcodes.** `FieldInfo` gains
   `is_private`; `GETFIELD`/`SETFIELD` raise `[Name error] … is private` on a private
   field. `this.<private field>` compiles to `GETFIELDRAW`/`SETFIELDRAW` (which skip
   both the accessor and the privacy check), so in-class access just works — reusing
   the raw path already built for the accessor recursion rule. The lowerer chooses
   the raw path when the object is literally `this` and the field name is in the
   class's private set (its own `local field`s plus, protected-style, those of its
   in-module base chain, gathered into `m_local_fields` per class).

2. **Field defaults now use `SETFIELDRAW`.** Construction runs outside the class, so
   a private field's default would otherwise trip the privacy check; the raw store
   also correctly bypasses any setter during default initialization.

3. **A `local field` may not declare accessors** (parse error) — private storage
   behind public accessors is incoherent (matches the design). `local` applies only
   to `field` in a class body (not `method`).

## M5 — Errors (Stage 2)

Implements the `Error` hierarchy, `throw`, and `try`/`catch`/`finally` (design §12,
architecture §10.5).

1. **`Error` is a builtin value class with a `message` field (`CID_ERROR`).** It goes
   through the ordinary instance machinery (`make_instance`, field layout, instance
   hooks), so user error types are plain subclasses (`class IOError is Error`) that
   inherit the `message` field and a native `init(this as Error, message)` — giving
   `IOError("…")` construction by constructor inheritance. `Error` and its subtree are
   the only builtin classes `NEW` will construct. Builtin errors raised by opcodes and
   natives (`iso.raise`) now create an `Error` instance via `make_error`, so every
   `[Type error]`/`[Math error]`/… is a catchable `Error` (category-specific builtin
   subclasses — `TypeError`, `ValueError`, … — are a later refinement; for now all
   builtin errors are the base `Error`).

2. **`is_instance` keys on the instance finalizer, not `CLASS_BUILTIN`.** Because
   `Error` is a builtin *and* field-bearing, "does this cell have fields?" is decided
   by `class->finalize == instance_finalize` (set by `add_user_class` and Error's
   registration), which the field opcodes and stringify use.

3. **A per-`Isolate` handler stack drives unwinding.** `PUSHTRY A sBx` records
   `{frame_depth, base, catch-dispatch ip, err_reg A}`; `POPTRY` drops it on normal
   completion; `THROW A` (and every `iso.raise`) throws the C++ `RuntimeError` carrying
   the `Error` value. The interpreter loop runs inside a retry `try`/`catch`: a caught
   `RuntimeError` whose innermost handler belongs to this run unwinds the frames above
   the try (releasing their registers, closing upvalues), binds the error into
   `err_reg`, and resumes at the catch dispatch; otherwise it propagates (to an outer
   run or `do_string`). This unifies opcode raises, native raises, and re-entrant
   `vm_call` errors on one mechanism.

4. **`try` lowering uses a single shared `finally` and a `pending` register.** The body
   is bracketed by `PUSHTRY`/`POPTRY`; the catch dispatch tests each clause with `IS`
   (same interval check as `is`), binds the catch variable, and on no match sets
   `pending = error`. All paths fall into one `finally` block, after which a non-null
   `pending` is re-`THROW`n. Catch clauses are tried in order; a bare `catch` / `catch
   e` (no type) matches anything. `throw` of a non-`Error` is a `[Type error]`.
   Re-raising a caught error is just `throw e` — the design doc's `rethrow(e)` function
   was dropped (owner decision): control flow dressed as a function is a wart, and it
   was redundant with `throw e` (backtraces are captured once at first raise and
   preserved across re-throw, so `throw e` re-raises with the original origin). A bare
   `rethrow` keyword could be added compatibly if a reset-vs-preserve distinction is
   ever needed.

5. **`return`/`break`/`continue` inside a `try` run the enclosing `finally`s
   (finally-inlining).** The lowerer keeps a stack of enclosing `try`s (finally body
   + whether the handler is still live); an early exit emits, innermost-first, a
   `POPTRY` for each still-active handler and a copy of each `finally` block, then the
   `RET`/`JMP`. `return` unwinds to the function's try-depth; `break`/`continue` to the
   enclosing loop's. A `return` value is evaluated before the `finally`s run (Java
   order). `RET` also defensively discards any handlers left by the returning frame.

6. **Errors carry a backtrace, captured once at first raise** (`Error` gains a
   `trace` field at slot 1). `iso.raise` and the `THROW` opcode call
   `capture_error_trace`, which fills `trace` only if empty — so re-raising with
   `throw e` **preserves the original origin** (this is why no separate `rethrow` is
   needed). `Isolate::backtrace` walks the frame vector, using each caller frame's
   `ret_ip` against its line table to render `  at <fn> (line N)` per frame
   (`<module>` for the top level). `e.trace` is a plain (public) `String` field.

7. **`RuntimeError` carries the thrown `Error` value** alongside `message`/`line`.
   An uncaught error reaching `do_string` releases the live register stack
   (`unwind_on_error`, which also clears the handler stack) and the error value, then
   re-throws with `message`/`line` for the embedder.

## M5 — Iteration (Stage 3a: for … in over builtin collections)

Implements `for [k,] v in coll` for List, Table, and Set via the fast path
(architecture §10.3 / design §12): iteration state lives in hidden loop registers,
so there is no iterator object and no per-step dispatch.

1. **Two opcodes, `ITER_INIT A` and `ITER_NEXT A B C`.** `ITER_INIT` normalizes the
   collection in R[A] to an index-walkable form — a List stays put, a Set is
   materialized to a List (one allocation at loop entry), a Table keeps its cell and
   stashes its key List in R[A+2] — and zeroes the cursor R[A+1]. `ITER_NEXT` writes
   the value to R[A+3] and, when `C == 2`, the key/index to R[A+4], advancing the
   cursor; R[B] receives whether the sequence is exhausted. The lowerer follows it
   with `JMPT R[B] -> exit`. The loop variables are ordinary locals pinned to A+3/A+4
   so the opcode writes them directly. `break`→loop exit, `continue`→the `ITER_NEXT`.

2. **Pair form.** `for i, v in list` binds the 1-based index and the element;
   `for k, v in table` binds key and value (unspecified order — Table/Set are
   unordered, DEVIATIONS M1 #7). Single-var `for x in …` binds the element/member.

3. **Deferred to Stage 3b (with `ref`):** `for ref x in …` (by-reference iteration,
   rejected today), String (grapheme) iteration, and user-class iteration via the
   `iterate`/`next` generic protocol (`method iterate()` delegating to a builtin
   iterator). Mutating the collection mid-iteration is unspecified (a Set/Table
   iterates a snapshot; a List walks live by index).

## M5 — References (`ref` parameters, design §7)

> **Superseded** by "First-class references" (below). The second-class,
> register-pointer implementation described in this section was fully reversed:
> ref-ness is now uniform per generic (not a dispatch dimension), call-site `ref` is
> gone, and a reference is a heap box rather than a tagged register pointer. This
> section is kept for history.

Second-class references: `ref` at both the parameter and the call site, implemented
as a tagged pointer to a caller register slot (the REF value already in `value.hpp`).

1. **Three opcodes.** `MAKEREF A B` (R[A] = reference to register B, at the call
   site), `DEREF A B` (read through a reference; identity if R[B] is not a ref), and
   `SETREF A B` (`*R[A] = R[B]`, write through). A `ref` parameter's register holds a
   REF; the lowerer flags such locals (`Local::is_ref`) and routes every use: reads
   emit `DEREF` (and `expr_any` no longer borrows a ref local's register), plain and
   compound assignment emit `SETREF`, and `x[i] = v` / `x.field = v` deref to a temp,
   mutate (with copy-on-write detach for a value type), then `SETREF` the possibly
   detached object back through the ref (write-back mode `wb == 3`, alongside the
   existing module/upvalue write-backs).

2. **Dispatch keys on ref-ness.** A method's `ref_mask` is built from its parameters'
   `by_ref` flags (the implicit `this` is never `ref`), so `f(ref x)` and `f(x)` are
   distinct methods and a `ref` argument selects the `ref` overload. `CALLG` sends any
   call carrying a `ref` argument down the full-resolve path (the inline cache reads
   argument classes directly and would misread a REF).

3. **Value-class mutation through `ref` works now.** Mutating an element or field of a
   value-type variable passed by `ref` detaches an alias correctly (CoW), so a helper
   can mutate a caller's `List`/instance. This covers much of the Stage-1 "value-class
   method mutation" gap for the *explicit-`ref`* case; making a method's implicit
   `this` a reference (so a mutating `method` needs no `ref class`) is still a
   follow-up.

4. **Scope: `ref` names a local variable.** `ref` of a module or upvalue variable is
   rejected (a clear `[Compile error]`); it needs the load-temp + post-call write-back
   dance and is deferred. Second-class discipline (no storing/returning/capturing a
   ref) is by construction — `ref` is only accepted as a call argument — but is not
   yet fully enforced against pathological cases (e.g. capturing a ref local in a
   nested closure); documented as a follow-up. `for ref x in …` (by-reference
   iteration) and the user `iterate`/`next` protocol remain in Stage 3b.

## First-class references (design/references.md)

Replaces the M5 second-class references above. A reference is now a heap-allocated,
refcounted **box** that transparently stands in for its value (PHP 7+ `zend_reference`
/ Phonometrica `Alias`). The box **is** the `UpvalueCell` (an upvalue is a reference to
a captured variable), so it is cycle-collected by the existing Bacon–Rajan machinery
with no collector changes.

1. **Uniform ref-ness; not a dispatch dimension.** `ref_mask` moved from `Method` to
   `GenericFunction`; `add_method` rejects an overload whose mask disagrees
   (`AddMethod::RefMaskConflict`). Ref-ness is removed from the dispatch/memo keys and
   the `CALLG` inline-cache path; a reference argument dispatches on its referent's
   type. `ref` on a reference-class parameter is rejected at DEFMETHOD.

2. **No call-site `ref`.** The parser rejects `ref` at a call site; promotion is driven
   by the callee's signature. A compile-time `name → ref_mask` map (`compile/lower.cpp`)
   supplies the mask for a named generic; no forward-declaration rule is imposed (the
   existing two-pass hoisting already exposes it). Non-lvalue → a `ref` position is a
   compile error for a direct call.

3. **The box and `DEREF`.** `Value` tag `110` (`SIG_REFERENCE`, `core/value.hpp`) points
   at a box Cell; tag `101` (register-pointer ref) is retired. `deref` (single-branch,
   `core/reference.hpp`) reads through a reference from any layer. `MAKEREF` now promotes
   a local to an **open** box via `find_or_make_open_upvalue` (the caller's register stays
   the source of truth while its frame lives, so the defining function needs no deref of
   its own locals); `DEREF`/`SETREF` go through the box. All `retain_value`/`release_value`
   helpers were unified onto `Value::owns_cell()`/`cell_ptr()` (CELL *or* REFERENCE).

4. **Argument promotion (§6).** Direct calls promote lvalue args at compile-time-known
   `ref` positions (`MAKEREF` for a plain local, `MOVE` to forward an existing reference).
   Indirect calls (a callable held in a variable) consult the callee's `ref_mask` at
   runtime: `Proto::ref_mask` + `callable_ref_mask()`; opcodes `MAYBEPROMOTE` (lvalue) and
   `MAYBEBOX` (non-lvalue → a closed box with no write-back, `make_reference_box`).

5. **Container-element and field references (§7, CoW × ref).** `f(xs[i])` / `f(o.field)`
   promote the slot to a **closed** box stored in place (opcodes `PROMOTEINDEX` /
   `PROMOTEFIELD`), detaching a shared value-semantic container first so aliases keep the
   plain value. Reads deref (`GETINDEX`/`GETFIELD`); writes to a reference slot go through
   the box without detaching; clone shares the box, and trace/finalize account for it via
   `owns_cell`. `value_hash`/`value_equals` deref. **Scope:** List elements and instance
   fields; Table/Set/String element references raise a clear runtime error.

6. **Auto-collapse (§2).** Reading a stored reference (`deref_collapse` at
   `GETINDEX`/`GETFIELD`) whose closed box has no other referrer moves the value back out
   and drops the box, so a temporary borrow does not leave an element permanently boxed.

7. **By-reference iteration (§12).** `for ref v in xs` (and `for i, ref v in xs`) binds
   the value variable to each List element's storage — the loop-body analogue of a `ref`
   parameter. Opcodes `ITER_INITREF` (makes the List uniquely owned) / `ITER_NEXTREF`
   (boxes the current element, binds the value slot to it, index by value); the collection
   is written back to its binding after the loop, so a copy taken before the loop detaches.
   The `List` C++ accessors (`get`/`pop`) deref so by-value readers never see a box. Scope:
   List and Table collections bound to a variable (Table iterates a snapshot of its keys).

8. **References to any variable kind.** Beyond a local, a `ref` argument may now name an
   **upvalue** (an upvalue *is* a box, so `PROMOTEUPVAL` just references it) or a **module
   (global) variable** (`PROMOTEMODULE` boxes the slot in place; `GETMODULE` derefs and
   auto-collapses, `SETMODULE` writes through). Table **value** references (`f(t[k])`,
   `PROMOTEINDEX` Table branch) join List elements; a Table **key** is never referenceable
   (owner invariant), so only its value is boxed. `Table::get`/`List::get`/`pop` deref.

Follow-ups: Set and String element references (a set member / grapheme substring is not a
mutable slot — kept as a clear error); element/field references through an *indirect* call
(they currently box-without-write-back); the subtle CoW-×-reference corner where a reference
element is written through a still-shared container.

## M5 — Cycle collector (architecture §8.2)

The backup Bacon–Rajan synchronous collector (Bacon & Rajan 2001; Jones/Hosking/Moss,
_The GC Handbook_ p. 66 ff.), ported from Phonometrica's Recycler and adapted to the
new Cell header. Reference counting reclaims everything except cycles of otherwise-dead
objects; this reclaims those. Files: `phon/engine/core/cycle_collector.{hpp,cpp}`.

1. **The GC color lives in the Cell header, narrowing the refcount to 26 bits.**
   §3.2 sketched `rc_bits` as a 29-bit refcount + BUFFERED/FROZEN/SHARED_BUFFER and
   mentioned only the BUFFERED bit for the collector; but Bacon–Rajan needs a per-object
   color. `rc_bits` is now refcount (bits 0–25, 26 bits ≈ 67M, still saturating), a
   3-bit **color** field (bits 26–28), then BUFFERED/FROZEN/SHARED_BUFFER (bits 29–31).
   Colors: GREEN (acyclic), BLACK (assumed live), GRAY/WHITE/PURPLE (the collector's
   working states). The color is fixed at allocation from the class: acyclic classes are
   born GREEN, potentially-cyclic ones BLACK. `set_refcount`/`set_gc_color`/`set_buffered`
   edit their subfields without disturbing the others.

2. **`release` gained two collector seams (declared in cell.hpp, defined in
   cycle_collector.cpp).** A decremented-but-still-live non-GREEN cell that is not yet
   buffered calls `cc_possible_root` (Bacon–Rajan PossibleRoot: paint PURPLE, push onto
   the candidate buffer). GREEN cells and already-buffered ones skip this via a cheap
   inline color/flag test, preserving the acyclic fast path. When the *last* reference to
   a **buffered** cell drops, `release` cannot free it synchronously (its slot in the
   candidate vector would dangle), so `cc_collect_deferred` parks it (refcount 0, BLACK)
   for the next MarkRoots to dispose — the vector analogue of Phonometrica's O(1)
   intrusive-list removal in `~Collectable`.

3. **The candidate set is a `Vector<Cell*>` (architecture §8.2), not the intrusive
   doubly-linked list Phonometrica used** — the 8-byte Cell has no room for prev/next
   pointers. `collect()` therefore snapshots the roots (`std::move`) and processes the
   snapshot, so fresh candidates buffered by finalizers during the pass accumulate
   cleanly for the next one. Deferred (parked) frees run last, after CollectWhite, so a
   finalizer's releases cannot disturb the trial-deletion phases.

4. **Per-class `trace` and `gc_free` hooks (class.hpp).** `trace(self, visit)` enumerates
   a cell's contained *cell* children (immediates skipped); wired for List, Table, Set,
   user instances (incl. Error subclasses), Closure (its upvalues), and Upvalue (its
   closed value — an *open* upvalue points at a live stack slot and enumerates nothing).
   `gc_free(self)` frees a white cell's *auxiliary* buffer (Table/Set's hash storage)
   **without** releasing its entries; it is null for inline-payload types (List,
   instances, closures). CollectWhite frees via `gc_free`+`cell_free`, bypassing the
   finalizer, because the child edges were already balanced by trial deletion.

5. **Acyclic (GREEN) children stay out of the cycle machinery; CollectWhite releases
   them normally.** The `trace` hooks enumerate *all* cell children, including acyclic
   ones (a List's String elements). The collector's MarkGray/ScanBlack skip GREEN
   children (never trial-adjusting their refcount or recoloring them), and CollectWhite
   releases a GREEN child through the ordinary `release` path — so an acyclic object whose
   only referrer was a garbage cycle is reclaimed **through its finalizer** (correct for a
   future `File` etc.), not silently `cell_free`d. This is stricter than a naïve port,
   which would have flipped Strings' color to BLACK and skipped their finalizer.

6. **Collection triggers: loop back-edges, a `collect_garbage()` builtin, and Isolate
   teardown.** The interpreter services the collector at loop back-edges (a backward
   `JMP`), threshold-gated (`collect_if_needed`, default 1024 candidates) — the §9.4
   safepoint where all live values sit in counted registers. `collect_garbage()` forces a
   pass (for scripts/tests). The Isolate destructor releases its roots (journal, kept
   cells, register stack, then module slots) and runs `collect_until_stable()` so a run's
   cyclic garbage is reclaimed before teardown (verified leak-clean under ASan). Function
   call/return safepoints and byte-based thresholds (§8.2/§9.4) are deferred.

7. **The current collector is a process-global, save/restored around a run** (mirroring
   the process-global current Isolate). `do_string` and the Isolate destructor set/restore
   it so releases feed the running Isolate's collector even when several Runtimes coexist;
   the real `thread_local` wiring lands with concurrency (M7).

8. **Deferred / limitations.** MarkGray/Scan/CollectWhite recurse over the object graph
   (as in Phonometrica), so a pathologically deep graph can overflow the C++ stack — an
   explicit work-stack is a later hardening. Byte-count collection thresholds, the
   function-call safepoint, incremental/concurrent collection, and the freeze/transfer
   interplay (§8.3) are all future work.

## M6 — Variadic/default/named parameters, splat & named args, slices (design §6/§9)

Signature segments (design §6) are, in order: fixed positional parameters, one optional
trailing vararg (`name as T...`), then keyword-only options (`name as T = default`). The
parser already produced every M6 node; lowering, the Proto/dispatch structures, the calling
convention, and the List/String runtime were extended to execute them.

1. **Owner decisions (recorded 2026-07-07).**
   - **List slice assignment** (`a[i:j] = rhs`): an equal-length List rhs replaces the
     selected positions element-wise; any other rhs is a **scalar broadcast** filling every
     selected position (`a[i:j] = 0`). A length-mismatched List rhs is an `[Index error]`.
   - **String slices** are read-only substrings (grapheme-based, inclusive, negatives,
     `step`); assigning to a String slice is a `[Type error]` (a grapheme is not a mutable
     slot).
   - **Multi-dimensional indexing** (`m[:, 3]`) stays deferred — it belongs to `Array`,
     which does not exist yet; `a[i, j]` errors with "arrives with Array".

2. **Calling convention.** `CALL`/`CALLG` gained operand `C` = the number of keyword
   arguments; keyword args are staged after the positional args as `(Symbol, value)` pairs.
   Frame setup is centralised in `setup_callee_frame` (`vm/interpreter.cpp`), shared by the
   inline `invoke` path and the re-entrant `vm_call`: it validates arity, packs a trailing
   vararg into a List, binds keyword options by name (snapshotting the pairs first, since the
   option slots overlap the staged-pair region), and nulls the remaining locals. `vm_call`'s
   old `argc == num_params` assertion is gone (a user variadic reached re-entrantly no longer
   trips it).

3. **The missing sentinel.** An unsupplied option is marked with a new immediate,
   `Value::make_missing()` (`IMM_MISSING`, `core/value.hpp`) — never user-observable, and an
   immediate so refcount ops treat it like null. The callee's prologue tests each option slot
   with the new `JMPSET` opcode ("jump if the slot is *not* missing") and evaluates the
   default otherwise. Option defaults are emitted in declaration order, so a later default may
   reference an earlier parameter or option (`function span(lo, hi = lo)`).

4. **Options are keyword-only and do not dispatch.** A method's dispatch signature is its
   fixed params plus (if present) the vararg element type; options are excluded. So an option
   can never be filled positionally — `box(3, 2)` where `h` is an option is "no applicable
   method", not `h = 2`. An unknown keyword at a call is an `[Argument error]`. Natives take
   no keyword options in M6 (a keyword to a native is a clear error, not silently dropped).

5. **Variadic dispatch (design §6).** `Method`/`Proto` gained `is_vararg`; the vararg element
   type is the last signature entry. Applicability: `argc ≥ fixed` and every trailing argument
   subtypes the element type. Specificity is **type-primary** — pointwise subtyping decides
   first, and only when the effective types are identical does the design's kind tiebreak
   apply (a fixed method beats a variadic; among variadics more fixed params win, then the
   element type). This matches the design for every case it names and makes a principled
   choice for the type-differing case. Definition-time ambiguity detection was generalised to
   compare methods at a representative arity (the larger fixed count, +1 when both are
   variadic so element types are compared). The arity-8 dispatch cap was lifted (a variadic
   call can pass any number of arguments).

   *Known limitation:* two zero-fixed variadics with **incomparable** element types
   (`f(xs as Int...)` vs `f(ys as String...)`) collide only at `argc == 0` (the empty call),
   which the definition-time check does not flag; such an empty call resolves deterministically
   rather than raising an ambiguity error. Non-empty calls are unambiguous. There is also no
   memo for `argc ≥ 3`, so a variadic call with three or more arguments runs full resolution
   each time (a future tuning knob).

6. **Splat (`f(xs...)`, dynamic arity).** A call containing a splat builds one positional
   List — `NEWLIST` then `LISTAPPEND` for singles and the new `LISTEXTEND` for splats — and
   hands it to the new `CALLD` opcode, which unpacks it into the callee window (bounds-checked
   per element against the stack) and resolves through the generic **memo, never the inline
   cache** (design §6). Splat forwarding into a vararg (`print(vals...)`) composes; it pays a
   double copy (spread out, then re-packed into the callee's vararg List) — a move-through
   optimisation is deferred. Splat combined with keyword options, and ref-promotion of splat
   arguments, are **not** supported in M6 (splat args are values); both are documented compile
   limitations.

7. **Cycle-collector fix (general, surfaced by splat).** Reallocating a cell that is a live
   cycle-collection candidate (`cell_realloc` moving a buffered FOREIGN cell during CoW
   growth) left a dangling raw pointer in the collector's candidate buffer. `cell_realloc` now
   notifies the collector (`cc_cell_moved` → `CycleCollector::cell_moved`) to repoint the slot.
   This was a latent bug for any buffered List/Table/Set that grew past its capacity; splat's
   small combined List (initial capacity 1) was the first to hit it reliably. `LISTAPPEND`/
   `LISTEXTEND` additionally adopt the target's reference (`List::adopt`) instead of
   retain-then-release, so they no longer buffer the transient at all.

## M-Array — the numeric tower + the Array type (design §9, architecture §5.3)

The `Number → Real → {Integer, Float}` class hierarchy and the numeric `Array` type.

1. **Numeric class tower re-parents Integer/Float.** The builtin hierarchy is now
   `Object → Number → Real → {Integer, Float}` (owner decision). `Real` is the common
   base of Integer/Float (Phonometrica's old `Number` role); `Number` is the abstract top,
   leaving room for `Complex` as a future sibling of `Real`. Complex is **not** built now.
   This intentionally **reverses the architecture note "[INVARIANT] builtin classes are
   never re-parented"** — mechanically safe because subtyping is interval-based
   (recomputed by `renumber_types()`), the stable ids CID_INTEGER=3/CID_FLOAT=4 are
   unchanged, and the specialized arithmetic opcodes key on the NaN-box tag, not the class
   id. `Number`/`Real` are abstract (`CLASS_ACYCLIC`, no instances); `class_of` still
   returns the leaf (Integer/Float) for a value. New ids CID_NUMBER/CID_REAL (and
   CID_ARRAY/CID_ARRAYBUFFER) were appended before CID_BUILTIN_COUNT.

2. **Array = the architecture's view/buffer split (§5.3), not Calao's layout.** A
   value-semantic **view** (`ArrayCell`: buf ptr, offset, rank, flags, dim[8], stride[8])
   over a separately refcounted **buffer** (`ArrayBuffer`: inline `double data[]`), stored
   **column-major**, so slicing is a **zero-copy view** sharing the buffer. Element type is
   `double` only. Both cells are `CLASS_ACYCLIC` (a view holds only doubles + one buffer
   pointer that can never point back) → born GREEN → the cycle collector never touches
   them. `phon/engine/types/array.*`, registered in `bootstrap.cpp`.

3. **Copy-on-write checks BOTH view and buffer refcounts.** `Array::detach()` mutates in
   place only when the view is unique, its buffer is unique, and it is contiguous;
   otherwise it builds a fresh contiguous buffer (compacting a strided/shared view) before
   dropping the old one, cloning the view cell too when the view itself is shared. All rc
   reads go through `is_unique()`/`is_shared()` so the M7 atomic-shared-buffer swap is a
   one-line change. The view cell never grows/moves (fixed shape), so no
   `reset_reallocated`/`cc_cell_moved` interaction. Hand-rolled like `List` (no clone hook).

4. **`@[…]` literals (Calao syntax); integers promote to Float.** `@[1,2,3]` (1-D),
   `@[1,2;3,4]` (2-D, `;` = row separator, equal-length rows). New `@` scanner token,
   `ArrayLiteral` AST node, `NEWARRAY` opcode (`B` = element count, `C` = nrow with 0
   meaning 1-D). The literal is written row-major but stored column-major (the handler
   transposes); each element is coerced to `double` (a non-number element is a Type error).

5. **Indexing** (1-based, negatives from the end, bounds-checked). A single scalar index
   uses GETINDEX/SETINDEX (an `is_array` arm); multi-dimensional scalar indexing (`m[i,j]`)
   uses new `GETIDXN`/`SETIDXN` (rank in an EXTRA_ARG for the write). A scalar `a[i]` reads
   a **Float**. Writes are copy-on-write.

6. **Slicing = zero-copy views.** A single slice (`a[i:j]`) reuses GETSLICE (a 1-D view);
   multi-axis slicing (`m[:,j]`, `m[1:2,2:3]`) uses new `GETVIEW` (a 3-register slice-part
   block per axis + a scalar-axis bitmask in EXTRA_ARG; scalar axes collapse and drop from
   the result rank). Inclusive bounds, negatives, `step`. **1-D slice assignment** works
   (same-length Array element-wise, or scalar broadcast, mirroring the M6 List rule).

7. **No references to Array elements.** A raw `double` cannot be boxed into a reference
   Value, so `f(ref a[i])` and `for ref x in a` raise a clear runtime error in
   PROMOTEINDEX / ITER_INITREF (compile-time rejection is impossible — the lowerer does not
   know the container type).

8. **Elementwise arithmetic** extends the existing `ADD/SUB/MUL/DIV/POW` opcodes with an
   `is_array` arm (after the int/number arms, before list): array⊕array (shape-checked),
   array⊕scalar and scalar⊕array (broadcast). Kernels are free functions over contiguous
   `double` spans in the single file `phon/engine/lib/array_kernels.*` (architecture invariant);
   strided operands are gathered contiguous first via `Array::contiguous()`. `div`/`mod`
   and unary `-` on an Array raise (they were never numbers) — floored/negation variants
   are follow-ups.

9. **Also fixed (pre-existing, surfaced by Array).** `builtin_to_string` returned a
   *borrowed* pointer into a temporary `String` (`stringify(...).to_value()`); for a
   *freshly built* string (Array/List/Table) this was a use-after-free. Now it retains the
   cell so the native's result carries the required `+1`. `len()` gained an Array case.

**Deferred (documented, not blocking):** SIMD/vectorized kernels, the thread pool, and
64-byte AVX buffer alignment (scalar loops over normally-aligned buffers now — all M7);
boolean masks + logical indexing; **multi-dimensional slice assignment** (`m[:,j] = …`);
`Array` as a structural Table/Set key (identity hashing for now, parity with List — it has
an `equals` hook but no `hash` hook); floored `div`/`mod` and unary `-` on arrays;
compound assignment to a multi-dimensional index; freeze/transfer (M7).

## M7 — Concurrency (architecture §8.3, §9.4, §13)

Landed incrementally; each stage is green under the normal, ASan, and TSan builds
(the M7 acceptance bar). A `PHON_TSAN` CMake option builds with ThreadSanitizer.

### Stage 1 — thread-local plumbing + cooperative interruption

1. **`current_isolate` / `current_collector` are now `thread_local`** (was a
   process-global stand-in in M4/M5, DEVIATIONS "M4/M5 single-threaded"). Each script
   thread installs its own on entry.
2. **Safepoint interrupt.** `Isolate` gains an atomic poll word and
   `request_interrupt()` / `clear_interrupt()`; `Isolate::safepoint(line)` honours a
   pending interrupt (raising `[Interrupt]`) then services the collector. Wired at both
   the `JMP` **and** `FORLOOP` back-edges — architecture §9.4 says "loop back-edges", and
   the counted loop has its own back-edge opcode, so it needs the check too. Exposed to
   embedders as `Runtime::request_interrupt()`.
3. **`bootstrap()` / `init_runtime()` are `std::call_once`** (were non-atomic bool
   guards). Multiple Runtimes, even on different threads, share one process-global
   registry initialised exactly once. (The atom table's own single-mutex interning from
   M1 note #5 is already thread-safe; 16-way sharding stays deferred as a perf item.)

### Stage 2 — atomic shared-buffer refcounts + `freeze()`

Supersedes M1 note #3 ("SHARED_BUFFER atomic regime is stubbed").

4. **Atomic refcounting keyed off `FLAG_SHARED_BUFFER`.** `retain`/`release` branch on
   the flag: a shared-buffer (frozen) cell increments/decrements with a CAS loop
   (`acq_rel` on the decrement so the final release synchronizes-with peers before
   disposal); confined cells keep the lock-free fast path. **Deviation from the "reads
   stay non-atomic" implication of §3:** every rc/flag read accessor
   (`refcount`/`is_frozen`/`is_shared_buffer`/`is_buffered`/`gc_color`) now goes through
   a *relaxed* `std::atomic_ref` load — a plain `mov` on a confined cell, but required so
   a CoW uniqueness check or transfer test on a *shared* cell never races a concurrent
   atomic RMW (TSan-clean). The collector's write-side setters stay non-atomic: they run
   only on confined candidate cells (shared buffers are GREEN/acyclic and never
   buffered).
5. **`freeze(x)` builtin** (String/Array; a no-op returning `x` for values that transfer
   by copy anyway). Strings **eagerly materialize every lazy cache** (grapheme length,
   hash, breadcrumb index) at freeze time, so a frozen string shared across threads is
   strictly read-only — otherwise lazy construction on first random access would race
   (§5.1). Arrays give the view a private, contiguous buffer, then freeze *the buffer*
   (the view stays confined and CoW). Both `String`/`Array` mutation gates treat a frozen
   cell as always-copy, so a frozen buffer is never written in place even by a sole local
   owner.

Zero-copy sharing across threads (transfer walk, channels, spawn) and the thread pool
land in Stages 3–6.

### Stage 3 — the transfer walk (`concurrency/transfer.*`)

6. **Central switch, not a per-class `transfer` hook.** §8.3 describes a "per-class
   `transfer` hook". Instead the walk is one function in `concurrency/` that switches on
   the class id (String/List/Table/Set/Array), deep-copies value-class instances
   generically via the instance field array, and rejects reference types. Rationale:
   a hook whose signature carried the recursion context (seen-map + recurse callback)
   would force `types/`/`object/` (lower layers) to depend on `concurrency/`, breaking
   the strict layering; the concrete type set is fixed in M7 anyway. Application types
   (M8/M9) and Channel's send-by-sharing opt-in can be added as a class flag or a hook
   with a low-layer signature later.
7. **Frozen leaves are shared, not copied.** A frozen String or frozen Array buffer is
   handed to the receiver by an atomic retain (zero-copy); only an Array's small view is
   copied so a frozen slice transfers as a slice. Everything else (unfrozen String,
   List, Table, Set, unfrozen Array, value-class instance) is deep-copied. The seen-map
   is keyed on the *source* cell so an in-graph DAG copies each node once; reference
   types are rejected before recursion, so the walked subgraph is always acyclic and the
   walk terminates without cycle detection.
8. **Partial-copy cleanup on rejection.** If a nested reference type is found mid-walk,
   the raise unwinds through RAII (`List`/`Set`/`Table` locals, a `Handle<Cell>` for the
   in-progress instance), releasing everything copied so far; the seen-map holds only
   borrowed pointers and is discarded. No two-pass validate-then-copy needed.

### Stage 4 — Channel (`concurrency/channel.*`)

9. **Channel is a non-builtin ref class + a `Channel` builtin generic.** §13 makes
   Channel a reference class. It is registered *without* `CLASS_BUILTIN`, so the
   compiler's name resolver does not see it and `Channel(...)` resolves to the `Channel`
   builtin native (construction), not the class-object constructor path (which only
   handles field-based user classes / the Error subtree — a channel's payload is a
   mutex+condvars+queue, not Value fields). Consequence: `x is Channel` is not yet
   available. Adding it needs either name-resolver visibility plus a native-constructor
   hook in the call-lowering, or exposing the class object separately — deferred.
10. **Channel cells are born SHARED_BUFFER.** A channel is shared across threads, so its
    refcount must be atomic; `builtin_channel` sets `FLAG_SHARED_BUFFER` at creation (only
    that bit — `FROZEN` governs copy-on-write, which a ref class never does). The class is
    `CLASS_ACYCLIC` (a channel never joins a reference cycle: payloads are transferred
    copies, and channels are not yet sendable through channels).
11. **The queue is a Vector with a head cursor, not a ring.** `send`/`receive` use a
    `Vector<Value>` FIFO with a consumed-prefix compaction once the head passes a small
    threshold, keeping the backing storage bounded (~2×capacity) without ring-index
    bookkeeping. `Channel()` is unbounded; `Channel(n)` blocks the sender when the live
    count reaches n; rendezvous (`Channel(0)` as a synchronous handoff) is not supported
    (0 means unbounded here — architecture §13 defers rendezvous).
12. **The transfer walk detaches the sender's cycle collector.** Building a copy briefly
    drops a wrapper's reference (rc 2→1), which would enroll the copy as a cycle candidate
    on the *sender's* collector — but the copy is handed to another thread, so the sender
    must not track it (found by TSan: the sender's teardown collect raced the receiver's
    release/free). `transfer_across_threads` nulls `current_collector()` for the walk
    (RAII-restored). Safe because the transferable subgraph is all value types, which are
    acyclic — refcounting alone reclaims them; the receiver buffers them on its own
    collector when it first releases them.

### Stage 5 — spawn + thread handles (`concurrency/spawn.*`)

13. **`spawn` is a statement, async, joined at Isolate teardown.** The parser makes
    `spawn f(args…)` a statement (no handle surfaced to scripts). The SPAWN opcode hands
    the created thread handle to the spawning Isolate (`adopt_thread`), which owns it and
    joins every worker when the Isolate is torn down (structured concurrency). Workers
    coordinate through channels — the design sample's pattern — so the acceptance
    producer/consumer needs no `wait`. `wait(handle)` (join-only, re-raises the worker's
    error) is implemented and registered but not yet reachable from scripts; surfacing the
    handle needs an expression form of `spawn` (deferred). Consequently `is Thread` is also
    deferred (the Thread class is non-`CLASS_BUILTIN`, like Channel).
14. **Only a named-function target; no captured state.** `spawn` requires the callee to
    resolve to a generic (a top-level function); the SPAWN opcode resolves the method on
    the current thread (arg classes are unchanged by transfer) and hands the concrete
    callable to the worker. `vm_spawn` rejects a closure with upvalues (`nupvals != 0`) —
    a capturing closure would share mutable upvalue boxes across threads. Arguments are
    transferred (§8.3); a non-sendable argument raises at the spawn site before any thread
    starts.
15. **The vm→concurrency seam.** vm sits below concurrency, so the SPAWN opcode calls
    `vm_spawn` (declared in `vm/interpreter.hpp`, defined in `concurrency/spawn.cpp`) —
    the same declaration-seam inversion as the `cc_*` collector hooks. The worker runs its
    target through `run_callable` (exposed from the interpreter), which builds a root frame
    on the fresh Isolate like `execute` does. The callee closure is kept alive by a +1
    taken and released on the spawner (stored in the handle), never refcounted across
    threads; the worker only reads it. An uncaught, never-waited worker error is printed to
    stderr at finalize rather than silently swallowed.

### Stage 6 — thread pool + pooled kernels (`concurrency/thread_pool.*`)

16. **Process-global pool, not Runtime-owned.** §13 says the Runtime owns the pool. It is
    instead a lazily-created process singleton (`global_thread_pool()`), sized
    `hardware_concurrency() - 1`, joined at process exit. The elementwise kernels
    (`lib/array_kernels.cpp`) live below the Runtime and can be called from the C++
    embedding without one, so a singleton is the reachable, robust choice.
17. **Static partition + caller serialization, no std::function.** `parallel_for` splits
    [0, n) into `worker_count()+1` contiguous ranges (workers + caller), each participant
    writing a disjoint output slice — so kernels need no data locking. Type erasure is a
    plain `void(*)(void*, intptr_t, intptr_t)` + `void*` (no `std::function`, keeping to
    the STL policy). A single job slot is guarded by a caller mutex, so concurrent callers
    (e.g. a pooled kernel triggered from a spawned script thread) serialize rather than
    corrupt the slot; workers only ever run the numeric callback and never re-enter the
    pool, so this cannot deadlock. Work-stealing / dynamic load balancing is an M8 concern.
18. **Threshold + kernels-only.** Kernels fan out only above `PHON_PARALLEL_THRESHOLD`
    (32768 elements, the §13 start value); below it they run inline. Parallel and serial
    paths compute bit-identically (same arithmetic, disjoint indices), so results never
    depend on element count. `parallel_map` with a script lambda (per-worker scratch
    Isolate) is deferred to M8, as §13 sequences ("implement kernels-only first,
    script-lambda parallel_map last"). `request_interrupt` — the other M7 §15 item —
    landed in Stage 1 (the safepoint poll word).

## Modules & imports (design §11, Calao-style)

The `import` system, staged. Syntax and semantics follow Calao (~/Devel/calao): functions
are global generics (callable bare after import), module state is per-module, types are
reached qualified (`M.x`) or brought bare via `for`.

### Stage 1 — loader, resolution, cache; `import M` (functions)

1. **Shared slot vector, per-module namespaces, session-global slot numbering.** Rather
   than per-module slot vectors + a new cross-module opcode, or Calao's module-as-instance
   lowering, the engine keeps its single `iso.module_slots` vector and hands every module's
   bindings a *process-unique* slot from one session allocator (`ModuleLoader::alloc_slot`).
   `GET_MODULE`/`SET_MODULE` are unchanged, and a future `M.x` is just a `GET_MODULE` of
   x's (globally-unique) slot. Least invasive; reuses the hot path verbatim.
2. **Compile-time loading, run-before-main execution.** `import M` during compilation
   resolves + compiles M once (recursively, via `ModuleManager`, cached by canonical path),
   so M's public function names are known generics when the importer is lowered. M's
   top-level is *run* by `do_string` before the main chunk, in dependency (post) order —
   no IMPORT opcode; the import statement emits nothing.
3. **Resolution matches Calao.** `import M` → `M.phon` or `M/initialize.phon`, searched in
   the importing file's directory, then `Runtime::add_import_path` dirs, then
   `$PHON_MODULE_PATH` (colon-separated). Canonical path is the cache key.
4. **Functions are the export surface (Stage 1).** A module's non-`local` top-level
   functions / class methods are recorded on the `LoadedModule` and injected into the
   importer's known-generics set, so `f()` from an imported module compiles and dispatches
   (it is already in the global table once M's top-level runs). `local` functions stay
   module-private (not recorded). Qualified `M.x`, `import as`, `for`, and full privacy
   enforcement land in later stages. A cyclic import is caught (in-progress cache marker)
   and reported; an unresolved module raises `[Import error] module 'M' not found` at the
   import node.

### Stage 2 — qualified access `M.x` (vars/consts/classes) + exported set

5. **`M.x` is compile-time slot resolution, not field access.** A module namespace now
   carries an `exported` set (non-`local` top-level var/const/class names). `import M`
   binds the module name as an alias; a `FieldAccess` whose object is an import alias is
   intercepted in lowering and emitted as a plain `GET_MODULE` of x's session-global slot
   (checked against `exported`) — so `M.x` costs exactly one array load, and a `local`
   member or a non-member is a compile error (`[Name error] … has no public member …`).
   The bare module name `M` on its own is not a value (only `M.x` is meaningful).
6. **Class *values* via `M.C`, construction deferred.** `M.C` loads the class object (it
   lives in a module slot). Constructing through a qualified name (`M.C(...)`) or `x is
   M.C` is not yet wired — the call-lowering treats only a bare `Variable` class name as
   construction. Deferred to a later stage.

### Stage 3 — `import M as A` + chained `import M1, M2`

7. **One statement, a list of clauses.** `ImportStatement` now holds a `std::vector<
   ImportClause>`; each clause is `{module, alias, for_all, names}`. The parser reads
   `M [as N] [for (X [as Z], … | *)]`, chained with commas. Because a `for` name-list
   greedily consumes its own commas, the top-level clause separator and the name separator
   share one token with no ambiguity: a non-`*` `for` clause is necessarily last on the
   line (matching Calao). `import M as A` binds the alias (not `M`) as the qualified-access
   name; functions stay flat-global regardless of the alias.

### Stage 4 — selective import `import M for X, Y` / `for *`

8. **`for` binds bare names to M's slots; functions need no binding.** A `for X` (or
   `for X as Z`) selector makes the name resolve to M's session-global slot for a public
   var/const/class — the same `GET_MODULE` as `M.X`, so reads/`is`/type annotations all
   work bare. Public *functions* are already flat-global after `import M` (design §11: no
   per-module function namespace), so naming one in `for` is an accepted no-op. `for *`
   brings every `exported` name in. Selecting a `local` or non-existent member is a
   compile error (`[Name error] module 'M' has no public member 'X'`).
9. **`for *` and the line-continuation rule.** A line ending in `*` normally continues
   (design §12), which would glue the next statement onto `import M for *`. The scanner now
   tracks the second-to-last token and treats a `*` immediately after `for` as the wildcard
   selector, not a trailing operator, so the newline still terminates the statement.

### Stage 5 — imported classes in `is`/annotations and construction

10. **`x is M.C` / `x is C` and `as M.C` / `as C` now resolve.** The `is` guard accepts a
    qualified `M.C` (import alias + exported class) as well as a bare name; `type_ref`
    resolves both a bare imported class (via `for`) and a qualified `M.C` to a
    `TypeRef::ModuleSlot`, since the imported class object lives in the shared slot vector
    that imported modules populate before `main` runs. `LoadedModule` records its public
    class names (`classes`) to drive these checks.
11. **Cross-module *construction* now works** (`M.C(...)` and bare `C()` via `for`) — the
    field-default obstacle is fixed by relocating default application off the construction
    site into a **per-class defaults thunk compiled in the class's defining module**, so
    every initializer resolves in the scope it was written in. Concretely: `compile_class`
    emits a `@defaults(this)` child proto (own-field `SETFIELDRAW`s + `RET this`) for any
    class that declares a field default and records its index in `ClassDef.defaults_proto`;
    `DEFCLASS` makes a nothing-capturing closure from it (like the accessor closures) and
    stores it on `Class::defaults`. Construction lowers to `NEW; INITDEFAULTS this; init(…)`
    where the new `INITDEFAULTS` opcode walks the instance's class chain **base→derived** in
    C++ and `vm_call`s each ancestor's thunk (`apply_default_chain`), threading the returned
    (possibly copy-on-write-detached, for value classes) instance back — the exact contract a
    routed field setter already uses. Because the chain walk uses the runtime `Class::base`
    pointer and each thunk is home-compiled, this also fixes the latent case the old inline
    `emit_full_defaults` never handled: defaults of a base defined in *another* module. The
    construction lowering no longer needs a `ClassDeclaration` (so an imported class, for
    which the importer has none, constructs fine); `construct()` lost its `cls` parameter and
    `emit_full_defaults`/`cross_module_ctor_msg` were removed. Value semantics are preserved
    (a default-carrying value class is uniquely owned after construction; aliasing then
    mutating still detaches). Tests: `test_modules.cpp` (bare/qualified construction,
    defaults naming the defining module's private const/function, base→derived chaining
    within a module and **across** modules via `modules/shapes.phon` subclassing
    `geometry.Widget`); `test_classes.phon` (local default-carrying value-class aliasing).

## M8 — Embedding + stdlib + performance (architecture §11, §12)

Landed incrementally; each stage stays green under the normal, ASan, and TSan builds
(the concurrency acceptance bar carries forward). M8 leads with the typed registration
API (§11.3), the primary surface for exposing C++ to scripts.

### Stage 1 — typed registration front end (`rt.add_function`, `runtime/native_traits.hpp`)

1. **Native callback ABI gains `self`.** `NativeFn` is now
   `Value(*)(Isolate&, NativeCell*, Value*, int)` (was without the `NativeCell*`). The
   thunk generated for a typed registration recovers its captured C++ callable through
   `self->env`; hand-written natives ignore the parameter (declared unnamed). All three
   VM call sites (`invoke`, `vm_call`, `run_callable`) and every builtin native were
   updated mechanically. This is the design's intent ("recovers the environment through
   the NativeFunction object", §11.3) made concrete in the ABI.
2. **Captured callables live in a `NativeEnvCell`.** A capturing lambda is moved into a
   cell (`native_env_class()`, registered in `register_function_classes`) that stores the
   callable inline after a fixed prefix `{Cell, void(*destroy)(void*), uint32_t
   payload_off}`; its finalizer runs the erased destructor, so a lambda that captures
   Handles/Variants releases them when the owning `NativeCell` dies. `NativeCell` gained a
   `Cell *env` field (null for hand-written/non-capturing natives) released on finalize —
   so `native_class()` now has a finalizer where it previously had none. The env prefix is
   duplicated as `NativeEnvHeader` in `vm/function.cpp` (which finalizes it without seeing
   the template) and `detail::NativeEnvCell` in `native_traits.hpp`; the two layouts must
   stay in sync (noted in both files).
3. **Env cells are marked acyclic (a known, bounded leak).** The collector never traces
   into a C++ callable, so a script cell captured *by value into a C++ lambda* is an
   uncollectable root: a reference cycle routed through such a capture leaks rather than
   being reclaimed. This matches natives already being GC-leaves and is acceptable —
   embedders capturing live script objects into long-lived callbacks is rare and the leak
   is bounded by the callback's lifetime. Revisit if a real embedding hits it.
4. **Type mapping (`ArgTraits`/`RetTraits`).** A C++ `double` (any floating type)
   dispatches on `Real` and unboxes via `to_double()`, so an integer argument satisfies it
   and coerces; any integer type dispatches on `Integer`; `bool` on `Boolean`; the
   `String`/`List`/`Table`/`Array` wrappers on their classes (owning unbox via
   `from_value`, `+1` box via `to_value`); `Variant`/`Value` are untyped pass-throughs on
   `Object`; `Handle<T>` dispatches on `T::phon_class` (usable once stage 2's `add_class<T>`
   sets that static). Registration goes through the ordinary `add_method`, so a C++ overload
   and a script method of the same name coexist and dispatch by argument type.
5. **Optional leading `Isolate&`.** If the callable's first parameter is `Isolate&`, the
   thunk passes the running isolate through and it is *not* part of the dispatch signature
   — the hook for callables that raise or call back into the VM. Detected by
   `strip_isolate` over the deduced argument tuple.
6. **`ref` parameters use the natural `T&` spelling (not `phon::Ref<T>`).** A **non-const
   lvalue-reference** parameter is a `ref` that writes back to the caller's slot; a by-value
   or `const T&` parameter is read-only. This departs from the design doc's `phon::Ref<Array>`
   sketch (§11.3) — the plain-reference spelling is more idiomatic C++ and the owner preferred
   it. Mechanics (`native_traits.hpp`): `is_ref_param<A>` drives the ref-mask installed via
   `add_method`, so a direct call promotes the argument (references.md §6) and the native
   receives a first-class reference box. Per argument the thunk builds a `Binder`: a
   by-reference binder **moves the referent out of the box slot** (transferring the slot's
   reference, nulling the slot) so a CoW value type mutates in place when uniquely held and
   copies when shared, then writes the possibly-changed value back on destruction (after the
   call). Binders are stack locals threaded through a recursion — never moved — so lifetimes
   and write-back ordering are exact. Supported ref types: numeric scalars and the
   String/List/Table/Array wrappers (`RefMarshal`); an unsupported `T&` is a compile error.
   Two accepted limitations, both matching existing reference behaviour: (a) the box slot is
   null for the duration of the call (shared with `for ref`), and (b) an **indirect** call
   (native reached through a first-class function value, not by name) does not promote —
   `callable_ref_mask` still returns 0 for natives — so the ref parameter silently degrades
   to by-value there; direct calls by name (the design's motivating `trim(str)` /
   `normalize(x)` cases) work.
7. **Registration is process-global; `add_function` is a `Runtime` method for ergonomics
   and to guarantee initialization.** It forwards to the free `phon::register_function`,
   which stdlib modules (stage 4) will call directly. An ambiguous signature against an
   existing overload throws `std::runtime_error` (an embedding bug, surfaced eagerly) rather
   than the script-level `AddMethod::Ambiguous` path.

### Stage 2 — class registration for C++ types (`add_class<T>`, `Handle<T>::make`)

8. **`rt.add_class<T>("Name", base, ClassKind)`** registers a C++ type as a phon class:
   records `sizeof(T)`, wires `~T()` as the finalizer, binds the static `T::phon_class`
   (which the stage-1 `Handle<T>` ArgTraits/RetTraits already keyed on — so typed
   registration of functions over `T` now works), and for a `ClassKind::Value` class wires a
   CoW clone hook. `T` must be cell-headed (first member `Cell header`) with a
   `static Class *phon_class` slot — the design's `T::phon_class` convention (§11.2), no macro
   required. `ClassKind` replaces the design's unspecified third argument with an explicit
   `{Reference, Value}` enum.
9. **Foreign classes are marked `CLASS_BUILTIN`.** This is what makes them nameable by the
   compiler's global resolver (`class_by_name` is builtin-only), so a registered class works
   in `is`/`as`/type annotations and dispatch. A deliberate consequence: `CLASS_BUILTIN`
   classes are **not script-constructible** (`Name(...)` in a script is rejected, as for any
   builtin except Error), which matches the design — instances come from C++ (`Handle<T>::make`)
   or a registered factory function (`make_point(...)`), never a script constructor. `find_class`
   (new, public) backs `rt.get_class("Object")`; it returns the first non-metaclass of that
   name (an embedder owns its namespace, so first-match is unambiguous in practice — the unit
   test uses a unique class name only because the *shared test process* already has another
   `Point` from `test_instance`).
10. **`Handle<T>::make(args…)` (design §11.5, replaces `rt.create<T>`).** cell_alloc'ing then
    placement-constructing `T` over the same bytes would clobber the header cell_alloc just
    stamped (T's leading `Cell header` is default-constructed by T's ctor), so `make` saves
    `hdr`/`rc_bits` and restores them after construction. Allocation is Isolate-independent
    (the FOREIGN cell path is still the only allocator — the M4 arena path was never needed),
    so `make` works on any thread with or without an Isolate, matching §11.5's foreign-allocation
    contract. The finalizer runs via the existing `cell_dispose` seam.
11. **Cycle-collector trace hook for foreign classes — now implemented (was a bounded leak).**
    A registered C++ type that captures script cells (a `Handle`/`Variant`/`Value` member) opts
    into cycle collection by defining `void gc_trace(void (*visit)(Cell *)) const`, calling `visit`
    once per cell it owns. `add_class<T>` detects it (a `has_gc_trace_v<T>` trait, selected with
    `if constexpr` so `foreign_trace<T>` is only instantiated for types that have it) and wires
    `Class::trace`; such a class stays potentially-cyclic. A type WITHOUT `gc_trace` holds no
    traceable cells and is marked `CLASS_ACYCLIC` — its cells are born GREEN and never buffered, so
    a leaf foreign object costs the collector nothing. Because the cyclic-free path bypasses the
    destructor (the collector balances the cell edges itself), a type that also owns non-cell
    resources or needs a death side effect provides the optional `void gc_free()` (detected the
    same way) — the exact counterpart of the engine's own `List`/`Table` gc_free hooks: free the
    non-cell resources, do NOT release the cells `gc_trace` enumerates. `register_foreign_class`
    gained `TraceHook`/`GcFreeHook`/`acyclic` parameters. Tests in `test_embed.cpp`: a
    `Variant`-holding node in a node↔list cycle is reclaimed after `collect_garbage` (verified both
    by a construction/destruction counter driven from `gc_free`, and under ASan, which would report
    the whole cycle as leaked if `trace` were absent); a trace-less class is confirmed acyclic.
    **Value-class registration is implemented but lightly exercised** — the tests cover reference
    classes (the primary case); the CoW clone path for a foreign value class only fires if
    something invokes the clone hook, which no script opcode does for a foreign type today.

### Stage 3 — values from C++ (`Variant::to<T>()` / `make()`, channel receive)

12. **`Variant::to<T>()` / `Variant::make()` reuse the registration type mapping.** `to<T>()`
    derefs, checks `value_is_a(v, ArgTraits<T>::dispatch_class())` (throwing
    `std::runtime_error` with a `<actual> to <expected>` message on mismatch), then returns
    `ArgTraits<T>::unbox` — an *owning* C++ value (a widening Integer→double is allowed, as in
    parameter passing). `make(x)` is `Variant::adopt(RetTraits<T>::box(x))` — box mints the +1,
    the new `Variant::adopt` takes it without a second retain. So the two conversion directions
    share one type table with function registration; no parallel mapping.
13. **The conversion helpers live in `native_traits.hpp`, not core.** The container wrappers
    (List/Table/Set) include `core/variant.hpp`, so Variant cannot include them back to
    implement `to<List>()` inline. `Variant::to`/`make` are thin members that forward to
    `detail::variant_to`/`variant_from`, *declared* in `core/variant.hpp` and *defined* in
    `runtime/native_traits.hpp` where every type is complete. Consequence: `v.to<T>()` requires
    the embedding header (runtime.hpp) at the use site — an embedder already has it.
14. **Typed container views were already present.** §11.4's "typed views for
    String/List/Table/Array (`ArrayView::dim(i)`, `data()`, iteration)" are the existing
    stack-value wrappers from the M-Array / M5 work — `Array` already exposes `dim(k)`, `size()`,
    `data()`. Stage 3 adds no new view type; the acceptance is a C++ test reading a
    script-produced Array's `data()`/`dim()`/`size()`.
15. **C++-side channel receive (`channel_receive`, `channel_try_receive`).** Free functions in
    `concurrency/channel.*` for a GUI/host thread to pull results a worker script pushed
    (design §11.4). No Isolate needed — queued values were already transferred into a
    standalone graph, so a pop is just a locked dequeue wrapped in `Variant::adopt`. The timed
    form uses `condition_variable::wait_for` (a non-positive timeout polls); it is the
    event-loop-polling variant the design calls for. `is_channel(Value)` is now public (moved
    out of the file-local anonymous namespace).

### Stage 4 (partial) — stdlib port: math + string (`lib/math.cpp`, `lib/string.cpp`)

Leading the stdlib port with two modules to lock the pattern; the rest (list, array,
system, file, regex, json) follow the same shape. `lib/lib.hpp` declares one
`register_<module>_lib()` per unit; `init_runtime()` calls them in the `call_once` block.
Everything routes through `register_function` (the typed API) — no hand-written natives.

16. **Scalar math takes `double`, integer-preserving functions carry a second overload.**
    `sin`/`cos`/`sqrt`/`log`/… take `double` (dispatch on Real), so an Integer or Float
    argument coerces — replacing the old engine's `Number`-typed callbacks. `abs`/`round`/
    `min`/`max` additionally register an `int64_t` overload (dispatch on Integer, more
    specific), so integer inputs stay integers and floats fall to the double method. This is
    the old engine's Integer/Number overload split expressed directly as two C++ registrations.
17. **String mutators are `ref` (`String &`); queries are `const String &`.** The old engine's
    REF-marked in-place functions (`trim`, `append`, `prepend`, `replace`, `remove`, `reverse`)
    become `String &` first-parameter registrations — the stage-1b ref support carries them
    with correct copy-on-write (an aliased string is not clobbered; verified in
    `test_stdlib.phon`). Non-mutating functions (`find`, `count`, `contains`, `to_upper`,
    `left`, `slice`, `char`, `split`, …) take `const String &`. `split` returns a `List`,
    exercising a container return. Grapheme, 1-based positions throughout; `find` returns 0
    when absent (old-engine semantics).
18. **Math constants are bare-name globals (`PI`, `E`) via compile-time inlining.** A small
    process-global name→Value table (`register_constant`/`find_constant` in `dispatch/generic.*`,
    the existing global-name-registry TU) holds builtin value constants. The compiler's
    Variable-read path checks it *last* — after locals, upvalues, module slots, imports,
    classes, and generics — so any binding of the same name shadows it (a `var PI` is a normal
    module/local var). A hit inlines as a `LOADK` (compile-time substitution, zero runtime
    lookup), so constants must be registered before the referencing chunk compiles — which
    init_runtime guarantees. Confined to the `NameKind::None` branch of the Variable case, so no
    new `NameKind` and no other resolver switch is touched. `register_math_lib` registers `PI`
    and `E`; cell-valued constants (none yet) would be retained by the table and released at
    `generic_registry_shutdown`. Note: because module-level `var`s hoist chunk-wide, a `var PI`
    anywhere in a chunk shadows the constant for the *whole* chunk (standard hoisting, not
    constant-specific).

### Stage 4 (cont.) — stdlib port: list + array (`lib/list.cpp`, `lib/array.cpp`)

19. **List module fully ported.** Queries (`contains`, `find`, `find_back`, `first`, `last`,
    `left`, `right`, `join`, `is_empty`) take `const List &`; mutators (`append`, `prepend`,
    `insert`, `remove`/`remove_first`/`remove_last`/`remove_at`, `clear`, `pop`, `shift`,
    `reverse`) take `List &` (ref/write-back). `pop`/`shift` return the removed element *and*
    mutate — a ref parameter with a value return. Elements are untyped, so element parameters are
    `Variant` (dispatch class Object), matching the old engine's `CLS(Object)` element signatures.
    The set operations (`unite`/`intersect`/`subtract`) are order-preserving and deduplicated,
    by value equality. `sample`/`shuffle` use a thread-local `mt19937_64` (never races). The
    ordering functions are covered by item 19a.
19a. **`value_compare` — a shared three-way ordering primitive.** `sort`, `is_sorted`,
    `sorted_find`, `sorted_insert` need `<` over arbitrary values, which previously lived only
    in the interpreter's `compare_ordered`. Lifted into `object/value_ops` as
    `bool value_compare(Value, Value, int &out)` (numbers numerically, Strings lexicographically;
    returns false for an unorderable pair so the caller chooses how to raise). The interpreter's
    `compare_ordered` (backing `<`/`<=`) now *delegates* to it, so operator ordering and the
    sorted list functions can never diverge. `value_ops.cpp` gained a `types/string.hpp` include
    (a `.cpp`→types include, no header cycle). `sort` is `std::stable_sort` (deterministic); a
    comparator that hits an unorderable pair raises through the native boundary (the partial
    result vector is local and discarded, the source list untouched).

### Stage 7 — example host app + a dispatch-memo concurrency fix

21. **Example host app (`examples/repl.cpp`, target `phon_repl`) — the M8 acceptance
    deliverable.** One program embedding the engine in three modes: an interactive REPL
    (persistent session, results stringified, SyntaxError/RuntimeError rendered with line),
    a script-file runner, and a `--workers` demo. The demo is the GUI stand-in: a script
    spawns 3 worker threads that `send` results to a shared `Channel` and hands the channel
    back to the host, which polls it with `channel_try_receive(0.05s)` like an event loop and
    checks the summed result (270). It also registers host C++ extensions — `hypot`/`host_name`
    functions and a reference class `Counter` with `make_counter`/`bump`/`count` — exercising
    `add_function`, `add_class<T>`, `Handle<T>`, and `Variant::to<T>` together. Two CTest cases
    (`example_workers`, `example_file`) run it under all three build configs; both return
    nonzero on failure.
22. **Dispatch memo is now bypassed while workers are live (a real concurrency fix the demo
    exposed).** `resolve`'s per-generic memo cache (and its epoch fields) is shared mutable
    state; multiple worker isolates first-dispatching a *cold* generic concurrently raced on it
    (TSan-caught by the demo — existing tests never hit concurrent cold dispatch because the
    main thread warmed the memos first). Fix: an atomic live-worker counter
    (`dispatch_enter_thread`/`dispatch_exit_thread` in `dispatch/generic.*`, bracketing each
    worker in `spawn.cpp`). A worker increments **before it dispatches**, so it always observes
    a non-zero count and takes the memo-free `full_resolve` path — meaning the memo is touched
    *only* by the main thread, and only when no worker is live, making that access exclusive by
    construction (correct even under relaxed ordering; `full_resolve` is read-only over the
    stable method set). Single-threaded dispatch is unchanged (counter 0 → same memo path). The
    per-call-site inline cache is per-isolate and was already race-free. TSan-clean across the
    unit suite and the worker demo.

### Stage 4 (cont.) — stdlib port: system + file (`os/`, `lib/system.cpp`, `lib/file.cpp`)

23. **A vendored, Unicode-correct path layer (`phon/engine/base/file_system.*`).** Per the requirement
    that paths behave like Phonometrica's on every platform, this is `phon/utils/file_system.*`
    ported to the new `String`: on Windows the wide Win32 API (GetFullPathNameW, FindFirstFileW,
    SHGetFolderPathW, CreateDirectoryW, DeleteFileW, _wrename, PathFileExistsW/PathIsDirectoryW)
    driven from a UTF-16 conversion of the UTF-8 String (`to_wide` = `to_utf16` widened to
    `wchar_t`, valid since Windows `wchar_t` is 16-bit); on POSIX the UTF-8 bytes go straight to
    the C library (realpath/stat/opendir/mkdir/…). Platform macros `PHON_WINDOWS`/`PHON_POSIX`
    are introduced here (the engine had none). `list_directory` returns a `List` (the new Array
    is numeric-only, unlike the old `Array<String>`). Failures throw `FileSystemError`, re-raised
    as a script `[System error]` in lib/system.cpp. **Only the POSIX path is exercised here
    (Linux); the Windows branches are ported faithfully from the field-tested original but not
    compiled/run in this environment.**
24. **The `File` type (`phon/engine/types/file.*`) opens Unicode paths via `os_open_file`** (`_wfopen`
    with a UTF-16 path on Windows, `fopen` on POSIX — the old `utils::open_file`). `File` is a
    cell-headed reference class registered through the embedding `add_class<File>` path, so its
    finalizer (`~File`) closes the handle when the value dies (RAII file handles via the cell
    lifecycle). **Multi-encoding reads are now ported** (see item 26 below); `eof`/`at_end`
    *peeks* a byte rather than
    trusting `feof` (which only latches after a read past end), so a file whose final newline was
    just consumed reports end correctly. The opener is named `open_file`, **not** `open` — `open`
    is a reserved class/function modifier keyword in the grammar.
25. **Latent journal interaction surfaced (fixed at the fixture).** A user script defining a
    function whose name+signature collide with a builtin (here `test_errors.phon`'s example
    `function read_file(path as String)`, written before `read_file` became a builtin) *replaces*
    the builtin method; when that one-shot chunk's Runtime is torn down, the registration journal
    (design §11) retracts the method via `remove_method`, leaving the builtin generic empty for
    the rest of the process. The immediate fix was renaming the test fixture (`read_file` →
    `read_notes`). The underlying behaviour — redefining-then-unloading a builtin does not restore
    it — is pre-existing and noted for a later journal pass; it only bites on a name+sig collision
    with a builtin.
25a. **Multi-encoding reads ported (`File`).** Reading now transcodes UTF-16/UTF-32 (little- and
    big-endian) to UTF-8, matching the old engine. On open for a read at byte 0, `detect_encoding`
    sniffs the BOM (checking the 4-byte UTF-32 marks before the 2-byte UTF-16 ones, since a
    UTF-32LE BOM starts with the UTF-16LE BOM) and positions the cursor past it; absent a BOM the
    encoding defaults to UTF-8 and the bytes are put back. `read`/`read_line`/`read_lines` keep the
    fast UTF-8 byte path and, for UTF-16/32, decode a codepoint at a time (`next_codepoint`, using
    the engine's `unicode::utf16_decode` for surrogate pairs and `std::endian`/manual byte-swap for
    byte order); line reads still strip a trailing `\r`. A BOM-less UTF-16/32 file is handled by a
    new three-arg `open_file(path, mode, encoding)` (names via `encoding_from_name`); `encoding(f)`
    reports the detected/forced encoding. **Writing stays UTF-8, no BOM** — the old engine never
    supported UTF-16/32 output either. Tested host-endianness-independently in
    `test/unit/test_file_encoding.cpp` (byte-exact fixtures incl. an astral codepoint = a UTF-16
    surrogate pair; BOM auto-detect; forced encoding; encoded line reads).

### Embedding transparency — plain application classes, transparent `Handle<T>`

26. **Application classes are now plain C++ classes; the engine boxes them.** Previously a
    registered C++ type had to *embed* the engine (`struct File { Cell header; …; static Class
    *phon_class; }`). Now `File`/`Sound`/`Annotation`/`Regex` are ordinary classes with **nothing
    on them** — usable as standalone stack objects with natural constructors — and the engine
    wraps them: `Handle<T>` points at `{ Cell header; T value }`, with the value at a computed
    offset after the header (`box_value_offset`/`box_value`/`box_total_size` in `core/cell.hpp`,
    same technique as NativeEnvCell). This matches Phonometrica's `Handle<T>` (smart pointer) →
    `TObject<T>` (storage) model, keeping the pervasive `Handle<T>` name.
27. **Cell-headed vs. boxed is a compile-time trait, not a per-type annotation.** `is_cell_headed_v
    <T>` (core/cell.hpp) detects a standard-layout type whose first member is a `Cell header`
    (the engine's own StringCell/ListCell/… — no boxing) vs. a plain class (boxed). We chose
    member-detection over the originally-floated `: Cell` inheritance because deriving would make
    the cell types non-standard-layout, which under `-Werror` breaks `offsetof` (`-Winvalid-
    offsetof`, used for flexible-array sizing + the Upvalue/RefBox coupling assert) and turns the
    ~113 first-member `reinterpret_cast<Cell*>` casts technically-UB — a large, risky churn for
    zero behaviour change. Member-detection keeps standard-layout intact, so nothing else moved.
28. **`T::phon_class` static replaced by a `class_of<T>()` template variable** (`core/handle.hpp`),
    so the class→`Class*` map lives outside the class — an app class needs no static member.
    `add_class<T>` sets it; `Handle<T>::make` and the `Handle<T>` ArgTraits read it. `Handle<T>::
    make` no longer needs the header save/restore hack (the boxed value is constructed *after* the
    header, so nothing clobbers it) and `static_assert`s `T` is not cell-headed.
29. **Native parameters may be `T&` / `const T&` for a registered class**, not only `Handle<T>` —
    the natural spelling. A third binder kind (`BindKind::ClassRef` in native_traits.hpp) holds a
    `Handle<T>` (keeping the cell alive) and hands the callable a reference *into the shared
    object* (identity, no copy, no write-back). Detection is by exclusion: a class type that is not
    an engine wrapper (String/List/Table/Set/Array/Variant/Value), `Handle`, or `Isolate` is a
    registered application class. Such a reference is **excluded from the write-back ref-mask**
    (`is_writeback_ref_v`), so the call site does not promote it to a reference box — a registered
    object is already a shared cell. Passing a registered class *by value* is a compile error (it
    would copy a shared object). `File`, the test `EmbedPoint`, and the example `Counter` were all
    converted to plain classes; `Counter&`/`const Counter&` are used in the example REPL.
20. **Array: constructors, reductions, and elementwise math overloads.** `zeros`/`ones`
    (1-D and 2-D, non-negative size checked via a leading `Isolate &`), `sum`/`mean`/`min`/`max`
    reductions (over `contiguous()` for correctness on strided/sliced views), `nrow`/`ncol`
    shape, in-place `clear` (`Array &`, CoW-correct via `detach()`). The elementwise functions
    (`sqrt`, `abs`, `sin`, `cos`, `exp`, `log`, `floor`, `ceil`) are registered as **overloads
    of the scalar math generics** on an `Array` argument — so `sqrt(9.0)` takes the Real method
    and `sqrt(@[…])` this one, the type-based dispatch replacing the old `math_array_func<f>`
    template overloads. They allocate a fresh contiguous result (no in-place surprise); a
    dedicated unary-apply kernel and the pooled/threaded threshold (§13) are a stage-5/6 tuning
    item, not needed for correctness here.

### Stage 4 (cont.) — stdlib port: json (`lib/json.cpp`)

30. **`to_json` / `from_json` as two flat-global functions** (not the old engine's `json.stringify`
    /`json.parse` module — the new stdlib is flat, like the builtins). `to_json(value[, indent])`
    serializes; a positive `indent` pretty-prints (that many spaces per level), else compact.
    `from_json(str)` is a self-contained recursive-descent parser (`struct Parser`) over the UTF-8
    bytes: object → `Table` (string keys), array → `List`, number → `Integer` when it has no
    fraction/exponent and fits the 48-bit NaN-boxed range (else `Float`), with full string-escape
    handling including `\uXXXX` and surrogate-pair decoding. Serialization dispatches on the
    argument as `const Variant &` (so the single generic method matches any value via `Object`);
    string escaping mirrors the old engine's `Variant::to_json`, and a non-finite Float or an
    unserializable type (function/user object/Set/Array) raises `[JSON error]`. This replaces the
    old engine's `dump_json` (= `to_json()`) and the `load_json` **hack** (which just `do_string`'d
    the text as source); `from_json` is a real parser, so it rejects malformed input with a
    positioned error instead of running it. Acceptance: `test/scripts/test_json.phon` (scalars,
    escaping, containers, round-trips, pretty output, error paths). Stdlib port now complete.

## M8 — Performance pass (architecture §14) + benchmark harness

31. **Fixed an O(n²) copy-on-write blow-up on in-place stores to a module/upvalue-held container.**
    `t[k] = v` where `t` is a module- or upvalue-scoped `List`/`Table`/`Array` compiled to
    `GETMODULE`(retaining copy) → `SETINDEX` → `SETMODULE`. During `SETINDEX` the slot still held a
    second reference, so the CoW `detach()` saw refcount ≥ 2 and **cloned the entire container on
    every write** — a loop of n inserts to a module table was O(n²) (n = 20 000 already cost ~35 s;
    it hit the 2-minute test cap at 40 000). A *local* `var t` was unaffected (its register *is* the
    storage, refcount 1). Fix (`emit_index_unshare` in `compile/lower.cpp`): after evaluating the
    index and value subexpressions, the compiler nulls the binding's slot (a `LOADNULL` + `SETMODULE`
    /`SETUPVAL` pair) so the loaded temp is the unique owner and the store mutates in place. Applied
    to the four builtin in-place stores that never re-enter script — `SETINDEX`, `SETIDXN`,
    `SETSLICE`, and compound `t[i] op= x` — but **not** `SETFIELD` (a user `set` accessor could run
    and would then observe the transiently-nulled binding). **Reentrancy-safe** because the
    subexpressions are evaluated *before* the null-out and a builtin indexed store calls no script.
    **Value semantics preserved**: a genuine second owner (`a = t; t[k] = v`) keeps refcount ≥ 2, so
    the clone still fires and the alias is unaffected — pinned by `test/scripts/test_cow_binding.phon`
    (aliasing on table/list/slice, upvalue mutation, reentrant reads, compound assignment). One
    minor semantic edge: if the store itself *raises* (e.g. an out-of-range Array index), the binding
    is left null rather than at its prior value — acceptable (an erroring indexed store aborts the
    statement regardless). The golden disassembly corpus (`arrays.dis`, `slices.dis`) was regenerated
    to show the inserted null-out; reads (`GETVIEW`/`GETSLICE`) correctly get none. **maps benchmark:
    O(n²) → O(n)** (>2 min → 157 ms at n = 400 000, Release).
32. **CALLG caches the resolved `GenericFunction` in its inline-cache slot.** A call site's callee
    symbol is a compile-time constant and generics are never deleted, so the by-name registry lookup
    (`find_generic`, a hash probe) that ran on *every* generic call is now done once and cached in a
    new `ICEntry::generic` field (`vm/isolate.hpp`), read on the hot path and only recomputed when the
    slot is cold. Shaves the lookup from ~5M calls/run on the `fib`/`dispatch` benchmarks; the arg-key
    monomorphic method cache and its epoch guards are unchanged.
33. **Benchmark harness (`bench/`, target `phon_bench`).** The §14 microbench set was missing; added
    `bench/scripts/{fib,loops,strings,maps,dispatch,arrays}.phon` and a C++ runner (`bench/bench.cpp`)
    that times each on a fresh `Runtime` and reports best + median over N runs (`--runs`, default 5).
    Recorded baselines in `bench/BASELINE.md` (Release, `-O3 -DNDEBUG`). Not wired into CTest (timing,
    not correctness). The heavier interpreter rewrites floated for the pass — computed-goto dispatch,
    refcount elision on borrowed locals — were **not** done: high-risk/invasive for the switch-based
    loop wrapped in the raise-handling `try`, with the two landed fixes (the O(n²) bug + the IC-generic
    cache) capturing the outsized wins. Left as a future item.
34. **`spawn` relaxed-refcount UAF — confirmed fixed and now guarded.** `vm_spawn` freezes the
    constants reachable from the callee (`freeze_reachable_constants`, spawn.cpp) before fanning out,
    exactly as `parallel_map` does: many workers running the same function each LOADK-retain its shared
    `String`/`Array`/cell constants, and a non-frozen cell's refcount is a thread-confined *relaxed*
    load/store — so concurrent retain/release across workers loses updates and frees a live constant
    (heap-use-after-free). The freeze routes those constants onto the atomic `SHARED_BUFFER` refcount
    path. The call was already present but had **no regression test** and had been mis-recorded as
    unfixed; this pass verified it by A/B under ASan (freeze removed → UAF within 1–2 runs, 5/5 & 6/6;
    freeze present → 8/8 clean) and added a deterministic guard, `test_concurrency.cpp`
    "concurrency: spawn freezes shared constants (relaxed-refcount UAF guard)" (16 workers × 50 000-iter
    churn of three shared string constants; each returns its loop count → total 800 000). ASan catches
    the regression; TSan does not (the racing accesses are relaxed-*atomic*, so no reported race).

## Embedding gaps for the Phonometrica cutover (design §11; MIGRATION_NOTES step 2)

Closes the C++ embedding-surface gaps that blocked Phonometrica's adoption of the
engine's String/File/Hashmap at VM cutover (Phonometrica's phase-1 audit trail:
`~/Devel/phonometrica/MIGRATION_NOTES.md`).

35. **The numeric array's C++ name is `NumArray`; a generic CoW `Array<T>` joins the
    core containers.** Phonometrica needs a generic, growable, copy-on-write
    `Array<T>` (element types: String, structs, handles, double, complex) as its
    workhorse application container, with the goal that its `phon/base/array.hpp`
    is replaced by an engine header at cutover with near-zero call-site churn —
    which fixes the template's name as `phonometrica::Array<T>`. C++ forbids a
    class and a class template with one name in one namespace (and a nested
    namespace + `using` alias hits the same wall), so the numeric script array
    (types/array.hpp, design §9/§5.3) keeps the **script-visible name "Array"**
    but its C++ class is renamed **`NumArray`** (registration string, `CID_ARRAY`,
    `ArrayCell`/`ArrayBuffer`, opcodes, error messages, and golden dumps are all
    unchanged). The generic template is **adopted wholesale from Phonometrica's
    phon/base/array.hpp** into `core/array.hpp` (0-based, `npos == -1`, buffer
    sharing with detach-on-first-mutation, 1-D growable / 2-D fixed column-major
    matrix), re-based onto engine primitives: `raw_alloc`/`raw_free`,
    `relocate_range` (memcpy for trivially relocatable T), an intrusive relaxed
    atomic refcount replacing `Countable`/`IntrusivePtr` (handles may be handed
    across threads; concurrent mutation of one buffer is unsupported, as before),
    and value-initialization where the old buffer relied on calloc zero-fill.
    Architecture §4 lists only Vector/FlatHashMap/SmallVector as core containers,
    so `Array<T>` is an addition, not a replacement: `Vector<T>` remains the
    engine-internal buffer (List's storage); `Array<T>` is the *application-facing*
    container (embedding surface, §11) and nothing in the engine core is required
    to use it. Bounds-checked paths (`at`, `insert` positions) throw
    `std::runtime_error` exactly like the old `error()` (which *was* a formatted
    `std::runtime_error`); unchecked `operator[]` asserts, engine-style. The public
    facade `phon/array.hpp` now exports both `Array<T>` and `NumArray`.
    Covered by `test/unit/test_generic_array.cpp` (CoW aliasing, growth schedule,
    matrix layout, npos queries, move-only and struct elements, span interop).

36. **`String::split(Substring)` / `String::join(Array<String>, Substring)`** (M1 #6
    deferral closed). Byte-wise scan with old Phonometrica's exact edge cases: an
    empty separator throws (`std::runtime_error`, "[Runtime error] Cannot split
    string with empty delimiter"); a separator whose byte length is >= the
    string's yields the whole string as the single element (so `"ab".split("ab")`
    is `["ab"]`, not `["", ""]`); leading/trailing/adjacent separators yield empty
    chunks; `join` inserts the separator between consecutive elements only.
    `split` returns the new generic `Array<String>` (types/string.hpp now includes
    core/array.hpp — a downward include).

37. **`String::replace(const Regex &, String after, intptr_t ntimes = -1)`** (M1 #6 /
    M5 deferral closed), defined in types/regex.cpp so the String TU stays
    PCRE2-free (string.hpp forward-declares `Regex`). Old Phonometrica semantics,
    pinned by tests: only the **first** match is replaced; `%%` in `after` is
    rewritten to the whole match and `%1`..`%9` to the capture groups **before**
    splicing; `ntimes` bounds those placeholder substitutions inside `after`
    (matching the old implementation, where it was forwarded to the inner
    replaces). One deliberate refinement: a capture group that did not participate
    in the match substitutes the **empty string** — the old code read an undefined
    PCRE2 ovector entry there (garbage), and Phonometrica's ported phon/base/regex
    throws; neither is right for a replacement template, and empty is Perl's
    behavior. `Match` gains the byte-level accessors this needs —
    `group_byte_start/group_byte_end` (0-based byte offsets, the layer under the
    1-based grapheme positions) and `subject()`.

38. **`String::arg(Substring...)` ×9 (Qt-style positional `%1`..`%9`)** (M1 #6
    deferral closed). Sequential `replace("%N", argN)` chains exactly like old
    Phonometrica, including the documented Qt caveat that a placeholder occurring
    in an earlier argument's *text* is rewritten by later substitutions (pinned by
    test).

39. **Qt conversions behind `PHON_WITH_QT`** (M1 #6 deferral closed): `String(const
    QString &)`, `explicit String(const QByteArray &)`, and `operator QString()`,
    mirroring old Phonometrica's string.hpp verbatim. Header-only and strictly
    macro-gated — no CMake option, no dependency added; an embedder defines
    `PHON_WITH_QT` and provides the Qt include path. Verified against system Qt6
    (compile + UTF-8 round-trip under ASan); not part of the default build or CI.

40. **String construction self-bootstraps (static-initializer Strings are safe).**
    Minting any cell needs the class registry (`cell_alloc` reads the class's
    acyclic flag), so a file-scope `String` in an embedder ran before
    `bootstrap()` and hit an unregistered registry. Rather than a documented
    "don't do that" pattern, `string_create` — the single choke point every String
    constructor funnels through — now calls `ensure_bootstrapped()` first.
    `bootstrap()` was already idempotent and thread-safe (`std::call_once`), but
    its fast path measured ~4% on the 2M-allocation strings bench, so the guard is
    an inline acquire load of `g_bootstrapped` (published with release order at
    the end of the once-body) + a predicted branch — the strings bench is then
    unchanged (A/B: 203.8 ms without any check, 206.2 with the inline guard,
    211.8 with call_once; best-of-5, Release). The declaration
    lives in object/class.hpp next to the registry API — a declaration-only seam
    like cell.hpp's cycle-collector seams, so the types layer never includes the
    runtime; the definition stays in runtime/bootstrap.cpp (upward link-time
    dependency only, all one library). Destruction order is safe by construction:
    the registry singleton (a function-local static) completes construction
    *inside* the first String's initializer, so it is destroyed after every static
    String. Pinned by a file-scope String in test_string.cpp (constructed
    pre-main, before the harness's bootstrap, ASan-clean). Other cell types keep
    the bootstrap-first requirement — the constraint was only ever about Strings
    (Phonometrica has static-initializer Strings; nothing else).

## Post-port hardening (Phonometrica script-port findings, 2026-07-18)

The Phonometrica shipped-script port (repo `../phonometrica`, MIGRATION_NOTES.md
step 3) ran real workloads over the engine and surfaced three script-reachable
crashes plus a set of embedding/stdlib gaps. All are fixed here; each stayed green
under the normal, ASan, and TSan builds (394 unit cases each).

1. **Recoverable script errors from the types layer: `ScriptError` /
   `PHON_SCRIPT_CHECK` (`base/script_error.hpp`).** `PHON_CHECK` aborts the process,
   which is right for invariants but wrong for script-caused conditions: an
   out-of-range List *write* (`lst[0] = x` — reads were opcode-checked and fine),
   `insert` past the end, `pop` from an empty List, and String index/insert
   positions all killed the interpreter. Those four sites now throw `ScriptError`;
   the interpreter's retry loop converts an in-flight `ScriptError` into a thrown
   script Error at the current line (sharing the handler-landing path with
   RuntimeError via a factored `land_on_handler`), so scripts can `catch` them.
   `PHON_CHECK` remains for genuine invariants.

2. **Script-level `insert` past the end appends (lib/list.cpp).** Restores the old
   engine's contract (pinned by Phonometrica's `test_base_migration.phon` L22); the
   C++ `List::insert` itself stays strict (a `ScriptError` on a bad position).

3. **Argument-staging register leak in ref promotion (compile/lower.cpp,
   `emit_promote_arg`) — the cause of a memory-corrupting bug.** The
   container/index temporaries allocated for `PROMOTEINDEX`/`PROMOTEFIELD` were
   never freed, so the NEXT argument staged past them and the callee read the
   leaked container temp as its argument: `append(t["k"], v)` appended the
   *container* into itself (a self-referential list → infinite recursion in
   stringify, segfaults). Single-argument promotions never showed it. The
   temporaries are now freed after emission. This was references.md §7's suspected
   "CoW × ref corner"; the promotion machinery itself was correct.

4. **`split` with a leading/trailing/doubled separator (lib/string.cpp).** The
   grapheme-cursor loop ran `mid(start)` one past the end when the subject ended
   with the separator (aborting via the String index check — hit by any
   `split(e.trace, "\n")`, since traces end with a newline). Rewritten with an
   explicit `start <= n` guard and a final empty field, so split/join round-trip.

5. **The in-place-mutation "unshare" fast path is disabled (compile/lower.cpp,
   `emit_index_unshare`), correctness over speed.** It nulled a module/upvalue
   binding around an indexed/field store so the store could mutate in place; with
   store failures now catchable (bad index, throwing field setter), the write-back
   that restores the binding can be skipped, leaving the variable null after
   `catch` — including a *pre-existing* corruption for Array out-of-range writes.
   Until an exception-safe variant exists (sketch: a pending-write-back journal on
   the Isolate that the error-landing path replays), stores to module/upvalue-held
   containers pay a copy-on-write clone. Golden disasm corpus regenerated
   (`arrays.dis`, `slices.dis`).

6. **`Runtime::do_file` + `get_script_path()` (E1).** `do_file(path)` runs a script
   file with its identity attached: `import` resolves in the script's own directory
   (before `add_import_path` dirs and `$PHON_MODULE_PATH`), and the new
   `get_script_path()` builtin (lib/system.cpp) reports the file whose code is
   currently executing — maintained as a stack on the Isolate, pushed around each
   module top-level and the main chunk, matching the old engine's dynamic
   `current_path` semantics (a path-less context raises). `examples/repl.cpp`
   `run_file` now uses it.

7. **Isolate globals are real (E2): `global var` + `Runtime::add_global`.** The
   parser accepted `global` but the lowerer made a plain module binding, so a
   global declared in module A was invisible to module B. The ModuleLoader now
   carries a session-wide name→slot map (`find_global`/`declare_global`, default
   implementations keep other loaders working); `global var` declares there in
   pass 1 (no shadowing module slot), reads/writes resolve through the ordinary
   GET/SET_MODULE opcodes on the shared slot vector, and the embedder injects
   values with `Runtime::add_global(name, value)` (design §11's GUI channel,
   demonstrated by repl.cpp's `host_version`).

8. **The REPL's interactive leniency #1 (design §11) is implemented.**
   `Runtime::set_interactive(true)` (set by the example REPL) makes bare
   assignment to an unresolved name auto-declare a session binding — previously
   `x = 5` errored even interactively. Reads of unknown names still error, and
   file runs are never lenient.

9. **Stdlib additions.** `lib/table.cpp` (new): `contains`/`is_empty`/`keys`/
   `values` and in-place `remove`/`clear` for Table — `contains` is the only way to
   distinguish a stored null from a missing key. `to_int`/`to_float` string
   parsing (raise `[Value error]` on unparseable input). List `find(xs, v, from)`.
   `ndim(Array)`. Regex `pattern(re)` accessor and `groups(m)` (the captures as a
   List, replacing the old engine's iteration over the regex object).

10. **Strings iterate (`for c in s`), by grapheme.** The ITER_INIT String arm
    materializes a List of one-grapheme Strings at loop entry (the Set precedent);
    pair form binds 1-based grapheme positions. Closes the M5 Stage 3b deferral
    for the builtin-collection path.

11. **Error traces carry the file, and a structured form.** Protos record their
    `source_path` (stamped over the compiled tree by the Runtime for file-backed
    chunks/modules); trace lines render `  at <fn> (line N) in <file>` when known
    (unchanged for path-less chunks, so existing exact-match tests still pass
    under do_string). `Error` gains a third builtin field **`frames`** (slot 2): a
    List of `{function, line, file}` Tables, innermost first, captured at first
    raise alongside the rendered `trace` — the structured surface the GUI's
    clickable trace / editor highlight needs, with no string parsing.

12. **Natives raise with the script call line.** The interpreter stamps
    `Isolate::native_call_line` before each native invocation; `raise(msg, 0)`
    (the native convention) substitutes it, so `assert` failures and stdlib
    errors report the call site instead of "(line 0)".

13. **Named functions are first-class values (closing M4 #1's remaining stub /
    M5 #6).** A generic name in value position loads a singleton native
    trampoline cell (`generic_function_value`, one per name for the process) that
    resolves the generic BY NAME on each call — so the value tracks overloads
    added or journal-retracted after it was taken, `f == g` is per-function
    identity, and calls through variables/list elements/table values dispatch like
    direct calls. The native "Function" class now derives from the closure
    "Function" class so `x is Function` covers both callable kinds. Limitations
    (documented): keyword options and `ref` promotion do not flow through an
    indirect call.
