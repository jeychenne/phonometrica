Mathematical functions and constants
====================================


This page describes the mathematical functions and constants that are available in Phonometrica.
A subset of these functions (``abs``, ``sqrt``, ``exp``, ``log``, ``sin``, ``cos``, ``floor`` and
``ceil``) also accept an ``Array`` argument, in which case the operation is applied to each element
in the array and a new array is returned.


Arithmetic notes
----------------

- Numbers distinguish ``Integer`` and ``Float`` values (``1`` vs ``1.0``).
- Float division by zero yields ``inf`` (it does not raise an error).
- Integer division uses the ``div`` keyword operator; ``1 div 0`` raises a math error.
- The modulo operation uses the ``mod`` keyword operator (the ``%`` operator does not exist).


Global functions
----------------

.. function:: abs(x as Number)

Returns the absolute value of ``x``. If ``x`` is an ``Integer``, the result is an ``Integer``.


------------

.. function:: abs(x as Array)

Returns a copy of the array in which ``abs`` has been applied to each element.


------------

.. function:: acos(x as Number)

Returns the arccosine of ``x``.


------------

.. function:: asin(x as Number)

Returns the arcsine of ``x``.

------------

.. function:: atan(x as Number)

Returns the arctangent of ``x``.

------------

.. function:: atan2(y as Number, x as Number)

Returns the four-quadrant inverse tangent of ``y`` and ``x``.

------------

.. function:: ceil(x as Number)

Returns the smallest integer no smaller than ``x``.

------------

.. function:: ceil(x as Array)

Returns a copy of the array in which ``ceil`` has been applied to each element.

------------

.. function:: cos(x as Number)

Returns the cosine of ``x``.

------------

.. function:: cos(x as Array)

Returns a copy of the array in which ``cos`` has been applied to each element.

------------

.. function:: exp(x as Number)

Returns the exponential of ``x``.

------------

.. function:: exp(x as Array)

Returns a copy of the array in which ``exp`` has been applied to each element.

------------

.. function:: floor(x as Number)

Returns the largest integer that is no larger than ``x``.

------------

.. function:: floor(x as Array)

Returns a copy of the array in which ``floor`` has been applied to each element.

------------

.. function:: log(x as Number)

Returns the natural logarithm of ``x``.

------------

.. function:: log(x as Array)

Returns a copy of the array in which ``log`` has been applied to each element.


------------

.. function:: log2(x as Number)

Returns the logarithm of ``x`` in base 2.

------------

.. function:: log10(x as Number)

Returns the logarithm of ``x`` in base 10.

------------

.. function:: max(x as Number, y as Number)

Returns the larger value between ``x`` and ``y``.

------------

.. function:: max(x as Integer, y as Integer)

Returns the larger value between ``x`` and ``y``.


------------

.. function:: min(x as Number, y as Number)

Returns the smaller value between ``x`` and ``y``.

------------

.. function:: min(x as Integer, y as Integer)

Returns the smaller value between ``x`` and ``y``.

------------

.. function:: random()

Returns a pseudo-random value in the interval [0, 1[ according to a uniform distribution.

------------

.. function:: round(x as Number)

Rounds ``x`` to the nearest integer (halfway cases are rounded away from zero). If ``x`` is an
``Integer``, it is returned unchanged.

------------

.. function:: round(x as Number, ndigits as Integer)

Rounds ``x`` to ``ndigits`` digits after the decimal point.

.. code:: phon

    print(round(3.14159, 2))   # prints 3.14

------------

.. function:: sin(x as Number)

Returns the sine of ``x``.

------------

.. function:: sin(x as Array)

Returns a copy of the array in which ``sin`` has been applied to each element.

------------

.. function:: sqrt(x as Number)

Returns the square root of ``x``.

------------

.. function:: sqrt(x as Array)

Returns a copy of the array in which ``sqrt`` has been applied to each element.

------------

.. function:: tan(x as Number)

Returns the tangent of ``x``.



Constants
---------


.. attribute:: E

Returns the value of *e*, the base of the natural logarithm (approximately 2.718281).

------------

.. attribute:: PI

Returns the value of pi (approximately 3.141593).

These constants are ordinary global bindings: a local or module variable with the same name
shadows them. (The old ``PHI`` and ``SQRT2`` constants no longer exist.)
