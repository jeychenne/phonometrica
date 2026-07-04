# Phonometrica Scripting Engine — Design Document

*Working draft — summarizes design decisions to date.*

## 1. Goals and Constraints

A high-performance, embeddable scripting engine in C++ for Phonometrica (phonetic
analysis software). The target audience is phoneticians and speech scientists familiar
with Praat scripting, MATLAB, R, and some Python.

Design constraints, fixed from the outset:

- Single-inheritance object model; all types derive from `Object`.
- Multiple dispatch: no methods on objects; generic functions with runtime type
  resolution select the correct overload.
- Designed from the ground up for multi-threading.
- Distinction between value types (Boolean, Integer, Float, String, List, Map, Array, …)
  and reference types (e.g. File), with the ability to pass value types by reference
  when needed.
- A specialized multi-dimensional numeric array type for fast numerical processing.
- NaN-tagging for value representation.
- 1-based indexing.

## 2. Threading Model

**Thread-confined heaps with message passing.** Each script thread owns its heap.
No script-visible shared mutable state.

- Values cross threads only through channels, by copy or by move.
- Within a thread, reference counts are plain non-atomic integers; the interpreter
  hot path contains no locks and no atomic operations.
- Large numeric arrays have a **freeze-and-share** path: an immutable (frozen)
  array's buffer is shared zero-copy across threads, with a single atomic refcount
  on the buffer object only — never on individual values.
- Fine-grained data parallelism lives **inside the runtime**, not in script-visible
  threads: a C++ thread pool executes `parallel_map` / elementwise array kernels on
  raw buffers above a size threshold. Script-level threads with channels cover
  coarse-grained parallelism (background analysis, batch jobs).

Copy-on-write uniqueness checks are exact (not racy) under thread confinement.

## 3. Memory Management

**Non-atomic reference counting with a Bacon–Rajan-style cycle collector** running
at safepoints. Rationale:

- Deterministic destruction: a `File` closes when its last reference dies; clean
  C++ interop with RAII, no finalizer complications.
- RC is what makes copy-on-write value semantics cheap: `refcount == 1` implies
  unique ownership, so mutation happens in place; otherwise clone. This single
  check is the entire implementation of value-type mutation.
- Naturally incremental pauses, appropriate for a GUI-embedded engine.

Supporting measures:

- Size-segregated thread-local free lists for allocation throughput.
- The bytecode compiler elides refcount operations on borrowed locals by tracking
  ownership; most locals never incur retain/release.

**Cell header:** 8 bytes — 32-bit class ID, 32-bit refcount with a few stolen flag
bits (cycle-candidate, frozen), followed by the payload.

## 4. Value Representation

**NaN-boxing on 64 bits, favoring doubles.** Doubles are stored as-is; all other
values live in the quiet-NaN space (sign bit + 3 tag bits + 48-bit payload).

Tags: null, boolean, small integer, cell pointer, interned symbol (32-bit index
into a global atom table; symbols are hot in dispatch and as field/enum names).

Details:

- **Pointers:** 48-bit raw pointers are no longer future-proof (Intel LAM,
  ARM 52/56-bit VA). All cells are allocated through the engine's own arenas;
  payloads store offsets from a heap base (or low-address allocations are
  requested), never raw high-half pointers.
- **Integers:** inline integers are **47-bit**. Sample counts and byte offsets in
  long audio files exceed 2^31; 2^47 covers all realistic indexing. Integer
  overflow is a runtime **error** — no silent promotion to double.
- **Division:** `/` always produces a Float; `div` and `mod` provide integer
  division and remainder.
- The both-operands-are-doubles fast path for arithmetic is a two-instruction
  NaN-mask test, which is the main payoff of this representation.

## 5. Type System

Single-inheritance hierarchy rooted at `Object`.

- **Class IDs by pre-order traversal:** each class owns a contiguous interval
  `[id, max_subclass_id]`; every subtype test is two integer comparisons
  (branch-free if desired). This makes `is` tests and slow-path dispatch cheap.
- **Value classes vs. reference classes.** `class` defines a value type
  (the default); `ref class` defines a reference type. Value types have
  copy-on-write value semantics; reference types have identity and are always
  passed by reference.
- **Classes are first-class values.** `Float`, `Sound`, etc. are expressions
  denoting singleton class objects. Each class has its own metaclass ID, so class
  objects participate in dispatch (this is what makes `cast` extensible, §7).
