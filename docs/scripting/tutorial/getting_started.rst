Getting started
===============


Phonometrica's scripting language was designed to be simple to use yet powerful. It bears many similarities with, and draws inspiration from,
existing scripting languages such as Python, Lua, MATLAB and R. Familiarity with any of these languages will certainly be helpful, but is not required
and the documentation does not assume that the reader has any experience in programming.


Fundamental notions
-------------------

The 'print' function
~~~~~~~~~~~~~~~~~~~~

The first thing one is usually taught when one learns a new language is how to display
the text "hello world!". There is no reason for us to break with tradition... The following piece of code illustrates
how to achieve this using the ``print`` function, which prints text to Phonometrica's console. The statement is preceded by one line of comment. Comments start with
the symbol ``#`` and end at the end of the line.

.. code:: phon

    # My first script
    print("hello world!")


Comments can also follow statements, so we could also write something like this:


.. code:: phon

    print("hello world!") # My first script


To print several values at once on the same line, you can pass them as separate arguments, separated by commas:

.. code:: phon

    print("h", "e", "l", "l", "o")

``print`` separates its arguments with a single space and appends a new line character at the end. To control the output more
precisely, build the string yourself using string interpolation or the concatenation operator ``&``, both of which are presented below.


**Note about comments**: Although comments can be helpful, we recommend to use them sparingly. In general, a comment should describe *what* the code does or *why*
something non-trivial is done in a certain way, not *how* things are done.



Variables
~~~~~~~~~

All programming languages allow you to store and refer to values using *variables*. A variable must start with a letter (upper case or lower case)
and can be followed by any letter, any digit or the symbol ``_``. The name of variables is case-sensitive, which means that ``myvar`` and ``Myvar``
are treated as two different variables. To create a new variable, declare it with the keyword ``var`` and give it a value using the assignment operator ``=``:

.. code:: phon

    var x = 5
    print(x)


In this example, the variable ``x`` is declared and simultaneously assigned the value 5 (an ``Integer``). Note that assignment alone never creates
a variable in a script: assigning to a name that has not been declared is a compile-time error, which protects you against typos. (In Phonometrica's
console, bare assignment still creates a session variable, which is convenient for interactive work.) Because Phonometrica's
scripting language is dynamic, variables can be bound to values of any type. In the following example, ``x`` is first declared as an integer, and is subsequently used to store a string:


.. code:: phon

    var x = 5
    print(x) # prints "5"
    x = "hello"
    print(x) # prints "hello"


If a variable should never be reassigned, declare it with ``const`` instead of ``var``:

.. code:: phon

    const SR = 16000



Built-in data types
-------------------


Null
~~~~

The ``Null`` type is a special type that has only one value, namely ``null`` (in lower case). It is used to represent an invalid value.


Boolean
~~~~~~~

A ``Boolean`` can take on two values: ``false`` and ``true``. Boolean values are used to express truth conditions about the state of a program. A condition in a control structure may evaluate to any value, but only two values are interpreted as false: ``false`` itself and ``null``. All other values — including ``0``, the empty string and the empty list — are interpreted as true. This makes the common idiom ``if sound then`` work for functions that return an object or ``null``.


Integer
~~~~~~~

An ``Integer`` represents a whole number, which can be positive or negative (e.g. ``0``, ``1``, ``-12``). Internally, integers are represented as an
integral number whose size is equal to a machine word. This means that on modern 64 bit machines, an integer occupies 64 bits (or 8 bytes) and its
value can range from -9223372036854775808 to 9223372036854775807. Note that the division operator ``/`` always yields a ``Float``, even when both of
its operands are integers; use ``div`` if you need integer division.


Float
~~~~~

The ``Float`` type is used to represent real numbers, such as ``3.1``, ``-153.9583`` or ``7.0``. Real numbers are represented as double-precision floating point numbers,
which use 64 bits (8 bytes).

There is a special float called "nan" ("not a number"), which represents an invalid numeric value. This is the value that you get when you try to measure pitch in an unvoiced part of the speech signal, for instance.

Note that the decimal point is always represented by the symbol ``.`` (dot), even if the language of your operating system uses a different symbol (some languages, such as French, use a comma instead). A float literal must have at least one digit after the decimal point: write ``2.0``, not ``2.``. Scientific notation is also supported (e.g. ``1.5e-3``).


Number
~~~~~~

``Number`` is an abstract numeric type, which is the base type for ``Integer`` and ``Float``. Some functions specifically request integers or floats as their arguments,
while others accept both; in the latter case, the type of the argument(s) is usually ``Number``, which is compatible with both ``Integer`` and ``Float``.

