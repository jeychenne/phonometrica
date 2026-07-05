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
- Distinction between value types (Boolean, Integer, Float, String, List, Table, Array, …)
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
- **Errors** derive from an `Error` base class (see §12, Error handling).

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
- **Variadics.** A method may declare one trailing vararg (`name as T...`) after
  its fixed parameters; it is applicable when argc ≥ the fixed count and every
  remaining argument subtypes `T`. Specificity: fixed-arity beats variadic;
  among variadics, more fixed parameters wins; ties compare pointwise, then on
  the vararg element type. Definition-time ambiguity detection extends to these
  comparisons. Varargs cannot be `ref`.
- Default and named arguments are compiled as trailing options and **do not
  participate in dispatch**, keeping method tables small. Options are
  **keyword-only**: positional arguments map exclusively to fixed parameters and
  the vararg, so the parser statically splits every call into a dispatched
  positional part and a named part, and no resolution ambiguity between
  overloads, vararg elements, and option fillers can arise.
- `ref`-ness participates in **applicability** (an rvalue never matches a `ref`
  parameter) but not in specificity ranking.

## 7. Value Semantics, `ref`, and Casting

**Value types** (String, List, Table, Array, …) are heap cells with value semantics:
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
- Slices are **inclusive on both ends** (`a[1:n]` has n elements). Negative
  indices count from the end (`-1` is the last element) and compose with slicing.

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
  - `for i = a to b [step s]` is the counted-loop form; the 1-based index
    adjustment is folded into it.
  - `list += [expr]` with a literal one-element list lowers to a direct append.

## 11. Scopes, Modules, and Execution Surfaces

### Storage tiers

Declaration syntax and binding storage are independent axes. There are three
storage tiers:

- **Function locals.** `var`/`const` inside a function declare register-allocated,
  lexically scoped locals that die with their scope.
- **Module bindings.** Every script file is a **module** with a namespace: a slot
  vector plus a name→slot index. `var`, `const`, `function`, and `class` at the
  top level of a script declare module bindings. These persist for the module's
  lifetime and are **public by default** — visible to importers with no extra
  ceremony (a plain analysis script "just works" as an importable library).
  The `local` modifier makes a top-level binding module-private:
  `local var cache = {}`, `local function helper(…)`, `local class Cursor`.
- **Isolate globals.** One distinguished namespace per Isolate, declared script-side
  with `global var` / `global const` (top level only) and written by the embedder
  via `rt.add_global(…)` (the GUI injecting the current selection, app settings, …).
  "Global" honestly means *per-Isolate*: a spawned isolate starts with a fresh
  global namespace plus whatever the runtime pre-injects. State intended for
  worker scripts travels through spawn arguments or channels, like everything else.

```
local var cache = {}          # module-private
var threshold = 0.025         # module-public (default)
global var session_dir = ""   # isolate-global
```

### Name resolution

**Assignment never declares; it resolves.** The search order is: innermost lexical
scope → enclosing functions (upvalues) → module namespace → isolate globals.
An assignment or read that resolves to nothing is a compile error. Consequences:

- No `global`/`nonlocal`-style statements are needed inside functions; a function
  body can write `total += x` and it resolves outward to the module binding.
- Typo protection is fully preserved in scripts: a misspelled assignment target
  fails to resolve and errors at compile time.
- Compiled access to namespace bindings is **slot-indexed**
  (`GET_MODULE n` / `SET_MODULE n` — an array load, as fast as an upvalue).
  The name→slot hash is consulted only at compile time and by reflective access.
  New declarations append slots; existing slot indices never move. Cross-module
  access via `import` resolves to (module, slot) pairs.

### Visibility rules of note

- **`local function` colliding with a visible generic is a compile error** (with a
  message directing the user to drop `local` to extend the generic, or rename).
  A module-private *method* on a shared generic is not a coherent concept under
  per-generic dispatch tables, and silently shadowing the whole visible method set
  is a footgun. Permitting shadowing later is compatible; retracting it would not be.
- **`local class` privatizes the name, not the type.** Public functions may return
  instances of a private class; callers can hold them and dispatch on them.
  Visibility governs the namespace only.

### Two-pass top level

