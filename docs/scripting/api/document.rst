Documents
=========

This page documents the ``Document`` type: this is an abstract base class for all files in a project that can be stored on disk and represented by a path. Subclasses include Annotation, Sound,
Dataset, Concordance and Script. ``Document`` and all its subclasses are :ref:`non-clonable <clonability>`: each value represents a unique entry in the project, and assignment shares
the underlying object rather than copying it.


Functions
---------


.. class:: Document


.. function:: load(path as String)

Imports the file at ``path`` into the current project (if not already present) and returns it as a ``Document``.
The return is polymorphic: the object can be used as an ``Annotation``, ``Sound``, ``Dataset``, etc. depending on
the file type. A relative ``path`` is resolved against the project's directory.

Example::

   var ds = load("my_data.csv")
   print(ds.nrow)


------------

.. function:: add_property(file as Document, category as String, value)

Adds a property to the document. ``category`` must be a string and ``value`` can be a string, a number or a Boolean.
If the file already has a property with the same category, the value will be replaced with the new one.


------------

.. function:: remove_property(file as Document, category as String)

Removes the property whose category is ``category`` from the document. If there is no such category, this method 
does nothing.

------------

.. function:: get_property(file as Document, category as String)

Gets the value of the property whose category is ``category`` from the document, or ``null`` if there is no such category.


Fields
------

.. attribute:: path

Returns the path of the file.

.. attribute:: label

Returns the label of the file.
