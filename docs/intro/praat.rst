.. _praat-integration:

Praat integration
=================

Phonometrica can interact with `Praat <http://www.praat.org>`_ in several ways: importing and
exporting TextGrid annotations, and opening files directly in Praat for further inspection or editing.
This page describes how to set up and use these features.


Setting up Praat
----------------

To use the "Open in Praat" features, Phonometrica needs to know where Praat is installed on your
computer. On macOS, Phonometrica automatically detects Praat if it is in the ``/Applications``
folder. On Windows, it checks the default installation directory. If Praat is installed elsewhere,
or if auto-detection fails, you need to set the path manually in
``Edit > Preferences... > General > Path to Praat`` (see :ref:`preferences`).

Phonometrica communicates with Praat using the ``--send`` command-line interface: Praat will be
launched automatically if it is not already running.


Opening files in Praat
----------------------

You can open files directly in Praat from several places in Phonometrica:

**From the file manager**

Right-click on a sound file, an annotation (TextGrid or native format), or a pair of bound files
in the file manager and choose **Open in Praat** from the context menu. If you select an annotation
that is bound to a sound file, both files are sent to Praat together so that you can view the
annotation overlaid on the sound.

**From annotation views**

While viewing an annotation, you can send the current sound and annotation to Praat using the
context menu or the toolbar. This is useful when you want to use Praat's acoustic analysis features
on a specific segment.

**From concordance views**

When browsing query results in a concordance, right-click on a row and choose **Open in Praat**
to send the sound file and annotation to Praat, positioned at the location of the selected match.
This is particularly convenient during manual validation: you can inspect a suspicious measurement
in Praat and then return to the concordance to correct or delete it.


Importing and exporting TextGrids
----------------------------------

Phonometrica can read and write TextGrid files (Praat's native annotation format). TextGrid files
can be imported into a project and used alongside Phonometrica's own XML-based annotations without
any conversion. Phonometrica reads TextGrids encoded in UTF-8 or UTF-16 and writes them in UTF-8.

To convert between formats, right-click on an annotation in the file manager:

- **Save as Praat TextGrid...**: converts a native Phonometrica annotation (``.phon-annot``) to a
  TextGrid. After saving, Phonometrica offers to import the new TextGrid file into the current project.
  Properties, description, and the sound file binding are copied to the new file.
- **Save as Phonometrica annotation...**: converts a TextGrid to a native annotation. After saving,
  Phonometrica offers to import the new file into the project. Properties, description, and the sound
  file binding are copied over.

Note that Praat's TextGrid format does not support metadata (properties or descriptions). When you
work with TextGrids in Phonometrica, metadata are stored inside the project file. This is entirely
transparent: TextGrid files and native annotations can be freely mixed within a project, and
metadata are handled identically regardless of the file format.