A module's top level is processed in two passes: the first collects `function`,
`class`, and `var`/`const` *declarations* (so top-level code can call functions
defined later in the file, and mutually recursive functions work); the second
executes statements top to bottom, running initializers in order. Reading a
declared-but-not-yet-initialized binding raises an error (sentinel-checked)
rather than yielding null, turning initialization-order bugs into clear messages.

### Execution surfaces

The engine distinguishes three ways of running code; the module machinery serves
all three, with interactive leniencies strictly gated to interactive mode:

- **REPL / console.** A session is one persistent module (`shell`) whose namespace
  lives as long as the session; each command compiles as a small chunk against
  that live namespace, so persistence across commands is automatic. Interactive
  mode enables three leniencies that never apply to files: (1) bare assignment to
  an unresolved name auto-declares a `shell` binding (reads of unknown names still
  error); (2) redeclaration rebinds instead of erroring (re-running edited lines,
  redefining a function under iteration); (3) a bare expression prints its value
  (with a trailing `;` to suppress, MATLAB-style).
- **Editor: run script.** Each run creates a **fresh module instance** — full
  recompile, new namespace, execute top to bottom — so re-running behaves as a
  first run and edited functions take effect with no stale bindings, matching the
  Praat mental model. Before the new instance loads, the previous instance's
  **registration journal** is retracted (below). After a successful run, the
  script's public namespace is visible from the console as if imported, so users
  can poke at results interactively.
- **Editor: run selection.** Semantically a REPL paste, not a document run:
  executes incrementally against the script's current live instance (or `shell`
  if the script hasn't run), with interactive leniencies active.

### Registration journal (reload and unload)

A module instance's effects are not confined to its namespace: it may add methods
to public generics it does not own, register classes, or create isolate globals.
Each instance journals these external registrations; re-running a script (or
unloading a plugin — same machinery) retracts the previous journal: added methods
are removed, registered classes are marked dead in the registry, created globals
are dropped, and affected generics' epochs are bumped so inline caches
self-invalidate. For a plain script that defines no classes and touches no
globals, the journal is empty and reload is trivially clean.

One accepted, documented limitation (shared with every reloadable environment):
**live instances of a redefined class keep their old class.** An object created
by run #1 and still alive during run #2 continues to work and finalize correctly,
but `x is MyFilter` against the new class object is false; dispatch failures on
such orphans produce a message identifying the instance as belonging to a
previous definition.

## 12. Syntax

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

`var` declares, `const` declares an immutable binding. Scope follows position
(§11): inside a function, declarations are lexically scoped locals; at the top
level of a script they are module bindings, public by default, with the `local`
modifier for module-private and `global` for isolate-global (top level only).
`local` also applies to top-level `function` and `class`. Assignment never
declares (§11).

Type annotations use `as` and are optional everywhere; an unannotated name is
`Object`. `as` is **declarative only** — it never appears as an expression
operator (conversion is `cast … as`, §7; type testing is `is`).

```
function analyze(s as Sound)
    var threshold as Float = 0.025   # function local (register)
    ...
end

const SR as Integer = 16000          # module binding, public
local var cache = {}                 # module binding, private
global var session_dir = ""          # isolate-global
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

for i = 1 to len(samples) step 2 do   # counted loop: for … = … to … [step …]
    total += samples[i]
end

for interval in tier do               # iteration: for … in … (see Iteration protocol)
    print(interval.text)
end
```

Two `for` forms, distinguished by the token after the loop variable (Lua-style):
the counted form `for i = a to b [step s]` (inclusive bounds; `step` may be
negative; a loop whose direction contradicts its step runs zero times) compiles
to counted-loop opcodes with no object and no protocol; the iteration form
`for x in expr` drives the iteration protocol. `for k, v in table` binds pairs.
There is no first-class Range value; evenly spaced vectors come from library
functions (`seq`, `linspace`), and slice syntax is bracket-only (see Indexing
and slicing).

### Functions

```
function duration(i as Interval) as Float
    return i.xmax - i.xmin
end

function duration(s as Sound) as Float      # adds an overload to the generic
    return dim(s.data, 1) / s.rate
end
```

