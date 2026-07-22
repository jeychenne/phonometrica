Signals and slots
=================


General concepts
----------------

Phonometrica provides an internal event handling mechanism which allows scripts to react to events triggered
by the program — for instance, when a file is loaded. A *signal* is a unique identifier that can be *emitted*
anywhere; it can be bound to any number of *slots* (callback functions), which are executed whenever the signal
fires.

The signal functions are defined by Phonometrica's standard library, which is loaded when the program starts
up. They are therefore available in the console, in scripts run from the script editor, and in plugins — but
not when a script is executed from a terminal with ``phonometrica -r``.


Functions
---------

.. function:: create_signal()

Returns a new unique signal identifier (a ``String``).

------------

.. function:: connect(id as String, slot as Function)

Connects signal ``id`` to the function ``slot``, which will be called whenever the signal is emitted. A slot
receives the value passed to :func:`emit` as its single argument (which may be ``null``). Connecting the same
function twice to a signal has no effect.

.. code:: phon

    function on_annotation(annot)
        print("annotation loaded: " & annot.path)
    end

    connect(SIGNAL_ANNOTATION_LOADED, on_annotation)

------------

.. function:: disconnect(id as String, slot as Function)

Disconnects ``slot`` from signal ``id``. Does nothing if the function was not connected.

------------

.. function:: emit(id as String [, arg as Object])

Emits signal ``id``: every slot connected to it is called, in connection order, with ``arg`` (or ``null`` if
no argument is given). Returns the list of the slots' return values.


Standard signals
----------------

Phonometrica emits the following signals; connect to them to react to application events (see
:ref:`page-plugins` for typical usage in plugins):

- ``SIGNAL_ANNOTATION_IMPORTED`` — an annotation was added to the project (payload: the ``Annotation``).
- ``SIGNAL_SOUND_IMPORTED`` — a sound was added to the project (payload: the ``Sound``).
- ``SIGNAL_ANNOTATION_LOADED`` — an annotation was loaded (payload: the ``Annotation``).
- ``SIGNAL_SOUND_LOADED`` — a sound was loaded (payload: the ``Sound``).
- ``SIGNAL_SCRIPT_LOADED`` — a script was loaded.
- ``SIGNAL_DATASET_LOADED`` — a dataset was loaded.
- ``SIGNAL_PROJECT_LOADED`` — a project finished loading (no payload).
