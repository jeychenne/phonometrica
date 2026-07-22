.. _page-functions:

Functions
=========

Phonometrica's scripting language is an object-oriented programming language.  Unlike most other object-oriented programming languages,
however, Phonometrica is based on the notion of *multiple dispatch*: when a function is called and there are several functions with the
same name, Phonometrica will decide which function to call based on the type of its arguments. This page explains what multiple dispatch
is and how to use functions efficiently in Phonometrica.


Basics
------


A ``Function`` is a special construct that represents a reusable block of code. Functions are created using the
keyword ``function``. Here is an example of a function that prints the area of a rectangle.
It has two *parameters* (``x`` and ``y``), which correspond to the rectangle's height and width.

.. code:: phon

    function area(x, y)
        print("The area of the rectangle is", x * y)
    end


We can then *call* the function with specific values (called *arguments*) for ``x`` and ``y``, using parentheses after the name of the function and passing the arguments to the
function by putting them inside the parentheses:

.. code:: phon

    area(100, 30) # prints 3000


In addition to executing statements, functions can also send a value back to the caller. This is achieved with the keyword ``return``
followed by the expression we want to send back to the caller. Let's rewrite the above code in a slightly different way:


.. code:: phon

    function area(x, y)
        return x * y
    end

    var a = area(100, 30)
    print("The area of the rectangle is {a}")


In this new example, the function ``area`` is only responsible for computing the area and returning the value. All the printing is done
outside the function.


Note: Functions are first class values in Phonometrica, which means that they can be assigned to variables, passed as function arguments to
other functions, and even used as a return value inside a function.

.. _funcparam:

Function parameters
-------------------

Our function ``area`` takes 2 parameters, ``x`` and ``y``, which we expect to be numbers. But what happens if we inadvertantly pass a value
that has a different type?


.. code:: phon

    function area(x, y)
        return x * y
    end

    var a = area(100, "30")


Phonometrica will throw an error because it can't apply the math operator ``*`` to a number and a string. But we might not always
be that lucky, and we might accidently introduce subtle and hard-to-find bugs if we pass the wrong type of argument and Phonometrica proceeds
with it without detecting that there is a problem.

Fortunately, Phonometrica allows us to minimize this kind of problem by specifying a type for each parameter. If we don't specify a type for a parameter,
Phonometrica will implicitly assign it the type ``Object``, which is the base type for all Phonometrica types. Our function could have
equivalently been written as follows:

.. code:: phon

    function area(x as Object, y as Object)
        return x * y
    end

The two forms are strictly equivalent: the former is shorter to type, the latter is more explicit. When the type of a parameter is ``Object``,
any value can be passed because all types inherit from Object, directly or indirectly. To make our code more robust, we could limit the
types of the parameters to numbers. This is done as follows:

.. code:: phon

    function area(x as Number, y as Number)
        return x * y
    end


If we now try to call the function with a number and a string:

.. code:: phon

    var a = area(100, "30")


Phonometrica will not even enter the function; it will give us a clear error message:

.. code::

    Error on line 5:
    [Dispatch error] no applicable method for 'area'


Type information is optional: if a parameter can accept any value, you can simply omit the type (or declare the type as ``Object`` to make your
intent clearer). Omitting type information can also save you some typing for small scripts. For scripts that you intend to redistribute,
however, we strongly encourage you to add type information because it will make your code more robust and will clarify the
intended use of your functions.


.. _function-overloading:

Function overloading
--------------------

Suppose that we want to create a function to concatenate two values. We want it to work with either two strings or two lists. One approach
would be to create a function that accepts two objects, and then decides what to do depending on the type of the objects, which we can
test with the ``is`` operator:

.. code:: phon

    function concat(x, y)
        if x is String and y is String then
            return x & y
        elsif x is List and y is List then
            var result = []
            for v in x do
                append(result, v)
            end
            for v in y do
                append(result, v)
            end

            return result
        else
            throw Error("Invalid types in concat()")
        end
    end


This approach works, but it is really tedious. Phonometrica offers a cleaner and more robust alternative: *function overloading*. Every
named function in Phonometrica is in fact a *generic function*: declaring another function with the same name, in the same scope, adds a
new *overload* to the generic function, as long as it has a different number of parameters and/or different parameter types. We can thus
rewrite our big function as two smaller functions:

.. code:: phon

    function concat(x as String, y as String)
        return x & y
    end

    function concat(x as List, y as List)
        var result = []
        for v in x do
            append(result, v)
        end
        for v in y do
            append(result, v)
        end

        return result
    end

We no longer need to take care of the error case ourselves, because Phonometrica will do it automatically for us. For example, if we try to
call ``concat`` with two integers:

.. code:: phon

    concat(3, 5)

We will get the following error:

.. code::

    Error on line 16:
    [Dispatch error] no applicable method for 'concat'