- Named arguments with defaults (do not participate in dispatch; **keyword-only** —
  they cannot be filled positionally, which keeps dispatch unambiguous and makes
  many-knob analysis calls self-documenting):

```
function pitch(s as Sound, floor as Float = 70, ceiling as Float = 500,
               step as Float = 0.01)
    ...
end

var track = pitch(snd, ceiling = 300)      # pitch(snd, 300) is an error
```

- Variadic parameters: one trailing `name as T...` after the fixed parameters
  and before the options; remaining positional arguments are packed into `name`
  as a List (one freelist allocation, paid only when the resolved method is
  variadic). Call sites can splat a List into positional arguments: `f(xs...)`
  (splat sites resolve through the generic's memo table rather than the inline
  cache, since their argc is dynamic). Signature segments in order:
  fixed parameters, optional vararg, keyword-only options.

```
function print(values as Object..., sep as String = " ", end_line as String = "\n")
function log_run(tag as String, values as Object...)
    print("[" & tag & "]", values...)       # forwarding via splat
end
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

Class bodies contain **state and special methods only**: `field` declarations
and `method` declarations drawn from a **closed set** of protocol hooks. General
behavior lives in generic functions — `method` is not general OO: the compiler
rejects unknown method names with a directed error.

**A `method` is surface syntax for an ordinary generic method** with `this` as
the first parameter: `method to_string()` in `class Fraction` registers
`function to_string(this as Fraction)` on the public `to_string` generic.
There is no second dispatch mechanism. Consequences: the hooks are invoked
through the ordinary functional spelling (`to_string(x)`, `get_item(x, i)`) and
by the constructs that lower through those generics (`&`, `print`, `x[i]`,
`for x in …`); overriding in a subclass is ordinary specificity; delegation is
ordinary calling; inline caches apply unchanged. No sigil marks the names —
being declared with `method` inside a class body *is* the marker, and the names
must stay clean because they appear at ordinary call sites.

The method set: `init` (constructor), `to_string`, `get_item`, `set_item`,
`iterate`, `next`. Reserved for future use (rejected as method and field names
today): `len`, `hash`, `compare`, `finalize`. `this` is available in all method
bodies. Constructing is calling the class name; `init` overloads participate in
ordinary dispatch on the metaclass, so constructor inheritance and copy
constructors need no special rules.

```
class Interval                       # value class (default)

    field xmin as Float = 0
    field xmax as Float = 0
    field text as String = ""

    method init(xmin as Float, xmax as Float, text as String)
        this.xmin = xmin
        this.xmax = xmax
        this.text = text
    end

    method to_string() as String
        return "[{this.xmin}, {this.xmax}] {this.text}"
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

A field declaration may omit its type (defaulting to `Object`) and/or its
initializer: `field xmin as Float`, `field text`, and `field label = ""` are all
valid. A field with **no initializer defaults to `null`** (the annotation is
declarative only — the engine does not synthesize a per-type zero value). A field
with an initializer runs that expression **once per construction**; the
initializer is an ordinary runtime expression (not restricted to constants) and
may reference module bindings, but not `this` or other fields, since the instance
is still being built. Instances are laid out as a fixed-size `Cell` header
followed by one `Value` slot per field, in declaration order with a subclass's own
fields appended after its base's.

Constructing an instance is **calling the class**: `Interval(1, 2, "x")` allocates
an instance, initializes its fields (defaults, then the matching `init` overload),
and yields the instance. `Interval()` with no `init` is the default constructor —
every field takes its default. A value `class` has copy-on-write value semantics
(sharing bumps a refcount; a mutation of a shared instance detaches a private
copy); a `ref class` has identity (mutations are visible through every alias).
`this` inside a method behaves as a `ref` to the receiver, so a method that
mutates `this` writes back to the caller's variable under the same detach rule.

When a class declaration carries several modifiers they are written in a fixed
order — **`local open ref class`** — so the reading is unambiguous: `local`
(namespace visibility) then `open` (sealing) then `ref` (value vs. identity).
Each is independently optional; other orderings are a syntax error. Functions
likewise take `local` then `open` (`local open function`); `ref` applies only to
classes and `global` only to `var`/`const`.

