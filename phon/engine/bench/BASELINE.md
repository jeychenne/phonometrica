# Benchmark baseline

Recorded results for the microbenchmark suite (architecture §14). Run with:

```
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target phon_bench
./build-release/phon_bench            # whole suite
./build-release/phon_bench --runs 9 fib dispatch   # subset, more repeats
```

Each benchmark runs on a fresh `Runtime` (no module-state carryover); the harness
reports best and median wall-clock over N runs.

## M8 perf pass (2026-07-10)

Machine: AMD Ryzen 7 8745HS (16 threads), Linux, GCC, `-O3 -DNDEBUG`.

| benchmark | work                                        | best (ms) | median (ms) |
|-----------|---------------------------------------------|----------:|------------:|
| fib       | `fib(32)` recursive — ~7M calls, CALLG      |     341   |     345     |
| loops     | 20M-iteration Float accumulator (ADD/MUL)   |     314   |     317     |
| strings   | 2M CONCAT (`&`) with Integer stringify      |     199   |     202     |
| maps      | 400k Table insert + 400k lookup, string keys|     157   |     163     |
| dispatch  | 5M mono + 15M poly generic calls (IC)       |     777   |     784     |
| arrays    | 40× (sqrt+sin+sum+mean) over a 1e6 Array    |     402   |     409     |

Notes:

- **maps** was **O(n²)** before this pass: an indexed store to a module/upvalue-held
  container (`t[k] = v` at module scope) cloned the whole container on every write
  because the binding's slot kept a second reference alive during `SETINDEX`. At
  n = 20 000 this already cost ~35 s and grew quadratically. The compiler now drops the
  binding's own reference just before the in-place store (see `emit_index_unshare`), so
  the container mutates in place — O(1) per write, value semantics preserved (a genuine
  alias still forces the clone). This is the headline fix of the pass.
- **fib**/**dispatch** benefit from the CALLG inline cache now caching the resolved
  `GenericFunction` per call site, skipping the by-name registry lookup on every call.
