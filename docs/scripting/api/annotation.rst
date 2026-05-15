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


------------


Structural transformations
~~~~~~~~~~~~~~~~~~~~~~~~~~

These functions produce a **new** annotation file on disk and return a fresh ``Annotation`` handle.
They do not modify the source, and they do not add the result to the current project — call
``import_file(path)`` if you want the new file in the project. Properties, description, and (where
the operation preserves it) sound binding are inherited from the source; for multi-source
operations, property collisions are resolved by last-input-wins.

The on-disk format of the result is inferred from the output path's extension (``.phon-annot`` →
native, ``.TextGrid`` → Praat TextGrid).


.. function:: duplicate_annotation(annot as Annotation, path as String)

Writes a copy of ``annot`` to ``path`` and returns the new ``Annotation``. The output format
follows the source.

------------

.. function:: extract_layers(annot as Annotation, indices as List, path as String)

Extracts the layers whose 1-based indices appear in ``indices`` into a new annotation at
``path`` and returns it. Indices must be distinct and in range; the order in the result follows
the order of ``indices``. The sound binding is preserved (duration is unchanged).

For example, to extract only the first and third layers::

    let small = extract_layers(annot, [1, 3], "/tmp/small.phon-annot")

------------

.. function:: merge_annotations(base as Annotation, others as List, path as String)

Builds a new annotation at ``path`` whose layers are the layers of ``base`` followed by the layers
of each annotation in ``others`` (in list order). All annotations must have the same effective
duration within 1 ms (the bound sound's duration if available, otherwise the maximum event end
time). The sound binding and description are inherited from ``base``. Layer-label collisions in
the output are disambiguated by appending ``" (2)"``, ``" (3)"``, and so on. Returns the new
``Annotation``.

------------

.. function:: extract_annotation_slice(annot as Annotation, t_start as Number, t_end as Number, path as String)
.. function:: extract_annotation_slice(annot as Annotation, t_start as Number, t_end as Number, clip_partial as Boolean, path as String)

Extracts the time slice ``[t_start, t_end]`` from ``annot`` into a new annotation at ``path`` and
returns it. Event times in the result are shifted so the slice starts at zero. The three-argument
form is equivalent to passing ``clip_partial = true``.

Boundary handling for interval events:

* fully outside the slice → dropped;
* fully inside → kept and shifted;
* partially overlapping → kept and clipped to the boundary when ``clip_partial`` is true,
  dropped otherwise.

Instants are not affected by ``clip_partial`` — an instant at time *t* is kept iff
``t_start <= t <= t_end``.

The sound binding is **not** inherited: the bound sound no longer matches the new duration. To
extract a matching sound slice, see ``extract_sound_slice`` (in :ref:`sound-type`).

------------

.. function:: concatenate_annotations(sources as List, path as String)
.. function:: concatenate_annotations(sources as List, durations as List, path as String)

Concatenates the annotations in ``sources`` end-to-end into a new annotation at ``path`` and
returns it. Events from source *i* are shifted by the cumulative duration offset. All sources
must have the same number of layers and matching layer kinds (interval vs. instant) at each index;
layer labels and the on-disk format are taken from the first source.

In the two-argument form, every source must be bound to a sound (the bound sound's duration is
used). In the three-argument form, ``durations`` must give one duration in seconds per source,
in the same order. Use the latter when any source is unbound — inferring durations from "maximum
event end" would silently truncate an annotation with meaningful trailing silence.

The result is **not** bound to any sound. To build a bound concatenation, concatenate the matching
sounds separately with ``concatenate_sounds`` and call ``bind_to_sound`` on the result.

------------


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
