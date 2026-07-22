File manipulation
=================

This page documents the ``File`` type, which can be used to read and write text files. ``File`` is :ref:`non-clonable <clonability>`.


General concepts
----------------

A ``File`` is a handle to an open text file. Handles are created with :func:`open_file` — the ``File`` class itself is
not constructible from a script, and ``open`` is a reserved keyword in the new language, which is why the opener is
named ``open_file``. The default encoding is UTF-8; UTF-16 and UTF-32 files are detected automatically from their
byte-order mark when read, and can be forced explicitly for BOM-less files. Writing always uses UTF-8.

A file is closed automatically as soon as the last reference to the handle is released, so calling :func:`close`
explicitly is only needed when you want to release the file early (for instance to reopen it in another mode).


Global functions
----------------

.. function:: read_file(path as String)

Return the content of the file named ``path`` as a string. This is a convenience that opens the file, reads
everything and closes it. The encoding is detected from the BOM, so a UTF-16/32 file reads back as a regular string.


Opening files
-------------

.. function:: open_file(path as String)

Opens the file named ``path`` for reading and returns a ``File`` handle. Phonometrica will try to guess the encoding
from the file's byte-order mark, and will default to UTF-8 otherwise.

------------

.. function:: open_file(path as String, mode as String)

Opens the file named ``path`` and returns a ``File`` handle. The option ``mode`` must be one of the following strings:

* ``"r"`` = open the file in reading mode, starting at the beginning of the file  (the file must exist)
* ``"w"`` = open the file in writing mode, starting at the beginning of the file (the file is overwritten if it already exists)
* ``"a"`` = open the file in appending mode, starting at the end of the file (the file is created if it doesn't exist)
* ``"r+"`` = open the file in reading and writing mode, starting at the beginning of the file (the file must exist)
* ``"w+"`` = open the file in reading and writing mode, starting at the beginning of the file (the file is overwritten if it already exists)
* ``"a+"`` = open the file in reading and writing mode, starting at the end of the file  (the file is created if it doesn't exist)

In reading mode, Phonometrica will try to guess the encoding and will default to UTF-8 otherwise. In writing mode,
Phonometrica will always use UTF-8.

------------

.. function:: open_file(path as String, mode as String, encoding as String)

Like the two-argument form, but forces the text ``encoding`` instead of relying on BOM detection. This is intended
for BOM-less UTF-16/32 files. An unknown encoding name raises an error.

Example::

   var f = open_file("/tmp/out.txt", "w")
   write_line(f, "hello")
   close(f)
   print(read_file("/tmp/out.txt"))


Reading
-------

.. function:: read(file as File)

Reads the file from the current position to the end and returns the content as a string.

------------

.. function:: read_line(file as File)

Reads a line from ``file`` and returns it without the trailing line separator. If the cursor is at the end of the
file, it returns an empty string.

------------

.. function:: read_lines(file as File)

Returns the remaining content of the file as a list whose elements are the lines of the file.

------------

.. function:: eof(file as File)

Returns ``true`` if the cursor is positioned at the end of the file, ``false`` otherwise.

------------

.. function:: encoding(file as File)

Returns the name of the file's text encoding (for example ``"utf-8"``), as detected or forced when the file was
opened.


Writing
-------

.. function:: write(file as File, text as String)

Writes ``text`` to ``file``.

------------

.. function:: write_line(file as File, text as String)

Writes ``text`` to ``file``, and appends a new line separator.

------------

.. function:: write_lines(file as File, lines as List)

Writes each element in ``lines`` to ``file``, and appends a new line separator after each of them.


Positioning and lifetime
------------------------

.. function:: close(file as File)

Closes the file. Once the file is closed, no further reading or writing operations are allowed. In general, you
don't need to call this function since a file is automatically closed as soon as the last reference to it is
released.

------------

.. function:: rewind(file as File)

Rewinds the cursor to the beginning of the file.

------------

.. function:: seek(file as File, pos as Integer)

Sets the position of the cursor in the file to ``pos``.

------------

.. function:: tell(file as File)

Returns the current position of the cursor in the file.
