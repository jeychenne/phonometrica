.. _notes:

Research notes
==============

Phonometrica lets you create and edit **research notes** directly within your project. Notes are rich-text documents
stored as HTML files with the ``.phon-note`` extension. They appear in the **Notes** folder in the file manager,
alongside your corpus files, queries, data tables, and scripts. This makes it easy to keep methodological remarks,
coding decisions, preliminary observations, and any other free-form text together with the data they relate to.


Creating a note
---------------

There are several ways to create a new note:

- Choose ``File > New > New note`` from the menu bar.
- Right-click on the **Notes** folder (or any subfolder within it) in the file manager and choose ``New note``.

A new, untitled note will open in the viewer. You can start typing immediately. When you save for the first time
(``Ctrl+S`` on Windows/Linux, ``Cmd+S`` on macOS), Phonometrica will ask you to choose a file name and location.
Once saved, the note is registered with the project and appears in the file manager.

You can also import existing ``.phon-note`` files into the project by right-clicking on the Notes folder
and choosing ``Add files...``.


Editing a note
--------------

The note editor is a rich-text editor with a formatting toolbar at the top. The following formatting options
are available:

**Text style**

- **Bold** (``Ctrl+B``): toggle bold on the current selection or at the cursor.
- **Italic** (``Ctrl+I``): toggle italic on the current selection or at the cursor.
- **Underline** (``Ctrl+U``): toggle underline on the current selection or at the cursor.

**Block style**

- **Heading level**: use the drop-down menu in the toolbar to switch between normal text and three heading
  levels (Heading 1, Heading 2, Heading 3).
- **Bullet list**: insert or remove an unordered (bulleted) list.
- **Numbered list**: insert or remove an ordered (numbered) list.

All formatting is preserved when the note is saved and reopened.


Saving a note
-------------

Press ``Ctrl+S`` (or ``Cmd+S`` on macOS), or click the **Save** button in the toolbar. The tab title shows
an asterisk (``*``) when the note has unsaved changes. When you close a modified note, Phonometrica will ask
whether you want to save or discard your changes.

Notes are stored as HTML files on disk. They can be opened in any web browser or text editor outside of
Phonometrica if needed.


Organizing notes
----------------

Notes live in the **Notes** folder of the file manager. Like other folders in the project, you can create
subfolders to organize your notes by topic, session, or any other criterion. Right-click on the Notes folder
(or a subfolder) and choose ``New folder`` to create a subfolder.
