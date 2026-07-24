# Engine-migration roadmap v2: from the A0 milestone to old-engine removal

*2026-07-19, updated after step 5 (A0, VFS de-celling) landed on main (commits
1155b23 + d26b2cf). Companion to MIGRATION_NOTES.md (the audit trail — read its
step-4a inventory, step-4b, and step-5 sections first; every claim below has its
evidence there). Written as a handoff: each item is self-contained, has file
anchors, a pattern to copy, and an acceptance gate. Target: implementable by
Claude Opus 4.8 without this session's context.*

**State at handoff.** The app builds and links ONLY the old engine
(`phon-runtime`) and is fully green (see A7 for what "green" can mean today).
The new engine (`~/Devel/engine`, CMake subproject via `PHON_WITH_NEW_ENGINE`)
passes its unit suite under normal/ASan/TSan and powers `phon_stats`
(stats_host/), a headless host running the ported statistics suite at oracle
parity (frequentist 552/552). **A0 is done:** Element, Directory, Document,
DataTable and all 16 subclasses are plain C++ classes — no `Atomic` base, no
`Class *` ctor params. The old engine boxes them via a poly-box seam
(phon/runtime/typed_object.hpp:53-113: `PolyObject`/`TPolyObject<T>`, fixed
`poly_payload_offset`, type-erased `Handle<T>`) that structurally mirrors the
new engine's plain-class boxing (its core/cell.hpp `box_value_offset`,
DEVIATIONS 26-29), so A2's flip is a handle swap, not a redesign. Serialization
class names come from a plain `virtual Element::class_name()` (vfs.hpp:112) —
engine-independent, keep it.

**Ground rules (unchanged since phase 1):**
- The two engines share type names in `namespace phonometrica` — they can never
  be linked into one binary or included into one TU (MIGRATION_NOTES
  "Coexistence").
- Engine changes are made directly in ~/Devel/engine with a DEVIATIONS.md
  record and must stay green under the normal/ASan/TSan unit builds
  (`cmake --build <build-dir> --target phon_unit_tests`).
- App changes before the A1 flip must keep the OLD build green:
  `cmake --build build --target phonometrica` + the A7 gates.
- Binding conventions established by stats_host/bindings.cpp: natives that can
  throw take a leading `Isolate &` and convert `std::exception` via
  `iso.raise`; script indices convert through a local `script_index` helper
  (1-based, negative-from-end); old `get_field` string dispatchers become
  `rt.add_field` getters; old `add_constructor` classes become
  class "TypeName" + a factory generic with the old constructor's name
  (Prior/PriorSpec pattern, G6b).

**Hard-won operational facts (step 5) — read before running anything:**
- The GUI is ALREADY broken at HEAD: std/ is ported to the new language, so
  the old engine aborts parsing the startup scripts (`import app_stubs`).
  This is expected and is fixed by A4, not before. Until A4, judge app health
  by baseline comparison (build the pre-change commit in a scratch worktree,
  compare behavior), never by "does the GUI start".
- The app dispatches script-vs-project on file EXTENSION: only `.phon` files
  run as scripts; any other suffix falls into the GUI path (which aborts, see
  above). Old-language runtime tests must keep a `.phon` extension.
- Debug-build statistics fits are impractically slow (30+ min per family).
  Keep a Release build dir (`build-rel`) for the run gates; both build types
  must still compile.
- The old scripting language has no `is` operator — old-language tests check
  dynamic classes with `type(x) == ClassName`.
- ~/Devel/engine has UNTRACKED files the build depends on
  (phon/engine/base/script_error.hpp, phon/engine/core/array.hpp,
  phon/engine/lib/table.cpp, test/unit/test_generic_array.cpp). **P0: commit
  them (with a DEVIATIONS entry if any lack one) before anything else** — a
  clean checkout of the engine does not currently build.

---

## E — engine work items (~/Devel/engine; each independent unless noted)

The engine `Runtime` today (phon/engine/runtime/runtime.hpp): `do_string`,
`do_file`, `set_interactive`, `add_global`, `add_function` (typed dispatch),
`add_class<T>`, `add_field<T>` (read-only getters), `add_import_path`,
`request_interrupt`. Everything below is missing and blocks the A-step named.