To find out which overload it should call, Phonometrica considers all the overloads that are *applicable*, i.e. those whose parameter count
matches the call and whose parameter types are each a base type of (or the same type as) the corresponding argument's type, and picks the
*most specific* one. For instance, if one overload expects an ``Object`` and another expects a ``Float``, and the function is called with a
``Float`` argument, the second overload wins because ``Float`` is more specific than ``Object`` (``Float`` inherits from ``Number``, which
inherits from ``Object``). If an argument's type doesn't inherit from the corresponding parameter type, the overload is not applicable; if
no overload is applicable, Phonometrica reports a dispatch error, as we saw above. Note that the value ``null`` only matches parameters
declared as ``Object`` (or left untyped, which is the same thing).

It is sometimes the case that two overloads are equally specific for some calls. Consider the following example:

.. code:: phon

    function test(x as Integer, y as Number)
        return 1
    end

    function test(x as Number, y as Integer)
        return 2
    end

Neither overload is more specific than the other: for a call such as ``test(1, 2)``, the first one is a better match for ``x`` and the
second one is a better match for ``y``. Phonometrica detects this problem as soon as the second overload is *declared* — before any call is
even attempted — and reports the following error:


.. code::

    Error on line 5:
    [Type error] ambiguous definition of 'test'


To solve this problem, we must provide a third overload that is more specific than the two others, so that calls like ``test(1, 2)`` have an
unambiguous best match. The disambiguating overload must already be visible when the conflicting overload is declared, so we declare it
first:


.. code:: phon

    function test(x as Integer, y as Integer)
        print("Now this works!")
    end

    function test(x as Integer, y as Number)
        return 1
    end

    function test(x as Number, y as Integer)
        return 2
    end



Value and reference parameters
------------------------------


:ref:`Clonable types <clonability>` in Phonometrica have value semantics, which means that assigning a variable to another one copies its
value. Value semantics extends to function parameters: by default, function parameters are *passed by value*. Suppose we want to create
a function that appends an element to a list but ensures that the element is not ``null``. We could write it like that:

.. code:: phon

    function append_item(list as List, item as Object)
        if item == null then
            throw Error("Cannot append a null item")
        end
        append(list, item)
    end


However, if we try to use it, it will not work as expected:

.. code:: phon

    var lst = [1, 2, 3, 4]
    append_item(lst, 5)
    print(lst) # prints [1, 2, 3, 4]


Since the ``List`` type has value semantics, a copy of ``lst`` will be passed to ``append_item``, and this copy (``list``) will be modified
but the original value will be unaffected. For our function to be able to work as intended, we need the first argument to be *passed by reference*.
This is achieved by adding the keyword ``ref`` before the corresponding parameter:

.. code:: phon

    function append_item(ref list as List, item as Object)
        if item == null then
            throw Error("Cannot append a null item")
        end
        append(list, item)
    end

    var lst = [1, 2, 3, 4]
    append_item(lst, 5)
    print(lst) # prints [1, 2, 3, 4, 5]


Note that the call site does not change: whether an argument is passed by reference is determined entirely by the function's signature.
The argument passed for a ``ref`` parameter must be something that can be written to — a variable, a list or table element (``lst[i]``),
or an object field — so that the function's mutations are visible to the caller.


.. _named_arguments:

Named arguments
---------------

Many functions need a handful of optional settings in addition to their main inputs. Phonometrica lets you declare such settings as
*optional parameters*: parameters with a default value, declared after the function's regular parameters.

Suppose we want a function that prints a value with some formatting options
(a prefix, a suffix, and whether to upper-case the value). We can write it like
this:

.. code:: phon

    function show(text as String, prefix as String = ">>", suffix as String = "", upper as Boolean = false)
        if upper then
            text = to_upper(text)
        end

        print(prefix & " " & text & suffix)
    end


The parameters ``prefix``, ``suffix`` and ``upper`` all have a default value, so the caller may omit any of them. They are filled at the
call site as **named arguments**, using the ``name = value`` syntax:

.. code:: phon

    show("hello")                               # prints: >> hello
    show("hello", prefix = "==>", upper = true) # prints: ==> HELLO


Optional parameters are **keyword-only**: they can *never* be filled positionally. A call such as ``show("hello", "==>")`` is an error,
because positional arguments only map to the function's regular (required) parameters. This restriction is what makes calls with many
options self-documenting — you always see the name of each setting at the call site — and it also keeps overload resolution simple, as
explained below.

A few rules to keep in mind:

* Optional parameters must be declared after all the required parameters:
  ``function f(x as Integer = 1, y as String)`` is an error.
* The name on the left of ``=`` must be one of the function's optional
  parameters: ``show("hi", bogus = 1)`` raises ``[Argument error] no such
  option 'bogus'``.
* Once you start using named arguments, any further argument must also be
  named: ``f(1, x = "hi", 99)`` is rejected, but ``f(1, x = "hi", y = 99)``
  is fine.
* The value on the right of ``=`` can be any expression, including another
  function call: ``f(label = to_upper(name))``.

