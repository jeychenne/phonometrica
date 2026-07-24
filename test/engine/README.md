# Engine regression tests

Pure-phon tests of the scripting language (see `MIGRATION_NOTES.md` at
the repo root for the old→new engine gap log). Each test exercises one
slice of the parser/compiler/runtime and aborts on the first failed
`assert` with a clear message.

## Running

The suite runs through the application binary in script mode. Files run
through `Runtime::do_file`, so imports resolve in the script's own
directory:

```
<build>/phonometrica -r test/engine/run_all.phon
```

or an individual test:

```
<build>/phonometrica -r test/engine/test_compound_assignment.phon
```

The engine's own script runner (`phon_repl`, built from
`phon/engine/examples/repl.cpp`) runs them just as well — it simply
lacks the application natives, which these tests do not use.

## Tests

| File                              | What it covers                                                                  |
|-----------------------------------|---------------------------------------------------------------------------------|
| `test_scientific_notation.phon`   | Numeric literals: `1e10`, `1.5e-3`, signed exponents, underscore separators     |
| `test_string_interpolation.phon`  | `{expr}` interpolation, `\{` escapes, raw single-quoted strings, nesting        |
| `test_compound_assignment.phon`   | `+= -= *= /= &=` on locals, module bindings, upvalues, subscripts               |
| `test_multiple_declaration.phon`  | Single-target equivalents of the REMOVED multi-LHS declaration feature          |
| `test_upvalues.phon`              | Captured-variable mutation across `local function`, mutating natives (`append`) |
| `test_try_catch.phon`             | `try`/`catch`/`finally`, Error values, typed catches, return/break inside try   |
| `test_try_catch_loops.phon`       | Stress test: break/continue × try/catch handler-stack accounting                |
| `test_nested_error_lines.phon`    | `e.trace` reports the inner throw site, not the outer call boundary             |
| `test_error_trace.phon`           | `e.trace` captures the full call stack, innermost-first                         |
| `test_list_comprehension.phon`    | Loop equivalents of the REMOVED comprehension syntax                            |
| `test_class_ref.phon`             | Value classes (copy-on-write) vs `ref` classes (identity)                       |
| `test_base_migration.phon`        | The phase-1 base-class contract: 1-based lists/arrays, stateless Regex/Match    |
| `test_fixed_gaps.phon`            | Pins the engine fixes for the gaps the port found (crashes, Table lib, traces…) |

`test_gui_trace.phon` is a manual smoke test (it deliberately ends
with an uncaught error) and is not part of `run_all.phon`.

## Adding a new test

1. Drop a new `test_<feature>.phon` in this directory.
2. Open with a banner comment explaining the feature and what would
   regress. Use `print("--- N. <description> ---")` between blocks so
   a failure localises clearly.
3. Use `assert(<expr>, "<message>")` for checks. The script aborts on
   the first failed assert.
4. Declare helpers with `local function` so they stay module-private
   (public top-level functions become process-global generic methods
   and could collide across test modules).
5. Add an `import test_<feature>` line to `run_all.phon`.

There's no shared assertion module here — engine tests are
deliberately small, self-contained, and free-standing.
