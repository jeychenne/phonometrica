# Base-class migration notes (old engine → new engine, phase 1)

*2026-07-13. Status: in progress — this file is the audit trail for the migration of Phonometrica's
core value types to the new base-class layer (the new engine lives in `../engine` and is built
alongside; see "Coexistence" below).*

## What changed

Three semantically load-bearing changes, per the migration plan:

1. **`Array<T>` is 0-based and copy-on-write** (`phon/base/array.hpp`, still included as
   `<phon/array.hpp>`). All native element indices are 0-based and non-negative; `find`/`rfind`
   return `npos` (−1) when absent instead of 0; copies share the buffer and the first mutation
   through a shared handle clones it (reads never do). Negative indexing, `slice()`, `iter()`,
   `dim_size()`, `from_memory()` and N-dimensional (ndim > 2) support were removed from the
   native API (N-D was unused and its indexing was broken; negative indices are script-level
   sugar handled at the bridge).
2. **The scripting language remains 1-based.** The ±1 conversion lives in exactly one header,
   `phon/runtime/index_conversion.hpp`, with one inbound choke point (`index_from_script`, plus
   the matrix-axis variant `dim_index_from_script` which differs only in error wording) and one
   outbound choke point (`index_to_script`). Only the old engine's script/native bridge uses
   them: `func_list.hpp`, `func_array.hpp`, the VM opcodes that build/destructure lists and
   arrays (`runtime.cpp`), the iterators (`iterator.cpp`), and app natives registered with
   `Runtime::add_global` that receive/return indices. Native C++ code never adds or subtracts 1.