Phonometrica lets you use the underscore ``_`` as a separator for thousands to improve readability. For example, you could write ``1_000_000`` instead of ``1000000``, or
``0.000_001`` instead of ``0.000001``.

String
~~~~~~

A ``String`` represents an ordered sequence of characters. Characters are understood as "extended grapheme clusters" in the sense of the Unicode,
specification. Strings must be enclosed between double quotes or single quotes. Thus, ``"abc"`` and ``'abc'`` represent the same string, which is formed by the concatenation of the three characters ``a``, ``b`` and ``c``.
Characters may correspond to single letters, but they can represent more complex units. For example, the string ``"é"`` is treated
one character, even though it is composed of the letter ``e`` and an acute accent. Likewise, the string ``"한글"`` (the name of the Korean alphabet, in Korean) contains two characters, although it is composed of two syllables, each of which contains three letters.

Internally, strings are encoded as UTF-8, which is the most widespread Unicode encoding. Source files are also expected to be encoded in UTF-8.

Although double-quoted and single-quoted strings look similar, they are processed differently. **Double-quoted strings** support escape sequences and string interpolation, both described below. **Single-quoted strings are raw**: their content is taken exactly as written, with no interpolation and no escape processing. This makes single quotes ideal for regular expressions and Windows paths, where backslashes and braces should be left untouched:

.. code:: phon

    var pattern = '\b[aeiou]+\b'
    var dir = 'C:\Users\me\Documents'

