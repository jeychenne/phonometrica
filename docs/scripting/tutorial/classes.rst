Classes and instances
=====================

Coding with class
-----------------

Sometimes, you need to define an aggregate structure because you need to treat a bundle of several values as a single unit. A very simple example would a two-dimensional point,
which has *x* and *y* coordinates. One way to do this would be to use a ``Table`` with strings as keys and numbers as values:

.. code:: phon

    let pt = {"x": 5, "y": 10}
    print "x = ", pt["x"]
    print "y = ", pt["y"]


If you use a lot of points, you could create a *factory* function that creates new points:

.. code:: phon

    function create_point(x as Number, y as Number)
        return {"x": x, "y": y}
    end

    let pt = create_point(5, 10) # same as above


This is a perfectly valid thing to do, and ``Table`` is a very flexible and useful data structure which can be used in many ways. Nevertheless, this approach has a few limitations.
First, it is relatively cumbersome: some languages do provide some ``syntactic sugar`` for tables to make this type of use case easier, but because tables in Phonometrica are based
on the JSON specification, this is not possible here. More importantly, this approach is not particularly robust because (a) nothing prevents you from adding or removing values 
from the table after it has been created and (b) there is no way for a function to know that it should expect a point, with *x* and *y* coordinates, and nothing else. Suppose we have the following code:

.. code:: phon

    function create_point(x as Number, y as Number)
        return {"x": x, "y": y}
    end

    function print_point(p as Table)
        print "(", p["x"], ",", p["y"], ")"
    end

    let pt = create_point(5, 10) # same as above
    ... # A lot of code
    remove(p, "x") # a small mistake somewhere
    print_point(p) # boom! "x" no longer exists


While this example may look contrived, it is easy to make this kind of mistake in relatively large codebases, and it requires a lot of discipline to avoid them. 

Fortunately, there is an alternative: like many modern programming languages, Phonometrica's scripting language lets you define **classes** to create new types.

Overview of classes
-------------------

Phonometrica's scripting language is an *object-oriented* programming language. This means that all values, including primitive values such as integers and Booleans,
are *objects* on which we can perform computations using functions. Every type in Phonometrica is represented by a *class*, which is kind of "blueprint" that allows
us to create *instances* of the type it defines. For example, the following code creates instances of the classes Integer and String, with values ``5`` and ``hello``, respectively.

.. code:: phon

    let i = 5
    let s = "hello"


Each class may define a number of *fields* (also called *attributes* or *properties*) which can be read and sometimes written to. For instance, the class ``String`` has a read-only field named ``length``, which corresponds to the number of characters it contains.
The fields of a class can be accessed with dot operator ``.``.


.. code:: phon

    let s = "été"
    print s.length # prints 3


In addition to using built-in classes, Phonometrica allows you to define your own classes with the keyword ``class``. Here is a minimal example: 


.. code:: phon

    class Point
    end


Executing will create an (empty) class named ``Point``, which can now be used to create instances of that class. (Note that by convention, the name of a class starts with an upper-case letter.)
This class will be global in scope but just like functions, you can declare a local class by adding ``local`` before class, in which case the class will only be visible in the current scope:

.. code:: phon

    local class Point
    end


Since this class is empty, it is not really useful. We can add fields by simply declaring them inside the class declaration, one per line:

.. code:: phon

    class Point
        x
        y
    end


This class now has two fields, named ``x`` and ``y``. Since we haven't assigned them any value, their value will default to ``null``, just like regular variables. 
To ensure that any newly created point has sensible values for its fields, we can assign them a default value when we declare them:

.. code:: phon

    class Point
        x = 0
        y = 0
    end


Assigning a default value to each field is recommended since it ensures that every newly created instance is in a valid state. To create an instance, 
we need to call the class' *constructor*, which is responsible for creating the instance of the class and initializing it. To do this, we call the name of the class
as if it was a function:



.. code:: phon

    class Point
        x = 0
        y = 0
    end

    let p = Point()
    print p # prints "<instance of Point at 0x6000024641b0>"
    print p.x # prints 0


In the above code, the call to ``Point()`` will return a new instance of the class ``Point``, and we can now access its fields using the dot operator. By default, fields 
can be read and written to, so we could modify the values of our point: 

