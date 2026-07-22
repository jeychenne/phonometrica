Classes and instances
=====================

Coding with class
-----------------

Sometimes, you need to define an aggregate structure because you need to treat a bundle of several values as a single unit. A very simple example would a two-dimensional point,
which has *x* and *y* coordinates. One way to do this would be to use a ``Table`` with strings as keys and numbers as values:

.. code:: phon

    var pt = {"x": 5, "y": 10}
    print("x =", pt["x"])
    print("y =", pt["y"])


If you use a lot of points, you could create a *factory* function that creates new points:

.. code:: phon

    function create_point(x as Number, y as Number)
        return {"x": x, "y": y}
    end

    var pt = create_point(5, 10) # same as above


This is a perfectly valid thing to do, and ``Table`` is a very flexible and useful data structure which can be used in many ways. Nevertheless, this approach has a few limitations.
First, it is relatively cumbersome: some languages do provide some ``syntactic sugar`` for tables to make this type of use case easier, but because tables in Phonometrica are based
on the JSON specification, this is not possible here. More importantly, this approach is not particularly robust because (a) nothing prevents you from adding or removing values
from the table after it has been created and (b) there is no way for a function to know that it should expect a point, with *x* and *y* coordinates, and nothing else. Suppose we have the following code:

.. code:: phon

    function create_point(x as Number, y as Number)
        return {"x": x, "y": y}
    end

    function print_point(p as Table)
        print("({p["x"]}, {p["y"]})")
    end

    var pt = create_point(5, 10) # same as above
    ... # A lot of code
    remove(pt, "x") # a small mistake somewhere
    print_point(pt) # boom! "x" no longer exists


While this example may look contrived, it is easy to make this kind of mistake in relatively large codebases, and it requires a lot of discipline to avoid them.

Fortunately, there is an alternative: like many modern programming languages, Phonometrica's scripting language lets you define **classes** to create new types.

Overview of classes
-------------------

Phonometrica's scripting language is an *object-oriented* programming language. This means that all values, including primitive values such as integers and Booleans,
are *objects* on which we can perform computations using functions. Every type in Phonometrica is represented by a *class*, which is kind of "blueprint" that allows
us to create *instances* of the type it defines. For example, the following code creates instances of the classes Integer and String, with values ``5`` and ``hello``, respectively.

.. code:: phon

    var i = 5
    var s = "hello"


You can test which class a value belongs to with the ``is`` operator, which also takes inheritance into account:

.. code:: phon

    print(5 is Integer)  # prints true
    print(5 is Number)   # prints true, because Integer inherits from Number
    print(5 is String)   # prints false


Each class may define a number of *fields* (also called *attributes* or *properties*) which can be read and sometimes written to. The fields of an instance
are accessed with the dot operator ``.``, as we will see below.

In addition to using built-in classes, Phonometrica allows you to define your own classes with the keyword ``class``. Here is a minimal example:


.. code:: phon

    class Point
    end


Executing this will create an (empty) class named ``Point``, which can now be used to create instances of that class. (Note that by convention, the name of a class starts with an upper-case letter.)
A class declared at the top level of a script is public, which means that it is visible to scripts that import yours; just like functions, you can declare a private class by adding
``local`` before ``class``, in which case the class will only be visible in the current script:

.. code:: phon

    local class Point
    end


Since this class is empty, it is not really useful. We can add fields with the keyword ``field``, one per line:

.. code:: phon

    class Point
        field x
        field y
    end


This class now has two fields, named ``x`` and ``y``. Since we haven't assigned them any value, their value will default to ``null``, just like regular variables.
To ensure that any newly created point has sensible values for its fields, we can assign them a default value when we declare them:

.. code:: phon

    class Point
        field x = 0
        field y = 0
    end


Assigning a default value to each field is recommended since it ensures that every newly created instance is in a valid state. (You can also give a field an explicit
type with ``as``, e.g. ``field x as Number = 0``.) To create an instance,
we need to call the class' *constructor*, which is responsible for creating the instance of the class and initializing it. To do this, we call the name of the class
as if it was a function:



.. code:: phon

    class Point
        field x = 0
        field y = 0
    end

    var p = Point()
    print(p)   # prints "<Point>"
    print(p.x) # prints 0


In the above code, the call to ``Point()`` will return a new instance of the class ``Point``, and we can now access its fields using the dot operator. By default, fields
can be read and written to, so we could modify the values of our point:

.. code:: phon

    p.x = 5  # x now has the value 5
    p.y = 10 # y now has the value 10


Note that the fields are specific to the instance of the class: this means that modifying the value of an instance's field only changes the value for that instance (``p`` in this case).


