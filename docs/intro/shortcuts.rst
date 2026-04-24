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
     - New script
   * - ``Ctrl+O``
     - Open project
   * - ``Ctrl+Shift+O``
     - Open the most recent project
   * - ``Ctrl+S``
     - Save the current view (annotation, note, script, concordance, dataset, etc.)
   * - ``Ctrl+Shift+S``
     - Save the project (all modified files)
   * - ``Ctrl+W``
     - Close the current view
   * - ``Ctrl+Return``
     - Execute the current view's script (in the script editor) or the selected portion
   * - ``Escape``
     - Context-dependent: cancel editing, stop playback, or dismiss a transient overlay
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
     - Play the selected match (if a sound file is bound)
   * - ``Escape``
     - Stop playback
   * - ``Ctrl+Return``
     - Open the selected match in its annotation
   * - ``Double-click`` (row)
     - Play the match
   * - ``Double-click`` (base cell)
     - Edit the cell in place (match text or auxiliary column)

Row deletion and other operations are available via the toolbar buttons and right-click
context menus.


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
   * - ``F5`` / ``Shift+F5``
     - View the FFT spectrum / FFT and LPC spectra at the cursor
   * - ``F6`` / ``Shift+F6``
     - Get formants at the cursor / mean formants over the selection
   * - ``F7`` / ``Shift+F7``
     - Get pitch at the cursor / mean pitch over the selection
   * - ``F8`` / ``Shift+F8``
     - Get intensity at the cursor / mean intensity over the selection


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
   * - ``Ctrl+B``
     - Bookmark the selected event on the focused layer (opens the bookmark editor)

.. note::

   ``Ctrl+B`` is also used for **Bold** in the note editor. The two shortcuts do not
   conflict because each is scoped to its own view: inside a note, ``Ctrl+B`` toggles
   bold; inside an annotation, it opens the bookmark editor.


Script editor
-------------

.. list-table::
   :widths: 35 65
   :header-rows: 1

   * - Shortcut
     - Action
   * - ``Ctrl+Return``
     - Run the current script (or the selected portion)
   * - ``Ctrl+F``
     - Open the Find bar
   * - ``Ctrl+H``
     - Open the Replace bar

Indent, unindent, comment, and uncomment are available from the script editor's toolbar.


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