#### Field accessors (`get` / `set`)

A `field` may carry an optional `get` and/or `set` block that intercepts reads and
writes. Reading or writing the field **from outside the class, or from another
method**, routes through the accessor when one is present; a field with no accessor
is a plain storage slot.

```
class Circle
    field radius as Float = 1.0

    field area as Float          # read-only: getter, no setter
        get
            return this.radius * this.radius * 3.14159
        end
    end

    field diameter as Float      # computed read/write
        get
            return this.radius * 2
        end
        set(v as Float)
            this.radius = v / 2
        end
    end

    field count as Integer = 0   # validated storage
        set(v as Integer)
            assert(v >= 0, "count must be non-negative")
            this.count = v        # raw slot here — see the recursion rule
        end
    end
end
```

Rules:

- **Recursion safety.** Inside an accessor's own body, `this.<that same field>` is
  the **raw storage slot**, not the accessor — so a setter can write
  `this.count = v` without recursing. Accesses to *other* fields route normally.
- A getter with **no** setter is **read-only**: writing it from outside is a
  runtime error. (A setter with no getter is likewise write-only.)
- Every field reserves a storage slot even when it is purely computed (uniform
  layout), so a computed field simply leaves its slot unused.
- `get` / `set` are **contextual** — meaningful only in a field body — so they
  remain usable as ordinary identifiers elsewhere.
- Accessors are **inherited**: a subclass inherits its base's fields together with
  their accessors, and dispatch reaches the base's getter/setter for an inherited
  field.

### Iteration protocol

`for x in expr` desugars to:

```
var it = iterate(expr)
var x
while next(it, ref x) do
    ...
end
```

- `iterate(x)` returns an iterator — any value, typically a `ref class`
  (mutable state with identity, so `next` mutates it without `ref`).
- `next(it, ref v)` writes the next element through the ref and returns `true`,
  or returns `false` when exhausted — one dispatch per step, no allocation per
  step, and no reserved sentinel (collections containing `null` iterate
  correctly).
- `for k, v in table` desugars to `next(it, ref k, ref v)`: the loop's variable
  count selects the `next` overload by arity, so pair iteration is not a
  special case and user types may support both shapes.

**By-reference iteration.** The *value* variable may be taken by reference so it
aliases the collection element and writes propagate back into the collection —
the loop-body analogue of a `ref` parameter (§7):

```
for ref sample in samples do    # single value, by reference
    sample *= 2                 # mutates samples in place
end

for i, ref v in list do         # index/value; value by reference
    v += 1
end

for k, ref v in table do        # key/value; value by reference
    v = normalize(v)
end
```

`ref` attaches only to the value: the **key/index is never by reference**
(`for ref k, v in …` and `for ref i = … to …` are syntax errors), since it is a
freshly produced index, not a slot in the collection. By-ref iteration lowers to
the iteration protocol with the value slot bound to the element's storage rather
than to a copy; plain (by-value) iteration is unchanged and remains the default.

Performance is two-tier. The VM's `ITER_INIT`/`ITER_NEXT` opcodes recognize
builtin collections directly and keep iteration state in hidden loop registers —
no iterator object exists and no dispatch occurs. User classes reach builtin
speed by **delegation**: `method iterate()` returning a builtin collection's
iterator (`return iterate(this.items)`) costs one generic call at loop entry,
after which `ITER_NEXT`'s fast path recognizes the builtin iterator class.
Builtin iterators therefore exist as real classes for explicit protocol use,
though direct loops never materialize them. Genuinely custom iterators
(`method next(ref v)` on a user iterator class) pay one monomorphic, IC-cached
call per step.

```
ref class Tier
    field intervals as List = []

    method iterate()
        return iterate(this.intervals)   # loop body runs at builtin speed
    end
end
```

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
| `:`, `step` | Slices — bracket context only (`a[2:5]`, `a[1:n step 2]`, `m[:, 3]`) |
| `is` | Runtime type test (expression) |
| `cast … as` | Checked conversion (expression) |
| `+= -= *= /= &=` | Compound assignment (lowered to in-place mutation via CoW) |

Principles:

