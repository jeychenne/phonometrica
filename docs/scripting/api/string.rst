String processing
=================

This page documents the ``String`` type. ``String`` is :ref:`clonable <clonability>`.

General concepts
----------------

A ``String`` is a sequence of characters enclosed between single or double quotes.
Double-quoted strings support interpolation with ``{expr}`` and escape sequences,
whereas single-quoted strings are *raw*: no interpolation and no escape processing,
which makes them ideal for regular expressions and Windows paths.
Strings in Phonometrica are mutable, which means that some
functions allow you to modify them directly: such functions take the string as a
``ref`` parameter, modify it in place and return nothing.

All string functions assume that strings are encoded according to the
UTF-8 `Unicode <http://www.unicode.org>`_ standard. A good tutorial
about UTF-8 can be found at the following address:
`http://www.zehnet.de/2005/02/12/unicode-utf-8-tutorial <http://www.zehnet.de/2005/02/12/unicode-utf-8-tutorial>`_.
In the remainder of this document, the term *character* is used to mean
*extended grapheme cluster* in the sense of the Unicode specification. This generally corresponds to the notion
of "user-perceived character". Positions in a string are 1-based character indices.


Functions
---------


.. function:: append(ref string as String, suffix as String)

Inserts ``suffix`` at the end of ``string``. The string is modified in place;
nothing is returned.

.. code:: phon

    var s = "hello"
    append(s, " world")
    print(s) # prints "hello world"

See also: :func:`prepend`

------------

.. function:: char(string as String, pos as Integer)

Get character at position ``pos``. If ``pos`` is negative, counting starts from the end.

.. code:: phon

    print(char("hello", 2))  # prints "e"
    print(char("hello", -1)) # prints "o"


------------

.. function:: contains(string as String, substring as String)

Returns ``true`` if ``string`` contains ``substring``, and ``false``
otherwise.


------------

.. function:: count(string as String, substring as String)

Returns the number of times ``substring`` appears in ``string``.

.. code:: phon

    var s = "cacococococa"
    var n = count(s, "coco")

    print(n) # prints "2"

Note: matches don't overlap.


------------

.. function:: ends_with(string as String, suffix as String)

Returns ``true`` if the string ends with ``suffix``, and ``false`` otherwise.

See also: :func:`starts_with`


------------

.. function:: find(string as String, substring as String)

Returns the start position of ``substring`` in ``string``, or 0 if it is not found. Searching
proceeds from left to right.

.. code:: phon

    assert(find("hello", "l") == 3)
    assert(find("hello", "zz") == 0)


.. function:: find(string as String, substring as String, pos as Integer)

Returns the start position of ``substring`` in ``string``, or 0 if it is not found. Searching
proceeds from left to right, starting at ``pos``.

------------

.. function:: is_empty(string as String)

Returns ``true`` if the string is empty, and ``false`` otherwise.

------------

.. function:: left(string as String, n as Integer)

Get the substring corresponding to the ``n`` first characters of the
string.

------------

.. function:: len(string as String)

Returns the number of characters in the string.

.. code:: phon

    var s = "안녕하세요"
    print(len(s)) # prints "5"

------------

.. function:: ltrim(ref string as String)

Removes whitespace characters at the left end of the string. The string is
modified in place.

.. code:: phon

    var s = "  hello  "
    ltrim(s)
    print("[{s}]") # prints "[hello  ]"

See also: :func:`trim`, :func:`rtrim`


------------

.. function:: prepend(ref string as String, prefix as String)

Inserts ``prefix`` at the beginning of ``string``. The string is modified in
place; nothing is returned.

See also: :func:`append`

------------

.. function:: remove(ref string as String, sub as String)

Removes all (non-overlapping) instances of the substring ``sub``. The string is
modified in place.

.. code:: phon

    var s = "cacococococa"
    remove(s, "coco")
    print(s) # prints "caca"

See also: :func:`replace`


------------

.. function:: replace(ref string as String, old as String, new as String)

Replaces all (non-overlapping) instances of the substring ``old`` by ``new``.
The string is modified in place.

.. code:: phon

    var s = "aaa bbb aaa"
    replace(s, "aaa", "X")
    print(s) # prints "X bbb X"

See also: :func:`remove`


------------

.. function:: reverse(ref string as String)


Reverses all characters in the string. The string is modified in place.

.. code:: phon

    var s = "noël"
    reverse(s)
    print(s) # prints "lëon"


------------

.. function:: right(string as String, n as Integer)

Get the substring corresponding to the ``n`` last characters of the
string.


------------

.. function:: rtrim(ref string as String)

Removes whitespace characters at the right end of the string. The string is
modified in place.

.. code:: phon

    var s = "  hello  "
    rtrim(s)
    print("[{s}]") # prints "[  hello]"

See also: :func:`ltrim`, :func:`trim`

------------

.. function:: slice(string as String, from as Integer)

Returns the substring starting at index ``from`` until the end of the string.


------------

.. function:: slice(string as String, from as Integer, to as Integer)

Returns the substring starting at index ``from`` and ending
at index ``to`` (inclusive). If ``to`` equals ``-1``, returns the
substring from ``from`` until the end of the string.

.. code:: phon

    var s = "c'était ça"

    print(slice(s, 3, 7))  # "était"
    print(slice(s, 3, -1)) # "était ça"

.. note:: In the old scripting engine, the third argument of ``slice`` was a
   *count* of characters. It is now an **end position**.

------------

.. function:: split(string as String, delim as String)

Returns a ``List`` of strings which have been split at each occurrence of
the substring ``delim``. A leading, trailing or doubled separator yields an
empty field, so that ``split`` and :func:`join` round-trip.

.. code:: phon

    print(split("a,b,c", ","))  # prints "[a, b, c]"
    print(split("a,,b,", ","))  # prints "[a, , b, ]"


------------

.. function:: starts_with(string as String, prefix as String)

Returns ``true`` if the string starts with ``prefix``, and ``false`` otherwise.

See also: :func:`ends_with`


------------

.. function:: to_float(string as String)

Parses the whole (whitespace-trimmed) string as a ``Float`` and returns it.
An unparseable string raises an error.

.. code:: phon

    assert(to_float("3.5") == 3.5)

See also: :func:`to_int`


------------

.. function:: to_int(string as String)

Parses the whole (whitespace-trimmed) string as an ``Integer`` and returns it.
An unparseable string raises an error.

.. code:: phon

    assert(to_int("42") == 42)

See also: :func:`to_float`


------------

.. function:: to_lower(string as String)

Returns a copy of the string where each character has been converted to
lower case.

.. code:: phon

    var s1 = "C'ÉTAIT ÇA"
    var s2 = to_lower(s1)

    print(s2) # prints "c'était ça"

See also: :func:`to_upper`


------------

.. function:: to_upper(string as String)

Returns a copy of the string where each character has been converted to
upper case.

.. code:: phon

    var s1 = "c'était ça"
    var s2 = to_upper(s1)

    print(s2) # prints "C'ÉTAIT ÇA"

See also: :func:`to_lower`


------------

.. function:: trim(ref string as String)

Removes whitespace characters at both ends of the string. The string is
modified in place.

.. code:: phon

    var s = "\t  hello  \n"
    trim(s)
    print("[{s}]") # prints "[hello]"

See also: :func:`ltrim`, :func:`rtrim`
