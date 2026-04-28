Annotations
===========

This page documents the ``Annotation`` type, which corresponds to a time-aligned annotation of a sound file, on one or more layers. ``Annotation`` is :ref:`non-clonable <clonability>`.


Functions
---------


.. class:: Annotation


.. function:: get_annotations()

Return a list of all the annotations in the current project.

------------


.. function:: get_annotation(path as String)

Return the ``Annotation`` object from the current project whose path is ``path``, or ``null`` if there is no such
annotation. If the object exists but is not an annotation, an error is thrown.


------------

.. function:: get_current_annotation()

Return the ``Annotation`` object loaded in the current view, or ``null`` if the current view is not an annotation view.


------------

.. function:: bind_to_sound(annot as Annotation, path as String)

Binds the annotation to the sound file whose path is ``path``. If the sound file is not in the current project,
it will be automatically imported.

------------

.. function:: get_layer_count(annot as Annotation)

Returns the number of annotation layers.

------------

.. function:: get_layer_label(annot as Annotation, layer_index as Integer)


Gets the layer's label.

------------

.. function:: set_layer_label(annot as Annotation, layer_index as Integer, new_label as String)


Sets the layer's label.


------------

.. function:: get_event_count(annot as Annotation, layer_index as Integer)

Returns the number of events on a given layer. 


------------

.. function:: get_event_start(annot as Annotation, layer_index as Integer, event_index as Integer)

Gets the start time of an event on a given layer. Note that if the event is an instant, its start time is equal to
its end time.

------------

.. function:: get_event_end(annot as Annotation, layer_index as Integer, event_index as Integer)

Gets the end time of an event on a given layer. Note that if the event is an instant, its end time is equal to
its start time.

------------

.. function:: get_event_text(annot as Annotation, layer_index as Integer, event_index as Integer)


Gets the text of an event on a given layer.

------------

.. function:: set_event_text(annot as Annotation, layer_index as Integer, event_index as Integer, text as String)


Sets the text of an event on a given layer.

------------

.. function:: get_event_index(annot as Annotation, layer_index as Integer, time as Number)


Returns the index of the event at the given time on the specified layer.

------------

.. function:: add_interval(annot as Annotation, layer_index as Integer, start as Number, end as Number, text as String)

Add a new interval on a given layer.

------------

.. function:: add_instant(annot as Annotation, layer_index as Integer, time as Number, text as String)

Add a new instant on a given layer.

------------

.. function:: remove_interval(annot as Annotation, layer_index as Integer, start as Number, end as Number)

Remove the interval with the given boundaries from the specified layer.

------------

.. function:: remove_events(annot as Annotation, layer_index as Integer)

Remove all events from the specified layer.

------------

.. function:: create_layer(annot as Annotation, index as Integer, name as String, has_instants as Boolean)

Creates a new annotation layer at the given index. If ``has_instants`` is ``true``, the layer
contains point events (instants); otherwise, it contains intervals.

Example::

   let annot = get_annotations()[1]
   # Add a new interval layer at the end
   create_layer(annot, get_layer_count(annot) + 1, "syllables", false)

------------

.. function:: remove_layer(annot as Annotation, index as Integer)

Removes the annotation layer at the given index.

------------

.. function:: clear_layer(annot as Annotation, index as Integer)

Removes all events from the layer at the given index, without removing the layer itself.

------------

.. function:: duplicate_layer(annot as Annotation, index as Integer, new_index as Integer)

Duplicates the layer at ``index`` and inserts the copy at ``new_index``.

------------

.. function:: layer_has_instants(annot as Annotation, index as Integer)

Returns ``true`` if the layer contains point events (instants), ``false`` if it contains intervals.

------------

.. function:: save(annot as Annotation)

Saves the annotation to disk in its current format (native XML or TextGrid, depending on the file's type).

------------

.. function:: write_as_native(annot as Annotation [, path as String])

Saves the annotation in Phonometrica's native XML format. If ``path`` is provided, the annotation is written
to that path; otherwise, it is saved in place.

------------

.. function:: write_as_textgrid(annot as Annotation [, path as String])

Exports the annotation as a Praat TextGrid file. If ``path`` is provided, the annotation is written
to that path; otherwise, it is saved in place.


Fields
------

.. attribute:: path

Returns the path of the annotation file.


------------

.. attribute:: sound

Returns the ``Sound`` object to which the annotation is bound, or ``null`` if it is not bound to any sound.


------------

.. attribute:: nlayer

Returns the number of layers in the annotation.