- **`&` is the only implicitly-converting operator in the language.** It lowers
  through the `to_string(x)` generic (as do `print` and interpolation), so any
  user type becomes `&`-able by defining one method — `method to_string()` in
  the class body, or a free `function to_string(x as T)`. The `String(x)`
  conversion constructor forwards to `to_string` for objects.
  Chains compile to a single variadic `CONCAT`.
  `&` binds below arithmetic: `"total: " & n + 1` means `"total: " & (n + 1)`.
- `+` never converts: `"3" + 1` is a type error. `list + list` concatenates.
- No `+` on tables; merging is a named function with an explicit collision policy.
- No bitwise operators in the grammar; `band`, `bor`, `shl`, … are library
  functions.
- Operator overloading is defining a method on the operator's generic:

```
function +(a as Fraction, b as Fraction) as Fraction
    return Fraction(a.num * b.den + b.num * a.den, a.den * b.den)
end
```

### Indexing and slicing

1-based, inclusive slice bounds, negative indices from the end. Slices use the
colon, **legal only inside brackets** (so it never collides with the map-literal
colon, which lives inside braces); `1:10` outside brackets is a syntax error
pointing at `seq()`. Strides use `step`, the same word as the counted loop.
The three-part MATLAB/NumPy colon is deliberately rejected: MATLAB reads
`1:2:100` as from:step:to and NumPy reads `1:100:2` as start:stop:step, so a
bare three-part form guarantees silent off-by-stride bugs in this audience.

```
x = a[i]
y = a[2:5]                      # inclusive both ends
m = spec[:, 3]                  # bare colon: everything along the dimension
w = samples[-400:-1]            # last 400 samples
a[1:100 step 2] = 0             # strided assignment
z = a[3:]                       # open end
v = track[track > 0]            # logical mask indexing
```

Table literals use `:` inside braces (freed by the move to `as`):

```
var codes = {"vowel": 1, "consonant": 2}
```

### Printing

`print` is an ordinary generic function — variadic, with keyword-only options —
with no statement-level special case (a trailing-comma rule would collide with
the line-continuation rule):

```
function print(values as Object..., sep as String = " ", end_line as String = "\n")
```

```
print(x)                        # ends with newline
print(x, end_line = "")              # suppress the newline
print(a, b, c, sep = ", ")
```

Each value is stringified through the `to_string` generic, so a user class with
`method to_string()` prints correctly with no further work.

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

## 13. Sample Program

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

## 14. Design Principles (Recap)

- One meaning per construct: `as` declares, `cast … as` converts, `is` tests,
  `&` stringifies and joins, `+` combines like with like, `ref` marks mutation.
- Mutation of a caller's variable is always visible at the call site.
- Verb-first, uniform call syntax; no privileged first argument.
- Value semantics by default; identity (`ref class`) is opt-in.
- Sealed by default; openness is opt-in.
- Declaration is always explicit; assignment never declares (interactive-mode
  leniencies are gated to the REPL and never weaken the language proper).
- Loosening later is compatible; tightening later breaks scripts — start strict.

## 15. Open Items

- Formal EBNF grammar.
- Method-specificity details: ranking of `Object`-typed parameters (variadics
  are now specified in §6).
- Standard library naming conventions (effectively part of the language under
  multiple dispatch).
- Option splatting (forwarding named options wholesale, e.g. `f(opts...)`) is
  deferred; positional splat plus explicit re-passing covers wrappers, and
  adding it later is compatible.
- Concrete NaN-tag bit assignments.
- `import` semantics details (selective import, renaming, qualified access) —
  visibility and storage are settled in §11.
- First-class Range values are deliberately deferred: slice syntax is
  bracket-only and the counted loop is its own form, so `..` is unspent and a
  Range type (constructed by `seq()` or by reintroducing `a..b` as an
  expression) can be added compatibly if a need materializes.
- Iteration over Array (elements? columns?) — decide before M6; masks and
  slices may make explicit element iteration rare.
- Interaction between the registration journal (§11) and sealing: whether an
  editor re-run may re-open a sealed generic it previously extended, or sealing
  is deferred until the owning module is finalized.
- C++ embedding API: template-based registration of C++ functions with automatic
  unboxing; where embeddability meets dispatch.
