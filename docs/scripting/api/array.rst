Numeric arrays
==============

The ``Array`` type represents a numeric array with one or two dimensions.

Array indexing
--------------

To get or set an element in an array, use the index ``[]`` operator. Elements along each dimension start at 1 and can be negative.
(Negative indices start from the end of the dimension.)
Two-dimensional arrays are accessed with a pair of indices noted *(i, j)*,
where *i* represents the *i* th row and *j* represents the *j* th column. For example, index *(3, 2)* represents the item in the third
row in the second column, whereas *(1, -1)* represents element in the first row and in the last column.

.. code:: phon

    let X = Array(3, 4)
    X[1,4] = 10
    print(X[1,4])



Constructor
-----------


.. class:: Array

.. method:: Array(m [, n])

Constructs a vector with ``m`` elements or an ``m`` by ``n`` matrix. All elements are initialized to 0.


Global functions
----------------

.. function:: zeros(m [, n])

Returns a vector with ``m`` elements (or an ``m`` by ``n`` matrix) initialized to 0. This is equivalent
to ``Array(m)`` or ``Array(m, n)``.

------------

.. function:: ones(m [, n])

Returns a vector with ``m`` elements (or an ``m`` by ``n`` matrix) initialized to 1.

------------

.. function:: min(array as Array)

Returns the smallest element in the array.

------------

.. function:: max(array as Array)

Returns the largest element in the array.

------------

.. function:: clear(ref array as Array)

Sets all elements in the array to 0.

In addition, all standard math functions (``abs``, ``log``, ``sqrt``, ``exp``, ``sin``, ``cos``,
``tan``, ``acos``, ``asin``, ``atan``, ``ceil``, ``floor``, ``round``, ``log2``, ``log10``) accept
an ``Array`` argument and return a new array with the function applied element-wise. The scale
conversion functions (``hertz_to_bark``, ``bark_to_hertz``, ``hertz_to_erb``, ``erb_to_hertz``,
``hertz_to_mel``, ``mel_to_hertz``, ``hertz_to_semitones``, ``semitones_to_hertz``) also accept arrays.

See also the statistical functions ``mean``, ``std``, ``sum``, and ``vrc`` documented in
:ref:`statistics <statistics-type>`.


Fields
------

.. attribute:: length

Returns the total number of elements in the array.

.. attribute:: ndim

Returns the number of dimensions of the array (1 or 2).

.. attribute:: nrow

Returns the number of rows in the array.

.. attribute:: ncol

Returns the number of columns in the array.