### E4 (gap G5) — polymorphic handle upcast for foreign classes  [BLOCKS A2] — DONE (2026-07-20)
- **Landed** in ~/Devel/engine main, commit fb1182b (P0 — commit the 4 untracked
  build-dep files — landed first as 3eef8d3). Handle<T> gained an implicit
  upcasting converting ctor (core/handle.hpp) and core/handle_cast.hpp added
  `handle_cast<D>(Handle<B>)` (upcast unchecked, downcast checked via the engine
  class registry, empty Handle on wrong-type). DEVIATIONS item 17; test_embed.cpp
  "polymorphic handle upcast, checked downcast, and base-handle containers". 397
  cases green ×3 builds. A2 swaps `recast<T>` → `handle_cast<T>` + implicit
  upcasts. Original spec below.

- **Goal:** explicit, asserted `Handle<Derived>` → `Handle<Base>` conversion
  (and a checked downcast, dynamic_cast-based) for `add_class` types under
  single non-virtual inheritance. The boxed payload address is identical
  (box_value_offset is alignment-only — core/cell.hpp:158-171), and dispatch
  already accepts derived cells for base-typed parameters (proven:
  Dataset→DataTable in stats_host).
- **Why:** `Array<Handle<Element>>` containers (ElementList, vfs.hpp:154;
  DocList; Project::m_files), Document-typed natives accepting any subclass,
  `load`'s polymorphic Document return. A0's old-engine seam already gave the
  app these semantics — E4 is the new-engine mirror. Copy the shape of the
  old seam's conversion operator + payload ctor (typed_object.hpp:293-302,
  180-186) if useful.
- **Gate:** engine test: container of Handle<Base> holding derived instances;
  base-typed native receives them; fields and dispatch resolve on the dynamic
  class; checked downcast returns null (or throws — pick one, document) on a
  wrong-type cast.

### E1 (gap G2) — public `Runtime::call` for script function values  [BLOCKS A4] — DONE (2026-07-20)
- **Landed** in ~/Devel/engine main, commit fe1c3d4. `Runtime::call(fn, args, nargs)`
  + `get_function(name)` (named-generic value, for `emit`) + `get_global(name)`
  (inverse of add_global). New `call_from_host` routes to `vm_call`, now root-based
  when `iso.frames` is empty — also drives a re-entering native (named-fn trampoline)
  from an idle session. DEVIATIONS item 18. Original spec below.

- **Goal:** `Variant Runtime::call(const Variant &fn, std::span<const Variant>
  args)` (or variadic) wrapping the internal `vm_call`
  (phon/engine/vm/interpreter.cpp, decl near the other vm entry points), with
  Variant↔Value conversion and RuntimeError propagation. An accessor to read
  an isolate global (the inverse of add_global) may need adding.
- **Why:** `Project::emit_signal` (phon/application/project.cpp:1610-1623)
  fetches the global script function `emit` and calls it with (String event,
  Variant payload). Named functions are first-class (DEVIATIONS item 13), so
  a get_global-style lookup + call reproduces the old shape.
- **Limits to document:** keyword options and `ref` promotion do not flow
  through indirect calls (DEVIATIONS M7 item 13) — emit's positional args are
  unaffected, but slots must not declare keyword/ref params.
- **Gate:** engine unit test calling a script-defined function from C++ with a
  boxed foreign-class payload; suites green ×3 builds.

### E2 (gap G3) — the `phon` namespace value  [BLOCKS A4] — DONE (2026-07-20)
- **Decided option (a)** and landed in ~/Devel/engine main, commit c040c49: Table
  dot-sugar. GETFIELD/SETFIELD on a Table read/write the string key; a missing dot-read
  raises `[Key error]` (index stays lenient/null). `phon` = a Table via add_global;
  members are keys (function values / nested Tables); `phon.settings = {...}` is a
  top-level global field write (persists). DEVIATIONS item 20.
- **A4 caveat (must heed):** Table is CoW. Nested writes `a.b.c = x` do NOT persist (same
  as nested index writes), and any field write to `phon` detaches it — **A4 must re-fetch
  `phon` via get_global, not cache a C++ Handle** across script runs. The app already
  re-reads settings after a script run, so this fits. If pointer-stable shared mutation is
  ever required, revisit with option (b) (a reference-typed namespace object).
- Original spec below.

