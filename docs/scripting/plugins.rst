.. _page-plugins:

Plugins
=======


Phonometrica can be extended with plugins, which are written in Phonometrica's scripting language.
When it starts up, Phonometrica loads all plugins which are located in the system plugin directory or in the user plugin directory. Plugins
can add functionality to Phonometrica in a number of ways, but most commonly they will create a submenu in the ``Plugins`` menu which provides
additional commands offered by the plugin and/or custom coding protocols.

Plugins can be shared and redistributed as ZIP files. To install a plugin, go to ``Plugins > Install plugin...`` and choose the ZIP
file corresponding to the plugin you wish to install. It will be installed in the current user’s plugin directory.

See :ref:`page-scripting` to learn more about scripting. If you are updating a plugin written for a version of
Phonometrica older than the current scripting engine, see :ref:`page-migration` for the list of language changes.

Structure of a plugin
---------------------

To be valid, a plugin must adhere to a number of conventions. A plugin usually contains the following items:

-  a description file, named ``description.phon`` (compulsory)
-  a script named ``initialize.phon`` (optional). If it exists, it will be run after reading the description file.
-  a script named ``finalize.phon`` (optional). If it exists, it will be run when the program exits.
-  a ``Scripts`` sub-directory, which contains all your scripts
   (optional).
-  a ``Protocols`` sub-directory, which contains coding protocols (optional)
-  a ``Resources`` sub-directory, which may contain anything (optional).
-  a ``Documentation`` sub-directory, which may contain HTML pages that menu actions can open (optional).

Plugins are loaded in alphabetical order, system plugins first, then user plugins. To temporarily disable
a plugin without uninstalling it, create an empty file named ``ignore.txt`` at the root of the plugin's
directory: Phonometrica will skip it at startup.

The description file (``description.phon``) is a script written in Phonometrica's scripting language: it is
executed when the plugin is loaded, and its result (the value of its last expression) must be a **table**
containing all the information necessary to initialize the plugin. In practice, the file usually consists
of a single table literal. It must **at least** contain a name.

.. code:: phon

    {
        "name": "My first plugin"
    }

Here is a more realistic (and useful) example:

.. code:: phon

    {
        "name": "PFC",
        "version": "0.1",
        "description": [
            "Plugin for the PFC project.\n",
            "See http://www.projet-pfc.net"
        ],

        "actions": [
            { "name": "Add metadata", "target": "add_metadata.phon" }
        ]
    }

The version field can be used to distinguish different versions of the plugin, for instance if a later update breaks backward compatibility. The
description field is displayed in the ``About`` entry in the plugin's menu (if it exists). This field can be a simple string or, as in the example
above, a list of strings (which are concatenated).

The optional field named ``actions`` maps to a list of tables: Each action describes a menu entry, and must have two fields: a ``name`` key,
which is the entry's label (as it is displayed in the menu), and a ``target`` field, which usually gives the name of a script (located in the plugin's
``Scripts`` sub-directory) which will be executed when the menu entry is clicked on. If the target ends with ``.html`` instead, it names a page in
the plugin's ``Documentation`` sub-directory, which is opened in the help browser when the entry is clicked on.

.. note::

    Since the description file is a regular script, ``#`` comments are allowed, and values may be computed
    (for instance, a version string stored in a variable and reused). The only requirement is that the last
    expression in the file evaluates to the description table.