- **Sealed by default.** Classes and generic functions are sealed at the end of
  module load; `open class` / `open function` opt in to later extension. Sealed
  generics with known method sets allow devirtualization to direct calls; a
  per-generic generation counter invalidates inline caches when an open generic
  gains a method.
- **Errors** derive from an `Error` base class (see §12).

## 6. Generic Functions and Multiple Dispatch

All behavior lives in generic functions. Defining a function with an existing name
and a new signature adds a method to that generic.

Dispatch pipeline, fast to slow:

1. **Per-call-site inline cache:** the last 1–4 argument-class-ID tuples with
   resolved method pointers. Monomorphic sites (the common case) dispatch in a
   couple of compares. Caches are per-thread by construction.
2. **Flat/compressed tables:** arity-1 and arity-2 generics (arithmetic, indexing,
   printing — the overwhelming majority) use tables indexed by class ID; higher
   arities hash on the ID tuple.
3. **Full resolution:** most-specific applicable method computed via interval
   subtype checks, memoized into the tables.

Policies:

- **Ambiguity is detected at method-definition time**, not call time. With single
  inheritance, specificity is a product order over linear chains; the user must
  write the disambiguating overload immediately.
- Arithmetic and indexing are generic functions semantically, but the VM emits
  type-specialized opcodes (`ADD`, etc.) with inlined double/double fast paths,
  falling back to generic dispatch otherwise.
- Default and named arguments are compiled as trailing options and **do not
  participate in dispatch**, keeping method tables small.
- `ref`-ness participates in **applicability** (an rvalue never matches a `ref`
  parameter) but not in specificity ranking.

## 7. Value Semantics, `ref`, and Casting

**Value types** (String, List, Map, Array, …) are heap cells with value semantics:
assignment and argument passing bump a refcount; mutation checks uniqueness
(`refcount == 1`) and clones if shared.

**Second-class references.** `ref` is required at both the parameter declaration
and the call site. References cannot be stored in data structures, captured by
closures, or returned. A reference is therefore implementable as a raw pointer to
a register slot in the caller's frame — no boxing, no write barriers, no escape
analysis.

- Call-site `ref` keeps the calling convention uniform under multiple dispatch
  (the compiler emits a register reference without first resolving the overload)
  and guarantees that any mutation of a caller's variable is visible at the call
  site.
- `ref` is only meaningful for value types. The compiler rejects `ref` on a
  parameter whose declared type is a reference class (it would be a silent no-op).
- Most in-place mutation needs no `ref` at all: compound assignment (`s &= ".txt"`),
  indexed assignment (`a[i] = 0`), and masked updates (`x[x > 0] *= 2`) are lowered
  directly to CoW-checked in-place operations.

**Casting.** `cast x as T` is the only conversion syntax; it lowers to a call to
the generic function `cast(x, T)`, dispatching on the singleton class object `T`.
Users extend casting by defining methods on `cast`. Failure throws; a library
`try_cast(x, T)` returns null instead. On reference types, `cast` is a checked,
identity-preserving downcast; on value types it is a conversion producing a new
value.

## 8. Strings

- UTF-8 storage; **indexing by code point**, 1-based.
- A lazy breadcrumb index (byte offset of every Nth code point) is built on first
  random access, so sequential scans never pay for it.
- Grapheme-cluster iteration (needed constantly for IPA with combining diacritics)
  is provided as library functions; indexing remains code-point-based and this is
  documented prominently.

## 9. The Array Type

The numeric workhorse, designed for signal processing:

- Header: rank + dims + strides + flags; separate refcounted **buffer** object, so
  slices are views sharing the buffer. 64-byte-aligned allocation (AVX-512).
- **Column-major** layout: matches the MATLAB/Praat mental model and gives free
  interop with LAPACK/FFTW conventions.
- Initial element type: `double` only, plus a boolean mask type for logical
  indexing. Additional dtypes (complex, etc.) deferred; each dtype multiplies the
  overload surface and SIMD kernel count.
- Elementwise operations are eager, SIMD-vectorized C++ kernels. A fused
  `map`-style primitive handles common broadcast expressions (`a*2 + b`) without
  intermediate temporaries. The internal thread pool parallelizes above a size
  threshold.
- **1-based indexing is a boundary concern:** index opcodes subtract 1, and the
  compiler hoists/folds the adjustment in counted loops, so inner loops pay zero
  cost.
