.. _modules:

Modules
=======

Overview
--------


When you start writing scripts that are relatively long and complex and/or that you would like to redistribute, you might want to break them down into smaller,
reusable components. Modules offer a way to achieve that. In Phonometrica, **every script file is a module**: the variables, constants, functions and classes
declared at the top level of a script form the module's *namespace*. Another script can *import* your module by name and use what it declares, without any
special packaging on your part — a plain analysis script "just works" as an importable library.

Top-level declarations are **public by default**: they are visible to any script that imports the module. Declarations that are implementation details can be
kept module-private with the ``local`` modifier, which applies to ``var`` and ``const`` as well as to top-level ``function`` and ``class`` declarations:

.. code:: phon

    var threshold = 0.025    # public: importers can read this as my_module.threshold
    local var cache = {}     # private: only visible inside this script

Modules are mostly used to create *namespaces*: instead of putting all your variables in the global scope, each script keeps its own, and importers refer to
your variables *qualified* by the module name (e.g. ``utils.version``). This makes your code easier to redistribute and reuse, and it avoids "polluting" the
global scope.


Creating modules
----------------

Let's create a module called ``utils`` in which we will put some utility functions. We will create a file named ``utils.phon`` for this purpose — the name of a
module is simply the name of its file, without the ``.phon`` extension. There is nothing else to do: no registration, no export list, and no special return
value at the end of the file. We only need to remember that everything at the top level is public unless it is declared ``local``:

.. code:: phon

    # utils.phon
    const version = "0.1"
    const author = "John Smith"

    # Calculate the perimeter of a rectangle
    function perimeter(length as Number, width as Number)
        return 2 * length + 2 * width
    end

    # Calculate the area of a rectangle
    function area(length as Number, width as Number)
        return length * width
    end

    # A private helper: invisible to scripts that import utils
    local function is_valid(x as Number)
        return x >= 0
    end

In this example, the module exposes two constants (``version`` and ``author``) and two functions (``perimeter`` and ``area``). The helper ``is_valid`` is
declared ``local``, so it can only be called from within ``utils.phon`` itself.


Importing modules
-----------------

We will now create a script called ``main.phon`` in the same directory as ``utils.phon``, and we will import and use the module we created earlier. This is
done with the ``import`` statement, which takes the name of the module (note: the *name*, a plain identifier — not a path and not a string):

.. code:: phon

    # main.phon
    import utils

    var l = 100
    var w = 20
    print("The perimeter of a rectangle with length = {l} and width = {w} is", perimeter(l, w))
    print("The area of a rectangle with length = {l} and width = {w} is", area(l, w))
    print("(computed with utils version {utils.version})")

Several things are worth noting here:

- ``import`` is a *compile-time statement*, not a function. The module is located and compiled before your script starts executing, and a missing module is a
  compile error (you cannot wrap ``import`` in ``try``/``catch``). The imported module's own top-level code runs *before* the importing script's first
  statement.
- To locate the module, Phonometrica looks for a file named ``utils.phon`` (or a directory module ``utils/initialize.phon``) first in the same directory as the
  importing script, and then in each directory on the module *search path* (which includes the ``Scripts`` directory of every installed plugin; see below).
- A module is loaded and executed only *once* per run, no matter how many scripts import it.
- The module's public **functions and classes** become visible as if they had been declared in your own script: we call ``perimeter(l, w)`` directly,
  unqualified. Public functions join the corresponding *generic function*, so they can add overloads to functions of the same name declared elsewhere
  (see :ref:`function overloading <function-overloading>`).
- The module's public **variables and constants**, on the other hand, are accessed *qualified* with the module name and the dot operator: ``utils.version``.
  Reading a private binding (e.g. ``utils.cache``) is an error: ``[Name error] module 'utils' has no public member 'cache'``.

Import variants
~~~~~~~~~~~~~~~

The ``import`` statement has a few convenient variants. You can give the module a shorter alias with ``as``:

.. code:: phon

    import utils as u
    print(u.version)

You can bring specific variables or constants into scope unqualified with ``for`` (optionally renaming them with ``as``):

.. code:: phon

    import utils for version
    print(version)

    import utils for author as who
    print(who)

``import utils for *`` brings in *all* of the module's public bindings unqualified, and several imports can be chained on one line with commas:
``import utils, mytools as mt``.


Reloading modules
-----------------

Since a module is only executed once per run, you may wonder what happens when you *edit* a module and want to see your changes take effect. In practice, this
takes care of itself: whenever you run a script from Phonometrica's script editor, the script and all the modules it imports are recompiled and re-executed
from scratch, so you always see the latest version of every file. There is therefore no "force reload" mechanism (and no need for one): simply re-run your
script after editing a module it depends on.


Distributing modules as plugins
-------------------------------

When Phonometrica loads a plugin, its ``Scripts`` directory is automatically added to the search path for modules. This means that you can
put your own modules in this directory and access them from your own scripts, but it also means that other users will be able to import your module
using its base name (without the ``.phon`` extension).

In order to avoid conflicts with other modules, it is recommended to give them a unique name. Since a module name must be a valid identifier (``import`` takes
a name, not a file path), use an underscore-separated prefix which is specific to your plugin. As an example, a utility module for a project named PFC could be
stored in a file named ``pfc_utils.phon``, and a user could load it as follows:

.. code:: phon

    import pfc_utils



Redistributing scripts
----------------------

If you intend to redistribute a script or module, we strongly recommend that you adhere to the following guidelines:

- remember that all top-level declarations are public by default: declare everything that is an implementation detail (helper functions, caches, temporary
  variables) with ``local``, so that your module's public interface contains only what you actually want to expose
- avoid ``global`` variables unless you really need them: public module bindings are accessed qualified by the module name, so they cannot clash with other
  modules, whereas globals live in a single shared namespace
- for all public functions, variables and fields, use ``snake_case`` rather than ``camelCase`` or ``PascalCase``; for example, use ``validate_item`` instead of ``validateItem`` or ``ValidateItem``
- provide an explicit type for function parameters: this makes your functions self-documenting, and it lets them coexist as overloads with same-named
  functions from other modules
- prefer names that are explicit, even if they are a bit longer, to names that are short but possibly difficult to understand; for example, ``list_directory`` is clearer than ``listdir`` or (worse) ``lsdir``

Following these rules will ensure that your code is easy to understand and works in a consistent and predictable way.