- **Goal:** an injected global object whose members are callable
  (`phon.project.save()`, `phon.get_version()`) and whose `settings` field is
  assignable from script (`phon.settings = {...}`) and shared mutably with C++
  (Settings::set_value writes the same live Table —
  phon/application/settings.cpp:218-307, 320-367).
- **Evidence:** MIGRATION_NOTES step 4a "Injected values"; old injection at
  phonometrica.cpp:96, members installed at project.cpp:1487-1500 and
  main_window.cpp:3320-3325.
- **Options:** (a) a Table of first-class functions injected via add_global —
  requires field-call syntax `t.f(x)` on Tables and field assignment on an
  injected global to work (verify first; smallest engine delta); (b) a
  dedicated namespace/object kind. Decide with the owner; (a) is the default
  hypothesis.
- **Gate:** headless test: inject a `phon` table with a callable member and a
  settings field; script calls the member, replaces settings wholesale, C++
  sees the new table and mutates a key the script then reads.

### E3 (gap G4) — output hooks on the Runtime  [BLOCKS A4] — DONE (2026-07-20)
- **Landed** in ~/Devel/engine main, commit fe1c3d4. Isolate carries
  output/error/clear hooks + write_output/write_error_output/clear_output; the
  `print` builtin routes through them; Runtime exposes set_output_hook /
  set_error_output_hook / set_clear_output_hook + host-side print/print_error/
  clear_output. DEVIATIONS item 19. stats_host/printers.cpp+bindings.cpp routed off
  std::printf onto Isolate::write_output (proof-of-API; phon_stats Release 552/552,
  summarize output confirmed through the sink) — phonometrica-side change validated,
  commit pending. Original spec below.

- **Goal:** redirectable print + error-styled print + a clear-output callback,
  mirroring the old Runtime's seams (`print`, `show_error`, `clear_output` —
  old runtime.hpp:290-306) that the GUI console swaps (phon/gui/console.cpp)
  and the statistics printers use. Route the engine's `print` builtin through
  it.
- **Consumers:** GUI console/OutputPanel; stats_host/printers.cpp currently
  uses std::printf and should move onto the hook (do it in the same change to
  prove the API).
- **Gate:** unit test capturing print output through a custom hook.

### E5 (G6 + step-3 leftovers) — decisions and small fixes  [DECIDE BEFORE A3] — DECIDED (2026-07-20)
- **G6a: ratify the rename policy** (owner decision). Per-generic ref-mask uniformity
  stays; overload sets that would mix ref-shapes under one name are renamed at A3 (4b
  already used `add_column`). No engine change.
- **G6c: DONE** — RuntimeError derives from std::exception (commit c040c49, DEVIATIONS
  item 21), so an embedder catch-all no longer terminates on a missed catch.
- **S6** (structural equality for value classes) and **L5** (`o.f += 1` field compound
  assignment — also blocks `t.a += 1` on a dot-sugared Table): **DEFERRED** (owner). No
  shipped script needs S6; L5 is an ergonomic wart, not a blocker.
- Original spec below.

- **G6a ref-masks:** either accept per-method ref-masks in `add_method`
  (object/generic.cpp) so old overload sets like `append(ref List, x)` +
  `append(DataTable, col, name)` can share a name, or ratify the rename policy
  (4b used `add_column`). Owner call — it changes native names in A3.
- **G6c:** derive `RuntimeError` from `std::exception` (vm/isolate.hpp) or
  document loudly; a missed catch turns an uncaught script error into
  terminate (bit 4b as a fake segfault).
- **S6:** structural equality for value-class instances (old engine compared
  value classes structurally; new `==` is identity — MIGRATION_NOTES step 3).
- **L5:** field compound assignment (`o.f += 1`) is still a compile error.
- Optional dregs (zero users, only if wanted): SQRT2/PHI constants, Set
  natives, unused string/list variants from the 4a ENGINE GAP list.

---

## A — app cutover (one branch off main, in this order)

