Signals and slots
=================



General concepts
----------------

Phonometrica provides an internal event handling mechanism which allows scripts to react to events triggered
by the program — for instance, when a file is loaded. A *signal* is a unique identifier that can be *emitted*
anywhere; it can be bound to any number of *slots* (callback functions), which are executed whenever the signal
fires.

.. note::

   The scripting API for signals and slots (``create_signal``, ``connect``, ``disconnect``, ``emit``) is
   planned but **not yet available** in the current version. The functions are listed in the autocompletion
   hints for forward compatibility, but calling them will produce an error. Plugin hooks that depend on
   signals will be documented once the API is finalized.
