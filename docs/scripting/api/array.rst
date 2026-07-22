Numeric arrays
==============

The ``Array`` type represents a numeric array with one or two dimensions. All elements are stored
as floating-point numbers. Arrays are value types: assigning an array to another variable copies
the value (cheaply, via a shared buffer), so writing to the copy never affects the original.

Array literals
--------------

An array can be written literally with the ``@[ ... ]`` syntax. Elements are separated by commas,
and rows (for a two-dimensional array) by semicolons:

.. code:: phon

    var v = @[1, 2, 3]        # vector with 3 elements
    var M = @[1, 2; 3, 4]     # 2 by 2 matrix

A two-dimensional literal is filled row by row: ``@[1, 2; 3, 4]`` has ``1`` and ``2`` in its
first row, ``3`` and ``4`` in its second row.

Array indexing
--------------

To get or set an element in an array, use the index ``[]`` operator. Elements along each dimension start at 1 and can be negative.
(Negative indices start from the end of the dimension.)
Two-dimensional arrays are accessed with a pair of indices noted *(i, j)*,
where *i* represents the *i* th row and *j* represents the *j* th column. For example, index *(3, 2)* represents the item in the third
row in the second column, whereas *(1, -1)* represents element in the first row and in the last column.
Accessing an index outside of the array's dimensions raises an error.

.. code:: phon

    var X = zeros(3, 4)
    X[1, 4] = 10
    print(X[1, 4])


Global functions
----------------

.. function:: zeros(m [, n])

Returns a vector with ``m`` elements (or an ``m`` by ``n`` matrix) initialized to 0.
(The old ``Array(m [, n])`` constructor no longer exists: use ``zeros`` instead.)

------------

.. function:: ones(m [, n])

Returns a vector with ``m`` elements (or an ``m`` by ``n`` matrix) initialized to 1.

------------

.. function:: len(array as Array)

Returns the total number of elements in the array.

------------

.. function:: ndim(array as Array)

Returns the number of dimensions of the array (1 or 2).

------------

.. function:: nrow(array as Array)

Returns the number of rows in the array. For a one-dimensional array, this is the number of
elements.

------------

.. function:: ncol(array as Array)

Returns the number of columns in the array (1 for a one-dimensional array).

------------

.. function:: min(array as Array)

Returns the smallest element in the array. Calling ``min`` on an empty array raises an error.

------------

.. function:: max(array as Array)

Returns the largest element in the array. Calling ``max`` on an empty array raises an error.

------------

.. function:: clear(ref array as Array)

Sets all elements in the array to 0.

In addition, the math functions ``abs``, ``sqrt``, ``exp``, ``log``, ``sin``, ``cos``,
``floor`` and ``ceil`` accept an ``Array`` argument and return a new array with the function
applied element-wise. The scale conversion functions (``hertz_to_bark``, ``bark_to_hertz``,
``hertz_to_erb``, ``erb_to_hertz``, ``hertz_to_mel``, ``mel_to_hertz``, ``hertz_to_semitones``,
``semitones_to_hertz``) also accept arrays.

See also the statistical functions ``mean``, ``std``, ``sum``, and ``vrc`` documented in
:ref:`statistics <statistics-type>`.


Shape queries
-------------

Arrays have no fields: shape information is obtained with the global functions
:func:`len` (total number of elements), :func:`ndim`, :func:`nrow` and :func:`ncol`
described above. (In the old engine these were the attributes ``length``, ``ndim``,
``nrow`` and ``ncol``.)
