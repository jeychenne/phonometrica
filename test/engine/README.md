# Engine regression tests

Pure-phon tests of the scripting language. Each test exercises one
slice of the parser/compiler/runtime and aborts on the first failed
`assert` with a clear message and line number.

## Running

```
phonometrica run_all.phon
```

or an individual test:

```
phonometrica test_compound_assignment.phon
```

## Tests

| File                              | What it covers                                                                 |
|-----------------------------------|--------------------------------------------------------------------------------|
| `test_scientific_notation.phon`   | Numeric literals: `1e10`, `1.5e-3`, signed exponents, underscore separators    |
| `test_compound_assignment.phon`   | `m.field += value` etc. on Module fields, locals, Tables — incl. closures      |
| `test_upvalues.phon`              | Upvalue capture across `local function`, mutating natives (`append`)           |
| `test_try_catch.phon`             | `try`/`catch`, throw/rethrow, cross-frame, return/break inside try             |
| `test_try_catch_loops.phon`       | Stress test: break/continue × try/catch handler-stack accounting               |

## Adding a new test

1. Drop a new `test_<feature>.phon` in this directory.
2. Open with a banner comment explaining the feature and what would
   regress. Use `print "--- N. <description> ---"` between blocks so
   a failure localises clearly.
3. Use `assert <expr>, "<message>"` for checks. The script aborts on
   the first failed assert.
4. Add an `import("test_<feature>")` line to `run_all.phon`.

There's no shared assertion module here — engine tests are
deliberately small, self-contained, and free-standing. The
statistics suite uses `import` + a shared `Module` for its harness;
the engine tests don't, so a regression in `import` itself doesn't
prevent the other tests from running.
