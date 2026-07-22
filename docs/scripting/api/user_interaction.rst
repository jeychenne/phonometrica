User interaction
================

This page describes global functions that allow you to interact with the user. These functions are only available
when Phonometrica runs with its graphical interface: they are not registered when a script is executed from the
command line.


Message boxes
-------------

.. function:: info(message as String [, title as String])

Displays an information dialog.

------------

.. function:: warning(message as String [, title as String])

Displays a warning dialog.

See also: :func:`alert`

------------

.. function:: alert(message as String [, title as String])

Displays an error dialog. This can be used for critical errors.

See also: :func:`warning`

------------

.. function:: ask(message as String [, title as String])

Asks a Yes/No question to the user. Returns ``true`` if the user clicked ``Yes``, and ``false`` otherwise.

------------

.. function:: get_input(label as String, title as String, text as String)

Displays an input dialog whose title is ``title`` and whose informative text is ``label``. The dialog contains a
field whose initial value is ``text``. This function returns the content of the field, or ``null`` if the user
cancelled the dialog.


File dialogs
------------

.. function:: open_file_dialog(message as String)

Displays a dialog that lets the user select a file. Returns the selected path, or ``null`` if the user cancelled
the dialog.

See also: :func:`open_files_dialog`, :func:`save_file_dialog`,
:func:`open_directory_dialog`

------------

.. function:: open_files_dialog(message as String)

Displays a dialog that lets the user select one or more files. Returns a sorted list of the selected paths, or
``null`` if the user cancelled the dialog.

See also: :func:`open_file_dialog`

------------

.. function:: save_file_dialog(message as String)

Displays a dialog that lets the user choose a path to save a file. Returns the chosen path, or ``null`` if the
user cancelled the dialog.

See also: :func:`open_file_dialog`,
:func:`open_directory_dialog`

------------

.. function:: open_directory_dialog(message as String)

Displays a dialog that lets the user select a directory. Returns the selected path, or ``null`` if the user
cancelled the dialog.

See also: :func:`save_file_dialog`,
:func:`open_file_dialog`


Progress dialogs
----------------

.. function:: create_progress_dialog(message as String, title as String, count as Integer)

Create a progress dialog with the provided message and title, set up for ``count`` elements (``count`` must be
positive). You must update the value of the dialog using :func:`update_progress_dialog`.

------------

.. function:: update_progress_dialog(value as Integer)

Update the progress dialog to the provided ``value``. The first value that should be provided to this function is
1, and the last one is the number of elements passed to the dialog when it was created. The dialog is closed when
``value`` reaches that count, or when the user clicks the dialog's Cancel button. This function returns ``false``
when the dialog was closed (finished or cancelled) and ``true`` while it is still running, so a processing loop can
stop early when the user cancels::

   var items = get_annotations()
   create_progress_dialog("Processing annotations...", "Please wait", len(items))
   var i = 0
   for annot in items do
       # ... process annot ...
       i += 1
       if not update_progress_dialog(i) then
           break
       end
   end


Custom dialogs
--------------

.. function:: create_dialog(spec as Table)

Displays a custom modal dialog described by ``spec`` and blocks until the user closes it. If the user accepts the
dialog, a ``Table`` mapping each named item to its value is returned; if the user cancels it, ``null`` is returned.

An overload accepting a string is also available for legacy scripts: the string is evaluated as script code and
must produce the specification table.

The specification table accepts the following keys, all optional:

* ``title`` (string): the dialog's window title.
* ``width``, ``height`` (integers): the dialog's initial size, in pixels.
* ``yes_no`` (boolean): if ``true``, the confirmation buttons are labelled ``Yes``/``No`` instead of
  ``OK``/``Cancel``.
* ``items`` (list): the widgets displayed in the dialog, in order. Each item is a table with a mandatory ``type``
  key; input widgets also require a ``name`` key, which becomes the corresponding key in the result table.

The supported item types are:

* ``label`` — a line of static text. Keys: ``text`` (string, required).
* ``button`` — a push button. Keys: ``label`` (string, required); ``action`` (string): script code executed when
  the button is clicked; ``position`` (string): ``"left"``, ``"center"`` or ``"right"``.
* ``check_box`` — a check box. Keys: ``name``; ``text`` (string): the label; ``default`` (boolean): initial state.
  Result value: a Boolean.
* ``combo_box`` — a drop-down list. Keys: ``name``; ``values`` (list of strings, required); ``default`` (integer):
  1-based index of the initially selected value. Result value: the 1-based index of the selected item.
* ``field`` — a single-line text field. Keys: ``name``; ``default`` (string): initial text. Result value: the
  field's text.
* ``check_list`` — a list of checkable items. Keys: ``name``; ``values`` (list of strings, required): the values
  reported for checked items; ``labels`` (list of strings): the labels displayed to the user (defaults to
  ``values``; both lists must have the same length). Result value: the list of checked values.
* ``radio_buttons`` — a group of mutually exclusive buttons. Keys: ``name``; ``values`` (list of strings,
  required); ``title`` (string): the group's title; ``default`` (integer): 1-based index of the initially selected
  button (the first by default). Result value: the 1-based index of the selected button.
* ``file_selector`` — a text field with a button that opens a file dialog. Keys: ``name``; ``title`` (string,
  required): the file dialog's title; ``default`` (string): initial path; ``filter`` (string): file name filter
  (e.g. ``"Text files (*.txt)"``); ``save`` (boolean): if ``true``, a save dialog is shown instead of an open
  dialog. Result value: the selected path.
* ``container`` — a nested group of items laid out together. Keys: ``items`` (list, required); ``orientation``
  (string): ``"vertical"`` or ``"horizontal"`` (the default).
* ``stretch`` — an expanding blank space (no other key).
* ``spacing`` — a fixed blank space. Keys: ``size`` (integer, required): the space in pixels.

Example, adapted from the standard ``transphon`` script::

   var ui = {
       "title": "Export annotations to plain text...",
       "width": 500,
       "items": [
           { "type": "label", "text": "Choose output file:" },
           { "type": "file_selector", "name": "path", "title": "Select text file...", "save": true },
           { "type": "label", "text": "Select layers, or leave empty to process all layers:" },
           { "type": "field", "name": "layers", "default": "1" },
           { "type": "label", "text": "Choose event separator:" },
           { "type": "radio_buttons", "name": "separator", "values": ["space", "new line", "none"] }
       ]
   }

   var result = create_dialog(ui)

   if result then
       var sep = result["separator"]
       print("Writing to " & result["path"])
       print("Separator index: {sep}")
   end


Miscellaneous
-------------

.. function:: view_text(path as String, title as String)

Opens the plain text file ``path`` in a new dialog with the given ``title``.

------------

.. function:: launch_browser(url as String)

Opens ``url`` in the user's default web browser.

------------

.. function:: clear_console()

Clears the currently active output surface. When called interactively from
the console, it empties the console and prints a new prompt. When called
from a script run through the script view, it empties the Output panel
instead.

(The old zero-argument ``clear()`` was renamed: ``clear(x)`` now empties a
:class:`List`, :class:`Table`, :class:`Array` or :class:`Set`, as documented
on their respective pages.)