.. code:: phon

    p.x = 5  # x now has the value 5
    p.y = 10 # y now has the value 10


Note that the fields are specific to the instance of the class: this means that modifying the value of an instance's field only changes the value for that instance (``p``` in this case).


Methods and initializers
------------------------

Default initialization of fields is convenient, but sometimes we might want to be able to initialize our new instance with custom values. One possibility would of course 
be to create a function outside of the class and call it to initialize the instance, and we could perform some checks to ensure that the values are valid. For example, if 
we want to ensure that points have non-negative coordinates, we could do something like that:


.. code:: phon

    class Point
        x = 0
        y = 0
    end

    function create_point(x as Number, y as Number)
        assert x >= 0 and y >= 0, "x and y must be non-negative"
        let p = Point()
        p.x = x
        p.y = y

        return p
    end

    let p = create_point(10, 30)

While this would work, it is a bit cumbersome and, perhaps more importantly, it decouples the initialization step from the class. Fortunately, 
there is a better approach: Phonometrica lets you define a special kind of functions *inside* the class: such functions are called *methods*, and are created with the keyword ``method``.
Methods are special in that they always have an implicit argument, named ``this``, which represents the instance of the class, and Phonometrica only recognizes 
a small number of methods which relate to the internal state and representation of a type.

The most important method is called `ìnitialize`` and, as its name suggests, it is responsible for intializing an instance. In fact, if there is no user-defined initialize method, 
Phonometrica will automatically create an empty one which takes no argument, so that an instance with default values can be created, as in the example above. But we could define our 
own intializer: 

.. code:: phon

    class Point
        x = 0
        y = 0
    
        method initialize(x as Number, y as Number)
            assert x >= 0 and y >= 0, "x and y must be non-negative"
            this.x = x
            this.y = y
        end
    end

    let p = Point(10, 30)


This code produces the same result as the previous code snippet, but it is more concise and the intialization code is now part of the class, which makes it easier to reason about.
Notice that we do not need to (and in fact, we can't) declare the ``this`` variable, which represents the instance of the Point class being initialized. Also note that we
can't return a value from an initializer, since this value would be ignored anyway. 
Like functions, methods can be overloaded. This means that we can create different versions of ``initialize`` with different signatures. In fact, in the example above, 
Phonometrica still creates a default initializer with no argument, so that calling ``Point()`` would still work and create a point with ``x`` and ``y`` set to 0. We could
replace this default initializer by our own if we wanted:

.. code:: phon

    class Point
        x = 0
        y = 0

        method initialize()
            print "Calling default initializer"
        end
    
        method initialize(x as Number, y as Number)
            assert x >= 0 and y >= 0, "x and y must be non-negative"
            this.x = x
            this.y = y
        end
    end

    let p = Point() # prints "Calling default initializer"


Whether your create your own inializer(s) or not, Phonometrica will always pre-initialize the fields of an instance with the value they were assigned when they were declared, or ``null`` if they were not assigned any value,
before calling any initializer. In the example above, this means that ``x`` and ``y`` are already set to 0 when we enter ``initialize()``.

.. code:: phon

    class Point
        x = 0
        y = 0

        method initialize()
            print this.x, ",", this.y # prints "0,0" 
        end
    
        method initialize(x as Number, y as Number)
            print this.x, ",", this.y # prints "0,0" 
            assert x >= 0 and y >= 0, "x and y must be non-negative"
            this.x = x
            this.y = y
        end
    end


String representation of classes
--------------------------------

Another useful method is ``to_string()``, which takes no argument and must return a string representation of the instance of the class it is attached to. This method 
will be called automatically whereever a string representation is expected. By default, printing an instance of our ``Point`` class would produce something like 
``<instance of Point at 0x6000024641b0>``, where ``at 0x6000024641b0`` represents the memory address of the object. This is better than generating an error but not particularly useful. We could extend our class with a ``to_string()`` method:

.. code:: phon

    class Point
        x = 0
        y = 0

        method initialize()
            print this.x, ",", this.y # prints "0,0" 
        end
    
        method initialize(x as Number, y as Number)
            print this.x, ",", this.y # prints "0,0" 
            assert x >= 0 and y >= 0, "x and y must be non-negative"
            this.x = x
            this.y = y
        end

        method to_string()
            return "Point(" & this.x & "," & this.y & ")" 
        end
    end

This will now provide a much more informative representation of the point:

.. code:: phon

    let p = Point(10, 30)
    print p # prints "Point(10,30)"


Value vs reference types
------------------------

By default, user-defined types have value semantics. This means that two instances of a class are considered equal if all their fields are equal, and copying an instance will copy its content. Consider the following code:

.. code:: phon

    class Point
        x = 0
        y = 0
        
        method initialize(x as Number, y as Number)
            this.x = x
            this.y = y
        end
    end

    let p1 = Point(10, 5)
    let p2 = Point(10, 5)
    assert p1 == p2
    let p3 = p1
    p1.x = 100
    assert p3.x == 10


As we can see, even though ``p1`` and ``p3`` are different instances of the class ``Point``, they are considered equal because they have the same coordinates. Furthermore, 
if we create a copy of a point (``p3``) and later modify the original instance, as we did with ``p1`` here, the copy is unaffected and preserves the values it had when it was assigned.
If you need to variables to share the same value, you can still you ``ref``, like you would do with built-in value types such as ``String`` or ``List``:


.. code:: phon

    let p1 = Point(10, 5)
    let p2 = ref p1
    p1.x = 100
    assert p2.x == 100


Value semantics is the default behaviour because it makes code safer and easier to reason about, since mutations are local to the variable that is being modified (unless you explicitly use ``ref``).
This behaviour works for many types, such as our ``Point`` example: you would expect two points to be considered equivalent if they have the same coordinates, no matter they 
correspond to the same instance. Sometimes, however, you do want reference semantics, because each instance should be considered unique in some way, and clonability doesn't really make sense for that type. 
Suppose you are implementing a graph: you would probably want each node and edge to be unique, so that for example several edges can reference the same node. 
Phonometrical lets you create reference types by adding the keyword ``ref`` before ``class`` when you declare your type. Here's a (minimalistic) example for nodes and edges with reference semantics:

.. code:: phon

    ref class Node
        label = ""
        edges = []

        method initialize(label as String)
            this.label = label
        end
    end

    ref class Edge
        source
        target

        method initialize(source as Node, target as Node)
            this.source = source
            this.target = target
        end
    end


When trying to decide whether you should use a value type or a reference type, remember that a value type holds its own data, so copying it makes a new independent copy,
whereas a reference type points to shared data, so changes affect all references. Phonometrica optimizes value copying, so don't choose a reference to make things faster: 
in general, use a reference type if you need shared data, otherwise use a value type.

Using classes as function parameters
------------------------------------

In the example we took at the beginning of this tutorial, our point object was represented with a table. Suppose that we want to define a function that reintializes a point. If we use a table, 
we have no way to distinguish tables that store a point from other tables, so we might need to be particularly careful when we pass table-as-a-point to a function. However, once we have defined 
a new ``Point`` class, we can use it as a parameter for a function:

.. code:: phon

    function reinitialize(ref p as Point)
        if p then
            p.x = 0
            p.y = 0
        then
    end


We can now be sure that only points (or the value ``null``) will be passed to this function, so that it's guaranteed to have all the attributes of a ``Point``.

Methods and functions
---------------------

Methods and functions are used encode bahaviour, so you may wonder why both exist and when to choose one over the other. Programming languages differ with respect to 
the amount of "object-orientedness" they allow: some languages like C and early versions of Pascal do not have methods at all, whereas others such as Java do not have 
functions defined outside of classes. Others, such as Python, allow both. 

Phonometrica differs from the majority of object-oriented programming languages in that it is based on *multiple dispatch* (see :ref:`page-functions`). This means that it allows several versions of 
a function to coexist as long as they have a different signature, and it will choose the correct function based on the number and type of arguments passed to the function. 
Because of this design choice, most behaviour should (and in fact, must) be encoded using functions, which are always defined outside of a class. Methods are reserved for 
very specific behaviour associated with the internal state or representation of an object. As a result, there is a fixed and very limited set of methods that can be defined 
for a class. Currently, these are ``initialize`` to initialize an object, and ``to_string`` to provide a meaningful string representation of the object. If you need implement
any of these behaviours, use methods defined inside the class. For everything else, use functions defined outside of the class. 