3. **`Regex` is immutable and reentrant; matching returns a `Match`** (`phon/base/regex.hpp`,
   ported from the new engine's design, still included as `<phon/regex.hpp>`). The `Match`
   keeps the old accessor names (`capture`, `capture_start/end`, `capture_*_iter`, `count`,
   `has_match`, `subject`) so call sites thread a `Match` through instead of reading state off
   the pattern. The same compiled `Regex` can be matched concurrently from several threads
   (each match owns its PCRE2 match data) — the groundwork for concurrent queries.

The scripting language's Regex object keeps its historical stateful API: the last match is
stored in the boxed bridge object `ScriptRegex` (`phon/runtime/script_regex.hpp`), never in the
`Regex` class. `ScriptRegex` is pinned non-clonable (`traits.hpp`) so script regexes keep
reference semantics, exactly as documented in `values_references.rst` — note the new C++
`Regex` *is* copyable (copying recompiles), which would otherwise have silently flipped script
regexes to value semantics.

**String, File, Hashmap/Dictionary are unchanged in this phase.** Their APIs already match the
new engine's (1-based grapheme Strings, `find` returning 0 when absent, etc.), so the
implementation swap is deferred to the VM cutover. Gaps to close in the engine before that
cutover: `String::split`/`join` (returning the engine's container), regex `replace`, positional
`arg`, Qt string conversions, and the bootstrap-before-any-String constraint (engine cells
require `bootstrap()`; static-initializer Strings would be unsafe).

## Coexistence

Both engines live in the code base until the migration completes. They define identically named
types in `namespace phonometrica` (`String`, `Handle<T>`, `Variant`, `Class`, `Runtime`, …), so
they can never be linked into one binary or included into one translation unit. Therefore:

- The application binary links only the old engine (`phon-runtime`).
- The new engine is added as a CMake subproject (option `PHON_WITH_NEW_ENGINE`, path
  `PHON_ENGINE_DIR`, default `../engine`), exposing its targets in this build tree:
  `cmake --build build --target phon_engine phon_unit_tests` (its unit tests pass from here).
- The new implementations that the app *does* adopt now (Array, Regex/Match) live in
  `phon/base/`, written to the new engine's semantics so the eventual type swap is mechanical.

Known future collision, resolved by policy now: the concordance domain class `Match` was renamed
`QueryMatch` (the `AutoMatch` alias is unchanged) because the regex `Match` takes the canonical
name — the same name the new engine will bring at cutover.

## Deliberate behavior changes (beyond the three above)

Everything else preserves behavior exactly, with these audited exceptions:

- **`rfind`/`find_back` on lists now finds a match at the last position.** The old
  `Array::rfind` used `result == rbegin()` as its not-found test, so a value sitting in the
  last slot was reported as absent. Pinned by `T-rfind` in `test/engine/test_base_migration.phon`.
- **Reading a capture group that did not participate in a match now throws** a clear error;
  the old code read an undefined PCRE2 ovector entry (garbage offsets).
- **Match data is now sized to the pattern.** The old fixed 30-slot ovector silently truncated
  captures beyond 14 groups; patterns with more groups now work.
- **`rfind` with a positive `from` used to be an unchecked precondition** (debug assert,
  silent nonsense in release); the bridge now resolves any valid script index.
- Old N-dimensional (ndim > 2) `Array` support was dropped — unused, and its index arithmetic
  was wrong (indices were normalized to 1-based but flattened as if 0-based).

## Script-level regression tests

`test/engine/test_base_migration.phon` (wired into `test/engine/run_all.phon`) pins the script
contract across the migration: 1-based list/array indexing incl. negative indices and errors,
find/find_back/insert/remove_at conventions, list value semantics vs. array COW value semantics
(`A18a/A18b`), 1-based iteration keys, destructuring order, the stateful script Regex API
(match/group/get_start/get_end/iteration/independent per-object state), and the unchanged
1-based String API. The full engine suite passes under AddressSanitizer.

Observations (pre-existing, not introduced by the migration):
- `import()`/`reload_module` leaks ~1.9 KB per run (25 allocations) via `Runtime::reload_module`;
  visible under LeakSanitizer.
- Invoking the binary with a *relative* script path leaves `Runtime::current_path` relative, so
  `get_script_path()`-based data resolution in the statistics suite fails; run the suites with an
  absolute script path (`phonometrica "$PWD/run_all.phon"`), or absolutize `argv` at startup
  (flagged as a separate follow-up task).

## Flagged 1-based call sites and COW audit

The exhaustive per-directory reports (every rewritten 1-based site, every Array mutation site
and its COW justification, ambiguous cases) are appended below as the migration passes land.

### GUI: analysis/dataset cluster (done — analysis_view, dataset_view/model/commands)

- `dataset_model.cpp`/`dataset_view.cpp`/`dataset_commands.cpp`: all Qt↔Dataset ±1 fudges removed
  (Qt sections now map 1:1 onto 0-based dataset rows/columns); move/duplicate commands store
  0-based positions; vertical-header `section+1` kept (displayed ordinal).
- `analysis_view.cpp` (~9 900 lines): EDA/effects/post-hoc/LaTeX-summary regions rewritten —
  column sentinels 0→−1, ~60 row/column loops → 0-based, packed-Cholesky index `r(r+1)/2+c`,
  posterior/EMM/contrast tables indexed `[i]`/`[b]` with first element `[0]`. Display ordinals
  (`.arg(j+1)`, "Model N") kept at display sites. LaTeX column-alignment loop intentionally
  starts at 1 (first column is `l`, the rest `r` — not an Array index).
- COW audit: all mutations are on freshly constructed or move-assigned unique buffers; noted the
  pre-existing pattern of binding models via non-const `Analysis::model()` references (a shared
  buffer would take one safe spurious clone on first non-const read; no site writes through a
  shared handle).
- No phonometrica-Regex use anywhere in the cluster (only Qt QRegularExpression, untouched).

### GUI: remaining files (done)

- Real out-of-bounds bug fixed in `main_window.cpp` find-silences flow: it created a layer at
  slot 0 (`create_empty_layer`) but then wrote intervals to layer **1** — the parallel
  transcription flow already used 0. The new debug assert would have caught it at runtime.
- 0-based rewrites: `project_model.cpp` (Directory get/insert, rowOfElement),
  `find_silences_dialog.cpp`/`transcribe_dialog.cpp` (sound lists), `record_sound_dialog.cpp`,
  `start_view.cpp` (recent projects, 10-item cap kept), `spectrogram_widget.cpp` (cached window,
  frequency mapping, formant matrix now (i,0)/(i,1)), `sound_view.cpp` (formant loops; displayed
  ordinals keep `i+1` at the display site), `protocol_builder_dialog.cpp` (12 loops),
  `intensity_widget.cpp`.
- **Flagged 1-based domain values, intentionally left**: sound *channel numbers* (1-based, 0 =
  average/all; `Sound::channel_view` asserts `n >= 1`) in waveform/spectrogram/wave_bar/
  sound_view loops; layer combo convention (0 = "All layers"); the query editors' reference
  constraint (1-based ordinal end-to-end); script-editor line numbers; `user_dialog.cpp`
  script-boundary ±1 (it never subscripts an Array with script indices, and the historical
  "out-of-range default is ignored" semantics must not become a throw).
- Persistence boundary: `QueryMatch::Target::layer` is 0-based in memory; concordance XML keeps
  the historical 1-based `layer` attribute (`+1` on write, `−1` on read), so saved concordance
  files from before the migration load unchanged.

### Application + concordance layer (done)

- `conc/concordance.cpp` second half (subset → EOF) was still 1-based: subset row copies,
  `add_numeric_column`/`add_text_column` (wrote one-past-the-end and left slot 0 default — a
  silent data shift), apply_protocol, merge/matches_equal, aux-column resolution
  (`resolve_aux_column` returned 0-for-absent while its callers tested `< 0` — now −1),
  KWIC/labels/event context boundaries, and `match_for_row`/`point_for_row` (the old formula
  produced a **negative point index** for every Long-layout row — each cell was wrong/OOB).
- Acoustic queries (`formant/pitch/intensity/spectral_moments/voice_quality_query.cpp`):
  measurement loops, `matches[i]`, formant matrices now `(i,0)/(i,1)`.
- `spectrum.cpp load()`: settings-header parsing used `String::find` (1-based, 0-for-absent)
  with a `< 0` test that could never fire and an off-by-one `left()` — every key failed to
  match. Rewritten with the iterator overload; this is a deliberate fix of a broken parser.
- `sound.hpp`: `average_channels` default `first_frame` contradicted its own 1-based assert.
- **Flagged 1-based domain values, intentionally left** (the audit list): QueryMatch target
  ordinals (`get(i)`, linked-list walk — consumed 1-based by the GUI), reference-constraint
  ordinal (serialized in XML), `Constraint::layer_index` (user-facing, 0 = any layer, converted
  once at query.cpp:646), F1..Fn formant numbers (labels), regex capture-group numbers, sound
  frames and channel numbers (1-based domain; converted only at the data-matrix subscript),
  XML/TextGrid serialization (±1 at parse/serialize boundaries), protocol error-message ordinals.
- COW audit: no `raw_data()` in the application layer; all shared-buffer copies (Concordance
  copy ctor, `Query::copy`, `copy_metadata_to`, `Query::search`) mutate through detaching
  accessors; `first()/last()` on possibly-empty arrays all guarded.
- Regex threading confirmed: praat, protocol_apply, project (import_metadata), data_table and
  conc/query.cpp all use local `Match` objects; constraint/metaconstraint own only immutable
  compiled patterns. No stateful match-then-capture sequence remains outside the script bridge.

### Analysis layer (done — validated against R gold references)

- `mixed_model.cpp` was entirely unmigrated at HEAD (~95 sites): coefficient/posterior tables,
  INLA grid-integration blocks, hyper-parameter writers, random-effects and conditional-mode
  indexing, packed-Cholesky unpacking, preliminary-fit initialization (`fe.beta[i+1]` reads one
  past the end — this was the crash the statistics suite caught), and final model population.
- `fitting.cpp`: two **live bugs** — `find_column(...) == 0` not-found checks (0 is a valid
  column now) → `< 0`; plus posterior/hyper summary loops.
- Everything else in `phon/analysis` and `phon/utils/matrix.*` verified correct (pattern sweeps
  for 1-based loops, hybrid `[i±1]` layouts, 2-D `(i+1,j+1)`, find-vs-0, and 0-as-absent
  sentinels). The `Eigen::Map(const_cast<double*>(arr.data()))` sites are read-only maps; the
  one in-place arithmetic candidate operates on an Eigen copy.
- **Oracle: frequentist statistics suite 552/552 against R references (lm/glmmTMB); Bayesian
  per-family tests pass except two `test_student` hyper-SD checks whose R reference the repo
  itself documents as untrustworthy (excluded from the frequentist suite; not an indexing
  regression — flagged for the Student-t CCD grid owner).**
- Note for the swipe pitch tracker: `signal_processing.cpp:439` passes
  `const_cast<double*>(input.data())` into the external C routine; it appears read-only, but if
  swipe ever wrote into its input this would be a shared-COW-buffer mutation. Worth an upstream
  const-audit eventually.
- `phon/utils/slice.hpp` (`Slice`) keeps its 1-based accessors: it is a view over the Sound
  frame domain (frames are 1-based domain values, used only by `sound.hpp`). Flagged as part of
  the Sound-domain convention, not an Array call site.

### Final verification (2026-07-14)

- Full build of `phon-runtime`, `phon-app`, `phon-gui`, `phonometrica`: clean.
- `test/engine/run_all.phon` (real binary): **all tests passed** (includes
  `test_base_migration.phon`).
- `test/statistics/frequentist/run_all.phon`: **552 passed, 0 failed**.
- Repo-wide sweeps: no `Array.find()==0` checks left; no `to_base0` outside `Slice`
  (Sound-frame domain); every remaining `for (i = 1; i <= n)` loop audited and attributable to
  a flagged 1-based domain (channels, frames, formant numbers, capture groups, script indices
  at the bridge, iteration counts).
- Build-tree note: `build/` was reconfigured with `CMAKE_CXX_FLAGS_DEBUG="-g -O2"` (asserts
  stay active; plain `-O0` made Eigen LDLT in the statistics suite impractically slow). Restore
  with `cmake -DCMAKE_CXX_FLAGS_DEBUG=-g .` if desired.

### Runtime layer (done, verified by the engine test suite under ASan)

- Bridge conversions installed in: `func_list.hpp` (get/set item, first/last fields, find×2,
  find_back×2, left/right element loops, remove_at, insert with allow_end, sorted_find),
  `func_array.hpp` (1-D and 2-D get/set), `runtime.cpp` (`NewArray` literal fill, `NewList`
  fill, list destructuring), `iterator.cpp` (`ListIterator` 0-based cursor with 1-based script
  keys).
- Internal 0-based rewrites: `list.cpp` (`to_string`, `to_json`), `table.cpp` (`to_json` key
  loop), `meta.hpp` (`to_string(Array<double>)`, incl. guarding 2-arg indexing on 1-D arrays),
  `string.cpp` (`String::join`), `func_math.hpp` (elementwise kernel), `compiler/token.cpp`
  (token-name table), `compiler/source_code.cpp` (`get_line`/`report_error` take 1-based line
  numbers as domain values, converted at the accessor).
- COW-sensitive sites called out: `List::traverse` (GC marking) uses `Array::raw_data()` —
  the one sanctioned non-detaching mutable access, because marking must observe the shared
  elements in place; read-only bridge functions bind `const` references so shared buffers are
  never cloned on reads; every mutating bridge function goes through `Variant::unshare()` (the
  script-level value-semantics clone) and then the Array's auto-detaching mutators.

# Step 3 — script port to the new engine language (2026-07-16)

*The shipped scripts (std/) and the engine script test suite (test/engine/) were ported
from the old Phonometrica scripting language to the new engine language and validated
under the new engine's script runner. Ported files live on the session worktree branch
(`claude/determined-kilby-88cb2a`); originals remain on `main`. The runner is the
engine's example host (`examples/repl.cpp` → target `phon_repl`, built in this tree via
`PHON_WITH_NEW_ENGINE`):*

```
cmake --build build --target phon_repl
PHON_MODULE_PATH=test/engine build/phon_engine_build/phon_repl test/engine/run_all.phon
PHON_MODULE_PATH=std        build/phon_engine_build/phon_repl std/headless_test.phon
```

Both runs are green. `PHON_MODULE_PATH` is required because `phon_repl` reads the file
and calls `do_string`, so the main chunk has no directory for import resolution
(gap E1 below).

## Per-script status

| Script | Status | Notes |
|---|---|---|
| test/engine/run_all.phon | green (headless) | `import` statements; module top-levels run before the orchestrator's own code, so only a closing banner |
| test/engine/test_scientific_notation.phon | green (headless) | mechanical port |
| test/engine/test_string_interpolation.phon | green (headless) | rewritten for `{expr}`; pins the new raw single-quote family |
| test/engine/test_compound_assignment.phon | green (headless) | Module-object targets → class instance/module bindings; field compound form is a compile error (L5) |
| test/engine/test_multiple_declaration.phon | green (headless) | FEATURE REMOVED (L1); file pins single-target equivalents |
| test/engine/test_upvalues.phon | green (headless) | adds a true-upvalue (function-local) case |
| test/engine/test_try_catch.phon | green (headless) | Error-only throws; adds typed catches + finally |
| test/engine/test_try_catch_loops.phon | green (headless) | all 14 handler-accounting shapes preserved |
| test/engine/test_nested_error_lines.phon | green (headless) | rewritten over `e.trace` (line-number sensitive); 1/0 case → `1 div 0` |
| test/engine/test_error_trace.phon | green (headless) | trace shape/order/line assertions parsed from `e.trace` |
| test/engine/test_list_comprehension.phon | green (headless) | FEATURE REMOVED (L2); loop equivalents pinned |
| test/engine/test_class_ref.phon | green (headless) | `field`/`init`; structural instance equality gone (S6) |
| test/engine/test_base_migration.phon | green (headless) | the phase-1 contract, minus the cases engine bugs make unrunnable (B1) |
| test/engine/test_gui_trace.phon | ported, manual | needs the GUI host; deliberately ends with an uncaught error; not in run_all |
| std/app_stubs.phon | NEW, harness only | headless stand-ins for every app native + the `phon` namespace object |
| std/headless_test.phon | NEW, harness only | drives all six std scripts and asserts their effects |
| std/initialize.phon | green headless (stubs) | app natives: get_settings/plugin/metadata/script_directory |
| std/read_settings.phon | green headless (stubs) | app value: `phon` namespace (settings field); table literal unchanged |
| std/write_settings.phon | green headless (stubs) | app natives: `phon`, get_config_path; dump_json→to_json, File()→open_file |
| std/signal.phon | green headless (pure) | no app natives; hit engine bug B3, worked around read-modify-write; slots must be closures (L6) |
| std/speech_analysis.phon | green headless (stubs) | app natives: get_current_sound, get_visible_channels, get_(mean_)intensity/pitch, get_formants, alert |
| std/transphon.phon | green headless (stubs) | app natives: get_annotations (.path/.nlayer), get_layer_label, get_event_count, get_event_text, create_dialog, ask, view_text; needs a string→int parser (M2) |

The headless harness validates real behaviour, not just compilation: initialize's
directories exist, read_settings' table is queried, write_settings' config file is
re-read, signal's connect/emit/disconnect/dedup/returns are asserted, speech_analysis
runs both the with-sound report paths and the alert branch, and transphon's full export
loop writes a file whose banners/layers/events are asserted (the stub dialog returns
the "OK with defaults" result).

## Engine BUGS found (script-reachable crashes — engine-side fixes needed)

- **B1. Out-of-range List *writes* and inserts abort the process.** `List::normalize`
  (types/list.cpp:152) guards with `PHON_CHECK`, which is a hard abort in this build —
  `lst[0] = x`, `lst[100] = x`, and `insert(lst, 100, v)` kill the interpreter instead
  of raising a catchable `[Index error]` (reads DO raise catchably: the read opcode
  checks first). Two old contract points are unrunnable until fixed:
  "out-of-range write throws" and "insert past the end appends" (the old engine
  appended; a decision is also needed on whether the new engine keeps that semantics
  or makes it an error).
- **B2. `split` with a trailing separator aborts** (`String::index_to_iter` via
  types/string.cpp:326): `split("a,b,c,", ",")` dies. Bites immediately in practice
  because `e.trace` ends with `\n` — `split(e.trace, "\n")` is the natural way to walk
  a trace. Ported tests work around it with `rtrim` first.
- **B3. Passing a Table slot to a mutating (`ref`) native corrupts the stored list.**
  `t["k"] = [f]; append(t["k"], g)` leaves element 2 broken ("value is not callable"
  for closures; a segfault variant with plain values when the slot was written by
  assignment first). This is the references.md §7 PROMOTEINDEX(Table)/auto-collapse
  path — the "CoW × reference corner" the DEVIATIONS follow-ups already flag.
  std/signal.phon originally used exactly this idiom; ported code uses copy → mutate →
  store-back instead.

## Missing builtins / embedding gaps (needed by real workloads)

- **E1. No path-aware script execution.** `phon_repl` loads files via `do_string`, so
  the main chunk has no source path: same-directory imports don't resolve (must set
  `PHON_MODULE_PATH`), and there is no `get_script_path()` (used by the statistics
  suite's data/reference resolution — see the survey below). Needs a `do_file`/chunk
  path on the Runtime surface.
- **E2. No embedder global injection.** `Runtime::add_global` (design §11) is not
  implemented, and `global var` declared in module A is not compile-visible from
  module B, so an app-injected namespace (`phon`, GUI natives) has no headless
  equivalent — ported std scripts carry an explicit `import app_stubs` marked for
  removal at app integration. The app-side natives themselves will be registered with
  `rt.add_function` (which IS ready).
- **M1. No Table function surface.** `contains(t, k)`, `keys`, `values`, `remove`,
  `is_empty` don't exist for Table (only len/indexing/iteration). Reading a missing
  key returns null, which is the workaround membership test — indistinguishable from
  a stored null.
- **M2. No string→number conversion** (`int()`, `number()`, `to_int`…).
  std/transphon.phon parses layer numbers from a text field; the port goes through
  `from_json(s)` as a stopgap.
- **M3. List `find` has no from-position overload** (old `find(lst, x, from)`).
- **M4. No `ndim()` for Array**, and the old `.ndim/.nrow/.ncol/.length/.first/.last`
  FIELD accessors on Array/List are gone (nrow()/ncol()/len()/first()/last() cover
  all but ndim).
- **M5. No structured error-trace API.** `e.trace` is one rendered String
  (`  at <fn> (line N)`, innermost-first, no file component). The old
  get_error_line()/get_error_trace() gave {file, line, function} records; the GUI
  editor (error highlight, clickable frames) needs the structured form — or at least
  a stable, documented trace format plus a file field.
- **M6. `assert` failures report `(line 0)`** — no source position on the assertion
  error, which hurts exactly the suites that lean on assert.
- **M7. Named functions are still not first-class** ("cannot be used as a value yet
  (M8)"): callback registration must use anonymous closures (std/signal.phon slots).
- **M8. No `pattern`/`length` accessors on Regex**, and a Regex/Match is not iterable
  (old scripts iterated a regex's captures); walk `1 .. group_count(m)-1` instead.
- **M9. String iteration is unsupported** — `for c in "abc"` raises "value is not
  iterable" (old comprehensions iterated strings); grapheme access is via
  `char(s, i)`/`len(s)`. (M5 Stage 3b deferral, confirmed still open.)

## Semantic changes recorded during the port (deliberate language decisions;
## the ports adopt the new semantics — listed so the cutover isn't surprised)

- **L1. Multiple declaration/assignment and list destructuring removed** (single-target
  `var`/assignment only; `for k, v in` is the sole multi-binding). Old
  test_base_migration L37 destructuring pin replaced by explicit reads.
- **L2. List comprehensions are NOT YET IMPLEMENTED in the new engine** (no
  `foreach` keyword, no `[y foreach x in xs if c]` syntax) — *not* a removed
  feature (owner correction, 2026-07-18): until they land, the ports use explicit
  loops or library functions, and the comprehension test file pins those
  equivalents so the originals can be restored on top of them.
- **L3. In FILES, assignment never declares** — undefined names, including the old
  runtime `[Symbol error]` test cases, are COMPILE errors. In the REPL the design
  §11 leniency applies: bare assignment to an unresolved name auto-declares a
  session binding. (Correction + fix, 2026-07-18: the leniency had never been
  implemented — the REPL rejected `x = 5` — and now works via
  `Runtime::set_interactive`.)
- **L4. Only `Error` values can be thrown**; a non-Error `throw` is itself a catchable
  `[Type error]`. A caught error is an instance (`e.message`, `e.trace`), not a bare
  String. No `rethrow` — `throw e` preserves the original trace.
- **L5. Compound assignment targets:** `+= -= *= /= &=` work on locals, module
  bindings, upvalues, and subscripts, but NOT on object fields (`o.f += 1` is
  "invalid compound-assignment target"); `%=`/`^=` no longer exist (no `%` operator).
- **L6. Truthiness:** null is the only falsy non-boolean (0, "", [] are truthy) —
  matches the old `if sound then` app idiom for null-or-object returns.
- **L7. Float division by zero yields inf** (old: runtime error); `1 div 0` raises
  `[Math error]`. Tests that used `1/0` as a native-error source now use `div`.
- **L8. `${expr}` → `{expr}` interpolation; `\{` escapes a literal brace; unknown
  escapes are hard errors; single-quoted (and triple-single) strings are RAW — the
  old engine interpolated them.**
- **L9. print(...) is a function with `sep = " "` (old statement juxtaposed args with
  no separator); ports use interpolation/`&` to preserve output.**
- **L10. `import` is a compile-time statement, not a function**: no
  `import("../lib/x")` path form, no try/catch around a failing import (a missing
  module is a compile error), and imported module top-levels run BEFORE the importing
  chunk's statements (run_all's banner had to move to the end). Module state replaces
  the old first-class Module values (`Module("name")` is gone).
- **S1. `sorted_find` returns 0 for an absent value** (old: its 1-based lower-bound
  slot). Pinned in the ported base-migration test.
- **S2. Regex API is the stateless design:** `regex(pat[, flags])` constructor
  (lowercase), `match(re, subject[, from])` → Match or null, `group/group_start/
  group_end/group_count(m, …)`; `group_count` INCLUDES group 0 (old `count()` did
  not); no per-object match state left to pin.
- **S3. String mutators are in-place ref natives** (`trim(s)`, `rtrim(s)`, `append(s,
  x)`…) returning nothing — old call sites that used their return value must
  restructure.
- **S4. dump_json → to_json, File(path, mode) → open_file(path, mode)** (File is not
  script-constructible; `open` is reserved).
- **S5. Class syntax:** `field x = 0` (old bare `x = 0`), constructor `init` (old
  `initialize`).
- **S6. No structural equality for class instances**: `==` is identity/sharing-based
  for both value and ref classes (no equals hook yet), so two independently
  constructed equal-valued instances compare unequal — the old engine compared value
  classes structurally. COW aliasing behaviour (alias keeps the old value after a
  mutation detaches) is unchanged and re-pinned.
- **S7. `local` at the top level** now expresses module-private state (signal.phon's
  `bindings`/`slot_id`, all test helpers) — public top-level functions become
  process-global generic methods, so test-suite helpers are `local function` to avoid
  cross-module collisions when run_all imports every test.

## test/statistics survey (NOT ported — dependencies logged)

The suite (26 .phon files: frequentist/, bayesian/, lib/assert.phon) is built on:

- **App natives from the analysis layer:** `fit()` (23 call sites — the glmmTMB-style
  mixed-model fitter), `logLik()`, `get_coef()`, `load()` (CSV → Dataset). None exist
  on the new engine; they arrive with the C++ cutover via `rt.add_function`.
- **`load_json()`** — replaceable today by `read_file` + `from_json`.
- **`get_script_path()`** (5 sites; lib/assert.phon's resolve_sibling /
  resolve_data_path) — blocked on gap E1, same relative-path caveat already noted in
  the phase-1 observations.
- **First-class Module values + path imports:** `let A = import("../lib/assert")`,
  shared counters on the module, `A.record_fail(...)`, and run_all's
  `try import(name) catch` keep-going pattern. Under the new engine this becomes
  `import assert` (name-based; needs the lib dir on the module path), qualified
  access `assert.x` for vars, flat-global functions — but there is no way to
  try/catch a failed import (L10) and no `str()`/`type()` builtins (to_string()
  exists; a class-of accessor does not).
- Everything else in the harness (JSON refs, tolerance checks, string helpers) maps
  onto existing engine builtins.

Porting the statistics suite is therefore blocked on the analysis-layer natives and
is deferred to the C++ cutover; the harness itself (assert.phon) is portable once E1
and the import-failure-tolerance question are settled.

# Step 3 addendum — engine fixes for the gap log (2026-07-18)

*On the owner's instruction, the script-reachable crashes, embedding gaps, and
missing builtins above were fixed IN THE ENGINE (~/Devel/engine — see its
DEVIATIONS.md, "Post-port hardening", for the per-change record). Engine unit
suite green under the normal, ASan, and TSan builds (394 cases each); the ported
suites below re-run green and now pin the fixed behaviour
(test/engine/test_fixed_gaps.phon is the dedicated regression file).*

Status of the step-3 gap list:

- **B1 FIXED.** Out-of-range List writes/inserts and pop-from-empty raise
  catchable `[Index error]`/`[Value error]` (new `ScriptError` seam from the types
  layer into the interpreter's handler dispatch). Script-level `insert` past the
  end APPENDS again (the old contract, L22 restored in test_base_migration).
  Bonus fix: a failed store no longer leaves the target binding null — the
  in-place-mutation "unshare" fast path was exception-unsafe (this also bit
  pre-existing Array out-of-range writes) and is disabled pending an
  exception-safe variant.
- **B2 FIXED.** `split` handles leading/trailing/doubled separators (empty
  fields; split/join round-trips). `split(e.trace, "\n")` is safe — the ported
  trace tests dropped their `rtrim` workaround comment context.
- **B3 FIXED.** Root cause was not the reference machinery but a compiler bug:
  `emit_promote_arg` leaked its container/index staging registers, so the NEXT
  argument staged past them and a native read the leaked temp as its argument
  (appending the container into itself). std/signal.phon is back on the natural
  `append(bindings[id], slot)` idiom.
- **E1 FIXED.** `Runtime::do_file` runs a script with its file identity: imports
  resolve in the script's own directory (`PHON_MODULE_PATH` no longer needed for
  the suites — run_all.phon/README updated) and the new `get_script_path()`
  builtin works (dynamic current-file semantics, like the old engine). The
  statistics suite's resolve_sibling pattern is now portable.
- **E2 FIXED.** Isolate globals are real: `global var` declared in one module
  resolves from every later-compiled module, and the embedder injects app state
  with `Runtime::add_global` — std/app_stubs.phon now declares `global var phon`,
  exactly the shape the app will use, and the std scripts reference `phon` bare
  (the `import app_stubs for phon` ceremony is gone). REPL leniency: see L3.
- **M1 FIXED.** Table stdlib module (contains/keys/values/remove/clear/is_empty);
  `contains` distinguishes a stored null from a missing key. signal.phon uses it.
- **M2 FIXED.** `to_int(s)` / `to_float(s)` (raise `[Value error]` on unparseable
  input). transphon.phon's from_json stopgap replaced.
- **M3 FIXED.** List `find(xs, v, from)` (L16 restored).
- **M4 FIXED.** `ndim(Array)` (A2/A6 restored).
- **M5 FIXED.** Structured traces: `e.frames` is a List of {function, line, file}
  Tables (innermost first), and `e.trace` lines name the source file for
  file-backed chunks (`  at <fn> (line N) in <file>`); Protos carry their source
  path. This is the GUI's error-highlight surface.
- **M6 FIXED.** Natives raise with the script call line ("(line 0)" gone; assert
  failures and stdlib errors now localize).
- **M7 FIXED.** Named functions are first-class values: a generic name in value
  position yields a singleton Function value (per-name identity, resolves
  overloads per call, `is Function` true — the native Function class now derives
  from the closure one). signal.phon slots can be named functions
  (headless_test.phon pins it). Known limits: keyword options and `ref`
  promotion do not flow through an indirect call.
- **M8 FIXED.** `pattern(re)` and `groups(m)` (R9 restored; capture iteration via
  a List).
- **M9 FIXED.** Strings iterate by grapheme (`for c in s`, pair form gives
  1-based positions).

Still open (unchanged): the app-native surface itself (fit/logLik/get_coef/load,
sound/annotation accessors, dialogs) arrives at the C++ cutover via
rt.add_function; structural equality for value-class instances (S6); field
compound assignment (L5); the statistics suite port (now unblocked on E1/E2 —
remaining blockers are the analysis-layer natives and the import-failure-
tolerance question, L10).

# Step 4a — app native inventory (2026-07-19)

*Complete enumeration of every native the app registers on the OLD engine
(`Runtime::add_global` and its satellites), classified into the four cutover
buckets. This section sizes the VM cutover and is the checklist the cutover
branch works from. Survey only — no C++ was modified.*

**Method.** `grep -rn add_global phon/` yields 359 hits; 8 are the machinery
itself (4 declarations in `phon/runtime/runtime.hpp`, the definitions + `GLOB`
macro in `phon/runtime/runtime.cpp:209/2318/2323`, 1 comment in
`index_conversion.hpp`), leaving **351 actual registration sites**: 189 in the
old engine's own builtin library (`phon/runtime/builtins.cpp`, callbacks in
`func_*.hpp`) and 162 in the application. On top of the `add_global` sites the
same surface includes 15 `GLOB` class injections (`runtime.cpp:209-225`), 13
module-member functions installed with `Module::define` (4 in main_window.cpp,
9 in project.cpp), 7 string-keyed `get_field` dispatchers installed with
`cls->add_method(rt.get_field_string, …)`, 1 script constructor
(`add_constructor`, `Prior()`), and the `rt["phon"]` namespace injection
(phonometrica.cpp:96). All are inventoried below.

**Two corrections to the step-4a task brief, established from the engine
sources** (record them so the cutover branch doesn't work from stale
assumptions):

1. **`add_class<T>` takes a PLAIN C++ class.** The cell-headed requirement
   (leading `Cell header` member + `phon_class` static) was removed by the
   engine's "Embedding transparency" pass (DEVIATIONS items 26–29): the engine
   boxes `T` as `{ Cell header; T value }`, `class_of<T>()` replaces the
   static, and `T&`/`const T&` parameters work alongside `Handle<T>`.
   Class-port invasiveness is therefore about **ownership migration** (old
   `Handle<T>`/`TObject` cells → new `Handle<T>` boxes), not type surgery.
2. **There is no `logLik` native.** The log-likelihood is the field
   `model.loglik`, served by the `model_get_field` dispatcher
   (data_table.cpp:1089). The step-4b trio is really `fit` + `get_coef` +
   *Model field access*.

## Bucket definitions

- **COVERED** — the new engine's stdlib already provides it; the old
  registration disappears at cutover (signature/semantic diffs noted).
- **FUNCTION PORT** — app logic re-registered via `rt.add_function` typed
  lambdas; old manual Variant branching/arity checks become multiple-dispatch
  overloads.
- **CLASS PORT** — an app type registered via `rt.add_class<T>`.
- **INJECTED VALUE** — a value pushed into the script namespace →
  `rt.add_global`.
- **(FIELD DISPATCHER)** — the 7 `get_field` sites; not a bucket of their own
  but called out because they need a new-engine property mechanism (engine gap
  G1 below).

## Old-engine builtins (phon/runtime/builtins.cpp — 189 sites): COVERED 149, ENGINE GAP 36, INJECTED VALUE 4

New-engine references: `~/Devel/engine/phon/engine/lib/*.cpp`, bootstrap
natives (print/len/assert/to_string) in its runtime.cpp:265-269.

### Generic / conversions / modules

| name | old site | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| type | builtins.cpp:80 | ENGINE GAP | none (`is` operator + compile-time class names only) | used by test/statistics/lib/assert.phon:154 (`type(x) == type([])`) |
| len | builtins.cpp:81 | COVERED | len | new accepts only List/String/Table/Set (old also Array/Regex/File); Array/File/Regex lengths via ndim/dims, file API, group_count |
| str | builtins.cpp:82 | COVERED | to_string | rename; `str(` used across test/statistics |
| bool / int / float | builtins.cpp:83-85 | ENGINE GAP | to_int/to_float(String) partially | old converted any Object; unused → drop |
| load_json | builtins.cpp:86 | COVERED | from_json | name AND semantics: old load_json EVALUATED the text via do_string (func_generic.hpp:90); new is a real parser. Used: statistics assert.phon:627 |
| dump_json | builtins.cpp:87 | COVERED | to_json | rename; new takes any value + optional indent |
| import (1-arg) | builtins.cpp:88 | COVERED | `import` statement | function form returning Module gone; relative-path calls throughout test/statistics must be rewritten (L10) |
| import (reload) | builtins.cpp:89 | ENGINE GAP | none | unused → drop |
| contains(Module,String) | builtins.cpp:98 | ENGINE GAP | none (no runtime Module type) | `Module(...)` record-builders in statistics libs need a Table/class idiom |

### Math (scalar + elementwise)

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| abs, ceil, cos, exp, floor, log, sin, sqrt (Number + Array forms) | builtins.cpp:101-135 | COVERED | lib/math.cpp + lib/array.cpp elementwise overloads | int-preserving abs kept (two overloads) |
| acos, asin, atan, atan2, log10, log2, tan (Number) | builtins.cpp:103-136 | COVERED | lib/math.cpp:37-47 | |
| acos, asin, atan, log10, log2, round, tan (Array form) | builtins.cpp:104-137 | ENGINE GAP | none | all unused in std//test — drop or trivial adds |
| max, min (Number×2 / Integer×2) | builtins.cpp:124-127 | COVERED | lib/math.cpp:64-67 | |
| random | builtins.cpp:128 | COVERED | lib/math.cpp:70 | old std::rand → proper RNG |
| round (Number[, ndigits]) | builtins.cpp:129,131 | COVERED | lib/math.cpp:56-58 | round-half-up both |
| E, PI | builtins.cpp:138-139 | INJECTED VALUE | register_constant (compile-time inlined, shadowable) | |
| SQRT2, PHI | builtins.cpp:140-141 | INJECTED VALUE → gap | not registered in new engine | unused — drop or 2 register_constant lines |

### String

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| contains, starts_with, ends_with, find(×2), left, right, count, to_upper, to_lower, is_empty, char, split | builtins.cpp:144-161 | COVERED | lib/string.cpp | find 1-based, 0-when-absent in both; new split yields trailing empty field |
| slice(s, from) | builtins.cpp:153 | COVERED | lib/string.cpp:38 | |
| slice(s, from, count) | builtins.cpp:154 | COVERED | lib/string.cpp:39 | **SEMANTIC CHANGE: old 3rd arg is a count (`mid(from,count)`), new is an inclusive end position** |
| reverse, append, prepend, trim, ltrim, rtrim, remove, replace (ref, in-place) | builtins.cpp:158-172 | COVERED | lib/string.cpp:90-99 | in-place ref natives returning nothing, both engines |
| find_back(×2), insert, remove_first/last/at, replace_first/last/at (String forms) | builtins.cpp:149-175 | ENGINE GAP | none | ALL unused (every test/std hit is the List variant) — drop |

### List

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| contains, first, last, find(×2), find_back(1-arg), left, right, join, clear, append, prepend, is_empty, pop, shift, sort, sorted_find, sorted_insert, is_sorted, reverse, remove, remove_first, remove_last, remove_at, shuffle, sample, insert | builtins.cpp:182-209 | COVERED | lib/list.cpp | insert-past-end appends (pinned test_fixed_gaps.phon:53); sorted_find returns 0 for absent (S1; old returned the lower-bound slot); old sorted_insert deduped (func_list.hpp:353); new sort raises on incomparable elements |
| find_back(xs, v, from) | builtins.cpp:188 | ENGINE GAP | none (1-arg exists) | unused — drop |
| intersect, unite, subtract | builtins.cpp:210-212 | COVERED | lib/list.cpp:246-266 | **old used std::set_* (assumed sorted inputs); new is order-preserving membership — results differ on unsorted input** |

### File

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| open(path[, mode]) | builtins.cpp:220-221 | COVERED | open_file (lib/file.cpp:78/81) | renamed (`open` is reserved); doubled as the `File(...)` ctor (builtins.cpp:236) — `File(` used in std/write_settings.phon + std/transphon.phon (already migrated in step 3, S4); new adds (path,mode,encoding) + encoding(f) |
| read_line, read_lines, write_line, write_lines, write, close, read, read_file, rewind, tell, seek, eof | builtins.cpp:222-233 | COVERED | lib/file.cpp | old `read` reads from CURRENT POSITION to EOF — verify new read_all position semantics at cutover |

### Table

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| contains, is_empty, clear, remove | builtins.cpp:240-243 | COVERED | lib/table.cpp (post-port M1 fix) | |
| get(t, key[, default]) | builtins.cpp:244-245 | ENGINE GAP | none | unused (indexing returns null for missing; contains disambiguates) — drop |
| (.keys/.values fields) | func_table.hpp:51-55 | COVERED | keys(t)/values(t) functions | new Table/Set unordered — iteration/key order unspecified |

### Array

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| zeros(×2), ones(×2) | builtins.cpp:253-256 | COVERED | lib/array.cpp:84-99 | zeros also served as `Array(...)` ctor |
| min, max | builtins.cpp:257-258 | COVERED | lib/array.cpp:118/130 | old empty-array results were garbage sentinels (DBL_MAX / numeric_limits::min) — new raises |
| clear (ref) | builtins.cpp:259 | COVERED | lib/array.cpp:144 | zero-fill, CoW-correct both |
| (.nrow/.ncol/.ndim fields) | func_array.hpp:32-50 | COVERED | nrow/ncol/ndim/sum/mean functions | field → function form (M4 fix) |

### Regex (old stateful → new stateless, S2)

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| match(re, s[, from]) | builtins.cpp:282-283 | COVERED | match → Match-or-null | return change Boolean → Match?; `from` 1-based grapheme both |
| has_match | builtins.cpp:284 | COVERED | test `match(...) != null` | subsumed |
| count | builtins.cpp:285 | COVERED | group_count(Match) | **off-by-one: new INCLUDES group 0** (old excluded it); takes the Match, not the Regex |
| group, get_start, get_end | builtins.cpp:286-288 | COVERED | group/group_start/group_end(Match, n) | renamed; new returns null for a non-participating group; adds groups(m), pattern(re) |
| (Regex ctor) | builtins.cpp:279-280 | COVERED | regex(pattern[, flags]) factory | |

### Set

All 8 Set natives (contains/insert/remove/is_empty/clear/intersect/unite/
subtract, builtins.cpp:291-298) are **ENGINE GAP**: the new engine has the Set
class (value type, structural equality, len) but ships no set library.
**Unused by std/ and test/**; old Set was ordered (std::set), new is unordered.
Defer until Set gets script-level construction.

### System / filesystem

| name | old sites | bucket | new-engine mapping | notes |
|---|---|---|---|---|
| get_user/current/temp_directory, get_script_path, set_current_directory, get_path_separator, get_os_name, get_full_path, join_path, get_temp_name, get_base_name, get_directory, create_directory, remove_directory(×2), remove_file, remove_path, list_directory(×2), exists, is_document, is_directory, rename, split_extension, get_extension(×2), strip_extension, genericize, nativize | builtins.cpp:304-335 | COVERED | lib/system.cpp | vendored Unicode path layer; new raises on failure where old was silent |
| get_error_line, get_error_trace | builtins.cpp:307-308 | ENGINE GAP (superseded) | Error fields `message`/`trace`/`frames` (M5 fix; same {function, line, file} shape) | used by test_gui_trace/test_error_trace/test_nested_error_lines (already ported to e.trace/e.frames in step 3) — migrate any remaining GUI use, then drop |
| clear_directory | builtins.cpp:328 | ENGINE GAP | none | unused — drop or one-liner |

### GLOB class injections (runtime.cpp:209-225) — 15 × INJECTED VALUE

Old engine injects each builtin Class object as a global (enables
`type(x) == String` and constructor calls). New engine: builtin class names
resolve at **compile time only** (`class_by_name`) for `is`/annotations; they
are not runtime values and not script-constructible. Constructor migration:
`String(x)` → to_string, `Integer/Float/Number(s)` → to_int/to_float,
`Regex(p)` → regex(p), `Array(...)` → zeros/ones, `File(p)` → open_file,
`Module(name)` → **no equivalent** (record-builder uses in
test/statistics/lib/{assert,bayes_assert}.phon and
test_compound_assignment.phon need a Table/class idiom), `Table()`/`List()` →
literals, `Set()` → none (unused).

## Sound + Spectrum (phon/application/sound.cpp — 32 sites; spectrum.cpp — 2): all FUNCTION PORT

No ref params in these files. Old overload sets map 1:1 onto multiple
dispatch. `channel` args everywhere are 1-based domain values (0 =
whole/averaged) passed straight to the DSP — no script-index conversion.

| name | old site | old signature | new-engine mapping | notes |
|---|---|---|---|---|
| get_pitch | sound.cpp:1065-1066 | (Sound, ch, time[, Table opts]) → Number | `[](Sound &s, intptr_t ch, double t) -> double` (+ Table overload) | parse_pitch_options (sound.cpp:733) is key-based option validation — stays manual; Settings-backed defaults |
| get_mean_pitch | sound.cpp:1067-1068 | (Sound, ch, t1, t2[, opts]) → Number | + `const Table&` overload | opts variant allows extra time_step key |
| get_formants | sound.cpp:1069-1070 | (Sound, ch, time[, opts]) → Array | `[](Sound &s, intptr_t ch, double t) -> NumArray` | matrix return; parse_formant_options (sound.cpp:776); sound.open() side effect |
| get_intensity | sound.cpp:1071 | (Sound, ch, time) → Number | `[](Sound &s, intptr_t ch, double t) -> double` | validates 0 ≤ time ≤ duration |
| get_mean_intensity | sound.cpp:1072 | (Sound, ch, t1, t2) → Number | `[](Sound &s, intptr_t ch, double t1, double t2) -> double` | LATENT BUG: error message at sound.cpp:653 formats `time` (the libc symbol) instead of t1/t2 — fix at port |
| get_voice_report | sound.cpp:1073-1074 | (Sound, ch, t1, t2[, opts]) → Table | → Table (15 keys, pack_voice_report sound.cpp:663) | opts: f0_min/f0_max (inline 75/600 defaults) |
| hertz_to_bark / bark_to_hertz / hertz_to_erb / erb_to_hertz / hertz_to_mel / mel_to_hertz | sound.cpp:1075-1086 | (Number)→Number and (Array)→Array pairs | `[](double) -> double` + `[](const NumArray&) -> NumArray` | pure speech-scale math |
| hertz_to_semitones / semitones_to_hertz | sound.cpp:1087-1094 | ×4 each: (Number[, ref]) / (Array[, ref]) | 2×2 typed overloads | optional ref-frequency arg = overload pair, exactly what dispatch handles |
| convert | sound.cpp:1095-1096 | (Sound, path, format[, samplerate]) → null | `[](Sound&, const String&, const String&[, intptr_t]) -> void` | old hand-validated "positive integer" via floor check (sound.cpp:1052) — typed intptr_t replaces it |
| get_spectrum | spectrum.cpp:620 | (Sound, ch, t1, t2) → Spectrum | `[](Handle<Sound>, intptr_t, double, double) -> Handle<Spectrum>` | Spectrum ctor STORES Handle<Sound> → take Handle, not Sound& |
| get_spectral_moments | spectrum.cpp:643 | (Sound, ch, time, window, minf, maxf) → Table | `[](Handle<Sound>, intptr_t, double, double, double, double) -> Table` | {cog, spread, skewness, kurtosis}; Gaussian window + 50 dB floor hard-coded |

Field dispatchers (not add_global): `sound_get_field` (sound.cpp:617, wired
:1063) — path/duration/nchannel/sample_rate; `spectrum_get_field`
(spectrum.cpp:566, wired :607) — 11 fields. Both → property mechanism (gap G1).

## Annotation (phon/application/annotation.cpp — 34 sites): all FUNCTION PORT

Layers/events cross the boundary only as 1-based indices + times — no
layer/event classes needed. Nearly every native starts with `a.open()`
(lazy load from disk) and converts layer/event indices via
`index_from_script` (both directions in get_event_index, the file's only
outgoing `index_to_script`).

| name | old site | old signature | new-engine mapping | notes |
|---|---|---|---|---|
| bind_to_sound | annotation.cpp:593 | (Annotation, String path) → null | `[](Annotation&, const String&)` | imports path into Project, then set_sound — Project side effect |
| get_event_start / get_event_end / get_event_text | annotation.cpp:594-596 | (Annotation, layer, event) → Number/String | `[](Annotation&, intptr_t, intptr_t) -> double/String` | both indices converted |
| set_event_text | annotation.cpp:597 | (Annotation, layer, event, String) → null | + `const String&` | mutates, sets modified flag |
| get_event_count | annotation.cpp:598 | (Annotation, layer) → Integer | `-> intptr_t` | |
| get_event_index | annotation.cpp:599 | (Annotation, layer, time) → Integer | `-> intptr_t` | converts the RETURN via index_to_script |
| get_layer_count | annotation.cpp:600 | (Annotation) → Integer | | |
| get_layer_label / set_layer_label | annotation.cpp:601-602 | (Annotation, layer[, String]) | | |
| add_interval / add_instant / remove_interval / remove_events | annotation.cpp:603-606 | (Annotation, layer, times/text) → null | | remove_interval identifies by time bounds, not index |
| create_layer / remove_layer / clear_layer / duplicate_layer / layer_has_instants | annotation.cpp:607-611 | (Annotation, index, …) | | create/duplicate use allow_end=true |
| save | annotation.cpp:612 | (Annotation) → null | `[](Annotation &a) { a.write(); }` | generic name — dispatch by class disambiguates |
| write_as_native / write_as_textgrid | annotation.cpp:613-616 | (Annotation[, path]) → null | arity-overload pairs → 2 registrations each | |
| new_annotation | annotation.cpp:619 | () → Annotation | `[]() -> Handle<Annotation> { return Handle<Annotation>::make(); }` | factory = required construction path (classes not script-constructible) |
| duplicate_annotation | annotation.cpp:622 | (Annotation, out_path) → Annotation | `-> Handle<Annotation>` | result NOT added to project |
| extract_layers | annotation.cpp:624 | (Annotation, List indices, out_path) → Annotation | per-element index_from_script stays manual (List elems are Variant) | |
| merge_annotations | annotation.cpp:626 | (Annotation, List others, out_path) → Annotation | per-element Handle<Annotation> extraction manual | |
| extract_annotation_slice | annotation.cpp:628-630 | (Annotation, t1, t2[, clip], out_path) → Annotation | arity-distinguished overload pair | |
| concatenate_annotations | annotation.cpp:632-634 | (List[, List durations], out_path) → Annotation | | |
| extract_sound_slice | annotation.cpp:636 | (Sound, t1, t2, out_path) → Sound | `[](Sound&, double, double, const String&) -> Handle<Sound>` | Sound natives registered HERE, not sound.cpp |
| concatenate_sounds | annotation.cpp:638 | (List sounds, out_path) → Sound | | |

Field dispatcher: `annot_get_field` (annotation.cpp:159, wired :592) —
path/sound/nlayer; returns Handle<Sound> → gap G1.

## DataTable + statistics (phon/application/data_table.cpp — 50 sites): all FUNCTION PORT

All in `DataTable::initialize` (line 970), block 2545-2625. Classes it
dispatches on are registered in project.cpp (below). The two `append` sites
carry the file's only ref-mask (`REF("01")`) — vestigial, since DataTable is a
reference class and mutates in place without write-back.

| name | old site | old signature | new-engine mapping | notes |
|---|---|---|---|---|
| fit ×6 | data_table.cpp:2547-2552 | (String formula, DataTable[, String family][, Table opts \| PriorSpec priors]) → Model | six typed registrations, `Isolate&` lead (rt.printf, errors); 3rd-arg classes disjoint → clean dispatch | callbacks: fit2:972, fit3:984, fit_opts2:1041, fit_opts3:1056, fit_bayes2:2495, fit_bayes3:2531; parse_fit_options:1013 |
| filter ×2 | data_table.cpp:2553-2554 | (DataTable, String expr[, label]) → Dataset\|Concordance | split into Dataset&/Concordance& overloads (filter_rows:893 branches on is<Dataset>()/is<Concordance>(), :950-962) | calls Project::updated() |
| summarize | data_table.cpp:2556 | (Model) → null | `[](Isolate&, const stats::Model&) -> void` | print_model_summary:341, rt.printf → output hook (gap G4) |
| get_coef | data_table.cpp:2557 | (Model) → Array | `[](const stats::Model &m) -> NumArray { return m.beta; }` | |
| compare | data_table.cpp:2558 | (Model, Model) → null | + Isolate& | ANOVA / WAIC / Bayes-factor printout |
| evaluate ×2 | data_table.cpp:2559-2560 | (Model[, Table opts]) → Table | | diagnostic table; opts per-key casts stay manual |
| polish / try_phase2 | data_table.cpp:2561-2562 | (Model) → Table | | Student-t refits; need the in-session model (Z design not serialized) |
| predict ×3 | data_table.cpp:2564-2566 | (Model[, Dataset[, Table opts]]) → Dataset | | registers result into the Project tree (:1878-1884) — headless must stub |
| get_cell / set_cell / get_header | data_table.cpp:2569-2571 | (DataTable, row[, col][, value]) | `[](DataTable&, intptr_t, …)` | dim_index_from_script (1-based + negative) |
| get_column ×3 | data_table.cpp:2572-2574 | (Dataset, col) / (DataTable, name) / (Concordance, col) | three dispatch overloads returning Variant | POLYMORPHIC RETURN: NumArray if numeric else List of String |
| get_column_type | data_table.cpp:2575 | (Dataset, col) → String | | "numeric"/"text"/"boolean" |
| append ×2 | data_table.cpp:2576-2577 | (ref DataTable, List\|Array values, String name) → null | `[](DataTable&, const List&\|const NumArray&, const String&)` | joins the stdlib append(list/string) dispatch family; branches on is<Dataset>() internally |
| to_csv ×2 | data_table.cpp:2580-2581 | (DataTable, path[, sep]) → null | | |
| emmeans ×2 / emtrends ×2 | data_table.cpp:2584-2587 | (Model, factor[, variable][, adjustment]) → null | + Isolate& | printed EMM/trend tables |
| mean/std/sum ×2 each, vrc | data_table.cpp:2590-2596 | (Array[, dim]) → Number/Array | 1-arg mean/sum: **likely COVERED** by the new stdlib's reductions (decide at cutover, verify NaN semantics); dim'd forms + std + vrc are ports | dim 1 = columns, 2 = rows |
| dharma | data_table.cpp:2599 | (Model) → null | + Isolate& | NAME MISMATCH: function_declarations.hpp:692 documents it as `test_residuals` — reconcile at port |
| set_fixed ×2 | data_table.cpp:2605-2606 | (PriorSpec[, name], mean, sd) → null | `[](stats::PriorSpec&, …)` — mutates in place | 2nd-arg String-vs-Number overload = clean dispatch |
| set_variance ×2 / set_residual ×2 | data_table.cpp:2607-2610 | (PriorSpec, type, p1[, p2]) → null | + Isolate& (parse_variance_type throws) | type ∈ {pc, half_cauchy, half_normal} |
| set_negbin_theta / set_beta_phi / set_lkj | data_table.cpp:2611-2613 | (PriorSpec, …) → null | | |

Not add_global, same surface: `Prior()` constructor (add_constructor,
data_table.cpp:2603) → **factory function** `add_function("Prior", …)` (new
classes aren't script-constructible); field dispatchers `model_get_field`
(:2616, def 1080 — ~60 keys incl. **loglik**, aic, bic, se, p, converged,
fitted, residuals, waic/loo families, posterior_*, hyper_*, ranef_*,
smooth_*), `dataset_get_field` (:2619) and `conc_get_field` (:2622) → gap G1.

## Project / VFS / Settings (project.cpp 9 + vfs.cpp 3 + settings.cpp 5): all FUNCTION PORT

| name | old site | old signature | new-engine mapping | notes |
|---|---|---|---|---|
| get_annotations / get_sounds / get_concordances / get_datasets | project.cpp:1478/1480/1482/1484 | () → List of Handle | `[](Isolate&) -> List` | lists of app-class handles |
| get_annotation / get_sound / get_concordance / get_dataset | project.cpp:1479/1481/1483/1485 | (String path) → Handle \| null | `[](const String&) -> Variant` | nullable returns |
| load | project.cpp:1505 | (String path) → Document | `[](const String&) -> Handle<Document>` (impl func_document.hpp:74) | polymorphic base return; imports into project if absent; **the 4b `load`** (CSV → Dataset among others) |
| add_property | vfs.cpp:704 | (Document, String category, Object value) → null | THREE overloads: (Document&, const String&, const String&/bool/double) | old body manually branches on args[2] type — replaced entirely by dispatch; Document param must accept subclass handles |
| remove_property | vfs.cpp:705 | (Document, String) → null | `[](Document&, const String&)` | fires file_modified() |
| get_property | vfs.cpp:706 | (Document, String) → String\|Real\|Boolean\|null | `-> Variant` | polymorphic return |
| get_settings_directory / get_metadata_directory / get_plugin_directory / get_script_directory / get_config_path | settings.cpp:73-77 | () → String | `[] { return Settings::…(); }` | pure path queries; used by std/initialize.phon + write_settings.phon (already stubbed in step 3) |

Plus the `phon.project` submodule (project.cpp:1487-1500): 9 member functions
(open, close, add_folder, add_file, refresh, has_path, save ×2, is_empty) —
FUNCTION PORTs whose *placement* depends on the namespace decision (gap G3;
project.cpp:1488 already carries "FIXME: DO we put this in phon or global?").
Field dispatcher: `document_get_field` (project.cpp:1504, impl
func_document.hpp:48-66) — path/label/length (.length delegates to
DataTable::row_count) → gap G1.

## GUI shell (main_window.cpp 26 + console.cpp 1): all FUNCTION PORT

All capture `MainWindow*` (except launch_browser) and must run on the Qt main
thread. No ref params.

| name | old site | old signature | new-engine mapping | notes |
|---|---|---|---|---|
| view_text | main_window.cpp:3293 | (path, title) → null | `[this](const String&, const String&)` | modal read-only text dialog |
| warning ×2 / alert ×2 / info ×2 / ask ×2 | main_window.cpp:3294-3301 | (msg[, title]) → null / Boolean | arity-overload pairs → 2 registrations each | QMessageBox family |
| open_file_dialog / open_files_dialog / open_directory_dialog / save_file_dialog | main_window.cpp:3302-3305 | (prompt) → String\|List\|null | `-> Variant` (null on cancel) | |
| get_input | main_window.cpp:3306 | (label, title, default) → String\|null | | |
| get_plugin_version / get_plugin_resource | main_window.cpp:3307-3308 | (plugin[, name]) → String\|null | | resource = pure path join, could be MainWindow-free |
| create_dialog ×2 | main_window.cpp:3309-3310 | (String json \| Table spec) → Table\|null | String/Table split = dispatch pair | UserDialog; button callbacks are SOURCE STRINGS run via do_string (user_dialog.cpp:270) |
| create_progress_dialog / update_progress_dialog | main_window.cpp:3311-3312 | (msg, title, count) / (value) | update returns true/false/null → Variant | |
| launch_browser | main_window.cpp:3313 | (url) → null | no capture | |
| get_current_sound / get_current_annotation | main_window.cpp:3314/3316 | () → Handle \| null | `[this]() -> Variant` | the current-view state accessors (std/speech_analysis.phon) |
| get_visible_channels | main_window.cpp:3315 | () → List of Integer | | |
| get_window_duration / get_selection_duration | main_window.cpp:3317-3318 | () → Real | | |
| phon.get_version / phon.get_date / phon.get_supported_sound_formats / phon.close_current_view | main_window.cpp:3322-3325 | () → String/List/null | members of the injected `phon` namespace (gap G3) | first three are pure; installed via `cast<Module>(m_runtime["phon"]).define(…)` (:3321) |
| clear | console.cpp:122 | () → null | `[](Isolate&)` via an output hook | clears active output through `rt.clear_output` (Console/OutputPanel swap); 0-arg overload coexists with stdlib clear(List/Table/Array/Set) — dispatch handles it; needs a clear_output seam (gap G4) |

## Injected values (bucket 4)

| name | injection site | holds | scripts mutate? | new-engine mapping |
|---|---|---|---|---|
| phon | phonometrica.cpp:96 (`rt["phon"] = make_handle<Module>(…)`) | namespace: settings (Table), project (Module), 4 functions | YES (field assignment) | `rt.add_global("phon", …)` — needs function-valued members + field assignment (gap G3) |
| phon.settings | settings.cpp:365 (`phon["settings"] = std::move(result)`) or script assignment | Table: ~15 scalar/list prefs + 8 nested tables (statistics, concordance, sound_plots, waveform, pitch_tracking, intensity, spectrogram, formants) | **YES** — std/read_settings.phon:26 and every saved config do whole-table `phon.settings = {...}`; C++ Settings::set_value (settings.cpp:218-307) mutates the SAME live Table | shared mutable Table behind the injected global; both whole-table replacement and per-key mutation must work from script AND C++ |
| phon.project | project.cpp:1489/1500 | Module of 9 functions, no data | no | function-valued table field |
| E, PI (+ dead SQRT2, PHI) | builtins.cpp:138-141 | math constants | no | register_constant (already done for E/PI) |
| 15 builtin Class objects | runtime.cpp:209-225 (GLOB) | class values | no | compile-time names; not runtime values (see GLOB section) |

## Field-accessor surface (cross-cutting → gap G1)

7 string-keyed `get_field` dispatchers serve `obj.field` on app classes:

| class | dispatcher | fields |
|---|---|---|
| Annotation | annotation.cpp:159/592 | path, sound (→ Handle<Sound>), nlayer |
| Sound | sound.cpp:617/1063 | path, duration, nchannel, sample_rate |
| Spectrum | spectrum.cpp:566/607 | 11 fields (path, bin_count, sample_rate, bandwidth, max_frequency, start_time, end_time, peak_dB, floor_dB, lpc_order, has_lpc) |
| Document | project.cpp:1504 (func_document.hpp:48-66) | path, label, length |
| Model | data_table.cpp:1080/2616 | ~60 keys (incl. loglik — the "logLik" API) |
| Dataset | data_table.cpp:1271/2619 | path, label, description, nrow/length, ncol, empty, headers |
| Concordance | data_table.cpp:1285/2622 | Dataset keys + target_count |

The new engine routes field access through get/set accessors for *script*
classes; registered foreign classes need an equivalent property-registration
hook (or these become getter *functions*, changing script syntax —
`model.loglik` → `loglik(model)`). Decision required before 4b, since the
statistics suite reads model fields pervasively.

## Event / signal hook surface (how C++ calls into scripts)

Storage and dispatch live in **script** (std/signal.phon — already ported in
step 3); C++ only invokes the script-side `emit`:

- `Project::emit_signal` (project.cpp:1610-1623): looks up the global script
  function `emit` (`rt[emit_name]`), pushes a String event name + optional
  Variant payload (moved-in Handle<Annotation>/Handle<Sound>), calls
  `rt.call(narg)` (runtime.hpp:316). Slots are script closures never held by
  C++.
- Event constants (C++ side project.cpp:48-55, script side
  std/signal.phon:94-100): PROJECT_LOADED (fired project.cpp:223, no payload),
  ANNOTATION_LOADED (:288/:916, Annotation), SOUND_LOADED (:294/:928, Sound),
  ANNOTATION_IMPORTED (:913/:1106, Annotation), SOUND_IMPORTED (:925/:1109,
  Sound); SCRIPT_LOADED/DATASET_LOADED are declared but their fire sites are
  commented out (project.cpp:1044/:1017).
- All other C++→script entry is `do_string`/`do_file`: console REPL
  (console.cpp:264/289), script editor (script_view.cpp:293), startup scripts
  (main_window.cpp:3604), plugins (plugin.cpp:36/48/68 —
  `description.phon`'s *return value* is data-as-script), protocols
  (protocol.cpp:39/50), settings (settings.cpp:343-371 via the run_script
  macro, definitions.hpp:50-52), user-dialog button callbacks as source
  strings (user_dialog.cpp:270).

New-engine equivalent: do_string/do_file/add_global/request_interrupt all
exist. **Gap (G2): no public call-a-function-value API** — `vm_call` is
internal to the interpreter; `emit_signal` needs a small public
`Runtime::call(const Variant &fn, args…)` wrapper (Variant↔Value conversion).
Named functions being first-class means `emit` can be fetched as a Variant and
called — same shape as today. Recorded limitation (engine DEVIATIONS M7 item
13): keyword options and `ref` promotion do not flow through indirect calls —
emit's single positional payload is unaffected, but slots must not declare
keyword/ref params.

## Class-port list, ranked by invasiveness

Old registration: all via `rt.add_standard_type<T>(name, base)` in
`Project::preinitialize` (project.cpp:1892-1913). Ownership: every
script-facing object already lives in old-engine cells, and **all** C++ owners
hold old-engine `Handle<T>`s (Project::m_files, Directory ElementLists, GUI
view members) — Qt never owns a script-facing document, and there are no
shared_ptr typedefs for script types (only non-script AutoPlugin/AutoProtocol/
AutoMetaConstraint). So the port is a wholesale handle swap, not shared_ptr
wrapping.

| rank | type(s) | script name | defined at | old kind | invasiveness | port strategy |
|---|---|---|---|---|---|---|
| 1 | stats::PriorSpec | Prior | analysis/prior.hpp:315 | plain struct, boundary-boxed | **low** | add_class + Handle::make; replace script ctor with `Prior()` factory function |
| 2 | stats::Model | Model | analysis/model.hpp:92 | plain struct, boundary-boxed (Analysis stores plain copies, GUI takes const&) | **low** | add_class<stats::Model>("Model", nullptr, Reference) + Handle::make(std::move(model)); port model_get_field |
| 3 | Script, Note, Query (+5 subclasses), Bookmark, TimeStamp, Analysis | — | script.hpp:49, conc/*.hpp, bookmark.hpp, analysis.hpp:35 | Document/Element subclasses, name-only registrations (project.cpp:1901-1913) | low each | register names for `is` checks; no natives |
| 4 | Spectrum | Spectrum | spectrum.hpp:46 | final : Document, is_clonable=false | medium | boundary-created, few aliases; waits on base layer |
| 5 | Concordance, Dataset | Concordance, Dataset | conc/concordance.hpp:36, dataset.hpp:30 | : DataTable, is_clonable=false | medium-high | GUI-shared identity; Project temp-list ownership; copy ctors stay C++-only |
| 6 | Sound, Annotation | Sound, Annotation | sound.hpp:48, annotation.hpp:34 | final : Document, is_clonable=false | high | largest native surfaces; Annotation::m_sound cross-ref; event payloads; GUI aliases |
| 7 | Element, Directory, Document, DataTable | — | vfs.hpp:54/108/188, data_table.hpp:45 | Atomic-based cell types with Class* ctor params | **high — the choke point** | drop the Atomic base + Class* ctor params (new Cell header carries the class); switch every Handle<T> container in Project/GUI to new-engine handles; needs an upcast-aware Handle<Derived>→Handle<Base> conversion (sound under single non-virtual inheritance — payload address identical, box_value_offset alignment-only — but must be an explicit, asserted API: gap G5) |

All classes are **ClassKind::Reference** (identity semantics; every registered
type has is_clonable=false or is mutated in place through its handle).

## Summary

**Bucket counts** (351 add_global sites + satellites):

| bucket | count | where |
|---|---|---|
| COVERED | 149 | all in builtins.cpp (plus 1-arg mean/sum in data_table.cpp pending a semantics check) |
| ENGINE GAP (old builtin, no new equivalent) | 36 | builtins.cpp; only 4 gaps have live users — type(), get_error_line/get_error_trace (superseded by Error.frames), the function-form import() in test/statistics, Module-as-record in the statistics libs. The other ~32 are unused → drop |
| FUNCTION PORT | 176 | 162 app add_global sites (data_table 50, annotation 34, sound 32, main_window 26, project 9, settings 5, vfs 3, spectrum 2, console 1) + 13 module-member functions + Prior() ctor→factory |
| INJECTED VALUE | 22 | phon / phon.settings / phon.project, E/PI (+dead SQRT2/PHI), 15 GLOB class objects (become compile-time names) |
| FIELD DISPATCHERS | 7 | Annotation, Sound, Spectrum, Document, Model, Dataset, Concordance → need gap G1 |

**Step-4b starter set** (headless statistics; everything lives in
data_table.cpp + project.cpp):

1. Classes: DataTable + Dataset (+ Concordance for filter), stats::Model,
   stats::PriorSpec — all rank-1/2/5 ports; Dataset/DataTable drag in the
   Document base (rank 7) *unless* 4b stubs a minimal DataTable without the
   VFS parentage (decision for the cutover branch).
2. `load` (project.cpp:1505) — CSV → Dataset; needs a Project stand-in or a
   headless load path.
3. `fit` ×6 (data_table.cpp:2547-2552) + `Prior()` factory +
   set_fixed/set_variance/set_residual/set_lkj/set_negbin_theta/set_beta_phi
   for the Bayesian suite.
4. `get_coef` (2557), `summarize` (2556; needs output hook G4), `compare`
   (2558).
5. **Model field access** (model_get_field — the loglik/aic/bic/se/p surface)
   → blocked on gap G1.
6. Verification accessors: get_cell/get_column ×3/get_header/get_column_type.
7. Already COVERED by the new engine: get_script_path, read_file, from_json,
   to_string — the harness (test/statistics/lib/assert.phon) additionally
   needs its `type()`/`Module(...)`/`import(path)` idioms rewritten (step-3
   survey, L10).

**New-engine gap list from this survey** (→ ~/Devel/engine work items, in
addition to the still-open S6/L5 items from step 3):

- **G1 — foreign-class field access.** A property/getter registration for
  `add_class` types (obj.field routing to C++), or an explicit decision to
  move scripts to accessor functions. Blocks 4b (model.loglik) and every
  `.path`/`.duration`-style read. 7 dispatchers, ~90 fields total.
- **G2 — public function-value call API.** `Runtime::call(const Variant &fn,
  args…)` over the internal vm_call, for Project::emit_signal (and any future
  C++→script callback). Indirect-call limits (no keyword/ref flow) accepted.
- **G3 — namespace values.** The `phon` object: a Table (or namespace value)
  injected via add_global whose members include callable functions
  (phon.project.save(), phon.get_version()) and a script-assignable field
  (phon.settings = {...}). Needs: function values as table members callable
  via field syntax, and field assignment on an injected global. Decide
  Module-replacement shape before the GUI port.
- **G4 — output/console hooks.** The old Runtime carries printf/clear_output
  seams the GUI console swaps (console.cpp:122, rt.printf throughout the
  statistics printers). The new Runtime needs equivalent redirectable
  print/clear hooks (print currently goes to stdout).
- **G5 — polymorphic handle upcast.** Handle<Derived>→Handle<Base> for boxed
  foreign classes (Array<Handle<Element>> containers, Document-typed natives
  accepting Annotation/Sound handles, the polymorphic `load` return). Payload
  layout already permits it under single non-virtual inheritance; needs the
  explicit conversion + dispatch acceptance of derived cells for base-typed
  parameters.
- Minor (unused, drop-or-oneliner): SQRT2/PHI constants, Set natives, string
  find_back/insert/remove_*/replace_* variants, elementwise
  acos/asin/atan/log10/log2/round/tan on Array, clear_directory,
  get(Table, key[, default]), type() (or add type_name()).

**Semantic-diff watchlist for COVERED natives** (script-visible changes to
re-check when the app's shipped scripts run on the new engine): 3-arg
`slice(s, from, count→to)`; `group_count` includes group 0; `len` narrowed to
List/String/Table/Set; list `intersect`/`unite`/`subtract` no longer assume
sorted inputs; `sorted_find` returns 0 for absent (S1); old `sorted_insert`
deduplicated; `load_json` evaluated script text (from_json parses); Table/Set
iteration order now unspecified; empty-Array min/max now raise; `read(File)`
position semantics to verify.

# Step 4b — headless statistics target on the new engine (2026-07-19)

*The step-4a starter set is implemented and validated: the analysis layer now
compiles and runs against the NEW engine in a headless host (`phon_stats`),
and the statistics suite — ported to the new language — reproduces the
phase-1 oracle. App-side work is commit 9c5163a on this worktree branch
(`claude/angry-hertz-377cb6`); engine-side work is uncommitted in ~/Devel/engine
(DEVIATIONS.md items 14–16).*

```
cmake -S . -B build -DPHON_ENGINE_DIR=~/Devel/engine -DCMAKE_BUILD_TYPE=Release
cmake --build build --target phon_stats
build/stats_host/phon_stats -I test/statistics/lib test/statistics/frequentist/run_all.phon
build/stats_host/phon_stats -I test/statistics/lib test/statistics/bayesian/test_<family>.phon
```

## Results

- **Frequentist suite: 552 passed, 0 failed** — the exact phase-1 oracle
  (lm/glmmTMB references), now produced by the new engine end to end.
- **Bayesian suite: 7 of 8 families green** (gaussian, poisson, binomial,
  negbin, beta, negbin_owls, real_schwa). `test_student` reports 10
  `nu(student)` hyper-parameter failures that are **value-for-value identical
  to the old engine at HEAD** (verified by running the old binary on the
  old-language suite: e.g. M1 hyper.mean 19.677134339 on both) — the
  documented Laplace-vs-HMC ν fundamentals, not a port regression. (The
  phase-1 note said "two hyper-SD checks"; the count at current HEAD is 10 on
  both engines — the drift happened in the analysis layer between phase 1 and
  now, not in this port.)
- Engine unit suite: 396 cases green under normal/ASan/TSan after the
  engine-side additions.

## Engine-side work (owner-selected design for G1; ~/Devel/engine, DEVIATIONS 14–16)

- **G1 CLOSED — foreign-class read-only fields.** `rt.add_field<T>(name,
  getter)`: typed getters installed as `FieldInfo` entries on the registered
  class; GETFIELD routes foreign cells through the getter by base-chain lookup
  (a foreign subclass inherits fields by lookup, not layout); SETFIELD raises
  read-only; duplicate registration throws. `model.loglik`, `d.nrow` etc. work
  exactly as on the old engine.
- `String::is_letter` (ID_Start-based) for the formula tokenizer's non-ASCII
  identifier path.
- Recorded findings (DEVIATIONS item 16): per-generic **ref-mask uniformity**
  rejects the old `append(ref DataTable, …)` overload family (G6 below);
  a registered **class name shadows a same-named factory generic** at compile
  time; **RuntimeError is not a std::exception** (an embedder's catch-all
  misses it — a missed catch turns an uncaught script error into terminate).

## App-side structure (all on the worktree branch)

- **`stats_host/`** — the headless host:
  - `shim/phon/application/data_table.hpp` + `data_table.cpp`: a minimal
    tab-separated table (get_cell/row_count/column_count/get_header, raw
    string cells, `.num/.bool/.text` header suffixes stripped, UTF-8 BOM
    stripped — several shipped CSVs carry one) that SHADOWS the real
    data_table.hpp in this build only, so `stats::fit` compiles unmodified.
  - `shim/phon/error.hpp` and `shim/phon/array.hpp`: the old `error(...)`
    formatted-throw helper over the engine String, and the old transitive
    include shape (analysis TUs get `<vector>`/error() via array.hpp);
    a stub `traits::maybe_cyclic` lets the old-engine GC-trait
    specializations in model.hpp/prior.hpp compile as dead metadata.
  - `bindings.cpp`: `add_class` DataTable/Dataset/Model/**PriorSpec** (script
    name — see G6b), `fit` ×6, `Prior()` factory, the priors-setter family,
    `load`, `get_cell`/`get_header`/`get_column` ×2/`add_column` ×2,
    `get_coef`/`summarize`/`compare`, ~60 Model field getters + 6 table
    fields via `add_field`. Convention: **every native that can throw takes a
    leading `Isolate&` and converts `std::exception` via `iso.raise`** (the
    engine only auto-converts RuntimeError/ScriptError).
  - `printers.cpp`: print_model_summary + the compare printer ported verbatim
    from data_table.cpp with `rt.printf` → `std::printf` (moves onto the
    engine output hook, gap G4, at full cutover).
  - `main.cpp`: `phon_stats [-I dir]... script.phon`; catches RuntimeError
    explicitly.
- **`Vector` → `ColVector`** in phon/utils/matrix.hpp and all users (analysis
  + analysis_view): the engine's core container takes the
  `phonometrica::Vector` name at cutover — same policy as Match → QueryMatch.
  Old build verified green after the rename (phon-app, phon-gui, phonometrica).
- **Include-order contract** (stats_host/CMakeLists.txt): shim → engine root →
  repo root → phon/third_party; the old engine is never on this target's
  include path or link line.

## Suite port (test/statistics, new language)

- `lib/assert.phon` → **`lib/stat_assert.phon`** (an imported module named
  `assert` would shadow the builtin `assert()` in every importing chunk).
  Module-value idioms → module vars + global functions; counters read as
  `stat_assert.n_failed`, mutated only via record_pass/record_fail;
  `type(x)==type([])` → `x is List`; 3-arg slice count→end fixed at each site;
  `load_json` → read_file+from_json; `str(e)` → `e.message`.
- `lib/bayes_assert.phon`: imports stat_assert; the old cross-module counter
  writes (`A.n_passed += 1`) became record_pass()/record_fail(msg).
- 12 frequentist + 8 bayesian test files: mechanical (`let`→`var`, strip
  `A.`/`B.` prefixes, print → print()); `run_all.phon` rewritten for L10
  (static imports; banner and tally AFTER the imports, since module top-levels
  run first; no reset_counters — it would zero the tally the imports just
  accumulated). Bayesian tests import BOTH libs: **compile-time generic
  visibility is not transitive through imports**.
- `append(d, col, name)` → **`add_column(d, col, name)`** in the two
  real_schwa tests (G6a). `fit(..., fit_method="REML")` named-arg calls →
  explicit `{"fit_method": "REML"}` option tables (test_reml_gaussian).

## New gaps found by 4b (→ cutover/engine follow-ups, joining the step-4a G-list)

- **G6a — per-generic ref-mask uniformity vs app overload sets.** The old
  engine mixed `append(ref List, item)` with `append(ref DataTable, …)`;
  the new dispatch rejects the mix (identity-class refs have mask 0). Either
  such natives keep new names (add_column) at cutover, or the engine moves to
  per-method ref-masks.
- **G6b — old `add_constructor` natives.** A registered class name resolves
  at compile time before generics, so a factory can't share the class's name:
  the idiom is class "PriorSpec" + factory `Prior()`. Applies to every
  script-constructible old class at cutover.
- **G6c — RuntimeError ⊄ std::exception**: embedders must catch it explicitly
  (documented in DEVIATIONS; consider deriving from std::exception).
- Data files: the real Dataset reads through File (BOM/encoding sniffing) —
  any future table stand-in must strip BOMs (three shipped CSVs have one).

Still open from 4a: G2 (public Runtime::call for emit_signal), G3 (the `phon`
namespace value), G4 (output hooks — summarize/compare print via std::printf
in the headless host), G5 (polymorphic handle upcast — partially exercised:
Dataset cells dispatch into DataTable& parameters and inherit its fields).

# Step 5 — VFS de-celling (2026-07-19, roadmap A0)

*Element, Directory, Document, DataTable and every subclass (Annotation, Sound,
Spectrum, Dataset, Concordance, Script, Note, Query + 5 subclasses, Bookmark,
TimeStamp, Analysis) are now plain C++ classes: no `Atomic` base, no `Class *`
constructor parameters. The app still builds and runs on the OLD engine only.
Work is on branch `claude/exciting-torvalds-dc0ff5` (commits 1155b23 + d26b2cf);
no engine-side (~/Devel/engine) changes were needed.*

## Investigation result: the TObject path cannot host the hierarchy

`TObject<T>` (typed_object.hpp) is `final` and stores `T m_value` by value, so
(a) `TObject<Document>` does not compile for abstract classes, and (b)
`TObject<Sound>` is unrelated to `TObject<Document>` — `Handle<Document>` could
never hold a Sound. Everything *above* the storage layer was already
polymorphism-ready: `cast<T>` accepts subclass cells via the Class graph
(`meta::is_base_of`), dispatch resolves on `Object::get_class()` (the dynamic
class), identity/refcount live in the box header. So the smallest intervention
was a second boxed-storage path in the old engine, not a change to the classes'
semantics.

## Chosen strategy: poly-boxing in the old engine (typed_object.hpp)

Mirrors the new engine's plain-class boxing (DEVIATIONS 26/28) so the A2 flip
is mechanical:

- `PolyObject : Atomic` + `TPolyObject<T> final : PolyObject` with the payload
  at a **fixed offset** (`poly_payload_offset`, header rounded up to
  `alignof(std::max_align_t)`) — same technique as the new engine's
  `box_value_offset`.
- `Handle<T>` for these types is **type-erased**: `object_type = PolyObject`,
  payload recovered by offset arithmetic (`poly_value<T>`). Upcast
  (`operator Handle<Base>`) rewraps the same box; `recast`/`raw_recast`
  unchanged (still *unchecked* static downcasts — parity with the old
  behavior, including the misleading `if (auto q = recast<...>(...))` sites in
  main_window.cpp).
- A payload-pointer constructor (`Handle<T>(T *payload)`, poly-only, via
  `poly_box_of`) covers the raw-pointer sites: `Handle<Element>(this)`
  (vfs.cpp set_parent/detach/move_to), `Handle<Sound>(&sound)` (spectrum.cpp
  natives), `Handle<Query>(query_doc)` (main_window.cpp). Debug builds assert
  the payload really is a box payload (`dynamic_cast<void*>` most-derived
  check); there are no stack-constructed hierarchy objects in the tree.
- Which types are poly-boxed is decided by `traits::hierarchy_root<T>`
  (traits.hpp primary; **all 20 specializations live in one block at the top
  of vfs.hpp**, before the class definitions). They must precede any
  `Handle<T>` instantiation — scattering them after the class bodies fails to
  compile ("specialization after instantiation"), which is also the safety
  net: a class added to the hierarchy but missing from the block breaks the
  build at the first `Handle<Derived>`→`Handle<Base>` conversion.
- Layout contract (asserted in the `TPolyObject` ctor): single, non-virtual
  inheritance throughout the hierarchy; `alignof(T) <=
  alignof(std::max_align_t)` (static_assert). Holds for all 20 classes (no
  Eigen/over-aligned members in the boxed objects).
- `cast<T>` (variant.hpp) and the `VTable<T>` methods (runtime.hpp) gained a
  poly branch (factored as `VTable<T>::payload`); `Variant`'s universal
  constructor now `static_assert`s against boxing a hierarchy value by copy.
- `traits::is_collectable` excludes poly-boxed types (they never hold script
  values). The old `maybe_cyclic` specializations were removed from
  vfs.hpp/data_table.hpp; the ones in the subclass headers are now redundant
  but left in place (A1 deletes them wholesale).

## App-side changes

- Ctors: `Element(Directory*)`, `Document(Directory*, String)`,
  `DataTable(Directory*, String)`, `Bookmark(Directory*[, String])`; every
  subclass drops the `meta::get_class<T>()` argument. Query's two ctors
  collapsed into one (`Query(Directory*, String)`); the box now carries the
  dynamic class, chosen by `make_handle<T>` at the construction site.
  Dataset/Concordance copy ctors no longer pass `other.klass`.
- **Serialization**: `Object::class_name()` was used inside
  to_xml/from_xml (vfs.cpp, bookmark.cpp, annotation.cpp, query.cpp,
  concordance.cpp, project.cpp) — replaced by a plain
  `virtual String Element::class_name()` with per-class overrides returning
  the registered names. Engine-independent, so it **survives the cutover
  unchanged** (A2 must keep registering the same names).
- Behavior note: the old `VTable<T>::clone` for Object-derived types copied
  the whole object *including the Object header* (klass + ref_count) into the
  clone; the poly clone constructs a fresh header (ref_count 1). Strictly a
  fix; not script-reachable today (no `copy` builtin is registered for these
  classes), so no observable change.

## Verification (all on the worktree branch, Debug build, vs a pristine-HEAD
## baseline build of f2e5504)

1. `cmake --build build --target phonometrica` — green (38 files changed).
2. **test/engine**: at HEAD the suite is already new-language; the old binary
   cannot run it (baseline-identical syntax error — NOT a step-5 regression).
   Substitutes, all green:
   - `test_base_migration.phon` (old-engine backup): passes identically on
     baseline and de-celled binaries.
   - **New** `test/engine/test_vfs_decell_old_engine.phon` (old language,
     committed): in-memory Annotation natives + field dispatch, Document- and
     DataTable-typed natives receiving subclasses, `type()` dynamic-class
     checks, shared identity through multiple handles and through a repeated
     `load()`, project lists. Byte-identical output on both binaries.
     (Note: must keep a `.phon` extension — the app dispatches on extension
     and a foreign suffix falls into the GUI path, which aborts at HEAD.)
   - Ported suite via the NEW runner (`phon_repl run_all.phon`): all green.
3. **Project XML byte-compatibility**: scripted save (Dataset + text/numeric
   properties) diffed across baseline and de-celled binaries: identical except
   the randomly generated `<UUID>`; `class="Dataset"` etc. now produced by the
   class_name() virtuals.
4. **Statistics**: `phon_stats` builds in Debug and Release;
   frequentist `run_all` via Release: **552 passed, 0 failed**; Bayesian
   gaussian family OK. (Debug fits are impractically slow for the full suite;
   Release used for the run gates. The Debug phon_stats binary passes a
   functional smoke — CSV load through the shim DataTable + nrow/ncol/header
   reads — and both configurations compile the analysis sources.)
5. **GUI smoke**: `QT_QPA_PLATFORM=offscreen ./build/phonometrica` aborts at
   startup **identically on the pristine-HEAD baseline** — std/ is already
   ported to the new language, so the old engine can't parse the startup
   scripts (`import app_stubs`). Pre-existing at HEAD, resolved by the A4
   cutover; script-mode (text) execution works and carried all runtime gates
   above.

## For A2 (cutover)

The whole poly-box seam (PolyObject/TPolyObject/poly_* in typed_object.hpp,
the cast/VTable branches, `hierarchy_root`) dies with the old engine. The
vfs.hpp specialization block maps 1:1 onto the `rt.add_class<T>` list; the
class_name() overrides and the merged ctors carry over as-is.

# A1 stage 2 — the base-type flip (2026-07-21, roadmap A1 + partial A2/A4)

The app now compiles and links ONLY the new engine (`phon_engine`); the old engine
(`phon-runtime`) is EXCLUDE_FROM_ALL, headers in-tree for archaeology until A6, and
no longer buildable (the shared phon/*.hpp and phon/utils/*.hpp headers point at the
new engine). Verified this session: full `phonometrica` binary + `phon-app` +
`phon-gui` + `phon_stats` build clean; engine unit suite 403 cases ×3 builds
(normal/ASan/TSan); the ported test/engine suite passes THROUGH THE APP BINARY
(`build/phonometrica -r test/engine/run_all.phon` — text mode works end to end,
ahead of the A4 expectation); frequentist statistics via Release phon_stats (see
gate log below).

## What moved where

- **Forwarders**: phon/{string,file,runtime,array,regex,hashmap}.hpp →
  `<phon/engine/...>`; phon/error.hpp is self-contained (the 4 `error()` helpers,
  modeled on stats_host/shim); phon/array.hpp also pulls phon/error.hpp
  (shim precedent — analysis TUs get error() transitively). phon/dictionary.hpp
  unchanged. NEW phon/definitions.hpp = app-domain remainder of the old
  runtime/definitions.hpp (PHON_LOG, thresholds, run_script, likely/unlikely,
  largest_integer) on top of engine base/definitions.hpp.
- **utils**: file_system.hpp is a forwarder onto engine base/file_system.hpp
  (engine gained append/application_directory and Array<String> list_directory —
  DEVIATIONS 24; nativize/genericize became value-returning, the 7 in-place call
  sites now assign). alloc.{hpp,cpp} + file_system.cpp are old-engine-only (not
  compiled into the app; signal_processing.cpp's lone utils::free → std::free).
  UTILS_SOURCES (helpers/matrix/os/print/text/zip) compile into phon-app now.
- **CMake**: engine include dir AFTER app root (app forwarders win); phon-app links
  phon_engine PUBLIC and owns Qt6::Core + PHON_WITH_QT=1 (engine String's QString
  interop is header-inline, opt-in per TU — the engine lib itself is Qt-free);
  app targets no longer link the vendored pcre2 (engine brings the system one).
- **index_conversion.hpp** moved to phon/index_conversion.hpp (app-owned, for the
  A3 native ports).
- **maybe_cyclic<stats::Model>/<PriorSpec>** and all 13 `is_clonable` specializations
  removed (the latter map to ClassKind::Reference at registration).

## A2 landed early

`Project::preinitialize` (project.cpp) now registers the whole hierarchy with
`rt.add_class<T>` base-first under the old names (Element/Directory/Document/…/
TimeStamp + Model + PriorSpec + Analysis), all Reference kind. The engine's
add_class gained an `if constexpr` guard so abstract/non-copyable classes register
(DEVIATIONS 25h). Note two fixes vs the OLD tree: FormantQuery-through-
VoiceQualityQuery now derive from **Query** (the old add_standard_type wired them
under Document — their C++ base is Query), and TimeStamp under **Bookmark** (as
before). recast<T> → handle_cast<T> and make_handle<T> → Handle<T>::make swept
tree-wide (37 files); raw_recast → static_cast on .get() (4 sites).

## A4 pieces landed early

- **Settings** (settings.cpp): fully ported. `phon` is a Table isolate-global;
  `phon.settings` a nested Table; both CoW ⇒ every write is fetch → set → write
  back through add_global (helpers fetch_settings/store_settings). get_list returns
  List BY VALUE; get_table deleted (no callers); set_value gained boxing template
  overloads. The GUI's recent-projects/recent-views sites do explicit write-backs.
- **Console** (console.cpp): output/error/clear hooks via set_*_hook (E3); result
  echo via `stringify(v.value())`; `clear` registered as a typed 0-arg function.
- **ScriptView**: hook swap/restore via the new hook getters (DEVIATIONS 25f).
- **Project::emit_signal**: E1 Runtime::call through get_function("emit"); null-fn
  no-op before the startup scripts load.
- **plugin.cpp / protocol.cpp**: description parsing rewritten onto Variant::is<T> /
  to<T> / Table::get. DELIBERATE FIX in protocol.cpp: the old code re-read the
  "choices" key for the "display" value (stale-iterator bug) so display texts
  silently mirrored match patterns; now reads "display".

## Cordoned behind `#ifdef PHON_TODO_A3` (grep for it — the A3 worklist)

Old-engine native registrations, compiled out with empty initialize() bodies:
annotation.cpp, data_table.cpp (970-2626), sound.cpp, spectrum.cpp,
vfs.cpp (Document::initialize), project.cpp (Project::initialize),
main_window.cpp (setShellFunctions incl. UserDialog uses), func_document.hpp,
user_dialog.{hpp,cpp} (rebuilt on Table specs at A4), script_view.cpp
(onViewBytecode — needs an engine compile-without-run entry; -l/-a in
phonometrica.cpp print "not available" meanwhile).

## Known losses until A3/A4 (deliberate, tracked)

- No app natives (get_annotations, fit, create_dialog, …) — scripts using them fail.
- ScriptIndex (phon/gui/script_index.hpp, app-owned): index_script is a stub —
  editor keeps builtin completions, loses script-local symbols and live squiggles.
- Error traces: RuntimeError carries message+line only; frames live in the script
  Error value — console/script_view show line-level errors, no trace blocks yet.
- Runtime has no program_path: Windows/macOS resource-dir discovery in
  Settings::initialize needs a channel at A7 (Linux unaffected).
- do_string has no chunk-path parameter: get_script_path() inside an editor buffer
  run resolves to nothing (do_file is fine).

# A3 — native re-registration (2026-07-21)

All PHON_TODO_A3 native cordons from stage 2 are ported to the new engine's typed
registration API and deleted, except the two A4-scope ones (user_dialog.{hpp,cpp}
— rebuilt on Table specs — and script_view.cpp's bytecode viewer, which needs an
engine compile-without-run entry). Conventions per stats_host/bindings.cpp,
shared helpers in the new phon/application/bindings.hpp (`guarded` converts
std::exception → Isolate::raise so domain throws become catchable script errors;
to_numarray/to_array_double convert 1-D/2-D column-major arrays; make_list).

Per file:
- **annotation.cpp**: 34 natives (events, layers, I/O, annotation_ops transforms)
  + fields path/sound/nlayer. Old per-native "[Index error] Couldn't find …"
  messages preserved via local layer_index/event_index raisers.
- **sound.cpp**: fields path/duration/nchannel/sample_rate; intensity/pitch/
  formants/voice-report (incl. strict options-Table parsers reading Settings
  defaults); 24 frequency-scale conversions (scalar + NumArray forms); convert().
- **spectrum.cpp**: 11 fields, get_spectrum, get_spectral_moments (Table result).
- **vfs.cpp**: add_property (String|bool|double typed overloads replace the old
  Object+runtime-check form), remove_property, get_property.
- **data_table.cpp**: the whole surface — fit ×6 (frequentist/options/Bayesian),
  filter ×2, summarize/get_coef/compare, evaluate ×2, polish, try_phase2,
  predict ×3, get_cell/set_cell/get_header, get_column ×3 (Dataset-typed,
  by-name auto-detecting, Concordance), get_column_type, add_column ×2 (the
  G6a rename of the old ref-masked `append`; List items stringify like print),
  to_csv ×2, emmeans ×2 / emtrends ×2 (shared print helpers), dharma,
  mean/std/sum (scalar + dim'd NumArray) + vrc, Prior() factory + 9 set_*
  functions, ~61 Model fields, Dataset/Concordance fields. Bodies largely
  mirror stats_host/bindings.cpp (kept in sync until A6 folds the shim back).
- **project.cpp**: subsystem initialize() chain restored; get_annotations/
  get_annotation (+ sounds/concordances/datasets) with polymorphic returns;
  phon.project.{open,close,add_folder,add_file,refresh,has_path,save,is_empty}
  as first-class function values in the phon Table (internal `__project_*`
  generics; `phon.project.is_empty()` chains GETFIELDs then calls — verified).
- **func_document.hpp**: rewritten as initialize_document_natives() — Document
  fields path/label/length + polymorphic load(). The old meta::to_string
  specialization for Document is gone (old-engine machinery; scripts get the
  default representation until a script-side to_string is wired if wanted).
- **main_window.cpp**: all shell functions typed (dialogs, file pickers, input,
  progress, view_text, launch_browser, plugin queries, view-level queries);
  phon.{get_version,get_date,get_supported_sound_formats,close_current_view}
  via trampolines. create_dialog ×2 register but raise "[Not implemented]"
  until the A4 UserDialog rebuild (better than an unresolved-name compile error).

Notes: sum/mean already exist as engine builtins for NumArray; re-registering
identical signatures is benign (verified: mean/sum/std give correct results).
`dharma` keeps its old script name (function_declarations.hpp's "test_residuals"
docs entry is a docs-side question for A5).

Gates: full binary + Release binary build; test/engine suite passes through the
app binary; A3 smoke (annotation natives, conversions, priors, error paths,
phon.project) passes; frequentist 552/552 through the RELEASE APP BINARY itself
(PHON_MODULE_PATH=test/statistics/lib build-rel/phonometrica -r
test/statistics/frequentist/run_all.phon) — see commit message for the result.

# A4 chunk 1 — startup wiring, Settings round-trip, first offscreen GUI smoke (2026-07-22)

The GUI is un-broken: full offscreen startup/shutdown cycles run clean, and
E1/E2 are validated end to end inside the real GUI process.

- **std/ integration**: dropped every `import app_stubs` from the six bundled
  scripts; deleted std/app_stubs.phon and std/headless_test.phon (headless
  stand-ins, obsolete now that the app registers real natives and runs scripts
  itself). Native coverage cross-checked: everything the std scripts name is
  registered (get_base_name/get_user_directory turned out to be engine builtins).
- **Startup ordering fix**: speech_analysis.phon moved from initialize() (main)
  into MainWindow::postInitialize() right after setShellFunctions(). The new
  engine resolves names at COMPILE time, so a script calling window natives
  (get_current_sound & co.) cannot even be loaded before the window registers
  them. initialize.phon + signal.phon stay early (no window deps; `emit` should
  exist as soon as possible for Project::emit_signal).
- **G6a rename**: console `clear()` → `clear_console()`. The engine's
  container `clear(ref x)` builtins and a 0-arg non-ref overload cannot share a
  generic (ref-mask uniformity). Autocompletion list updated; docs at A5.
- **Shutdown-order bug (real, latent since the flip)**: Project's static
  singleton died in atexit handlers AFTER the stack Runtime in main —
  cell_dispose on dead engine = segfault as soon as the project holds files
  (only empty-project runs ever exited clean). Fix: Project::destroy() +
  explicit ordering in main (window scope → Project::destroy() → ~Runtime),
  in both GUI and text mode.
- **Legacy settings normalizer (real upgrade bug)**: the OLD engine's dump_json
  wrote trailing-dot floats ("2259.", "0."), which the new lexer rejects by
  design ('.' is a decimal point only before a digit, protecting `1.method()`)
  → every pre-cutover settings.phon was silently reset to defaults. Fix:
  settings.cpp normalize_legacy_floats() — string-aware (quoted paths
  untouched), inserts the missing '0' before do_string. Verified against the
  real user settings file: autoload/info_ratio/geometry/recent_projects all
  survive the round-trip.
- **Smoke harness**: PHON_GUI_SMOKE=<seconds> env quits the event loop after N
  seconds (so finalize() runs and settings are written); Project::notify_error
  routes to stderr instead of a modal QMessageBox in smoke mode (a modal
  dialog's nested event loop is unbreakable by QApplication::quit — the smoke
  would hang forever; observed with sandbox-missing .phon-conc files).
- **E1 validated observably**: a user Scripts/ probe connecting
  "__SIGNAL_PROJECT_LOADED" fires when a project opens via argv — C++
  emit_signal → Runtime::call → script slot → file written. E2 validated by the
  settings round-trips (script-side `phon.settings = {...}` + C++ read-modify-
  write + post_initialize migrations). E3's GUI console path is wired
  (A1 stage 2) but only observable interactively — defer to manual testing.
- **Finding for chunk 2 / A5**: `open` is a KEYWORD in the new language, so
  `phon.project.open(...)` is unwritable in scripts (workaround:
  `var f = phon.project["open"]`). Decide a rename (open_project?) or keyword
  relaxation at chunk 2.

Gates: Debug+Release build; test/engine suite through both binaries;
frequentist 552/552 through the Release binary (with Project::destroy in the
text-mode path); offscreen smokes: fresh profile (settings created, byte-stable
second round-trip), argv project open + E1 probe, real legacy settings file
(upgrade path).

# A4 chunk 2 — UserDialog rebuild + error-trace surfacing (2026-07-22)

- **UserDialog rebuilt on Table specs (app commit a06c686):** the dialog builder
  reads the new engine's Table/List values directly (lenient Table::get + small
  required/optional accessors keeping the old error messages). create_dialog's
  stubs replaced by the real natives: result Table on OK, null on cancel; the
  String overload evaluates the string to a Table first. Button "action" script
  errors no longer escape into the Qt event loop. In smoke mode dialogs
  auto-accept with defaults — an offscreen probe covering ALL widget types
  (field/check_box/combo_box/file_selector/check_list/radio_buttons/container/
  spacing/stretch) verified spec parsing and result marshaling:
  {"path":..., "layers":"1,2", "sep":2, "annots":[], "flag":true, "combo":2}.
- **Error traces (engine commit 4b93703 DEVIATIONS 26 + app commit 50db369):**
  the engine boundary releases the in-flight error value (frames were
  unreachable from the host) → RuntimeError now carries plain
  std::vector<ErrorFrame> {function,file,line}, populated from the Error
  value's frames field (slot 2) right before the release, in both State::run
  and Runtime::call. New engine test pins the 3-frame shape (404 cases ×3
  builds green, ASan/TSan clean). App: Console::runCode shows the trace under
  the message (multi-frame only); ScriptView picks the deepest buffer-owned
  frame for the in-editor highlight (empty-file frames == the buffer, since
  do_string has no chunk path); text mode prints the trace to stderr —
  verified end to end with a nested-throw script through -r.

Still-open A4 stragglers (tracked for chunk 3 / A5):
- Bytecode viewer (script_view.cpp PHON_TODO_A3 cordon) — needs an engine
  public compile-without-run entry (compile/disassembler.hpp), also -l/-a.
- `open` is a keyword in the new language → `phon.project.open(...)` cannot be
  written (workaround `phon.project["open"]`). Owner decision: rename the key
  (open_project?) vs relaxing keywords-as-field-names in the engine.
- do_string chunk-path passthrough (get_script_path for saved editor buffers;
  would also give buffer frames a real file in traces).

Gates: engine 404 ×3 (ASan leak-free, TSan clean); test/engine suite through
Debug+Release app binaries; offscreen smokes green incl. project open + E1
probe + create_dialog probe.

# A4 CLOSED (2026-07-22, app commit d2b59b3; engine commit 14f0205 DEVIATIONS 27)

The three stragglers landed:
- **phon.project.open → phon.project.load** (owner decision: `open` stays a
  keyword). Internal trampoline renamed __project_load; dot-call verified
  (`phon.project.load(path)` → is_empty flips → close() → empty again).
- **Bytecode viewer + CLI -l/-a**: new engine API `Runtime::disassemble(code)`
  compiles against the LIVE session (module loader + shell), so app natives and
  add_global values resolve — the free disassemble_source's throwaway namespace
  could not compile app scripts. Chunk is not executed (unit test pins a
  compile-time-declared slot staying null). script_view.cpp cordon deleted;
  -l prints the listing, -a lists then runs.
- **do_string chunk-path**: new `Runtime::do_string(code, path)` threads the
  path into State::run — get_script_path() works in saved buffers, imports
  resolve relative to the file, error frames carry the real file. ScriptView
  passes the saved path; unsaved buffers keep the empty-path behaviour (and
  the editor's empty-file == buffer frame matching stays as harmless
  defence-in-depth).

NO PHON_TODO_A3 cordons or TODO(A4) markers remain anywhere in the tree
(the only tagged leftover is the A7 program-path TODO in phonometrica.cpp,
which belongs to A7 by design).

Gates: engine 406 cases ×3 builds (ASan/TSan clean); test/engine through
Debug+Release app binaries; offscreen smoke (project open + E1 probe) green;
frequentist 552/552 through the Release binary (see commit message).

Interactive GUI pass (real display: open project, sound view, annotation
edit, query, plugin script, settings dialog) remains an owner activity —
everything automatable offscreen is green.

# A5 COMPLETE (2026-07-22, app commit 5a3f658; engine commits 55ee38a DEVIATIONS 28)

Scripts and docs ported to the new language. Deliverables:
- docs/scripting/migration.rst — the old→new language guide (L1-L10 + S1-S7 +
  watchlist + app renames), every claim verified against the binary. Corrections
  vs the step-3 notes discovered during verification: NO call-site `ref` mark
  (references.md superseded design.md §7), `catch e` takes no `do`, print has
  no sep/end_line options, len() DOES cover arrays, last-expression implicit
  return is gone, defining any `init` removes the default constructor,
  compound assignment works on subscripts but not fields.
- All 24 scripting/analysis RST files converted by a 7-agent fan-out with
  every self-contained snippet EXECUTED through the app binary (~90+ assertion
  scripts); API pages verified entry-by-entry against registrations (stale
  entries deleted, undocumented natives added: new_annotation, open_files_dialog,
  launch_browser, evaluate/polish/try_phase2, ~20 Model fields...).
- plugins.rst rewritten against the ported loader (description.phon table
  scripts, .html doc targets, initialize/finalize.phon, plugin Scripts/ on the
  import path, full signals section). signal.rst documents the std signal API.
- Pygments lexer rewritten; docs build has ZERO phon-lexer warnings (R-formula
  and shell blocks tagged text/bash); overall warnings 93 vs 106 at baseline
  (rest are pre-existing duplicate-object noise from overload entries).
- Editor metadata (autocompletion_list/function_declarations) regenerated from
  registered natives + std signal API.
- PFC user plugin ported (~/.config/phonometrica/Plugins/PFC; backup at
  ~/.config/phonometrica/PFC.old-engine-backup).

Bugs found and fixed during A5 gates:
- get_formants binding flattened the nformant×2 matrix to 1-D (sound.cpp,
  broke report_formants); now uses shape-preserving to_numarray.
- ENGINE UAF (DEVIATIONS 28): host-side last release of a buffered cycle
  candidate (Runtime::add_global overwriting `phon`) freed the cell while the
  collector's candidate buffer still pointed at it → crash in the final
  collection as soon as a plugin with protocols loaded. cc_collect_deferred
  now parks unconditionally. Regression test validated both ways under ASan.
- Modal error dialogs in plugin/startup-script loading hang offscreen smokes →
  stderr in smoke mode.

Chips spawned by doc agents (open, not blocking): protocol-builder brace
escaping (escape_json_string vs interpolation), by-ref iteration write-back
through a ref parameter (engine), action "shortcut" key unimplemented.

Gates: engine 407×3 (ASan/TSan); test/engine Debug+Release; frequentist
552/552 Release (re-run after the collector fix); offscreen smokes incl.
plugin+protocols+project+E1 probe, plus a full ASan smoke (0 errors).

# A6 — demolition (2026-07-24)

The old engine is gone. 79 files / ~24.7k lines deleted; nothing in the tree
compiles, includes or links anything under `phon/runtime` or `phon/base` any
more.

## Deleted

- **`phon/runtime/`** (the whole old engine: compiler, VM, object model,
  builtins, func_*.hpp tables). This takes the **A0 poly-box seam** with it —
  `PolyObject`/`TPolyObject<T>`/`poly_value`/`poly_box_of`/`poly_payload_offset`
  in typed_object.hpp, the poly branches in variant.hpp's `cast<>` and
  runtime.hpp's VTable, and `traits::hierarchy_root`/`is_poly_boxed`/
  `maybe_cyclic` in traits.hpp. Nothing outside phon/runtime referenced any of
  them: the vfs.hpp `namespace traits { hierarchy_root ... }` block the roadmap
  points at had already been replaced by the `rt.add_class<T>` comment when A2
  landed early in the A1 stage-2 flip, and the `maybe_cyclic`/`is_clonable`
  specializations went with it.
- **`phon/base/`** (old shim `Array<T>` and the PCRE2-backed `Regex`).
- **`phon/utils/alloc.{hpp,cpp}`** and **`phon/utils/file_system.cpp`** —
  old-engine-only since the A1 flip (not in `UTILS_SOURCES`); their
  functionality comes from the engine's `base/alloc` and `base/file_system`,
  which `phon/utils/file_system.hpp` forwards onto.
- **`test/engine/test_vfs_decell_old_engine.phon`** (old language, unrunnable —
  it existed to prove step 5 on the old binary; the content lives in git
  history). Nothing referenced it: not run_all.phon, not validate_tests.
- **`stats_host/shim/phon/{array,error}.hpp`** — exact duplicates of the app's
  own forwarders once the include order is fixed (see below).

## Kept, deliberately

- **`phon/index_conversion.hpp`** — since A3 this is the app-side 1-based
  converter used at the binding boundary of every app native (the original A6
  bullet predates that move). Its header comment no longer claims it dies with
  the old engine.
- **The `phon/*.hpp` forwarders** (string/array/regex/file/runtime/error/
  hashmap/dictionary/definitions) — they point at the engine and are the app's
  include surface. Only stale comments naming `phon/runtime/*` or
  `phon/base/*` were rewritten (array.hpp, definitions.hpp,
  index_conversion.hpp).
- **`Element::class_name()` and its overrides** — engine-independent, and the
  project-XML format depends on the strings they return.
- **`phon/utils/{ref_count,slice}.hpp`** — still used by property.hpp and
  sound.hpp respectively.
- **The vendored `phon/third_party/pcre2` tree** — see "Vendored PCRE2" below.
  (`phon/third_party/utf8proc` had only old-engine callers and was deleted, with
  its README/install.rst/acknowledgements/debian-copyright entries; the engine
  carries its own Unicode tables, `base/unicode.cpp`.)

## vfs.hpp

15 of the 21 forward declarations at the top were dead (`DataTable`, `Dataset`,
`Concordance`, `Spectrum`, `Script`, `Note`, `Query`, the four `*Query`
subclasses, `VoiceQualityQuery`, `Bookmark`, `TimeStamp`, `Analysis`) — they
existed to name the classes in the deleted `hierarchy_root` specializations;
the surviving hits were the `FileType` enumerators and comment text, not the
class names. `Element`/`Directory`/`Document`/`Runtime` stay (used by the
declarations below). The class list is no longer duplicated in a comment:
`Project::preinitialize` is the authoritative registration order.

## CMake

- `phon-runtime`, `RUNTIME_SOURCES` and `THIRDPARTY_SOURCES` deleted. The
  engine subproject was already unconditional (`PHON_ENGINE_DIR` + a
  FATAL_ERROR if missing); no `PHON_WITH_NEW_ENGINE` toggle survived on the app
  side — the only mentions left were stale comments in stats_host/CMakeLists.txt
  and test/engine/README.md, both rewritten.
- The `-DUTF8PROC_STATIC` / `-DPCRE2_CODE_UNIT_WIDTH=8` / `-DPCRE2_STATIC=1`
  global definitions are gone: no app TU includes `pcre2.h` any more (the
  engine's `phon/engine/types/regex.hpp` defines the code-unit width itself,
  and the vendored PCRE2 target propagates `PCRE2_STATIC` through its usage
  requirements). `shlwapi`/`m` were already linked on the top-level target.

## Vendored PCRE2 (owner decision, 2026-07-24)

Phonometrica keeps using the **vendored** PCRE2 — for the engine as well, not
just for itself. The engine resolves PCRE2 with
`find_library(PCRE2_LIBRARY NAMES pcre2-8 REQUIRED)` / `find_path(PCRE2_INCLUDE_DIR ...)`,
so the app pre-seeds both cache entries **before** `add_subdirectory` on the
engine: `PCRE2_LIBRARY` becomes the vendored alias target `pcre2-8`
(→ `pcre2-8-static`, which propagates `PCRE2_STATIC` and its generated
`interface/` include directory) and `PCRE2_INCLUDE_DIR` the generated header
directory. **No engine change was needed**, and standalone engine builds still
find the system library.

Verified on the linked binary: `libpcre2-8.a` is on the link line, `objdump -p`
shows no direct `NEEDED` entry for any system PCRE2, and `pcre2_compile_8` is
not undefined. The two `libpcre2-*.so` entries `ldd` reports are transitive —
glib pulls the 8-bit library, Qt6Core the 16-bit one. Vendored version is
10.47, ahead of the 10.46 the system provided.

## stats_host: the shim fold, and why one header stays

Include order is now **shim → repository root → `${PHON_ENGINE_DIR}`**, the
same order the application targets use. That single change made two of the
three shim headers redundant: with the repo root ahead of the engine, the
analysis sources pick up the app's own `phon/array.hpp` and `phon/error.hpp`
(the engine's `phon/error.hpp` is a different header — it exposes
RuntimeError/SyntaxError, not the formatted `error(...)` helper the analysis
layer throws). The `traits::maybe_cyclic` stub the shim carried had no
remaining specializations to serve and went with it.

**`stats_host/shim/phon/application/data_table.hpp` stays** (decision recorded
per the roadmap's "record in MIGRATION_NOTES" escape hatch). Folding it back
into the real `Dataset` is not a header problem — the real `DataTable` derives
from `Document` → `Element`, so compiling against it pulls in vfs.cpp,
project.cpp, dataset.cpp, concordance.cpp, the property model, pugixml, sqlite,
sndfile and rtaudio: effectively the whole `phon-app` library. That would turn
the headless statistics runner into "the app minus the GUI" and cost the
property this target exists for — a small, fast Release build for the
552-assertion frequentist suite (Debug fits through the app binary take hours).
The shim is ~90 lines of read-only CSV table and its header documents the exact
parity contract with `Dataset::get_cell`.

## Gates (all green)

1. **Builds**: Debug (`-g -O2`, mirroring the main tree) and Release, both from
   the worktree, `phonometrica` + `phon_stats`. Release was a from-scratch
   configure+build.
2. **test/engine/run_all.phon** through both binaries.
3. **Frequentist statistics 552/552** through the Release *app* binary
   (`PHON_MODULE_PATH=$PWD/test/statistics/lib build-rel/phonometrica -r
   test/statistics/frequentist/run_all.phon`) and through the Release
   `phon_stats` (the shim path, same suite).
4. **Offscreen GUI smokes** (sandboxed `HOME`, `QT_QPA_PLATFORM=offscreen`,
   `PHON_GUI_SMOKE=8`): validate_tests.phon-project opened from argv — clean
   exit, settings.phon written; plus `test/gui/run_smoke_formants.sh`
   (ALL OK, F1/F2/F3 on target, shape checks).
5. **Project-XML byte-compatibility** vs a pre-A6 Debug binary built from
   ab6452a: a scripted save of a project with a Script, two Datasets and all
   three property types (text/numeric/boolean) in text mode, and a second
   offscreen save covering the **Corpus/Sound** branch (audio formats only
   register during GUI initialization). Both diffs are **byte-identical** —
   even `<UUID>`, which the roadmap allows to differ.

## Pre-existing breakage surfaced (fixed separately)

Two optional CMake targets turned out to be broken, neither of them by A6:
`tools/calibrate_formants.cpp` (`BUILD_CALIBRATION=OFF` by default, so no
acceptance gate covered it) still spoke the old engine, having been missed by
the A1–A5 port; and `BUILD_UNIT_TEST` globbed a `unit_test/` directory that has
never existed in this repository. Both were left alone in the demolition commit
as out of scope and fixed on their own branch — see **"A6 straggler"** below.

# A6 straggler — tools/calibrate_formants.cpp ported (2026-07-24)

The optional `calibrate_formants` target (`-DBUILD_CALIBRATION=ON`, OFF by
default, so no acceptance gate covered it) still spoke the OLD engine. It was
last touched on 2026-07-10, before the A1 base-type flip, and A6 only made the
breakage visible. Two independent breaks, one of which the compiler cannot see:

- **Runtime**: `Runtime rt(argv[0])` → `Runtime rt` (new engine's ctor takes no
  arguments). This is the whole compile-level port — the tool touches no other
  engine API (`Project::preinitialize(rt)` + `Sound::set_sound_formats()` were
  already correct).
- **Array is 0-based now** (silent at compile time, `PHON_ASSERT` at run time):
  `Sound::get_formants` returns an nformant×2 Array whose `operator()` was
  1-based in the old engine and is 0-based in the new one. Every read had to
  drop the +1 and shift the column: `ff(j+1,1)/ff(j+1,2)` → `ff(j,0)/ff(j,1)`
  (7 sites, incl. the diphthong path and the 8-pole persistence probe, whose
  loop bounds went from `1..8` to `0..7`). Anything else ported by eye and not
  actually RUN is worth re-checking for this.

Also fixed while here: the H95 F3 prior table keys were written as single
literals (`"man\x1fei"`), so `\x1fe` parsed as one out-of-range hex escape and
swallowed the vowel's first letter — the lookup silently missed for every vowel
starting with a hex digit (ei/eh/ae/ah/aw/er, half the inventory) in `--f3`
modes 5 and 6. Keys are now joined at build time from (class, vowel) triples,
the same way `cell_key`/`prior_of` join them at lookup.

`BUILD_UNIT_TEST` dropped: it globbed a `unit_test/` directory that has never
existed in this repository's history, so turning it on failed at configure time
with an empty `add_executable`. The C++ unit suite is the engine's
(`--target phon_unit_tests`); the script-level suite is `test/engine`.

Gates: `cmake -B build-cal -DBUILD_CALIBRATION=ON -DPHON_ENGINE_DIR=…` +
`--build --target calibrate_formants` clean (no warnings from this file);
tool run end to end on a 6-token synthetic manifest (impulse train through 3
resonators, known formants) — all modes exercised (default report, --oracle,
--performant, --f3, --heur, --persist, --consensus, --tune, --diph, --external).
Measured formants track the synthesized ones (Weenink MAE 30 Hz), and
--external reproduces injected offsets exactly (+30/-25/+40 → 30.0/25.0/40.0).

# A7 closure (2026-07-24, on merge commit a54d6c5)

The full acceptance-gate list re-run on the merged post-A6 tree (A6 demolition
+ the calibrate_formants straggler + vendored PCRE2). All five green.

**1. Engine unit suite ×3.** `~/Devel/engine`, all three existing build dirs
rebuilt and run: `build` (Debug, PHON_WERROR=ON), `build-asan`
(PHON_SANITIZE=ON), `build-tsan` (RelWithDebInfo, PHON_TSAN=ON). Each reports
**407 cases (0 failed), 2 213 309 checks (0 failed)**, exit 0, with no ASan or
TSan diagnostic. The engine tree is unmodified since A5 (55ee38a) — this is a
re-verification, not a new result. Note `phon_unit_tests` also builds and
passes from the APP's build tree (`cmake --build build --target
phon_unit_tests`), which is the fact A8 leans on.

**2. test/engine.** `run_all.phon` green through both the Debug and the
Release application binary.

**3. Statistics.** Frequentist **552 passed / 0 failed** through the Release
app binary, and the same through the Release `phon_stats` (the stats_host shim
path; run pre-merge, and the merge touches nothing under stats_host/).
Bayesian: gaussian, poisson, binomial, negbin, beta, negbin_owls and
real_schwa all OK. `student` fails **exactly 10 assertions, every one of them
`nu(student)`** (M1 mean/sd/hi, M2 mean, M3 mean, M4 mean/hi, M5 mean/sd/hi)
and nothing else — M1 `hyper.mean` comes out at 19.677134338965 against the
19.677134339 recorded for the OLD engine in the step-4b notes. That is the
documented Laplace-vs-HMC ν fundamental, unchanged by the whole migration.

**4. Project-XML byte-compatibility.** Scripted saves diffed against a
pre-A6 Debug binary (built from ab6452a and kept aside): identical in text
mode (Script + two Datasets + text/numeric/boolean properties) and in
offscreen GUI mode (Corpus/Sound branch, which text mode cannot reach because
audio formats register only during GUI initialization). Byte-identical
including `<UUID>`, which the gate would have allowed to differ.

**5. GUI smokes.** Sandboxed HOME, `QT_QPA_PLATFORM=offscreen`,
`PHON_GUI_SMOKE=8`: validate_tests.phon-project opened from argv, clean exit,
settings.phon written; `test/gui/run_smoke_formants.sh` reports ALL OK with
F1/F2/F3 on target and the shape checks passing. The gate's fuller manual list
(sound view, annotation edit, query, plugin script) was exercised at A4/A5 and
is not re-driven here — the scripted smokes are what runs unattended.

**A7 is closed. The gate list stays live**: re-run it at every future step
boundary, starting with A8 (see the roadmap).