### A1 — base-type swap (mostly mechanical; phase 1 prepared it)
- **IN PROGRESS** on branch `engine-a1-base-type-swap` (off main). Done so far:
  - **Stage 0** (app commit 5b0a20c): dropped the 18 poly-boxed VFS-hierarchy
    `maybe_cyclic` specializations (dead since A0); old build still green. KEPT
    `maybe_cyclic<stats::Model>`/`<stats::PriorSpec>` — TObject-boxed, their trait selects
    the GC base class (removal breaks the old build); they come out at the flip.
  - **Stage 1** (engine main commit 360bd0b, DEVIATIONS 22-23): closed the engine
    base-type API gaps so the flip needs no String/File rewrites — `String::to_wide`/
    `from_wide` + `std::wstring` ctor; `File` `Mode` enum + path-opening ctor + `format()`
    (+ `Encoding::Undefined` sentinel). 403 engine cases green ×3.
  - **Stage 2 DONE (2026-07-21, app commit 7a1b2aa; engine f8fcc70, DEVIATIONS 24-25):**
    the flip landed GREEN, not red — the full binary, phon-gui and phon_stats build and
    link against phon_engine only; the ported test/engine suite passes through the app
    binary itself and frequentist stats are 552/552. A2 landed with it (add_class
    registration in Project::preinitialize, handle_cast/Handle<T>::make sweep), plus the
    Settings/Console/emit_signal/plugin/protocol parts of A4. The old native
    registrations are compiled out behind `#ifdef PHON_TODO_A3` — grep for that macro:
    it is the A3 worklist. Full details in MIGRATION_NOTES.md "A1 stage 2".

- Replace phon/base Array/Regex and the old String/File/Hashmap-Dictionary
  with the engine's headers (`<phon/string.hpp>`, `<phon/array.hpp>`, engine
  Table/Set/File as applicable). The engine adopted the app's Array<T>
  wholesale (DEVIATIONS 35) and gained split/join/regex-replace/arg/Qt
  conversions/static-init safety (36-40) precisely for this.
- Known knock-ons, handled in 4b, to copy from: `error(...)` helper location
  (stats_host/shim/phon/error.hpp shows the shape), `Vector`→`ColVector`
  (already done tree-wide), `traits::maybe_cyclic` — delete the
  specializations everywhere (subclass headers still carry them; they exist
  only for the old GC and are already redundant since A0 made
  `is_collectable` ignore poly-boxed types).
- From this step on, the app binary links the NEW engine only; everything
  below happens with the old engine unlinked (keep `phon/runtime` in-tree
  until A6 for archaeology).

### A2 — register the class hierarchy (needs A1 + E4)
- The checklist IS the A0 seam: the `traits::hierarchy_root` block at
  phon/application/vfs.hpp:78-99 lists exactly the 20 classes to register.
  `rt.add_class` them in base-first order (mirror the old registrations at
  project.cpp:1890-1913, same names — the `class_name()` overrides depend on
  those names for project-XML compatibility): Element, Directory, Document,
  Annotation, Sound, Spectrum, DataTable, Dataset, Concordance, Script, Note,
  Query, FormantQuery, PitchQuery, IntensityQuery, SpectralMomentsQuery,
  VoiceQualityQuery, Bookmark, TimeStamp, Analysis; plus stats::Model
  ("Model"), stats::PriorSpec ("Prior" → class "PriorSpec" + Prior() factory
  per G6b), moving those two from stats_host into the app. All
  ClassKind::Reference.
- Swap old handles to engine handles: `make_handle<T>(...)` →
  `Handle<T>::make(...)`; the payload-pointer constructions
  (`Handle<Element>(this)` in vfs.cpp, `Handle<Sound>(&sound)` in
  spectrum.cpp, `Handle<Query>(query_doc)` in main_window.cpp) need the
  engine's equivalent payload→handle path (part of E4's design);
  `recast<T>(h)` sites (~30, mostly GUI) become the E4 checked downcast.
  Tree-wide but mechanical; the compiler finds every site.
- Delete nothing yet: the old-engine seam (typed_object.hpp poly-box,
  variant.hpp/runtime.hpp poly branches, the hierarchy_root block) dies in A6
  with the whole old engine.

### A3 — re-register the natives (the 4a tables are the checklist) — DONE (2026-07-21, commit 253e49a)
All PHON_TODO_A3 cordons ported to typed registrations and removed (see
MIGRATION_NOTES "A3"); only user_dialog + the bytecode viewer remain cordoned
(A4 scope). Frequentist statistics run 552/552 through the Release app binary.
Original checklist below.

