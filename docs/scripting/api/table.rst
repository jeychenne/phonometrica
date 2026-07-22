Tables
======

This page documents the ``Table`` type. ``Table`` is :ref:`clonable <clonability>`.

General concepts
----------------

A ``Table`` (also known as map, hash map, hash table, associative array or dictionary) is an **unordered** mapping of key/value pairs. Each key/value pair represents a *field*. Keys can be any clonable value (except ``null``), whereas values can be anything.
Tables can be declared with a table literal:

.. code:: phon

    var person = { "name": "john", "surname": "smith", "age": 38 }

In this example, ``person`` is declared with three pairs separated by commas: the key and the value are separated by the symbol ``:`` (colon). Note that there is no need for the keys and/or values to be homogeneous: any valid value (even ``null``!) may appear in a table.
Even though we declared the key/value pairs in a specific order in our example, a table is unordered: the iteration and key order are unspecified, and you should consider the order of the elements as random.

To create an empty table, use an empty table literal:

.. code:: phon

    var tab = {}
    assert(is_empty(tab))


To access any element of a table, you can use the index operator ``[]``. Reading
a key that is missing yields ``null``:

.. code:: phon

    var person = { "name": "john", "surname": "smith", "age": 38 }
    print(person["name"])       # prints "john"
    person["age"] = person["age"] + 1
    print(person["nickname"])   # prints "null"

Keys that are strings can also be accessed with dot notation: ``t.name`` reads
or writes the key ``"name"``. Note the difference for missing keys: a dot-read
of a key that doesn't exist is an *error*, whereas ``t["name"]`` yields
``null``. (Use :func:`contains` to distinguish a stored ``null`` from a missing
key.)

.. code:: phon

    var person = { "name": "john" }
    print(person.name)      # prints "john"
    person.city = "Paris"   # same as person["city"] = "Paris"
    # print(person.nickname) would raise a key error

Tables can be iterated with ``for k, v in table``, which binds each key/value
pair in turn (in an unspecified order):

.. code:: phon

    var scores = { "anna": 3, "ben": 5 }
    for k, v in scores do
        print("{k} -> {v}")
    end

If you need to process the table in sorted order, sort its keys explicitly
(assuming you have a table named ``tab``):

.. code:: phon

    var tab = { "b": 2, "a": 1, "c": 3 }
    var ks = keys(tab)
    sort(ks)
    for key in ks do
        var value = tab[key]
        # do something with the key and the value
        print("{key} = {value}")
    end


Functions
---------

.. function:: clear(ref table as Table)

Removes all the elements in the table.


------------

.. function:: contains(table as Table, key as Object)

Returns ``true`` if there is an element in the table whose key is equal to ``key``, and ``false`` otherwise.
This is the one way to distinguish a key whose stored value is ``null`` from a
missing key, since an indexed read yields ``null`` in both cases.


------------

.. function:: is_empty(table as Table)

Returns ``true`` if the table contains no element, and ``false`` otherwise.

------------

.. function:: keys(table as Table)

Returns the keys in the table as a ``List``, in an unspecified order.

.. code:: phon

    var tab = { "b": 2, "a": 1 }
    var ks = keys(tab)
    sort(ks)
    print(ks) # prints "[a, b]"

See also: :func:`values`

------------

.. function:: len(table as Table)

Returns the number of elements in the table.

------------

.. function:: remove(ref table as Table, key as Object)

Removes the element whose key is equal to ``key``. If there is no such element, this function does nothing.

------------

.. function:: values(table as Table)

Returns the values in the table as a ``List``, in an unspecified order.

See also: :func:`keys`