Methods and initializers
------------------------

Default initialization of fields is convenient, but sometimes we might want to be able to initialize our new instance with custom values. One possibility would of course
be to create a function outside of the class and call it to initialize the instance, and we could perform some checks to ensure that the values are valid. For example, if
we want to ensure that points have non-negative coordinates, we could do something like that:


.. code:: phon

    class Point
        field x = 0
        field y = 0
    end

    function create_point(x as Number, y as Number)
        assert(x >= 0 and y >= 0, "x and y must be non-negative")
        var p = Point()
        p.x = x
        p.y = y

        return p
    end

    var p = create_point(10, 30)

While this would work, it is a bit cumbersome and, perhaps more importantly, it decouples the initialization step from the class. Fortunately,
there is a better approach: Phonometrica lets you define a special kind of functions *inside* the class: such functions are called *methods*, and are created with the keyword ``method``.
Methods are special in that they always have an implicit argument, named ``this``, which represents the instance of the class, and Phonometrica only recognizes
a small number of methods which relate to the internal state and representation of a type.

The most important method is called ``init`` and, as its name suggests, it is responsible for initializing an instance. If a class defines no ``init`` method,
Phonometrica provides a *default constructor* which takes no argument and sets every field to its default value, as in the example above. But we could define our
own initializer:

.. code:: phon

    class Point
        field x = 0
        field y = 0

        method init(x as Number, y as Number)
            assert(x >= 0 and y >= 0, "x and y must be non-negative")
            this.x = x
            this.y = y
        end
    end

    var p = Point(10, 30)