Copy the stats_host/bindings.cpp conventions. Sub-items, independently
landable; registration sites to port are marked `initialize(Runtime &rt)`:
- **annotation.cpp:157 + :~590-680** (34 natives) + Annotation fields
  path/sound/nlayer via add_field.
- **sound.cpp:615 + spectrum.cpp** (34) + Sound fields (4) and Spectrum
  fields (11).
- **data_table.cpp remainder** beyond 4b: filter ×2, evaluate ×2, polish,
  try_phase2, predict ×3, emmeans ×2, emtrends ×2, dharma (naming: reconcile
  with function_declarations.hpp `test_residuals`), to_csv ×2, set_cell,
  get_column_type, dim'd mean/std/sum + vrc; Dataset/Concordance fields.
- **project.cpp:1478-1505** (9, incl. the polymorphic `load` — needs E4),
  **vfs.cpp add_property/remove_property/get_property** (Document-typed),
  **settings.cpp:73-77** (5).
- **main_window.cpp:3293-3325** (30, all capture MainWindow, Qt main thread)
  + **console.cpp clear** (needs E3).
- Old `append(table, …)` per the E5/G6a decision.
- Drop at cutover: the 149 COVERED builtins, the GLOB class injections, the
  36 unused ENGINE GAP builtins (the 4a tables mark every one).

### A4 — runtime wiring (needs E1 + E2 + E3)
- **CHUNK 1 DONE (2026-07-22, commit 75ce03d):** startup wiring + Settings
  round-trip + first offscreen GUI smokes are green (fresh profile, argv
  project open with an E1 signal probe, real legacy settings upgrade).
  app_stubs/headless_test deleted; speech_analysis.phon loads in
  postInitialize after setShellFunctions (compile-time name resolution);
  console clear()→clear_console() (G6a); Project::destroy() fixes a
  shutdown-order segfault; Settings::read repairs old dump_json trailing-dot
  floats ("2259.") that the new lexer rejects. PHON_GUI_SMOKE=<secs> is the
  smoke hook. Details in MIGRATION_NOTES "A4 chunk 1".
- **CHUNK 2 DONE (2026-07-22, app commits a06c686 + 50db369; engine 4b93703
  DEVIATIONS 26):** UserDialog rebuilt on Table specs (create_dialog real,
  all widget types verified offscreen via smoke-mode auto-accept); error
  traces surfaced — RuntimeError now carries plain ErrorFrame data across the
  embedding boundary, rendered in the console (showTrace), the editor
  highlight (deepest buffer-owned frame), and text mode's stderr. Details in
  MIGRATION_NOTES "A4 chunk 2".
- **A4 CLOSED (2026-07-22, app commit d2b59b3; engine 14f0205 DEVIATIONS 27):**
  phon.project.open → phon.project.load (`open` stays a keyword, owner
  decision); bytecode viewer + CLI -l/-a on the new session-scoped
  `Runtime::disassemble`; `do_string(code, path)` gives saved buffers
  get_script_path/file-carrying frames/relative imports. Zero PHON_TODO_A3 /
  TODO(A4) markers remain. Details in MIGRATION_NOTES "A4 CLOSED". The
  interactive (real-display) GUI pass is the owner's; all offscreen-automatable
  gates are green. NEXT: A5 (docs/plugins language guide) → A6 (demolition) →
  A7 (full gates incl. project-XML byte-compat diff).
- Construct the engine Runtime in the app shell; `do_string`/`do_file` call
  sites are API-compatible (console.cpp:264/289, script_view.cpp:293,
  main_window.cpp:3604, plugin.cpp:36/48/68, protocol.cpp:39/50,
  settings.cpp:343-371, user_dialog.cpp:81/270).
- `phon` namespace + settings via E2; `Project::emit_signal`
  (project.cpp:1610) via E1; console and the statistics printers via E3;
  script-editor error highlighting reads `e.frames` (engine ready —
  DEVIATIONS post-port item 11). Interrupt button →
  `Runtime::request_interrupt` (exists).
- std/ is already new-language: delete the `import app_stubs` lines and
  std/app_stubs.phon itself as each stub gains its real native (they were
  headless stand-ins, marked for removal at app integration).
- **This is the step that un-breaks the GUI.** First A7 gate run with a
  working GUI happens here.