Named arguments interact cleanly with :ref:`function overloading <function-overloading>`: optional parameters do **not** participate in
dispatch. Only the positional arguments of a call are used to select the overload; the named arguments are then matched against the
selected overload's optional parameters. As a result, no ambiguity can ever arise between a positional argument and an option.



.. _closures:



Closures
--------


Functions can be defined inside other functions. Such nested functions have access to their enclosing scope(s): as a result, they can *capture* variables in their environment (*non-local* variables) and
keep a reference to them, even if they go out of scope. Such functions are called *closures*. Consider the following example:

.. code:: phon

    function make_counter()
        var x = 0
        function inner()
            x += 1
            return x
        end

        return inner
    end

    var counter1 = make_counter()
    var counter2 = make_counter()
    print(counter1()) # prints 1
    print(counter1()) # prints 2
    print(counter1()) # prints 3
    print(counter2()) # prints 1


Let's go through the above code chunk to understand what it does. When we create ``counter1``, we execute the function ``make_counter``, which first creates a variable named ``x`` and then creates a function named ``inner``, which
*captures* ``make_counter``'s local variable ``x``. Finally, ``make_counter`` returns the function ``inner``. This means that ``counter1`` is now a function (the function ``inner``). When we initialize ``counter2``, we call ``make_counter`` again: it will create a new variable
named ``x`` and a new function named ``inner``, which it will return. As a result, ``counter1`` and ``counter2`` each have their own "version" of ``inner`` and ``x``. Each time a counter is called,
it will call its own version of ``inner``, which will increment its own version of ``x``. Functions which can capture non-local variables, such as ``inner`` in this example, are called *closures*.

Closures are a powerful construct that allows us to create *stateful* functions, that is functions that can retain state across calls. In the above example, the state is the counter represented by
the variable ``x``. In the above example, a closure was used to create a *generator*, i.e. a function that generates a new value every time it is called, depending on its internal state.
Here is another example of a closure which generates the next number in the Fibonacci sequence every time it is called.

.. code:: phon

    function fibonacci()
        var first = 0
        var second = 0

        function fib()
            if first == 0 then
                first = 1
                second = 1
                return 0
            else
                var current = first
                var tmp = second
                second = first + second
                first = tmp

                return current
            end
        end

        return fib
    end

    var f = fibonacci()

    for i = 1 to 10 do
        print(f())
    end


Function expressions
--------------------

Another way to use functions is to create a *function expression*. Function expressions are anonymous functions which can be used like
any other expression. As an example, the following function:

.. code:: phon

    function area(x as Number, y as Number)
        return x * y
    end

could be written equivalently as:

.. code:: phon

    var area = function(x as Number, y as Number)
        return x * y
    end

The advantage of function expressions is that you can use them wherever you can use an expression, for instance as the return value of another
function:

.. code:: phon

    function make_counter(start as Integer)
        return function()
            var n = start
            start += 1
            return n
        end
    end

    var counter = make_counter(10)
    print(counter()) # prints 10
    print(counter()) # prints 11


As you can see, in this example, we create a closure that captures the non-local variable ``start``, but this closure is an anonymous function expression,
which we can return directly.

When an anonymous function consists of a single expression, Phonometrica offers an even more compact notation: the *lambda arrow* ``->``.
The expression on the right of the arrow is the function's return value:

.. code:: phon

    var double = x -> x * 2
    print(double(21)) # prints 42

Lambdas are particularly convenient when a short function is passed as an argument to another function:

.. code:: phon

    function apply_twice(f as Function, x as Object)
        return f(f(x))
    end

    print(apply_twice(x -> x * 2, 10)) # prints 40

Note: declaring a named function and assigning a function expression to a variable are similar but not identical. A named ``function``
declared at the top level of a script adds an overload to a (public) generic function, which supports :ref:`overloading <function-overloading>`
and is visible to scripts that import yours (declare it with ``local function`` to keep it private to your script). A named function declared
*inside* another function is local to that function, just like a variable. A function expression assigned to a variable, on the other hand,
is just a plain value stored in a variable: assigning another function with the same name replaces the previous one instead of overloading it.


Implicit return values
----------------------

As we saw above, we can explicitly return a value from a function using the keyword ``return``. If a function terminates without executing
a ``return`` statement, it implicitly returns the value ``null``. For instance, the following piece of code is valid:

.. code:: phon

    function do_nothing()
    end

    var x = do_nothing()
    assert(x == null)

Note that the result of an expression statement is *not* used as an implicit return value: a function such as the following also returns
``null``, because the value ``3`` is simply discarded:

.. code:: phon

    function test()
        3
    end

    assert(test() == null)

If you want a function to return the value of an expression, use ``return`` explicitly (or a :ref:`lambda <closures>`, whose single
expression is always its return value):

.. code:: phon

    function modify(ref strings as List, f as Function)
        for i = 1 to len(strings) do
            strings[i] = f(strings[i])
        end
    end

    var names = ["toto", "tata", "titi"]
    modify(names, x -> x & ".txt")
    print(names) # prints [toto.txt, tata.txt, titi.txt]