This code produces the same result as the previous code snippet, but it is more concise and the initialization code is now part of the class, which makes it easier to reason about.
Notice that we do not need to (and in fact, we can't) declare the ``this`` variable, which represents the instance of the Point class being initialized. Also note that we
can't return a value from an initializer, since this value would be ignored anyway.

Be aware that as soon as you define your own initializer, the automatic default constructor is no longer available: with the class above, calling ``Point()``
with no argument is now a dispatch error, because the only ``init`` method takes two numbers. Like functions, methods can be overloaded, so if you still want to
be able to create a default-initialized point, simply provide a no-argument ``init`` overload as well:

.. code:: phon

    class Point
        field x = 0
        field y = 0

        method init()
            print("Calling default initializer")
        end

        method init(x as Number, y as Number)
            assert(x >= 0 and y >= 0, "x and y must be non-negative")
            this.x = x
            this.y = y
        end
    end

    var p = Point() # prints "Calling default initializer"


Whether you create your own initializer(s) or not, Phonometrica will always pre-initialize the fields of an instance with the value they were assigned when they were declared, or ``null`` if they were not assigned any value,
before calling any initializer. In the example above, this means that ``x`` and ``y`` are already set to 0 when we enter ``init()``.

.. code:: phon

    class Point
        field x = 0
        field y = 0

        method init()
            print("{this.x},{this.y}") # prints "0,0"
        end

        method init(x as Number, y as Number)
            print("{this.x},{this.y}") # prints "0,0"
            assert(x >= 0 and y >= 0, "x and y must be non-negative")
            this.x = x
            this.y = y
        end
    end


String representation of classes
--------------------------------

Another useful method is ``to_string()``, which takes no argument and must return a string representation of the instance of the class it is attached to. This method
will be called automatically wherever a string representation is expected, for instance by ``print``, by string interpolation, or by the concatenation operator ``&``.
By default, printing an instance of our ``Point`` class produces the rather terse ``<Point>``. This is better than generating an error but not particularly useful. We could extend our class with a ``to_string()`` method:

.. code:: phon

    class Point
        field x = 0
        field y = 0

        method init(x as Number, y as Number)
            assert(x >= 0 and y >= 0, "x and y must be non-negative")
            this.x = x
            this.y = y
        end

        method to_string()
            return "Point(" & this.x & "," & this.y & ")"
        end
    end

This will now provide a much more informative representation of the point:

.. code:: phon

    var p = Point(10, 30)
    print(p) # prints "Point(10,30)"


Value vs reference types
------------------------

By default, user-defined types have value semantics: copying an instance behaves like copying its content, so mutations made through one variable are never
visible through another. Consider the following code:

.. code:: phon

    class Point
        field x = 0
        field y = 0

        method init(x as Number, y as Number)
            this.x = x
            this.y = y
        end
    end

    var p1 = Point(10, 5)
    var p3 = p1
    p1.x = 100
    assert(p3.x == 10)


If we create a copy of a point (``p3``) and later modify the original instance, as we did with ``p1`` here, the copy is unaffected and preserves the values it had when it was assigned.
(Behind the scenes, Phonometrica doesn't actually copy anything until one of the two variables is modified, so this is cheap.)

Note that equality between class instances is based on *identity*, not on content: two independently constructed instances are never equal, even if all their
fields hold the same values.

.. code:: phon

    var p1 = Point(10, 5)
    var p2 = Point(10, 5)
    assert(p1 != p2) # different instances, even though the coordinates are equal

If equality-by-content makes sense for your class, define an ordinary function that compares the relevant fields, for instance ``function same_location(a as Point, b as Point)``.

Value semantics is the default behaviour because it makes code safer and easier to reason about, since mutations are local to the variable that is being modified.
This behaviour works for many types, such as our ``Point`` example. Sometimes, however, you do want reference semantics, because each instance should be considered unique in some way, and several
variables should be able to observe mutations of the same underlying object.
Suppose you are implementing a graph: you would probably want each node and edge to be unique, so that for example several edges can reference the same node.
Phonometrica lets you create reference types by adding the keyword ``ref`` before ``class`` when you declare your type. Here's a (minimalistic) example for nodes and edges with reference semantics:

.. code:: phon

    ref class Node
        field label = ""
        field edges = []

        method init(label as String)
            this.label = label
        end
    end

    ref class Edge
        field source
        field target

        method init(source as Node, target as Node)
            this.source = source
            this.target = target
        end
    end

With a ``ref class``, assignment shares the instance instead of copying it:

.. code:: phon

    var n1 = Node("a")
    var n2 = n1        # n1 and n2 refer to the same node
    n1.label = "b"
    print(n2.label)    # prints "b"

When trying to decide whether you should use a value type or a reference type, remember that a value type holds its own data, so copying it makes a new independent copy,
whereas a reference type points to shared data, so changes affect all references. Phonometrica optimizes value copying, so don't choose a reference to make things faster:
in general, use a reference type if you need shared data, otherwise use a value type.

Inheritance
-----------

A class can inherit from another class with the keyword ``is``. The subclass has all the fields of its base class, plus the fields it declares itself,
and an instance of the subclass can be passed wherever the base class is expected:

.. code:: phon

    class Point2D
        field x = 0
        field y = 0
    end

    class Point3D is Point2D
        field z = 0
    end

    var p = Point3D()
    print(p.x, p.z)      # prints 0 0
    print(p is Point2D)  # prints true

Phonometrica supports *single* inheritance: a class has at most one base class.

Using classes as function parameters
------------------------------------

In the example we took at the beginning of this tutorial, our point object was represented with a table. Suppose that we want to define a function that reinitializes a point. If we use a table,
we have no way to distinguish tables that store a point from other tables, so we might need to be particularly careful when we pass table-as-a-point to a function. However, once we have defined
a new ``Point`` class, we can use it as a parameter for a function:

.. code:: phon

    function reinitialize(ref p as Point)
        p.x = 0
        p.y = 0
    end


We can now be sure that only points will be passed to this function, so that it's guaranteed to have all the attributes of a ``Point``. (Note that unlike a parameter
declared as ``Object``, a parameter declared with a specific class does not accept ``null``.)

Methods and functions
---------------------

Methods and functions are used encode behaviour, so you may wonder why both exist and when to choose one over the other. Programming languages differ with respect to
the amount of "object-orientedness" they allow: some languages like C and early versions of Pascal do not have methods at all, whereas others such as Java do not have
functions defined outside of classes. Others, such as Python, allow both.

Phonometrica differs from the majority of object-oriented programming languages in that it is based on *multiple dispatch* (see :ref:`page-functions`). This means that it allows several versions of
a function to coexist as long as they have a different signature, and it will choose the correct function based on the number and type of arguments passed to the function.
Because of this design choice, most behaviour should (and in fact, must) be encoded using functions, which are always defined outside of a class. Methods are reserved for
very specific behaviour associated with the internal state or representation of an object. As a result, there is a fixed and very limited set of methods that can be defined
for a class. Currently, these are:

- ``init``, to initialize a new instance;
- ``to_string``, to provide a meaningful string representation of the instance;
- ``get_item`` and ``set_item``, to make instances indexable with the bracket operator (``x[i]`` reads via ``get_item``, ``x[i] = v`` writes via ``set_item``);
- ``iterate`` and ``next``, to make instances iterable with ``for ... in`` loops.

If you need to implement any of these behaviours, use methods defined inside the class. For everything else, use functions defined outside of the class, taking the
instance as their first parameter.