- Ranges are **inclusive on both ends** (`1..n` has n elements). Negative indices
  count from the end (`-1` is the last element) and compose with slicing.

## 10. Interpreter Architecture

- **Register-based bytecode**, Lua-style: fixed 32-bit instructions, 8-bit opcode,
  contiguous register frames on a contiguous stack.
- Computed-goto dispatch; each opcode body written as a self-contained unit,
  keeping the door open to a copy-and-patch template JIT later without bytecode
  redesign.
- Shallow compiler pipeline: parse → AST → single lowering pass with constant
  folding, register allocation for locals, and opcode specialization. No heavy IR:
  the performance wins are in value representation, dispatch caching, and array
  kernels.
- Notable lowerings:
  - String interpolation compiles to a variadic `CONCAT n` opcode (one buffer,
    no intermediate strings).
  - `for x in a..b` lowers to a counted loop.
  - `list += [expr]` with a literal one-element list lowers to a direct append.

## 11. Syntax

### Lexical rules

- Statements are newline-terminated. **A line continues iff it ends with an
  operator, a comma, or an opening bracket.** No backslash continuations.
- Comments: `#` to end of line; `#* ... *#` for blocks. Shebang lines work.
- Numeric literals: `_` separators and scientific notation allowed; `1` is an
  Integer, `1.0` a Float.
- Two string literal forms: double quotes interpolate with `{expr}`; single
  quotes are raw (regexes, Windows paths).

```
var msg = "Analyzing {path}: {n} intervals found"
var pat = '\b[aeiou]+\b'
```

### Declarations and annotations

`var` declares, `const` declares an immutable binding. Type annotations use `as`
and are optional everywhere; an unannotated name is `Object`. `as` is
**declarative only** — it never appears as an expression operator (conversion is
`cast … as`, §7; type testing is `is`).

```
var threshold as Float = 0.025
const SR as Integer = 16000
```

### Control flow

```
if f0 > 0 then
    count += 1
elsif voiced then
    interpolate(track, i)
else
    count = 0
end

while not eof(file) do
    process(read_line(file))
end

repeat
    line = read_line(file)
until line == ""

for i in 1..len(samples) do      # single loop form: iteration
    total += samples[i]
end

for interval in tier do          # anything with an iterate method
    print(interval.text)
end
```

Ranges: `a..b` (inclusive), strides with `by` (`1..n by 2`), open ends in slices
(`2..`, `..5`), bare `..` for "everything along this dimension".

### Functions

```
function duration(i as Interval) as Float
    return i.xmax - i.xmin
end

function duration(s as Sound) as Float      # adds an overload to the generic
    return dim(s.data, 1) / s.rate
end
```

- Named arguments with defaults (do not participate in dispatch):

```
function pitch(s as Sound, floor as Float = 70, ceiling as Float = 500,
               step as Float = 0.01)
    ...
end

var track = pitch(snd, ceiling = 300)
```

- Anonymous functions: full form `function(x) return x^2 end`; thin-arrow lambdas
  for single expressions: `map(x -> x * 2, values)`. `->` is exclusively the
  lambda arrow.
- **No dot-call sugar.** `play(snd)`, never `snd.play()`. Verb-first calling is
  uniform; no argument is syntactically privileged.
- By-reference parameters and calls:

```
function normalize(ref x as Array)
    x /= max(abs(x))
end

normalize(ref samples)      # mutation visible at the call site
```

### Classes

Class bodies contain **only state** — fields and constructors; behavior lives in
generic functions. `is` declares single inheritance. `this` exists only inside
constructors. Constructing is calling the class name; constructor overloads
participate in ordinary dispatch (so a copy constructor is just another method).

```
class Interval                       # value class (default)

    field xmin as Float = 0
    field xmax as Float = 0
    field text as String = ""

    constructor(xmin as Float, xmax as Float, text as String)
        this.xmin = xmin
        this.xmax = xmax
        this.text = text
    end
end

ref class File                       # reference class: identity semantics
    ...
end

class PointTier is Tier              # single inheritance
    field points as List = []
end
```

Classes and generics are sealed after module load by default; `open class` /
`open function` opt in to extension.

### Operators

