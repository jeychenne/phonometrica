Regular expressions
===================

This page documents the ``Regex`` and ``Match`` types. A ``Regex`` is an
immutable compiled pattern: matching it against a string produces a fresh
``Match`` object (or ``null``), so a ``Regex`` carries no state of its own and
can be shared and reused freely.

General concepts
----------------

Regular expressions are widely used in text processing to perform
pattern matching and pattern substitution. Simply put, a regular
expression (regex) is a string which describes a *set of strings*.
Suppose that we want to any of the following strings: ``"petit"``,
``"petite"``, ``"petits"``, ``"petites"``. Instead of looking for each
string separately, we can use a regular expression to look for any of
them. The corresponding regular expression would be ``'petite?s?'``.

.. tip:: Write regular expression patterns as single-quoted strings: single
   quotes create *raw* strings, in which backslashes and braces are not
   interpreted, so a pattern such as ``'\d+'`` can be written without
   double escaping.

Syntax
~~~~~~

Regular expressions always try to match a pattern from left to right; in
their simplest form, they match a sequence of (non-special) characters
and are equivalent in this case to a plain text search. Regular
expressions provide a number of special symbols and operators that can
match classes or sequences of characters. Here we only provide the most
useful ones:

-  ``.`` : match any character
-  ``^`` : match the beginning of a string
-  ``$`` : match the end of a string
-  ``[xyz]`` : match either of the characters ``x``, ``y`` or ``z``
-  ``[^xyz]`` : match any character except ``x``, ``y`` or ``z``
-  ``[a-z]`` : match any character in the range from ``a`` to ``z``
-  ``\b`` : match a word boundary
-  ``\s`` : match a white space character
-  ``\d`` : match a digit character (equivalent to ``[0-9]``)
-  ``\w`` : match a word character, including digits and ``_``
   (underscore)

In addition, regular expressions offer a number of quantifiers:

-  ``E?`` : match 0 or 1 occurrences of the expression E
-  ``E*`` : match 0 or more occurrences of the expression E
-  ``E+`` : match 1 or more occurrences of the expression E
-  ``E{n}`` : match exactly n occurrences of the expression E
-  ``E{n,m}`` : match between n and m occurrences of the expression E
-  ``E{n,}`` : match at least n occurrences of the expression E
-  ``E{,m}`` : match at most m occurrences of the expression E (and
   possibly 0)

In this context, an expression must be understood as either a character
(e.g. ``o{2,}`` matches the string ``"zoo"``) or a sequence of
characters enclosed by parentheses (e.g. ``(?:do){2}`` matches the
string ``"fais dodo"``). Another useful character is ``|``, which is
used to combine expressions (logical OR). For example, the pattern
``(?:est|était)`` will find all occurrences of the strings est and
était.

Regular expressions are "greedy" by default, which means they will match
the longest string that satisfies the pattern. For instance, given the
pattern ``j.*e``, which matches the character ``j`` followed by zero or
more characters followed by ``e``, and the string ``"je te l'ai dit"``,
a greedy search will return the substring ``"je te"`` by default.
Non-greedy search, on the other hand, will yield the substring ``"je"``
since it extracts the shortest string that satisfies the regular
expression. To enable non-greedy behavior, we must use the quantifier
``?`` after the star (in this case, ``'j.*?e'``).

A typical matching sequence looks like this:

.. code:: phon

    var re = Regex('^a(...)(..)(..)')
    var m = match(re, "abracadabra")

    if m then
        # Prints "bra", "ca", "da"
        for i = 1 to group_count(m) - 1 do
            print(group(m, i))
        end
    end


Construction
------------

.. function:: Regex(pattern as String)

Creates a new regular expression from a string pattern, with default options.
The regex can then be matched against any string with :func:`match`.
An invalid pattern raises an error.

.. code:: phon

    var re = Regex('^(..)')
    # Do something with re...

See also: :func:`pattern`


.. function:: Regex(pattern as String, flags as String)

