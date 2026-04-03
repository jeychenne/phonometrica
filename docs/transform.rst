.. _transform:

Column transformations
======================

Phonometrica lets you create new numeric columns by applying a mathematical formula to an existing column. To transform a column, right-click on its header in a dataset or concordance view and choose **Transform...** from the context menu. This opens the transform dialog.


Transform dialog
----------------

The dialog contains the following elements:

- **Formula**: enter a mathematical expression using ``x`` as the variable representing each cell value.
- **New column name**: automatically generated from the formula (e.g. ``bark(F1)``), but you can edit it.
- **Preview**: shows the first few values of the column along with the computed result, updated in real time as you type.

The formula is validated as you type. If there is an error, it is displayed in red below the formula field and the **OK** button is disabled.


Syntax
------

Formulas follow standard mathematical notation:

- **Variable**: ``x`` represents the value in each cell of the source column.
- **Constants**: ``pi`` (3.14159...) and ``e`` (2.71828...).
- **Operators**: ``+``, ``-``, ``*``, ``/``, ``^`` (exponentiation).
- **Parentheses**: use ``(`` and ``)`` to group sub-expressions.
- **Unary minus**: ``-x`` negates a value. Unary plus (``+x``) is also accepted.

Operator precedence follows standard conventions (from lowest to highest):

1. Addition and subtraction (``+``, ``-``)
2. Multiplication and division (``*``, ``/``)
3. Unary minus and plus
4. Exponentiation (``^``, right-associative)

For example, ``-x^2`` is interpreted as ``-(x^2)``, and ``2 * x ^ 3`` is interpreted as ``2 * (x^3)``.


Math functions
--------------

The following general-purpose mathematical functions are available:

.. list-table::
   :widths: 25 75
   :header-rows: 1

   * - Function
     - Description
   * - ``log(x)``
     - Natural logarithm (base *e*). Requires *x* > 0.
   * - ``log10(x)``
     - Base-10 logarithm. Requires *x* > 0.
   * - ``log2(x)``
     - Base-2 logarithm. Requires *x* > 0.
   * - ``sqrt(x)``
     - Square root. Requires *x* ≥ 0.
   * - ``abs(x)``
     - Absolute value.
   * - ``exp(x)``
     - Exponential function (*e*\ :sup:`x`).
   * - ``pow(x, n)``
     - Power function (*x*\ :sup:`n`). Equivalent to ``x^n``.
   * - ``round(x)``
     - Round to the nearest integer.
   * - ``floor(x)``
     - Round down to the nearest integer.
   * - ``ceil(x)``
     - Round up to the nearest integer.


Phonetic scale functions
------------------------

The following functions convert acoustic values (typically in Hertz) to perceptual scales commonly used in phonetics:

.. list-table::
   :widths: 25 75
   :header-rows: 1

   * - Function
     - Description
   * - ``bark(x)``
     - Convert *x* Hz to the Bark scale (Traunmüller 1990). Formula: 26.81 / (1 + 1960 / *x*) − 0.53. Requires *x* > 0.
   * - ``erb(x)``
     - Convert *x* Hz to the ERB-rate scale (Glasberg & Moore 1990). Formula: 21.4 × log\ :sub:`10`\ (0.00437 × *x* + 1).
   * - ``mel(x)``
     - Convert *x* Hz to the mel scale (O'Shaughnessy 1987). Formula: 2595 × log\ :sub:`10`\ (1 + *x* / 700).
   * - ``st(x)``
     - Convert *x* Hz to semitones relative to 100 Hz. Formula: 12 × log\ :sub:`2`\ (*x* / 100). Requires *x* > 0.
   * - ``st(x, ref)``
     - Convert *x* Hz to semitones relative to *ref* Hz. Formula: 12 × log\ :sub:`2`\ (*x* / *ref*). Requires *x* > 0 and *ref* > 0.


Handling of special values
--------------------------

- If the input value is **NaN** (not a number), the result is NaN.
- If a function receives an input outside its domain (e.g. ``log(-1)`` or ``sqrt(-2)``), the result is NaN.
- Division by zero produces NaN.
- After the transform is applied, a message indicates how many values produced NaN (if any).


Examples
--------

Here are some common transformations:

.. list-table::
   :widths: 40 60
   :header-rows: 1

   * - Formula
     - Purpose
   * - ``log(x)``
     - Log-transform (e.g. for normalizing skewed distributions)
   * - ``bark(x)``
     - Convert formant values from Hz to Bark
   * - ``erb(x)``
     - Convert formant values from Hz to ERB-rate
   * - ``st(x)``
     - Convert F0 in Hz to semitones (ref = 100 Hz)
   * - ``st(x, 200)``
     - Convert F0 in Hz to semitones (ref = 200 Hz)
   * - ``x / 1000``
     - Convert milliseconds to seconds
   * - ``(x - 500) / 100``
     - Center and scale a variable
   * - ``x ^ 2``
     - Square the values
   * - ``1 / x``
     - Inverse (e.g. for speech rate from duration)
   * - ``round(x)``
     - Round to the nearest integer


References
----------

- Glasberg, B. R. & Moore, B. C. J. (1990). Derivation of auditory filter shapes from notched-noise data. *Hearing Research*, 47(1–2), 103–138.
- O'Shaughnessy, D. (1987). *Speech Communication: Human and Machine*. Addison-Wesley.
- Traunmüller, H. (1990). Analytical expressions for the tonotopic sensory scale. *Journal of the Acoustical Society of America*, 88(1), 97–100.