| Operator | Meaning |
|---|---|
| `+ - * /` | Arithmetic; `/` always yields Float |
| `^` | Exponentiation |
| `div`, `mod` | Integer division, remainder |
| `== != < <= > >=` | Comparison |
| `and or not` | Boolean logic (keywords) |
| `&` | Concatenation **with implicit stringification** of operands |
| `+` (sequences) | List concatenation — no implicit conversion |
| `..`, `by` | Ranges and strides |
| `is` | Runtime type test (expression) |
| `cast … as` | Checked conversion (expression) |
| `+= -= *= /= &=` | Compound assignment (lowered to in-place mutation via CoW) |

Principles:

- **`&` is the only implicitly-converting operator in the language.** It lowers
  through the `String(x)` constructor generic, so any user type becomes `&`-able
  by defining one overload. Chains compile to a single variadic `CONCAT`.
  `&` binds below arithmetic: `"total: " & n + 1` means `"total: " & (n + 1)`.
- `+` never converts: `"3" + 1` is a type error. `list + list` concatenates.
- No `+` on maps; merging is a named function with an explicit collision policy.
- No bitwise operators in the grammar; `band`, `bor`, `shl`, … are library
  functions.
- Operator overloading is defining a method on the operator's generic:

```
function +(a as Fraction, b as Fraction) as Fraction
    return Fraction(a.num * b.den + b.num * a.den, a.den * b.den)
end
```

### Indexing and slicing

1-based, inclusive ranges, negative indices from the end:

```
x = a[i]
y = a[2..5]
m = spec[.., 3]                 # whole third column
w = samples[-400..-1]           # last 400 samples
a[1..100 by 2] = 0              # strided assignment
v = track[track > 0]            # logical mask indexing
```

Map literals use `:` (freed by the move to `as`):

```
var codes = {"vowel": 1, "consonant": 2}
```

### Printing

`print` is an ordinary generic function with named options — no statement-level
special case (a trailing-comma rule would collide with the line-continuation
rule):

```
print(x)                        # ends with newline (end = "\n" default)
print(x, end = "")              # suppress the newline
print(a, b, c, sep = ", ")
```

### Error handling

Errors derive from the `Error` base class. `catch` clauses select on type using
`as`, resolved with the same interval subtype check as `is`:

```
try
    var snd = read_sound(path)
catch e as IOError
    print("cannot read {path}: {e.message}")
catch e as Error
    rethrow(e)
finally
    cleanup()
end
```

`throw` raises any `Error` value.

### Concurrency surface

One keyword (`spawn`); channels are ordinary values and `send` / `receive` are
generic functions:

```
var jobs = Channel()
var out  = Channel()

spawn worker(jobs, out)          # runs the call in a fresh thread + heap

for path in files do
    send(jobs, path)             # value types copied; frozen arrays shared
end

var report = receive(out)
```

Runtime-internal data parallelism (`parallel_map` over arrays) needs no syntax.

## 12. Sample Program

```
import textgrid

function mean_f0(s as Sound, i as Interval) as Float
    var chunk = extract(s, i.xmin, i.xmax)
    var track = pitch(chunk, floor = 60)
    var voiced = track[track > 0]
    return if len(voiced) > 0 then mean(voiced) else 0 end
end

var snd = read_sound('vowels.wav')
var tg  = read_textgrid('vowels.TextGrid')

var results = []
for interval in tier(tg, 1) do
    if interval.text != "" then
        results += [mean_f0(snd, interval)]
        print("{interval.text}: {mean_f0(snd, interval)} Hz")
    end
end
sort(ref results)
```

## 13. Design Principles (Recap)

- One meaning per construct: `as` declares, `cast … as` converts, `is` tests,
  `&` stringifies and joins, `+` combines like with like, `ref` marks mutation.
- Mutation of a caller's variable is always visible at the call site.
- Verb-first, uniform call syntax; no privileged first argument.
- Value semantics by default; identity (`ref class`) is opt-in.
- Sealed by default; openness is opt-in.
- Loosening later is compatible; tightening later breaks scripts — start strict.

## 14. Open Items

- Formal EBNF grammar.
- Full method-specificity rules (ranking of `Object`-typed parameters, variadics).
- Standard library naming conventions (effectively part of the language under
  multiple dispatch).
- Concrete NaN-tag bit assignments.
- Module system details (`import` semantics, visibility).
- C++ embedding API: template-based registration of C++ functions with automatic
  unboxing; where embeddability meets dispatch.
