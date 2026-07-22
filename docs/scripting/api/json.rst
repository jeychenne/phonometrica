JSON
====

General concepts
----------------

Phonometrica values can be converted to and from JSON (JavaScript Object Notation), a lightweight declarative format which is
often used to store data. See http://www.json.org.

The mapping between JSON and Phonometrica values is the natural one: a JSON
object maps to a ``Table`` (with string keys), an array to a ``List``, and a
number to an ``Integer`` when it has no fraction or exponent (and fits), or a
``Float`` otherwise. ``true``, ``false`` and ``null`` map to the corresponding
Phonometrica values.

Parsing JSON never evaluates code, so it is a safe way to load data from an
untrusted source. To load a JSON file, combine :func:`from_json` with
``read_file``:

.. code:: phon

    # var data = from_json(read_file("/path/to/data.json"))


Functions
---------

.. function:: to_json(value as Object)

Converts ``value`` to a string according to the JSON specification, in compact
form. Since a ``Table`` is unordered, the order of the keys in the output is
unspecified. A value that has no JSON representation (such as a ``Sound``, or a
non-finite number) raises an error.

.. code:: phon

    var o = { "name": "John", "pi": 3.14 }

    var s = to_json(o)
    print(s) # prints e.g. '{"name":"John", "pi":3.14}'


------------

.. function:: to_json(value as Object, indent as Integer)

Converts ``value`` to a JSON string, pretty-printed with ``indent`` spaces per
nesting level.

.. code:: phon

    print(to_json([1, 2], 4))

This prints::

    [
        1,
        2
    ]


------------

.. function:: from_json(str as String)

Parses the string ``str`` as a JSON document and returns the corresponding
value. An invalid document raises an error indicating the position of the
problem.

.. code:: phon

    var s = '{"name": "John", "pi": 3.14}'
    var o = from_json(s)
    print("The value of pi is {o.pi}")
