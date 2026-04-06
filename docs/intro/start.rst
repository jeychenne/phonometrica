Getting started
---------------


Main window
~~~~~~~~~~~

The main window is divided into several areas:

- The **file manager** (left panel) displays the hierarchical structure of the project. It contains
  folders for your corpus files, queries, data tables, analyses, scripts, research notes, and bookmarks.
- The **information panel** (right panel) displays and lets you edit metadata about the file(s) currently
  selected in the file manager. When multiple files are selected, the information panel shows a batch editing
  view where you can add, edit, or remove properties for all selected files at once.
- The **tools panel** (bottom) contains three tabs:

  - **Console**: an interactive command line for Phonometrica's scripting engine.
  - **Output**: a read-only panel where results from acoustic measurements, script execution, and
    other operations are displayed.
  - **IPA**: a clickable IPA chart for inserting phonetic symbols into annotations, scripts, or any
    text field. Click on a symbol to insert it at the cursor in the currently focused text widget.

- The **viewer** (center) is the main working area. It displays views as tabs, similar to web pages in a
  modern browser. The default **start view** provides quick-access buttons for common operations.

All panels can be shown or hidden via the **Window** menu. You can also resize and rearrange them by
dragging their borders.


Typical workflow
~~~~~~~~~~~~~~~~

A typical workflow in Phonometrica involves the following steps:

1. **Create a project** and import your sound files and annotations.
2. **Add metadata** to your files using properties (e.g. Speaker, Gender, Dialect).
3. **Annotate** your speech data, or import existing annotations (e.g. Praat TextGrids).
4. **Run queries** to search for patterns in your annotations and/or extract acoustic measurements (formants, pitch, intensity).
5. **Explore and filter** the results in concordance or dataset views: recode categories, transform values, exclude outliers.
6. **Fit statistical models** in the analysis view: linear regression, mixed-effects models, GAMs.
7. **Export** your results to CSV or LaTeX for inclusion in publications.

Each of these steps is described in detail in the following sections of the documentation.


Corpus management
~~~~~~~~~~~~~~~~~

Several commands in the ``File`` menu let you import files into a project, either individually
(``File > Add files to project...``) or by importing a directory recursively
(``File > Add content of directory to project...``).

The logical structure of a project is independent from the
physical organization of the files on your computer: once files
have been added to a project, they can be moved around, merged into new
directories or removed without affecting the files on disk. Phonometrica supports
several annotation formats, including TextGrid (Praat) and its own native XML-based
format. It also supports a number of audio formats, including WAV,
AIFF and FLAC (the exact number of supported formats depends on the
platform). By default, Phonometrica will try to automatically bind an
annotation and a sound file if they have the same base name but a
different extension. If the names differ, it is possible to bind them
manually, by right-clicking on them and choosing the corresponding
option in the context menu, or semi-automatically using the
``Import metadata...`` feature from the file menu or using the scripting
engine.


Properties
~~~~~~~~~~

The hierarchical organization of a project is a matter of pure
convenience to the user and is irrelevant for Phonometrica. Instead, the
program relies on metadata to keep files organized internally and to
perform queries. File names represent the most basic type of metadata
and for small projects (containing a dozen of files or so) this may be
all that is needed. When you need to sort and organize a larger
collection of files, Phonometrica offers a flexible mechanism called
**properties**. A property is a typed key/value pair. Each file can be
tagged with an arbitrary number of properties: the key represents a
**category**, which is always a text string, and the value may be either
Boolean, textual or numeric. Typical examples of properties would be
*Speaker* (where each unique speaker identifier represents a distinct
value, for example *11ajp1*) and *Gender* (with the values *Male* and
*Female*). Properties can be managed via the information panel when a
file is selected.

When multiple files are selected in the file manager, the information panel displays a **batch
editing** view. This shows a union table of all properties across the selected files, with a
coverage count indicating how many of the selected files have each property. You can add, edit,
or remove properties for all selected files at once.

In addition to properties, each file can be annotated with a
**description**, a free-form string which can be used to store any kind of
information, and which is also exposed to the search engine to filter
files.


Import metadata
~~~~~~~~~~~~~~~

Phonometrica lets you import metadata from a CSV file, which is a tabular format where each line 
represents a row and values in a row are separated by a **separator character**. When you click on 
``Import metadata...`` in the ``File > Import`` menu, a dialog opens and asks you to choose 
a file as well as the separator character. The semicolon is used by default, since this is the separator used by
Excel when it exports a spreadsheet to CSV. The file must have the following structure:

* the first line must be a header: the first field is ignored, and the following fields correspond to property categories.
* the first column must contain the name of the file to which you want to add metadata; the remaining columns contain values for the corresponding categories. Cells can be left empty for files that don't have a given property.

The cells in the first column represent file names as they are normally displayed by your operating system. For instance,
if you want to tag the file ``C:\Test\loc1f26.wav``, i.e. a file named ``loc1f26.wav`` located in a directory named 
``Test`` on the ``C:`` drive (on Windows), you should only provide ``loc1f26.wav`` as the file name. (Bear in mind that matching
is case sensitive, so ``LOC1F26.WAV`` would not work in this case.) In addition, Phonometrica allows you to use regular expressions
to define a *file name pattern* instead of an exact file name. To use a regular expression, the file name pattern must start with ``^`` 
(the metacharacter that indicates the beginning of a string) and end with ``$`` (the metacharacter that indicates the end of a string). You
can use any valid regular expression supported by Phonometrica, including capturing parentheses. You can refer to the whole matched pattern or to any subgroup in subsequent fields, using either ``%%`` or the placeholders ``%1`` to ``%9``, respectively. 

By default, properties are assigned the type ``text``. You can add the suffixes ``.bool``, ``.num`` or ``.text`` to the category in 
the header line to indicate that the property is a Boolean, a number or a text string, respectively. In addition, there are two special fields for columns: ``%SOUND%`` and ``%DESCRIPTION%``. ``%SOUND%`` allows you
to provide the full path of a sound file for an annotation. If the sound file exists and the file is indeed an 
annotation, Phonometrica will bind the annotation to the sound; otherwise, the value will be dismissed. ``%DESCRIPTION%`` 
allows you to set the description field of a file.