### A5 — scripts and docs — DONE (2026-07-22, app commit 5a3f658; engine 55ee38a DEVIATIONS 28)
Migration guide written (docs/scripting/migration.rst, binary-verified); all
scripting/analysis RST converted with executed snippets; plugins.rst rewritten
against the real loader; lexer updated (zero phon-lexer warnings); editor
metadata regenerated; PFC user plugin ported. Two real bugs fixed: get_formants
1-D flattening (app) and a cycle-collector UAF on host-side release of a
buffered candidate (engine, DEVIATIONS 28 — found by the first plugin-loading
smoke). Details in MIGRATION_NOTES "A5 COMPLETE". Original spec below.
- std/ and test/engine are already ported (steps 3/4b; on main).
- Plugin/user scripts and the documentation need the language-change guide:
  L1-L10 + S1-S7 (MIGRATION_NOTES step 3) plus the 4a "semantic-diff
  watchlist" (3-arg slice, group_count includes group 0, File()→open_file,
  str→to_string, load_json→from_json, len narrowing, unordered Table/Set, …).
- values_references.rst and the scripting docs: update for the new language.
- The docs build lexes literal blocks with the old pygments lexer and warns
  on new syntax (visible in any full build log) — update the lexer or the
  snippets while here.

### A6 — demolition — DONE (2026-07-24, app commit a59bc0c)
The old engine is deleted; see MIGRATION_NOTES "A6 — demolition" for what went,
what deliberately stayed (index_conversion.hpp, the phon/*.hpp forwarders, the
class_name() virtuals, one stats_host shim header), the stats_host include-order
fold, and the gate log. Two amendments to the spec below, both recorded there:
the vfs.hpp `hierarchy_root` block had already been removed when A2 landed early
in the A1 stage-2 flip, and the headless DataTable shim is kept (folding it back
into the real Dataset would pull the entire application layer — VFS, project,
pugixml, sqlite, sndfile, rtaudio — into the headless statistics runner).

**Current-state amendments (2026-07-22, after A5; read together with the
original spec below):**
- KEEP `phon/index_conversion.hpp` — since A3 it is the shared 1-based
  converter used by the app bindings (the original bullet predates that move).
- The forwarder headers at phon/*.hpp (string/array/regex/file/runtime/error/
  hashmap/dictionary) point at the NEW engine and STAY — only their stale
  comments (if any) mentioning phon/runtime or phon/base need touching.
- Old-language files to delete: test/engine/test_vfs_decell_old_engine.phon.
  (test_base_migration.phon.old-engine-backup was untracked and already removed
  on 2026-07-22; its pre-port content lives in git history.)
- stats_host: keep the phon_stats runner; fold shim/ back per the original
  spec (or, if folding proves invasive, keeping a minimal shim is acceptable —
  decide by build friction and record in MIGRATION_NOTES).
- After the deletions, run the FULL A7 gate list (below), including the
  project-XML byte-compat diff vs a pre-change baseline build (git worktree of
  the previous commit; only <UUID> may differ) and the offscreen GUI smokes
  (PHON_GUI_SMOKE=<secs> env; see MIGRATION_NOTES "A4 chunk 1"; the formants
  smoke is test/gui/run_smoke_formants.sh <binary>).
- Repo hygiene: HUGE untracked junk exists (.cache/, build*/); NEVER
  `git add -A` — stage explicit paths or `git add -u`.
- Delete `phon/runtime` (old engine) wholesale — this takes the A0 seam with
  it (PolyObject/TPolyObject/poly_value/poly_box_of/poly_payload_offset in
  typed_object.hpp, the poly branches in variant.hpp cast<> and runtime.hpp
  VTable, `traits::hierarchy_root`/`is_poly_boxed`). Then delete the
  `namespace traits { hierarchy_root ... }` block in vfs.hpp:78-99 and the
  forward declarations above it that nothing else uses. KEEP the
  `class_name()` virtuals — serialization depends on them.
- Delete `phon/base` shims, stats_host/shim (fold the headless DataTable back
  into the real Dataset; keep phon_stats itself — it is the headless test
  runner), index_conversion.hpp (app natives keep their own 1-based converts
  at the binding), test/engine/test_vfs_decell_old_engine.phon and
  test/engine/test_base_migration.phon.old-engine-backup (old-language,
  unrunnable after this step), dead old-only helpers as the linker finds
  them.
- CMake: drop phon-runtime; `PHON_WITH_NEW_ENGINE` becomes unconditional.

