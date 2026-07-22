Datasets and concordances
=========================

This page documents the ``Dataset`` and ``Concordance`` types, which represent tabular data in Phonometrica.
Both types inherit from ``DataTable`` and share common functions for cell access and export. A ``Dataset``
is typically a CSV file imported into the project, while a ``Concordance`` is the result of a query.
Both ``Dataset`` and ``Concordance`` are :ref:`non-clonable <clonability>`: assignment shares the underlying
project document rather than producing an independent copy.


Functions
---------

.. function:: get_datasets()

Return a list of all the datasets in the current project.

------------

.. function:: get_dataset(path as String)

Return the ``Dataset`` object from the current project whose path is ``path``, or ``null`` if there is no such
dataset.

------------

.. function:: get_concordances()

Return a list of all the concordances in the current project.

------------

.. function:: get_concordance(path as String)

Return the ``Concordance`` object from the current project whose path is ``path``, or ``null`` if there is no such
concordance.


Cell and column access
----------------------

.. function:: get_cell(table as DataTable, row as Integer, col as Integer)

Returns the value of the cell at row ``row`` and column ``col`` as a string. Both indices are 1-based
and can be negative (negative indices start from the end).

Example::

   var ds = load("data.csv")
   print(get_cell(ds, 1, 1))         # first cell
   print(get_cell(ds, ds.nrow, 1))   # last row, first column

------------

.. function:: set_cell(table as DataTable, row as Integer, col as Integer, value as String)

Sets the value of the cell at row ``row`` and column ``col``. The string ``value`` is automatically
converted to the appropriate type (numeric, boolean, or text) based on the column's type.

------------

.. function:: get_header(table as DataTable, col as Integer)

Returns the header (column name) of column ``col`` (1-based).

------------

.. function:: get_column(dataset as Dataset, col as Integer)

Returns all the values in column ``col`` (1-based). For numeric columns, returns an ``Array``; for text
or boolean columns, returns a ``List``.

Example::

   var ds = load("vowels.csv")
   var f1_values = get_column(ds, 2)   # assuming F1 is column 2
   print(mean(f1_values))

------------

.. function:: get_column(table as DataTable, name as String)

Returns all the values in the column named ``name``. Raises an error if the table has no such
column. For a ``Dataset``, the column's declared type decides the result type (``Array`` for
numeric columns, ``List`` otherwise), as in the previous overload. For a ``Concordance``, the
column type is auto-detected: if every cell parses as a number (or is a missing-value marker),
an ``Array`` is returned, otherwise a ``List`` of strings.

Example::

   var ds = load("vowels.csv")
   var f1_values = get_column(ds, "f1")
   print(mean(f1_values))

------------

.. function:: get_column(concordance as Concordance, col as Integer)

Returns all the values in column ``col`` (1-based) of a concordance. The index space includes the
system columns (file, match, context, metadata) as well as auxiliary measurement columns. The
column type is auto-detected as described above.

------------

.. function:: get_column_type(dataset as Dataset, col as Integer)

Returns the type of column ``col`` as a string: ``"numeric"``, ``"text"``, or ``"boolean"``.


Adding columns
--------------

.. function:: add_column(table as DataTable, values as List, name as String)

Appends a new text column named ``name`` to the table (a ``Dataset`` or a ``Concordance``).
``values`` must have exactly one item per row; each item is converted to a string the same
way ``print`` and string interpolation do. (This function was named ``append`` in the old
engine.)

Example::

   var ds = load("data.csv")
   var labels = []
   for i = 1 to ds.nrow do
       append(labels, "item {i}")
   end
   add_column(ds, labels, "item_label")

------------

.. function:: add_column(table as DataTable, values as Array, name as String)

Appends a new numeric column named ``name`` to the table. ``values`` must have exactly one
element per row.

Example::

   var ds = load("data.csv")
   var f1 = get_column(ds, "f1")
   add_column(ds, hertz_to_bark(f1), "f1_bark")


Export
------

.. function:: to_csv(table as DataTable, path as String [, separator as String])

Exports the table to a delimited text file at ``path``. If ``separator`` is not provided, a comma is used.

Example::

   var ds = load("data.csv")
   var filtered = filter(ds, "gender == 'F'")
   to_csv(filtered, "/tmp/females.csv")
   to_csv(filtered, "/tmp/females.tsv", "\t")


Dataset fields
--------------

.. attribute:: path

Returns the path of the file.

.. attribute:: label

Returns the label of the dataset.

.. attribute:: description

Returns the description of the dataset.

.. attribute:: nrow

Returns the number of rows.

.. attribute:: ncol

Returns the number of columns.

.. attribute:: empty

Returns ``true`` if the dataset has no rows.

.. attribute:: headers

Returns a list of column names.

.. attribute:: length

Same as ``nrow``.


Concordance fields
------------------

.. attribute:: path

Returns the path of the file.

.. attribute:: label

Returns the label of the concordance.

.. attribute:: description

Returns the description of the concordance.

.. attribute:: nrow

Returns the number of rows (occurrences).

.. attribute:: ncol

Returns the number of columns.

.. attribute:: empty

Returns ``true`` if the concordance has no rows.

.. attribute:: headers

Returns a list of column names.

.. attribute:: target_count

Returns the number of target columns in the concordance.

.. attribute:: length

Same as ``nrow``.
