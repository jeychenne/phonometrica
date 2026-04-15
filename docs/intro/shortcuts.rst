.. _shortcuts:

Keyboard shortcuts
==================

This page lists the keyboard shortcuts available in Phonometrica. On macOS, replace ``Ctrl`` with
``Cmd`` throughout.


Global shortcuts
----------------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Ctrl+N``
     - New project
   * - ``Ctrl+O``
     - Open project
   * - ``Ctrl+S``
     - Save the current file (annotation, note, script, concordance, etc.)
   * - ``Ctrl+Shift+S``
     - Save all modified files in the project
   * - ``Ctrl+W``
     - Close the current tab
   * - ``Ctrl+Q``
     - Quit Phonometrica


Query shortcuts
---------------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Ctrl+Shift+F``
     - Find in annotations (open the text query editor)
   * - ``Ctrl+L``
     - Edit last query (reopen the most recent query for modification or re-execution)


Concordance and dataset views
-----------------------------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Space``
     - Play the sound for the selected match
   * - ``Escape``
     - Stop playback
   * - ``Delete``
     - Delete the selected row(s)


Sound and annotation views
--------------------------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Space``
     - Play the current selection (or the visible window if no selection)
   * - ``Escape``
     - Stop playback
   * - ``Middle click`` (scroll wheel)
     - Zoom to the current selection
   * - ``Scroll wheel`` (over wave bar)
     - Shift the visible window forward (scroll down) or backward (scroll up)


Annotation editing
------------------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Enter``
     - Open the label editor for the focused event; press again to validate
   * - ``Escape``
     - Cancel label editing
   * - ``Left`` / ``Right``
     - Move to the previous / next event on the same layer
   * - ``Up`` / ``Down``
     - Move to the same event position on the previous / next layer


Script editor
-------------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Ctrl+R``
     - Run the current script (or the selected portion)
   * - ``Ctrl+/``
     - Comment or uncomment the selected lines


Note editor
-----------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Ctrl+B``
     - Bold
   * - ``Ctrl+I``
     - Italic
   * - ``Ctrl+U``
     - Underline
   * - ``Ctrl+S``
     - Save the note


Plugin shortcuts
----------------

Plugins can define their own keyboard shortcuts for their menu actions. These are specified in
the plugin's ``description.json`` file (see :ref:`page-plugins`). For example, a plugin action
might use ``Alt+M`` to run a metadata import script.