### A7 — acceptance gates (run after every A-step; all must hold at the end) — CLOSED (2026-07-24, on merge commit a54d6c5)

**Closure run, all five green on the merged post-A6 tree** (evidence in
MIGRATION_NOTES "A7 closure"):
1. `phon_unit_tests` **407 cases / 0 failed, 2 213 309 checks / 0 failed** in
   each of the three configurations (build, build-asan, build-tsan), no ASan
   or TSan report.
2. `test/engine/run_all.phon` green through BOTH the Debug and Release app
   binaries.
3. Frequentist **552/552** (Release app binary; and through `phon_stats`).
   Bayesian: 7 of 8 families OK; `student` produces **exactly 10 failures, all
   `nu(student)`, none elsewhere**, with M1 `hyper.mean` 19.677134338965 —
   the documented old-engine value (19.677134339). Baseline, not regression.
4. Project-XML saves byte-identical to a pre-A6 binary in text AND GUI mode
   (Corpus/Sound + Data/Dataset, all three property types); `<UUID>` matched
   too, though the gate permits it to differ.
5. Offscreen GUI smokes: project opened from argv (clean exit, settings.phon
   written) and `test/gui/run_smoke_formants.sh` (ALL OK).

The gate list itself stands — re-run it at every future step boundary,
starting with A8.

1. Engine unit suite ×3 builds (normal/ASan/TSan) in ~/Devel/engine.
2. `test/engine/run_all.phon` through the real app binary (new engine; until
   A4 lands, through `phon_repl` from the engine build as today).
3. Statistics: frequentist 552/552 + Bayesian parity (the 10 nu(student)
   failures are the documented baseline, value-identical across engines).
   Use a Release build dir for the runs (Debug fits take hours).
4. Project-XML byte-compatibility: script a save (Dataset + properties, the
   step-5 pattern in MIGRATION_NOTES) and diff against a pre-change binary;
   only `<UUID>` may differ.
5. Full GUI build + smoke (`QT_QPA_PLATFORM=offscreen` minimum): from A4
   onward — app starts, opens a project, sound view, annotation edit, query,
   run a plugin script, settings round-trip. Before A4 the GUI aborts at
   startup by design; use baseline comparison instead.

### A8 — absorb the engine into this repository (planned 2026-07-24)

*Beyond the original scope of this roadmap (which ended at old-engine removal).
Goal: `~/Devel/engine` ceases to exist as a separate project; the engine lives
in `phonometrica/phon/engine/` and is built as part of this repository.*

**Start from what is already true.** The engine is ALREADY consumed as a CMake
subdirectory of the app (`add_subdirectory(${PHON_ENGINE_DIR} …)`,
CMakeLists.txt:50, since A1). Its whole target set is reachable from the app's
build tree — verified 2026-07-24: `cmake --build build --target phon_unit_tests`
builds, and the binary at `build/phon_engine_build/phon_unit_tests` reports
407 cases / 0 failed. So A8 is a **file move plus a de-duplication**, not a
build restructure. Budget the risk accordingly: the dangerous part is #2 below,
not the wiring.

**The include paths already line up.** The engine's public headers live at
`phon/engine/...` in its repo, and every app TU already spells them
`<phon/engine/...>`. Moving `~/Devel/engine/phon/engine/` (47 .cpp + 59 .hpp
across base/compile/concurrency/core/lib/object/runtime/types/vm) to
`phonometrica/phon/engine/` changes **zero includes** on either side.

**1. Move, preserving history.** Use `git subtree add --prefix=phon/engine`
(or a git-filter-repo rewrite) rather than a plain copy: DEVIATIONS.md cites
engine commit hashes throughout, and MIGRATION_NOTES cites them too (`55ee38a
DEVIATIONS 28`, `4b93703 DEVIATIONS 26`, `14f0205 DEVIATIONS 27`). A plain copy
kills 57 commits of provenance and every one of those citations.

