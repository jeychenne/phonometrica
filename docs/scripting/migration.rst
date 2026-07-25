.. _page-migration:

Migrating old scripts
=====================

Phonometrica's scripting engine was rewritten from the ground up. The new
language keeps the overall look and feel of the old one — newline-terminated
statements, ``end``-delimited blocks, 1-based indexing — but a number of
constructs changed, some silently. This page lists everything you need to know
to update scripts written for the old engine. Each entry shows the old form and
its replacement.

The single most important change is that **names are now resolved when a script
is compiled, not when it runs**: calling an unknown function or reading an
undeclared variable is an error *before* the first statement executes. As a
result, most broken scripts fail immediately and loudly rather than midway
through an analysis.


Declarations and scope
----------------------

**Use** ``var`` **instead of** ``let``, **and declare before you assign.**
Assignment never declares a variable in a script file: assigning to an unknown
name is a compile error. (In the console, bare assignment still creates a
session variable, as before.)

.. code:: phon

    # Old                          # New
    let x = 10                     var x = 10
    y = 20   # created y           var y = 20   # required in a script

``const`` declares an immutable binding. At the top level of a script, ``var``
creates a module binding (visible to code that imports the module), ``local
var`` a module-private one, and ``global var`` an isolate-global. ``local``
also applies to top-level ``function`` and ``class`` declarations.

**Multiple assignment and destructuring are gone.** Declare one name at a time.
The only multi-binding form is ``for k, v in table``.


Control flow
------------

**Use** ``for ... in`` **instead of** ``foreach``, **and** ``step -1``
**instead of** ``downto``:

.. code:: phon

    # Old                                # New
    foreach x in xs do                   for x in xs do
        print x                              print(x)
    end                                  end

    for i = 10 downto 1 do               for i = 10 to 1 step -1 do
        ...                                  ...
    end                                  end

Counted loops (``for i = a to b [step s]``) have inclusive bounds; a loop whose
direction contradicts its step runs zero times. ``for k, v in table`` iterates
key/value pairs.

**List comprehensions** follow the same rule — they are written with ``for``,
not ``foreach``, so they read like the loop statement they replace:

.. code:: phon

    # Old                                # New
    [y foreach x in xs]                  [y for x in xs]
    [y foreach x in xs if c]             [y for x in xs if c]
    [k & v foreach k, v in t]            [k & v for k, v in t]

An ``if cond`` clause filters, so the yield expression is not evaluated on a
rejected iteration. ``if cond else other`` yields on every iteration instead,
so the result keeps the length of the collection. The two-variable form gives
key/value pairs over a table and index/value pairs over a list.

One further difference: the collection accepts any expression without
parentheses. The old engine required ``[y foreach x in (xs if c else zs)]``
because its conditional was a postfix ``if``, which would otherwise have
swallowed the comprehension's own filter; this engine's conditional is the
prefix ``if c then a else b end``, so the ambiguity does not arise.

**Truthiness**: ``null`` is the only non-boolean value that counts as false.
``0``, ``""`` and ``[]`` are all true. The common idiom ``if sound then`` for
functions that return an object or ``null`` keeps working.


Strings
-------