A single-quoted or double-quoted string must be terminated on the same line where it begins: a literal line break inside such a string is reported as a syntax error. For multi-line strings, use a *triple-quoted* string, which is enclosed between three double quotes (``"""``) or three single quotes (``'''``):

.. code:: phon

    var message = """Dear participant,

    Please read the following sentences aloud,
    at a comfortable speaking rate.

    Thank you."""
    print(message)

Inside a triple-quoted string, line breaks and isolated occurrences of the delimiter character are part of the content; the string ends only when three delimiter characters appear in a row. This is convenient for embedding longer pieces of text (instructions, prompts, formatted reports) without having to concatenate strings line by line. Triple double-quoted strings behave like double-quoted strings (escapes and interpolation are processed); triple single-quoted strings are raw.

You can include special characters in a double-quoted string with *escape sequences*. An escape sequence starts with a backslash (``\``) followed by a single character, and is interpreted as shown below:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Sequence
     - Character
   * - ``\n``
     - line feed (newline)
   * - ``\t``
     - horizontal tab
   * - ``\r``
     - carriage return
   * - ``\\``
     - backslash
   * - ``\'``
     - single quote
   * - ``\"``
     - double quote
   * - ``\v``
     - vertical tab
   * - ``\b``
     - backspace
   * - ``\f``
     - form feed
   * - ``\a``
     - bell
   * - ``\{``
     - literal opening brace (suppresses ``{...}`` interpolation)

A backslash followed by any other character is a syntax error, so mistyped escape sequences are caught immediately. (Remember that single-quoted strings are raw: a backslash inside them is just a backslash.)

You can use the concatenation operator ``&`` to concatenate two or more values. If they are not strings, they will automatically
 be converted to strings, if possible.

.. code:: phon

    var pi = 3.14
    print("The value of pi is " & pi)

For diagnostic messages and constructing file paths, building strings out of repeated ``&`` calls quickly becomes hard to read. As a more concise alternative, you can use **string interpolation**: any expression enclosed in ``{...}`` inside a double-quoted string is evaluated and converted to a string at the place it appears. The conversion is the same one that ``&`` performs, so you do not need to call ``to_string(...)`` explicitly. Compare:

.. code:: phon

    # without interpolation
    print("Speaker " & speaker & ": F1=" & f1 & " F2=" & f2)

    # with interpolation
    print("Speaker {speaker}: F1={f1} F2={f2}")

Both lines produce the same output. The expression inside ``{...}`` can be any valid expression — a variable, an arithmetic expression, a function call, a table lookup:

.. code:: phon

    var n = 7
    print("n squared is {n * n}")          # n squared is 49
    print("rounded: {round(3.7)}")         # rounded: 4
    var info = {"name": "Lobanov"}
    print("method: {info["name"]}")        # method: Lobanov

Interpolation is available inside double-quoted strings (``"..."`` and ``"""..."""``). Single-quoted strings are raw and never interpolate, which is precisely what you want when the braces belong to the text itself, as in regular expressions.

If you need a literal opening brace in a double-quoted string, escape it as ``\{``:

.. code:: phon

    print("the sequence \{x} is printed verbatim")

Unlike most scripting languages, strings in Phonometrica are *mutable*, which means that some functions can modify them directly:

.. code:: phon

    var s = "hello"
    append(s, " world!")
    print(s) # prints "hello world!"

Functions such as ``append`` and ``trim`` modify their argument in place because their first parameter is declared as a reference (``ref``); they do not return a new string. See :ref:`the page on values and references <clonability>` for details.


List
~~~~

A ``List`` is an ordered collection of items. Like strings, lists can be modified and their capacity is automatically adjusted when items are added. Lists can be created directly using a *list literal*:

.. code:: phon

    var lst = [ "a", "b", "c", 3.14 ]


The variable ``lst`` contains four elements, three strings and one number. To access elements in the list, we use array indexing by using the name of the variable followed by square brackets containing the index, as follows:

.. code:: phon

    print(lst[2]) # prints "b"


We can also assign a new value at a given index, like so:

.. code:: phon

    lst[3] = "C"


Indices start at 1 and can be negative: -1 represents the last element, -2 the second-to-last element, and so on.


Array
~~~~~

An ``Array`` is a one or two dimension numeric array. Elements along each dimension start at 1 and can be negative.
(Negative indices start from the end of the dimension.) Two-dimensional arrays are accessed with a pair of indices noted *(i, j)*,
where *i* represents the *i*\ th row and *j* represents the *j*\ th column. To get or set an element in an array, use the index ``[]`` operator.

You can create a new array filled with zeros by passing the size of each dimension to the function ``zeros``. For instance, here is how to create an array containing 3 rows and 5 columns:

.. code:: phon

    var array = zeros(3, 5)

    for i = 1 to nrow(array) do
        for j = 1 to ncol(array) do
            array[i, j] = i + j
        end
    end

    print(array)

This code will produce the following output:


.. code:: phon

    [2, 3, 4, 5, 6; 3, 4, 5, 6, 7; 4, 5, 6, 7, 8]


Another way to produce the same output would be to use an array literal, which is indicated with the ``@[]`` operator. Inside the brackets, rows are separated by semicolons and
columns are separated by commas. Therefore, our array could be written as follows:

.. code:: phon

    var array = @[2, 3, 4, 5, 6; 3, 4, 5, 6, 7; 4, 5, 6, 7, 8]


Table
~~~~~

A ``Table`` (also known as map, hash map, hash table, associative array or dictionary) is an unordered mapping of key/value pairs. Each key/value pair represents a *field*. Keys can be any clonable value (except ``null``), whereas values can be anything.
Tables can be declared with a table literal:

.. code:: phon

    var person = { "name": "john", "surname": "smith", "age": 38 }

In this example, ``person`` is declared with three pairs separated by commas: the key and the value are separated by the symbol ``:`` (colon). This table could correspond to mappings from names (keys) to ages (values) for instance. Note that there is no need for the keys and/or values to be homogeneous: any valid Value (even null!) may appear in an object.
Note that even though we declared key/value pairs in a specific order in our example, there is no guarantee that they will be stored in this particular order. You should consider the order of the elements as random.

To create an empty table, use an empty table literal:

.. code:: phon

    var tab = {}
    assert(is_empty(tab))


To access any element of a table, you can use the index operator ``[]``:

.. code:: phon

    var person = { "name": "john", "surname": "smith", "age": 38 }
    print(person["name"])
    person["age"] += 1
    print(person)



If you need to process the table in sorted order, you can do as follows (assuming you have a table named ``tab``):

.. code:: phon

    var ks = keys(tab)
    sort(ks)
    for key in ks do
        var value = tab[key]
        # do something with the key and the value
    end


Set
~~~

A ``Set`` represents an unordered collection of unique values. Sets can be declared using a *set literal*:

.. code:: phon

    var names = { "john", "peter", "anna", "patricia" }

The declaration of a set is similar to that of a table, except there are only values, no keys. (Note that ``{}`` denotes an empty *table*, not an empty set.) A value can appear in a set only once: inserting a value that is already present has no effect. Like tables, sets are unordered: the order in which elements are stored and iterated over is unspecified and may differ from the order in which they were declared. If you need to process the elements of a set in a well-defined order, copy them to a list and sort it explicitly.

Sets are useful to keep track of a collection of (unique) values.


Function
~~~~~~~~

A ``Function`` is a special construct that represents a reusable block of code. Functions are created using the
keyword ``function``. Here is an example of a function that prints the area of a rectangle.
It expects two arguments (``x`` and ``y``), which correspond to the rectangle's height and width.

.. code:: phon

    function area(x as Number, y as Number)
        print("The area of the rectangle is {x * y}")
    end


We can then *call* the function with specific values for ``x`` and ``y``:

.. code:: phon

    area(100, 30) # prints "The area of the rectangle is 3000"


The annotations ``as Number`` declare the type of each parameter; they are optional (an unannotated parameter accepts any value), but they document the function's intent and allow Phonometrica to check the arguments at each call.

In addition to executing statements, functions can also send a value back to the caller. This is achieved with the keyword ``return``
followed by the expression we want to send back to the caller. The following example illustrates how this can be done. First, we create the
function ``fibonacci`` to calculate the *n*\ th Fibonacci number. Next, create a list in which we store the first 10 Fibonacci numbers, and
finally we print the list.

.. code:: phon

    function fibonacci(num as Integer)
        var a = 1
        var b = 0
        var temp = 0

        while num >= 0 do
            temp = a
            a += b
            b = temp
            num -= 1
        end

        return b
    end

    var result = []
    for i = 1 to 10 do
        append(result, fibonacci(i))
    end

    print(result) # prints [1, 2, 3, 5, 8, 13, 21, 34, 55, 89]


Object
~~~~~~

``Object`` is an abstract type: it is the base type for all types in Phonometrica. This means that all types inherit from ``Object``, directly or indirectly. ``Object``
is the default :ref:`parameter type <funcparam>` for functions.

Class
~~~~~

A ``Class`` represents a type. Every value is an instance of a class, and every class describes a type. You can test the dynamic type of a value with
the ``is`` operator, which checks whether its left operand is an instance of the class named on the right (or of one of its subclasses):

.. code:: phon

    var s = "hello"

    print(s is String)  # prints "true"
    print(s is Object)  # prints "true", since all types inherit from Object
    print(10 is Float)  # prints "false", since 10 is an Integer
    print(10 is Number) # prints "true"


Because classes are also values, you can print them, pass them as arguments to functions or store them in variables:


.. code:: phon

    print(String) # prints "String"
    var t = Integer
    print(1 is t) # prints "true"


Module
~~~~~~

Every script file is a *module*. Modules let you split a larger project into reusable files: the top-level variables, functions and classes that a script declares are visible to any other script that imports it with the ``import`` statement. For example, if you have a script ``mytools.phon`` on the module search path, you can write:

.. code:: phon

    import mytools

    print(mytools.version)   # access a variable declared in mytools
    greet("world")           # call a function declared in mytools

``import`` is a compile-time statement: it is resolved by module *name* on the module search path, and a missing module is reported as an error before your script starts running. A module's variables are accessed with the dot operator, qualified by the module's name (``mytools.version``), whereas its public functions become directly visible.

Modules are particularly useful if you intend to redistribute scripts or create plugins. See the :ref:`dedicated page <modules>`.



Control flow
------------

If statement
~~~~~~~~~~~~

It is often necessary to execute a code block only if a certain condition is satisfied. This can be achieved with the ``if`` statement

.. code:: phon

    if extension == ".txt" then
        print("This is a text file")
    elsif extension == ".xml" then
        print("This is an XML file")
    else
        print("extension '{extension}' not recognized")
    end


This block of code tries to execute the block following the ``if`` branch if its condition is true, otherwise it tries to execute the first
elsif branch (if any), and if all else fails, it executes the ``else`` branch. The ``elsif`` and ``else`` branches are optional, and there
is no limit on the number of ``elsif`` branches. The ``else`` branch, if it exists, but always come last.


A condition may also *declare* the value it tests. This is useful whenever a function
returns either a result or ``null``, since it saves you from naming the result on one
line and testing it on the next:

.. code:: phon

    if var m = match(Regex('(\d+)'), line) then
        print("found the number {group(m, 1)}")
    end

The variable exists only inside the branch it guards. It is not visible after the
statement, not in the ``else`` branch — where it would always be ``null``, so every use
would be an error waiting to happen — and not in a later ``elsif`` condition. Each
branch may declare its own:

.. code:: phon

    if var sound = get_selected_sound() then
        analyse(sound)
    elsif var annot = get_selected_annotation() then
        analyse(annot)
    else
        print("nothing selected")
    end

Only ``var`` may be used here. ``const``, ``local``, ``global`` and ``ref`` are
rejected: the name lives for one branch, so it is never a module binding and never
something you assign through.

There is also an expression form of ``if``, which takes the following form:

.. code:: phon

    if condition then expression1 else expression2 end


This *if expression* evaluates to ``expression1`` if ``condition`` is true, and to ``expression2`` otherwise. Consider the
following example:

.. code:: phon

    var x = 7 mod 2
    var y = if x == 1 then "odd" else "even" end
    print(y)

We define ``x`` as the remainder of the division of 7 by 2, which is 1. We then assign the result of the if expression that evaluates ``x == 1`` to ``y``. Since
``x`` is indeed equal to 1, the string that will be printed is ``odd``.


While loop
~~~~~~~~~~

The ``while`` loop allows you to execute a block of code while some condition is true.

.. code:: phon

    var x = 1
    # Print numbers from 1 to 10
    while x <= 10 do
        print(x)
        x += 1
    end

A ``while`` condition may declare its variable too, which is the natural way to consume
a source that signals exhaustion by returning ``null``:

.. code:: phon

    while var task = next_task() do
        handle(task)
    end

The initializer is re-evaluated on every pass — including after a ``continue``, which
jumps back to it — and the variable is scoped to the loop body. The loop ends as soon
as the initializer yields ``null``.

.. note:: This idiom relies on the source returning ``null`` when it is exhausted.
   :func:`read_line` is not such a source: at the end of a file it returns an empty
   string, which is *truthy*, so ``while var line = read_line(f) do`` would never
   stop. Test ``eof(f)`` or compare against ``""`` and ``break``.

If you need to exit a loop early, use the keyword ``break``:

.. code:: phon

    var x = 0
    while true do
        if x > 10 then
            break
        end
        print(x)
        x += 1
    end

If you only want to break the current iteration of the loop and move to the next iteration, use the keyword ``continue``:

.. code:: phon

    # Print odd numbers up to 10
    var x = 0
    while x < 10 do
        x += 1
        if x mod 2 == 0 then
            continue
        end
        print(x)

    end



Repeat loop
~~~~~~~~~~~

The ``repeat`` loop is similar to the ``while`` loop but there are two key differences: the block of code is executed *until* some condition is
satisfied, and it is executed at least once since it precedes the evaluation of the condition.

.. code:: phon

    var x = 1
    # Print numbers from 1 to 10
    repeat
        print(x)
        x += 1
    until x > 10



For loop
~~~~~~~~

The ``for`` loop, as in other programming languages, is used to iterate through a block of instructions, incrementing (or decrementing) a counter at each iteration. The counted ``for`` loop must always have a ``start`` condition and an ``end`` condition, and may optionally have a ``step`` condition, which indicates by how much the counter should be incremented/decremented (if no ``step`` is specified, the default is 1). Both bounds are inclusive.
Here is a simple example, which prints the numbers from 1 to 10 (inclusive):

.. code:: phon

    for i = 1 to 10 do
        print(i)
    end


Note that in this case, we didn't need to declare the variable ``i``: Phonometrica will automatically declare it make it local to the ``for`` loop (i.e. it will only be visible inside the ``for`` loop).


To print all the odd digits between 1 and 10, we can use the following loop:

.. code:: phon

    for i = 1 to 10 step 2 do
        print(i)
    end



To iterate in decreasing order, use a negative ``step``:

.. code:: phon

    for i = 10 to 1 step -1 do
        print(i)
    end

A loop whose direction contradicts its step — for instance ``for i = 1 to 10 step -1`` — simply runs zero times.


Iterating over collections
~~~~~~~~~~~~~~~~~~~~~~~~~~

The second form of the ``for`` loop, ``for ... in``, offers a simple way to iterate over the content of an iterable object.

.. code:: phon

    # Iterate over a list
    var lst = ["a", "b", "c"]

    for value in lst do
        print(value)
    end


If there is a single loop variable (``value`` in this example), Phonometrica will iterate over the values in the collection. You can add another loop variable
if you would like to iterate over the indexes (or keys) as well as the values:

.. code:: phon

    # Iterate over a list
    var lst = ["a", "b", "c"]

    for i, value in lst do
        print("{i} -> {value}")
    end


Here is another example where we iterate over the keys and values in a table:

.. code:: phon

    var person = { "name": "John", "surname": "Smith", "age": 38 }

    for key, value in person do
        print("{key} -> {value}")
    end


As for the counted ``for`` loop, the loop variable(s) is/are automatically declared and are made local to the loop.



Here are the builtin types that support iteration with the ``for ... in`` loop:

.. list-table::
    :widths: 25 25 50
    :header-rows: 1

    * - Type
      - key (optional)
      - value
    * - List
      - index
      - value
    * - Set
      - index
      - value
    * - String
      - index
      - character
    * - Table
      - key
      - value

Remember that tables and sets are unordered: the order in which their elements are visited is unspecified.

A common use of the ``for ... in`` loop is to build a new list from an existing collection, keeping or transforming some of its elements. Simply start from an empty list and ``append`` to it:

.. code:: phon

    var nums = [1, 2, 3, 4, 5, 6]
    var evens = []

    for n in nums do
        if n mod 2 == 0 then
            append(evens, n)
        end
    end

    print(evens) # prints [2, 4, 6]


Scope of variables
~~~~~~~~~~~~~~~~~~

The scope of a variable is the region of code where it is visible (and accessible). Where a declaration appears determines its scope.

Inside a function, ``var`` and ``const`` declare *local* variables: they are visible from the point of declaration until the end of the block in which they are declared. Any new block created by an ``if`` statement, a ``for`` loop, etc. defines a new scope, and a variable declared in an inner block temporarily hides a variable with the same name declared in an outer one. Such scoping rules are sometimes refered to as *lexical scoping*. Consider the following example:

.. code:: phon

    function demo()
        var x = "outer"
        if true then
            var x = "inner"
            print(x) # prints "inner"
        end
        print(x) # prints "outer"
    end

    demo()


At the top level of a script, ``var``, ``const``, ``function`` and ``class`` declare *module* variables, which live for as long as the module (for a regular script, until the end of the run). Module variables are public by default: they are visible to any script that imports the module, as we saw in the section on modules. If a top-level variable or helper function is an implementation detail that should *not* be visible to other modules, declare it with the ``local`` modifier:

.. code:: phon

    local var cache = {}

    local function helper()
        print("this function is private to this module")
    end

If you intend to redistribute a script or plugin, we strongly encourage you to declare all top-level variables and helper functions as ``local``, unless they are part of the interface you want to expose, of course.

Finally, ``global var`` (at the top level only) declares a *global* variable, which is shared by all scripts and lives for as long as Phonometrica is running. Globals should be used sparingly, to avoid "polluting" the global namespace.

Remember that assignment never declares a variable: an assignment such as ``total += x`` inside a function first looks for ``total`` among the local variables, then in enclosing functions, then among the module's variables, and finally among the globals. This means that a function can update a top-level variable directly, without any special declaration. If the name is not found anywhere, the script does not compile.

Global, module and local variables are the most common kinds of variables, but there is a fourth kind: non-local variables. Consider the following example:

.. code:: phon

    function outer()
        var s = "hello"
        function inner()
            return s
        end

        return inner
    end

    var f = outer()
    print(f()) # prints "hello"


From the point of view of function ``outer``, the variable ``s`` is local since it is defined in the scope created by that function. But what about function ``inner``? This function creates
a new scope embedded in ``outer``'s scope, so from ``inner``'s perspective, ``s`` is neither local, since it is not defined in the function's own body, nor global, since it is not visible outside of ``outer``'s scope.
What is it, then? In this case, ``s`` is regarded as a *non-local* variable in the scope defined by ``inner``. When we declare the variable ``f``, we execute the function ``outer``,
which first creates a variable named ``s`` and then creates a function named ``inner``, which  *captures* ``outer``'s local variable ``s``. Finally, ``outer`` returns the function ``inner``.
This means that ``f`` is now a function (the function ``inner``). When we call it, it returns the value of the variable ``s``. Functions that capture non-local variables are called :ref:`closures <closures>`.


Errors
------

Throwing errors
~~~~~~~~~~~~~~~

It is sometimes necessary to interrupt a script because it can no longer proceed further. To signal an error, use the keyword ``throw``
followed by an ``Error`` value that describes the problem. An ``Error`` is constructed from a message string. Here is a typical example:

.. code:: phon

    function area(x as Number, y as Number)
        if x <= 0 or y <= 0 then
            throw Error("x and y must be positive")
        end

        return x * y
    end


Only ``Error`` values (and instances of its subclasses) can be thrown. When a ``throw`` is not handled by an enclosing ``try`` block (see below), it terminates the script and prints an error message
that includes the error's message and a trace of the function calls that led to the error.


Catching errors
~~~~~~~~~~~~~~~

When you call code that may fail — a built-in operation that could be misused, a function that uses ``throw``, or any expression that
might raise a runtime error such as an out-of-bounds index or a failed type conversion — you can intercept the error using a ``try`` block.
The general form is::

    try
        <statements that may fail>
    catch <name>
        <statements that handle the error>
    end

If everything in the body of ``try`` succeeds, the ``catch`` clause is skipped. If anything in the body raises an error, control jumps
immediately to the ``catch`` clause and the variable named after ``catch`` is bound to an ``Error`` object describing the error. The bound name
is local to the ``catch`` clause: it is not visible outside it. Execution then resumes after ``end``.

All errors are ``Error`` objects, whether they were raised by an explicit ``throw`` or by the language itself (an invalid index, a
missing field, a failed type check). An ``Error`` carries the following fields:

- ``message``: the error message, as a ``String``;
- ``trace``: a formatted backtrace string showing the function calls that led to the error;
- ``frames``: the same backtrace as a list of tables, each with the fields ``function``, ``line`` and ``file``.

The following example illustrates both a user-defined error and a built-in one:

.. code:: phon

    # User-defined throw.
    try
        throw Error("bad input")
    catch e
        print("caught: {e.message}")   # prints: caught: bad input
    end

    # Built-in error: an invalid index raises an Error as well.
    var xs = [10, 20, 30]
    try
        var bad = xs[100]
    catch e
        print("caught: {e.message}")
    end


If you do not need the bound value, you can omit the identifier entirely:

.. code:: phon

    try
        risky_operation()
    catch
        print("something went wrong, ignoring")
    end


A ``catch`` clause may also select errors by type, using ``as`` followed by an ``Error`` subclass: the clause only handles errors of that
type, and other errors propagate to the next matching clause (or outward). An optional ``finally`` clause runs whether or not an error
occurred, which makes it the right place for cleanup code::

    try
        <statements that may fail>
    catch e as IOError
        <handle input/output errors>
    catch e as Error
        <handle any other error>
    finally
        <cleanup code, always executed>
    end

``try`` blocks may be nested. When an error is raised, the most recently entered ``try`` is given the chance to handle it; if its
``catch`` clause itself raises (for example by re-throwing the error), the next enclosing ``try`` takes over. Note that ``throw e``
inside a ``catch`` clause preserves the original error's backtrace, so no separate "rethrow" construct is needed.

.. code:: phon

    try
        try
            throw Error("first")
        catch e
            print("inner caught: {e.message}")
            throw Error("second")   # handled by the outer catch
        end
    catch e
        print("outer caught: {e.message}")
    end


An error raised inside a function call propagates back through the call stack until it reaches a matching ``try``. This means you
can wrap a single ``try`` around a high-level operation and catch errors raised deep inside the functions it calls, without having
to add error handling to every intermediate function.


Interaction with control flow
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``return``, ``break``, and ``continue`` inside the body of a ``try`` block work the way you would expect: they leave the ``try``
silently (the ``catch`` clause is not entered) and the corresponding return / loop-exit takes effect. In particular, you can use
``break`` to leave a loop from inside a ``try`` block without disturbing any error handling registered outside the loop, and you
can use ``return`` to leave a function from inside a ``try`` without leaking its error handling into the caller.


When NOT to use try/catch
~~~~~~~~~~~~~~~~~~~~~~~~~

``try`` blocks are useful when an operation has multiple plausible failure modes that the calling code wants to recover from
distinctly. For routine input validation, prefer an explicit check, an early ``return``, or an ``assert``: these communicate the
contract more directly and are cheaper at runtime. Use ``try`` for the cases where you genuinely want the script to keep running
after a failure.


Assertions
----------


Another way to trigger errors is to use the function ``assert``, which takes a Boolean expression that must be true, and an optional error
message. It will trigger an error with the error message if the condition is false.

.. code:: phon

    function area(x as Number, y as Number)
        assert(x > 0, "x must be positive")
        assert(y > 0, "y must be positive")
        return x * y
    end


Debugging
---------

It is sometimes necessary to check the state of the program at a given point. The simplest tools for this are the ones you have already
seen: ``print`` calls to inspect values, and ``assert`` calls to state conditions that must hold. Assertions are usually preferable to
temporary ``print`` statements: they document the assumption they check, they cost nothing as long as the assumption holds, and they fail
loudly — with a message and a backtrace — as soon as it is violated.

When a script fails, running it from a terminal can help pinpoint the problem::

    phonometrica -r my_script.phon

Compile-time errors point at the offending line; runtime errors print the full call-stack trace. The same trace appears in the console
inside Phonometrica, and the script editor highlights the failing line.


The ``debug`` statement
~~~~~~~~~~~~~~~~~~~~~~~

Diagnostic code often needs to stay in a script while you work on it, but not run when the script is used for real. Deleting and
retyping it each time is tedious, and commenting it out leaves it to rot. The ``debug`` statement marks code as diagnostic so that you
can switch it off in one place.

It has two forms. It can precede a single statement on the same line::

    var formants = get_formants(sound, t)
    debug print("formants at {t}: {formants}")

or it can mark a whole block, closed by ``end``::

    debug
        print("layer count: " & get_layer_count(annot))
        print("duration: " & get_duration(sound))
    end

Debug code is included by default, so both examples above run as written. To switch it off, put an ``option`` directive at the very top
of the file — before any other statement::

    option debug = false

Every ``debug`` statement in that file is then **removed at compile time**. This is not a runtime test that happens to be false: the code
is never compiled at all, so it costs nothing, not even the check. A consequence worth knowing is that a switched-off ``debug`` body is
never checked for undefined names either, so it can refer to a helper that no longer exists without the file failing to compile. The
flip side is that a typo in debug code stays hidden until you switch it back on.

The directive accepts ``= true`` as well, and a bare ``option debug`` means the same thing. Since debug code is on by default,
``option debug = false`` is the form you will normally write.

A ``debug`` block opens no scope of its own — including it behaves exactly as if the ``debug`` and ``end`` lines were deleted. That
means a variable created in one debug block is still available later, which is what makes the common timing pattern work::

    debug
        var started = 0
    end

    # ... the work being measured ...

    debug
        print("elapsed: " & started)
    end

The directive is per file: a script that switches debug off does not switch it off for the modules it imports, and each file decides for
itself. If you are distributing a plugin and do not want your diagnostic code running on someone else's machine, put
``option debug = false`` at the top of the files you ship — that is the setting you control as the author.

There is also a global switch, for the reader rather than the author: **Preferences → General → "Run debug statements in scripts"**,
which is on by default. Unchecking it strips debug code from *every* script, including any that asks for ``option debug = true`` — a
file can narrow the global setting but never widen it. That is the one to reach for when a plugin you did not write is noisy, or when
you want a run free of diagnostic overhead without editing anyone's files.

Because ``debug`` is resolved when a script is compiled, changing that preference applies to scripts compiled from then on. Code that
has already been compiled — including modules the session has cached from an earlier ``import`` — keeps whatever setting it was built
with, so restart Phonometrica if you want the change to apply to everything.


Operators
---------

Mathematical operators
~~~~~~~~~~~~~~~~~~~~~~

Phonometrica supports the following mathematical operators: ``+`` (addition), ``-`` (subtraction),
``*`` (multiplication), ``/`` (division), ``^`` (power), ``div`` (integer division) and ``mod`` (remainder). The division operator ``/`` always yields a ``Float``, even between two integers. The power operator has highest precedence,
followed by the multiplication, division and remainder operators. Addition and subtraction have lowest precedence. You can use
grouping parentheses ``()`` to alter the precedence of operators:

.. code:: phon

    print(3 + 5 * 10)   # prints 53
    print((3 + 5) * 10) # prints 80
    print(7 / 2)        # prints 3.5
    print(7 div 2)      # prints 3
    print(7 mod 2)      # prints 1


Boolean operators
~~~~~~~~~~~~~~~~~

Phonometrica supports the 3 standard Boolean operators ``and``, ``or`` and ``not``. ``and`` and ``or`` are binary operators: ``x and y`` is
true if both ``x`` and ``y`` are true, whereas ``x or y`` if ``x`` is true or ``y`` is true (or both are true). ``not`` is a unary operator:
``not x`` is true if ``x`` is false, and vice versa.

Note that in the case of ``and`` and ``or``, Phonometrica will not necessary evaluate the second operand. For instance, in the expression
``x and y``, ``y`` will not be evaluated if ``x`` is false, since ``x and y`` will always be false whatever the truth condition of ``y`` is;
likewise, ``y`` will not be evaluated in ``x or y`` if ``x`` is true since this is enough to determine that the whole expression is true.
Therefore, you shouldn't rely on the second operand being evaluated.

Comparison operators
~~~~~~~~~~~~~~~~~~~~

Like most programming languages, Phonometrica's scripting language allows you to use a number of binary operators that compare their operands:

- ``x == y`` is true if ``x`` is equal to ``y``
- ``x != y`` is true if ``x`` is not equal to ``y``
- ``x < y`` is true if ``x`` is less than ``y``
- ``x <= y`` is true if ``x`` is less than or equal to ``y``
- ``x > y`` is true if ``x`` is greater than ``y``
- ``x >= y`` is true if ``x`` is greater than or equal to ``y``


Concatenation operator
~~~~~~~~~~~~~~~~~~~~~~~~

The concatenation operator ``&`` allows to concatenate two or more strings. It implicitly converts values to
``String`` if needed:

.. code:: phon

    var pi = 3.14
    var s = "The value of pi is " & pi
    print(s)

``&`` is the only operator that performs implicit conversions. In particular, ``+`` never converts its operands: ``"3" + 1`` is a type error. (Between two lists, ``+`` concatenates them.)