**2. Resolve the facade-header collision — THE load-bearing step.** Eight
top-level headers exist in both trees at the same path: `array.hpp`,
`dictionary.hpp`, `error.hpp`, `file.hpp`, `hashmap.hpp`, `regex.hpp`,
`runtime.hpp`, `string.hpp`. Today the app's copies win by include ORDER
(CMakeLists.txt:146-149, app root before engine root — the same trick
stats_host uses, stats_host/CMakeLists.txt:33-37). Once both live in one tree
that ordering has nothing left to disambiguate, so each name must resolve to
exactly one file. **Keep the app's**, which are supersets where they differ —
`phon/error.hpp` in particular carries the formatted `error(...)` helpers that
the whole analysis layer throws, and the engine's version does not (it exposes
RuntimeError/SyntaxError instead; the app's forwarder pulls those in). Three
engine-only facades (`list.hpp`, `set.hpp`, `table.hpp`) simply move in.
Delete deliberately, one at a time, then force a clean full rebuild of both
configurations — this is exactly the failure mode where the compiler silently
sees a different header than you think it does.

**3. Bring the non-source assets across.** `phon_flags` (INTERFACE target
carrying the warning/sanitizer flags), `phon_unit_tests` + `test/unit`
(35 files), `test/golden` (ast + disasm), `test/scripts`, `phon_repl` +
`examples/`, `phon_bench` + `bench/`, `tools/unicode` (generator + the UAX #29
conformance data referenced by `PHON_UNICODE_DATA_DIR`), plus `DEVIATIONS.md`,
`design/{architecture,design,references}.md` and the engine's `CLAUDE.md`
(this repo has none, so it lands at the root uncontested).

**4. Keep the engine buildable in isolation — the gate that A8 endangers.**
A7 gate 1 is "engine unit suite ×3 (normal/ASan/TSan)", cheap today because the
engine builds alone. Nested naively it stops being cheap: `PHON_SANITIZE` flows
through `phon_flags` → `phon_engine` (PUBLIC) → `phon-app`, so an ASan run would
instrument Qt, whisper, sndfile, REAPER and SPTK to test the VM. Requirement:
`-DWITH_APPLICATION=OFF -DWITH_GUI=OFF` must configure and build `phon_engine`
+ `phon_unit_tests` alone, with `PHON_SANITIZE`/`PHON_TSAN`/`PHON_WERROR`
surviving as options. If that regresses, the sanitizer gate becomes too slow to
run, and a gate too slow to run is a gate that stops being run.

**5. Retire the seam.** Drop `PHON_ENGINE_DIR` and its FATAL_ERROR guard
(CMakeLists.txt:46-49), the second `include_directories` (CMakeLists.txt:149),
and the `${PHON_ENGINE_DIR}` entry in stats_host's include list
(stats_host/CMakeLists.txt:36) — with its include-order comment rewritten,
since "repository root then engine" collapses to one root. Sweep the stale
`PHON_ENGINE_DIR` mentions in MIGRATION_NOTES (lines 54, 956, 1563, 1593, 1675)
and any `-DPHON_ENGINE_DIR=…` in build instructions. Update the vendored-PCRE2
block (CMakeLists.txt:29-40): with the engine in-tree, the cache pre-seeding
that makes its `find_library` bind to the vendored target can become a direct
`target_link_libraries(phon_engine PUBLIC pcre2-8)`, which is what the seeding
is emulating.

**Gates:** the full A7 list, plus (a) `phon_unit_tests` 407/0 in all three
configurations built FROM THIS REPOSITORY, (b) an engine-only configure
(`-DWITH_APPLICATION=OFF -DWITH_GUI=OFF`) that does not require Qt/sndfile,
(c) `phon_repl` still runs `examples/demo.phon` and `--workers`, and (d) the
project-XML byte-compat diff against a pre-A8 binary — a header-resolution
mistake in step 2 is exactly the kind of thing that shows up as changed
serialization rather than as a compile error.

**Sequencing note:** close A7 BEFORE starting A8. While the engine is still a
separate repo with its own suite, "engine bug" and "app bug" are separable by
construction, and A8 is the step most likely to need that separation.

---

**Suggested sequencing:** P0 (commit engine untracked files) → E4 → A1 → A2 →
A3 (subsystem by subsystem), with E1/E2/E3 landing anytime before A4 and E5's
decisions made before A3 starts (they affect native names) → A4 → A5 → A6 →
A7 → A8. E-items are parallel to A1. Run the A7 gates at every step boundary;
anything that can't be gated directly gets a pre-change-baseline comparison
build (the step-5 pattern: `git worktree add <scratch> <commit>` + build +
diff behavior).