Creates a new regular expression from a string pattern. The ``flags`` argument
can contain any of the following options, separated by the character ``|``:

- ``caseless``: ignore case
- ``multiline``: ``^`` and ``$`` match at internal line breaks as well
- ``dotall``: ``.`` also matches newline characters
- ``extended``: ignore unescaped whitespace and ``#`` comments in the pattern
- ``anchored``: only match at the start of the subject
- ``dollar_endonly``: ``$`` only matches at the very end of the subject
- ``ungreedy``: invert the greediness of quantifiers

.. code:: phon

    var re = Regex('^(..)', "caseless|multiline")
    # Do something with re...

See also: :func:`pattern`


Matching
--------

.. function:: match(regex as Regex, subject as String)

Matches ``regex`` against the string ``subject``. Returns a ``Match`` object
if there was a match, and ``null`` otherwise. Since ``null`` is the only
non-Boolean value that counts as false, the result can be tested directly
with ``if``:

.. code:: phon

    var re = Regex('\d+')
    var m = match(re, "abc 123 xyz")

    if m then
        print(group(m, 0)) # prints "123"
    end

See also: :func:`group`, :func:`group_count`


------------


.. function:: match(regex as Regex, subject as String, from as Integer)

Matches ``regex`` against the string ``subject``, starting at position ``from``
(a 1-based character index). Returns a ``Match`` object if there was a match,
and ``null`` otherwise.


Match accessors
---------------

.. function:: group(match as Match, nth as Integer)

Returns the ``nth`` captured sub-expression of ``match``. If ``nth`` equals
``0``, the whole matched string is returned. If group ``nth`` exists in the
pattern but did not participate in the match (for example, an optional group
that was not filled), ``null`` is returned. An out-of-range group index raises
an error.

.. code:: phon

    var re = Regex('^a(...)(..)(..)')
    var m = match(re, "abracadabra")

    print(group(m, 0)) # prints "abracada"
    print(group(m, 1)) # prints "bra"

See also: :func:`group_count`, :func:`groups`, :func:`match`

------------

.. function:: group_count(match as Match)

Returns the number of groups in the match, **including group 0** (the whole
match). A pattern with three capturing groups therefore yields 4.

.. code:: phon

    var re = Regex('^a(...)(..)(..)')
    var m = match(re, "abracadabra")
    assert(group_count(m) == 4)

.. note:: The old ``count()``/``len()`` functions on a regex returned the
   number of *captures* only; ``group_count`` counts the whole match too.

------------

.. function:: group_start(match as Match, nth as Integer)

Returns the position (1-based character index) of the first character of the
``nth`` group in the subject string. If ``nth`` equals ``0``, it returns the
start of the whole matched string. Returns 0 if the group did not participate
in the match.

See also: :func:`group_end`

------------

.. function:: group_end(match as Match, nth as Integer)

Returns the position (1-based character index) *just past* the last character
of the ``nth`` group in the subject string. If ``nth`` equals ``0``, this is
the position past the whole matched string. Returns 0 if the group did not
participate in the match.

.. code:: phon

    var subject = "abracadabra"
    var m = match(Regex('(...)$'), subject)
    var text = slice(subject, group_start(m, 1), group_end(m, 1) - 1)
    assert(text == "bra")

See also: :func:`group_start`

------------

.. function:: groups(match as Match)

Returns the captured sub-expressions as a ``List``: ``[group(m, 1), group(m, 2),
...]``. Group 0 (the whole match) is not included; a group that did not
participate in the match contributes ``null``.

.. code:: phon

    var m = match(Regex('^a(...)(..)(..)'), "abracadabra")
    print(groups(m)) # prints "[bra, ca, da]"


Regex accessors
---------------

.. function:: pattern(regex as Regex)

Returns the pattern (as a ``String``) from which the regular
expression was constructed.

.. code:: phon

    var re = Regex('\d+')
    assert(pattern(re) == '\d+')
