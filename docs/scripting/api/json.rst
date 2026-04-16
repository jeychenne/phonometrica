JSON
====

General concepts
----------------

Phonometrica objects can be converted to JSON (Javascript Object Notation), a lightweight declarative format which is
often used to store data. See http://www.json.org.

Functions
---------

.. module:: json

.. function:: stringify(object)

Convert ``object`` to a string according to the JSON specification.

.. code:: phon

    let o = {
        "name": "John",
        "pi": 3.14
    }

    let s = json.stringify(o)
    print(s)


------------

.. function:: parse(str)

Convert the string ``str`` to an object according to the JSON specification.

.. code:: phon

    let s = '{"name":"John", "pi":3.14}'
    let o = json.parse(s)
    print("The value of pi is " + o.pi)


