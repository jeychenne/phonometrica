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

12. **Files placed under `phon/vm/` and `phon/compile/`.** New: `phon/vm/`
    (`opcode`, `proto`, `function`, `isolate`, `interpreter`) and
    `phon/compile/{lower,disassembler}`, plus the `phon/runtime/runtime.*` façade
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
