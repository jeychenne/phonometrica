Values and references
=====================


.. _clonability:

Clonability
-----------

In computer programming, it is often necessary to assign the content of a variable to another variable. An important question that arises is
"what happens when one of the variables is modified?". Different languages solve this problem in different ways. In Phonometrica, the answer
depends on the type of the value. Most types in Phonometrica are *clonable*, which means that it makes sense for such types to make a copy of
a value, and two values that have the same content and type should be considered identical. While this is true in most programming languages for
basic types such as ``Float`` and ``String``, Phonometrica extends this notion to other types such as ``List`` and ``Table``:

.. code:: phon

    var n1 = 2.178
    var n2 = 2.178
    assert(n1 == n2)

    var s1 = "hello"
    var s2 = "hello"
    assert(s1 == s2)

    var lst1 = ["a", "b", "c"]
    var lst2 = ["a", "b", "c"]
    assert(lst1 == lst2)


When we assign a variable that contains a clonable value to another variable, the assignment *behaves as if it made a copy of the original
value*. This ensures that modifying one variable will not affect the other. Consider the following example:

.. code:: phon

    var s1 = "hello"
    var s2 = s1
    print(s1) # prints "hello"
    print(s2) # prints "hello"
    append(s1, " world!")
    print(s1) # prints "hello world!"
    print(s2) # prints "hello"


As we can see, at first ``s1`` and ``s2`` have the same value, but after we modify ``s1``, ``s2`` retains the same value. Types that behave in
this way are said to have *value semantics*. All clonable types in Phonometrica have value semantics, and most builtin types are clonable.

Under the hood, no copying actually takes place when a value is assigned or passed to a function: the two variables share the same
underlying storage, and Phonometrica keeps track of how many variables use it. A value is only really copied at the moment it is
*modified* while it is being shared: the variable being modified quietly detaches its own private copy first, and the other variables keep
the original. This strategy is known as *copy-on-write*. You never need to think about it to reason about the behaviour of a script — every
assignment behaves like a copy — but it does mean that passing a large list, table or array around is cheap, and that you never pay for a
copy you don't use.

Not every type is clonable, however. Instances of reference classes (declared with ``ref class``, see :doc:`classes`) and resource-like builtin
types such as ``File`` represent a unique entity rather than a piece of data: cloning an open file handle, for instance, would not make sense.
Non-clonable types have *reference semantics*, which means that when we assign a variable to another one, they share the same value: modifying
one modifies the other. This is easy to observe with a ``File``, whose reading position is part of its state:

.. code:: phon

    var f1 = open_file(path, "r")
    var f2 = f1

    print(read_line(f1)) # prints the file's first line
    print(read_line(f2)) # prints the SECOND line: f2 shares f1's state

As you can see, ``f1`` and ``f2`` refer to the same file handle, so these variables can be considered as two *aliases* for the same value.

The same applies to documents in the current project. Each ``Document`` represents a unique resource: a file at a specific path, possibly bound
to an open view and registered with the project. Cloning such a value would produce a detached copy with no clear identity (no path, no project
membership, no view), so Phonometrica treats documents as references throughout. When you obtain a document from the project — for example with
``load``, ``get_dataset`` or ``get_concordance`` — assignments and function calls all share the same underlying object, and any modification is
visible to the project, to any open view of the document, and to every variable that refers to it:

.. code:: phon

    var d1 = load("subjects.csv")
    var d2 = d1
    add_column(d1, "age", @[42.0, 37.0, 51.0])
    print(ncol(d2))  # the new column is visible through d2 as well

If you genuinely want a detached working copy of a document — say, to run a destructive transformation without affecting the original — the
right approach is to save it to a new path or otherwise create a new document explicitly, rather than relying on assignment to make a copy.



References
----------

While most programming languages have reference semantics for non-primitive types such as ``List`` and ``Table``, Phonometrica is not an
isolated case since there are a number of languages that have value semantics, including R, MATLAB, PHP, Swift and C++, to name a few.

Value semantics makes it easier to reason about your code and can prevent a number of subtle bugs because modifying a variable in one place
will not affect variables with the same value in other places. In particular, a function can never accidentally modify its caller's
variables: inside a function, a parameter behaves like a copy of the argument, so modifying it leaves the caller's value untouched:

.. code:: phon

    function tweak(xs as List)
        append(xs, 99)
        print(xs)   # prints [1, 2, 3, 99]
    end

    var data = [1, 2, 3]
    tweak(data)
    print(data)     # prints [1, 2, 3]: the caller's list is unchanged


However, in some circumstances, modifying the caller's variable is precisely what we want: think of a function whose whole purpose is to
normalize a sound's samples, or to trim whitespace from a string. To do this in Phonometrica, the function must explicitly declare the
parameter as a reference using the keyword ``ref``. A ``ref`` parameter is an *alias* for the argument at the call site: the parameter and
the caller's variable designate the same value, so any modification made through the parameter is visible to the caller after the function
returns. Consider the following example:

.. code:: phon

    function shout(ref s as String)
        s = to_upper(s)
    end

    var greeting = "hello"
    shout(greeting)
    print(greeting) # prints "HELLO"


Note that the call site is written exactly like an ordinary call: whether an argument is passed by reference is determined by the
function's signature alone, so ``shout(greeting)`` needs no special marking. This is how the builtin string mutators seen earlier work:
functions such as ``append``, ``trim`` and ``sort`` declare their first parameter as ``ref``, which is why they modify their argument in
place instead of returning a new value.

The argument passed for a ``ref`` parameter must be something that can be assigned to — a variable, a list element or an object field.
Passing an element of a collection is particularly useful, since it lets a function modify the collection in place:

.. code:: phon

    var words = ["good", "morning"]
    shout(words[2])
    print(words) # prints [good, MORNING]

Passing a value that has no storage location, such as a literal or the result of an arithmetic expression, is reported as a compile-time
error (``a 'ref' parameter requires an lvalue: a variable, a list element, or an object field``), since there would be no variable for the
modification to be written back to.

References are also available in ``for ... in`` loops, where they let you modify the elements of a collection in place. Prefix the loop
variable with ``ref`` and it becomes an alias for each successive element:

.. code:: phon

    var lst = ["a", "b", "c"]

    for ref value in lst do
        value = to_upper(value)
    end

    print(lst) # prints [A, B, C]

Note however that you can only take references to values, not to keys or indexes. The following example is rejected with a syntax error,
because the keys of a table are not storage locations in the collection (and table keys are immutable):

.. code:: phon

    for ref key, val in tab do # error: the key/index of a 'for ... in' loop cannot be taken by reference
        ...
    end

You can nevertheless modify the values:

.. code:: phon

    var tab = {"name": "John", "surname": "Smith"}

    for key, ref val in tab do
        val = to_upper(val)
    end

    print(tab["name"], tab["surname"]) # prints "JOHN SMITH"

The two features combine as you would expect: iterating by reference over a collection that the function received as a ``ref``
parameter modifies the caller's collection.

.. code:: phon

    function double_all(ref numbers)
        for ref n in numbers do
            n = n * 2
        end
    end

    var values = [1, 2, 3]
    double_all(values)
    print(values) # prints [2, 4, 6]

Leaving such a loop early with ``break`` or ``return`` keeps the modifications made up to that point. An error thrown out of the
loop, on the other hand, discards them: the collection is only updated when the loop is left normally.


References to non-clonable types
--------------------------------

Non-clonable types have reference semantics, so you might wonder whether a function can take a ``ref`` parameter of a reference type.
The answer is "no" — because it would be redundant. Since assigning or passing a non-clonable value always shares it, an ordinary
parameter of a reference type is *already* an alias for the caller's object, and any modification made through it is visible everywhere.
Phonometrica therefore rejects ``ref`` on a parameter whose declared type is a reference class, rather than letting it silently do nothing.
The following example illustrates the point with a user-defined reference class:

.. code:: phon

    ref class Counter
        field count = 0
    end

    function bump(c as Counter)   # no ref: a Counter is already shared
        c.count = c.count + 1
    end

    var c1 = Counter()
    var c2 = c1
    bump(c1)
    print(c2.count) # prints "1": c1 and c2 are aliases for the same object

``ref`` is only meaningful for clonable types, where it is the one way to opt out of value semantics.


Avoid references!
-----------------

You might be tempted to declare ``ref`` parameters everywhere to avoid the "cost" of copying values. Don't do that! As explained above,
Phonometrica's copy-on-write strategy already ensures that no copying occurs when a value is assigned or passed to a function: a real copy
is only made when a shared value is modified, which is exactly when it is needed to preserve value semantics. Use ``ref`` when the point of
a function is to modify its argument in place. Everywhere else, use values!