.. warning::

    In the scripting language, double-quoted strings interpolate expressions between braces (``"{...}"``)
    and process escape sequences. A literal brace in a double-quoted string must therefore be written
    ``\{``. Alternatively, use single-quoted strings, which are raw (no interpolation and no escapes) —
    this matters mostly for regular expressions in coding protocols (see below).

    Plugins written for older versions of Phonometrica used a JSON file named ``description.json``. That
    file is no longer read: Phonometrica will refuse to load the plugin and ask for the file to be renamed
    to ``description.phon``. The JSON syntax for tables and lists is essentially unchanged, so the conversion
    usually amounts to renaming the file (and escaping any literal braces inside double-quoted strings).

Initialization, finalization and startup scripts
------------------------------------------------

If the plugin contains a script named ``initialize.phon``, it is run immediately after the description file has
been read. This is the place to set up state shared by the plugin's actions or to connect to events (see below).
Variables and functions declared at the top level of ``initialize.phon`` are visible to the plugin's action
scripts, which run later in the same scripting session.

The plugin's ``Scripts`` sub-directory is added to the module search path when the plugin is loaded, so any
script file it contains can also be loaded as a module with ``import``, by name and without extension: an
action script can run ``import mytools`` to load ``Scripts/mytools.phon``.

If the plugin contains a script named ``finalize.phon``, it is run when Phonometrica exits, which gives the
plugin a chance to do some cleanup.

In addition to plugins, Phonometrica also runs standalone startup scripts: every file located in the
``Scripts`` directory of the user settings directory (next to the ``Plugins`` directory) is executed at
startup, in alphabetical order. This is a convenient way to customize Phonometrica without creating a
full-fledged plugin.

A plugin's scripts have access to the whole scripting API, including GUI functions such as ``info()``,
``warning()``, ``alert()`` and ``ask()``, and the ``phon`` namespace (e.g. ``phon.project``). For example,
an action script might look like this:

.. code:: phon

    var annot = get_current_annotation()

    if annot then
        info("Current annotation: " & annot.path)
    else
        alert("No annotation is currently open")
    end

Two functions are specifically useful in plugin scripts: ``get_plugin_version(name)`` returns the version
string of the plugin called ``name`` (or ``null`` if there is no such plugin), and
``get_plugin_resource(plugin_name, file_name)`` returns the path of the file ``file_name`` in the
``Resources`` sub-directory of the plugin ``plugin_name``.

Reacting to events
------------------

Phonometrica provides a simple signal/slot mechanism (also known as event/callback), implemented in the
``signal`` library that is preloaded at startup. A *signal* is a unique string identifier; any number of
functions (*slots*) can be connected to it. The following functions are available:

- ``create_signal()`` creates and returns a new signal identifier.
- ``connect(id, slot)`` connects the function ``slot`` to the signal ``id``.
- ``disconnect(id, slot)`` disconnects ``slot`` from ``id``.
- ``emit(id, arg)`` emits the signal ``id``: each connected slot is called in turn with ``arg`` as its
  argument. The values returned by the slots are collected into a list, which is returned by ``emit``.
  ``emit(id)`` emits the signal with ``null`` as the argument.

Phonometrica itself emits a number of signals as the project changes. The corresponding identifiers are
available as global constants:

- ``SIGNAL_PROJECT_LOADED``: a project was loaded (slots receive ``null``)
- ``SIGNAL_ANNOTATION_LOADED``: an annotation was added to the project (slots receive the ``Annotation``)
- ``SIGNAL_SOUND_LOADED``: a sound was added to the project (slots receive the ``Sound``)
- ``SIGNAL_ANNOTATION_IMPORTED``: an annotation was imported (slots receive the ``Annotation``)
- ``SIGNAL_SOUND_IMPORTED``: a sound was imported (slots receive the ``Sound``)
- ``SIGNAL_SCRIPT_LOADED``: a script was added to the project (slots receive the ``Document``)
- ``SIGNAL_DATASET_LOADED``: a dataset was added to the project (slots receive the ``Document``)

A plugin typically connects to these events in its ``initialize.phon`` script. For example, the following
code displays a message box every time an annotation is loaded into the project:

.. code:: phon

    function on_annotation_loaded(annot)
        info("Loaded annotation " & annot.path)
    end

    connect(SIGNAL_ANNOTATION_LOADED, on_annotation_loaded)

Plugins can also define their own events to decouple their components:

.. code:: phon

    var my_signal = create_signal()

    function greet(name)
        return "Hello " & name & "!"
    end

    connect(my_signal, greet)
    var results = emit(my_signal, "world")

Defining coding protocols
-------------------------

If you have devised a coding scheme for your data, Phonometrica lets you define a **coding protocol**. A coding protocol is a description of your
coding scheme which offers a user-friendly interface for querying your data; it tells Phonometrica what to look for and how to present the
information to the user in the query editor. Phonometrica will automatically load all valid coding protocols in your plugin's submenu.

A coding protocol defines a number of **fields** which can take on a number of values. The user is presented with a number of checkboxes for each
field, and Phonometrica converts the query to the corresponding regular expression, as defined by the coding protocol. Like the description
file, a coding protocol is a script whose last expression evaluates to a table: every file in the plugin's ``Protocols`` sub-directory is run
through the scripting engine when the plugin is loaded. Here is a simple yet realistic example, drawn from the
`PFC project <http://www.projet-pfc.net>`_:

.. note::

    Writing a coding protocol by hand is not strictly necessary. The **protocol builder** dialog (available from
    ``Plugins > Build coding protocol...`` or from the right-click menu of any concordance column) provides an
    interactive editor with live preview and saves the result as a file that can be dropped into a plugin's
    ``Protocols/`` directory. Hand-editing remains useful when you need features that the builder does not expose
    (for instance the ``layer_index``, ``layer_field``, or ``fields_per_row`` attributes described below), or when
    you want to version-control the protocol source directly. See :ref:`applying-coding-protocols` for more on
    using the builder. The builder escapes braces for you, so a quantifier such as ``[aeiou]{2}`` typed into a
    field is written to the file as ``"[aeiou]\{2\}"`` and read back unchanged; when writing a protocol by hand,
    the escaping is yours to do (or use single-quoted strings, which are raw).

.. code:: phon

    {
        "type": "coding_protocol",
        "name": "Schwa coding",
        "version": "0.1",

        "separator": "",
        "layer_index": 2,
        "fields_per_row": 3,

        "fields": [
            {"name": "Spelling", "match_all": ".",
                "values": [
                {"match": "e", "text": "graphical e"},
                {"match": "[^e]", "text": "no e"}
                ]
            },

            {"name": "Schwa", "match_all": "[0-2]",
                "values": [
                {"match": "0", "text": "Absent"},
                {"match": "1", "text": "Present"},
                {"match": "2", "text": "Uncertain"}
                ]
            },

            {"name": "Position", "match_all": "[1-5]",
                "values": [
                    {"match": "1", 	"text": "monosyllable"},
                    {"match": "2", 	"text": "initial syllable"},
                    {"match": "3", 	"text": "median syllable"},
                    {"match": "4", 	"text": "final syllable"},
                    {"match": "5", 	"text": "metathesis"}
                ]
            },


            {"name": "Left context", "match_all": "[1-5]",
                "values": [
                    {"match": "1", 	"text": "vowel"},
                    {"match": "2", 	"text": "consonant"},
                    {"match": "3", 	"text": "start of an intonational phrase"},
                    {"match": "4", 	"text": "uncertain vowel"},
                    {"match": "5", 	"text": "simplified cluster"}
                ]
            },

            {"name": "Right context", "match_all": "[1-4]",
                "values": [
                    {"match": "1", "text": "vowel"},
                    {"match": "2", "text": "consonant"},
                    {"match": "3", "text": "weak prosodic boundary"},
                    {"match": "4", "text": "strong prosodic boundary"}
                ]
            }
        ]
    }

The ``type`` field is required and indicates that this file is a coding protocol. The ``name`` field corresponds to the name of the grammar, as it
will be seen by the user, and ``version`` is an optional field which corresponds to the version of the protocol.

Next, the ``separator`` is an optional attribute which indicates the separator to be used between fields. In this case, it is an empty string,
which means that the fields are concatenated directly (e.g. ``1412``). If the separator
was ``_``, for instance, each field should be separated by this symbol (e.g. ``1_4_1_2``).

Next the ``layer_index`` attribute indicates the index of the layer in which codings
should be searched for. The default value is 0, which means that codings are searched for in all
annotation layers.

The following attribute, ``fields_per_row``, lets us specify how many fields should be displayed in a row. In our case, since there are 5
fields, we decide to distribute them across 2 rows. The default value is 3.

Finally, the ``fields`` attribute contains a list of fields, each of them corresponding to a table. The ``name`` attribute provides a
descriptive label for the field. The ``match_all`` attribute is a partial regular expression that should match all possible values for the field. If a
user doesn't check any value for a field, this attribute will be used to retrieve all possible values. The ``values`` attribute contains a list
of values. Each of them contains (at least) a ``match`` attribute, which is a string corresponding to the value, and a ``text`` attribute which
is the label that will be displayed in the user interface for the corresponding value, along with a check box. Note that leaving all values unchecked
has the same effect as checking them all.

.. warning::

    Because protocol files are parsed by the scripting engine, a regular expression that contains braces —
    for instance a counted repetition such as ``[0-9]{2}`` — must not be written as-is in a double-quoted
    string: the ``{2}`` part would be interpreted as string interpolation. Either escape the braces
    (``"[0-9]\{2\}"``) or, preferably, use a single-quoted raw string (``'[0-9]{2}'``), in which every
    character stands for itself.