**Interpolation is** ``{expr}`` **instead of** ``${expr}``, and only
double-quoted strings interpolate. **Single-quoted strings are raw**: no
interpolation and no escape processing, which makes them ideal for regular
expressions and Windows paths. Escape a literal brace in a double-quoted string
as ``\{``; unknown escape sequences are errors.

.. code:: phon

    # Old                                # New
    print "value: ${x}"                  print("value: {x}")
    let pat = "\\d+"                     var pat = '\d+'

**String mutators work in place and return nothing.** Functions such as
``trim``, ``rtrim``, ``ltrim`` and ``append`` modify their first argument
directly (it is a ``ref`` parameter). Old code that used their return value
must be restructured:

.. code:: phon

    # Old                                # New
    let s = trim(line)                   var s = line
                                         trim(s)


Printing
--------

``print`` **is a regular function**, not a statement. Its arguments are
separated by a single space by default; use string interpolation or
concatenation (``&``) to control the output precisely.

.. code:: phon

    # Old                                # New
    print "F0: ", f0, " Hz"              print("F0: {f0} Hz")


Functions
---------

Function declaration syntax is unchanged, but the semantics are richer: every
named function is a *generic function*, and declaring two functions with the
same name and different parameter types adds *overloads* selected by the
argument types at each call.

- Optional parameters with defaults are **keyword-only**: a parameter declared
  ``floor as Float = 70`` can only be filled as ``pitch(snd, floor = 50)``,
  never positionally.
- Anonymous functions: ``function (x) ... end``, or the lambda arrow for a
  single expression: ``x -> x * 2``.
- Named functions are first-class: they can be stored in variables and passed
  to functions such as ``connect``.
- By-reference parameters are declared with ``ref`` in the function signature
  (``function normalize(ref x as Array)``); arguments are passed normally at
  the call site (``normalize(samples)``).


Classes
-------

Field declarations now use the ``field`` keyword, and the constructor is named
``init`` (it was ``initialize``):

.. code:: phon

    # Old                                # New
    class Point                          class Point
        x = 0                                field x = 0
        y = 0                                field y = 0
        function initialize(x, y)            method init(x as Number, y as Number)
            this.x = x                           this.x = x
            this.y = y                           this.y = y
        end                                  end
    end                                  end

Class bodies contain only fields and a closed set of ``method`` hooks
(``init``, ``to_string``, ``get_item``, ``set_item``, ``iterate``, ``next``);
all other behaviour is written as ordinary functions taking the instance as
their first parameter. ``class`` declares a value class (copy-on-write value
semantics); ``ref class`` declares a reference class with identity semantics.
Use ``class Sub is Base`` for single inheritance (it was ``inherits``).

**Equality between class instances is identity-based**: two independently
constructed instances with equal field values compare unequal, for both value
and reference classes. (The old engine compared value classes structurally.)
Test dynamic types with the ``is`` operator: ``x is List`` replaces
``type(x) == type([])``.


Errors
------

**Only** ``Error`` **values can be thrown**, and a caught error is an object,
not a string:

.. code:: phon

    # Old                                # New
    throw "bad input"                    throw Error("bad input")

    catch e do                           catch e
        print e                              print(e.message)
    end                                  end

An ``Error`` carries ``message`` (the text), ``trace`` (a formatted backtrace
string) and ``frames`` (a list of ``{function, line, file}`` tables). There is
no ``rethrow``: ``throw e`` inside a ``catch`` block preserves the original
backtrace.

Two arithmetic changes in the same spirit: float division by zero yields
``inf`` instead of raising an error, while integer division by zero (``1 div
0``) raises a math error. The ``%`` operator no longer exists; use ``mod``.


Modules and imports
-------------------

``import`` **is a compile-time statement**, not a function:

.. code:: phon

    # Old                                # New
    let M = import("../lib/mytools")     import mytools

- Imports are resolved by module *name* on the module search path, not by
  relative path expression.
- A missing module is a compile error; you cannot wrap ``import`` in
  ``try``/``catch``.
- The top-level code of an imported module runs *before* the importing
  script's own statements.
- First-class module values are gone (``Module("name")`` no longer exists).
  A module's public ``var``/``const`` bindings are accessed qualified
  (``mytools.x``); its public functions become globally visible generic
  functions.
- Top-level helpers that should *not* be visible to other modules must be
  declared ``local function`` (this is what replaces private module state).


Renamed and changed functions
-----------------------------

===============================  ====================================================================
Old                              New
===============================  ====================================================================
``dump_json(v)``                 ``to_json(v)``
``load_json(path)``              ``from_json(read_file(path))`` — parses JSON; never evaluates code
``File(path, mode)``             ``File(path, mode)`` — unchanged; ``open_file(path, mode)`` is an
                                 alias (``open`` alone is a reserved keyword)
``str(x)``                       ``to_string(x)``
``int(s)``                       ``to_int(s)``
``type(x) == SomeClass``         ``x is SomeClass``
``clear()`` (console)            ``clear_console()`` — ``clear(x)`` now empties a list, table,
                                 array or set
``append(table, col, name)``     ``add_column(table, col, name)`` (DataTable)
``phon.project.open(path)``      ``phon.project.load(path)``
``slice(s, from, count)``        ``slice(s, from, to)`` — the third argument is now an **end
                                 position**, not a count
===============================  ====================================================================

Behavioural changes to keep in mind:

- **Regular expressions** use a stateless API: ``Regex(pattern[, flags])``
  builds a pattern, ``match(re, subject)`` returns a ``Match`` object or
  ``null``, and ``group(m, i)``, ``group_start``, ``group_end`` and
  ``group_count`` inspect it. ``group_count`` **includes group 0** (the whole
  match), unlike the old ``count()``.
- ``len`` applies to lists, strings, tables, sets and arrays (element count);
  use ``nrow``/``ncol``/``ndim`` for array shapes.
- ``Table`` and ``Set`` are **unordered**: iteration and key order are
  unspecified. Sort keys explicitly when order matters.
- ``sorted_find`` returns 0 when the value is absent (it used to return the
  insertion slot).
- ``intersect``, ``unite`` and ``subtract`` on lists no longer require sorted
  inputs.
- ``min``/``max`` on an empty array raise an error.
- Numbers distinguish ``Integer`` and ``Float`` (``1`` vs ``1.0``); ``_``
  digit separators and scientific notation are supported. Float literals need
  a digit after the decimal point (write ``2.0``, not ``2.``).
- Compound assignment (``+=`` and friends) works on variables and subscripts
  (``xs[i] += 1``), but **not on fields**: ``obj.f += 1`` (including
  ``table.key += 1``) is a compile error — write ``obj.f = obj.f + 1``.


Getting help
------------

If a script fails after migration, run it from a terminal::

    phonometrica -r my_script.phon

Compile-time errors point at the offending line; runtime errors print the full
call-stack trace. The same trace appears in the console inside Phonometrica,
and the script editor highlights the failing line.
