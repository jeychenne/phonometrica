.. _queries:

Queries
=======


Phonometrica offers a number of features to search through annotated corpora and extract acoustic measurements.
Query features are available in the **Analysis** menu.


.. _text-queries:

Text queries
------------

To run a new text query, click on ``Analysis > Find in annotations...`` or use
the shortcut ``Ctrl+Shift+F``. This opens the query editor, which lets you search
through all the annotations in your corpus.

The Files box
~~~~~~~~~~~~~

The ``Files`` box allows you to select which annotation files to search in. You can select files individually
to restrict your query, or leave all files unchecked to search in all annotation files.

The Search box
~~~~~~~~~~~~~~

The ``Search`` box allows you to enter a text pattern or a regular expression to search for.
Next to the search field, a spin box lets you select the **layer** you want to search in. The default choice
is ``Any layer``, which means that Phonometrica will search in all layers of the selected files. You can
restrict the search to a particular layer by selecting its index. Alternatively, you can specify a **layer
name pattern** using a regular expression. If you specify a layer name pattern, Phonometrica will ignore the
layer index and search instead in any layer whose name matches the pattern.

By default, the text in the search field is interpreted as a regular expression. If you prefer plain text
matching, you can select ``plain text`` instead of ``regular expression`` in the selector below the ``+``
and ``-`` buttons. Whether you use plain text or a regular expression, the search is case-insensitive by
default. To perform a case-sensitive search, check the ``case sensitive`` box.

Concordances from a simple query follow the **KWIC** (Key Word In Context) model, which means that a match
is extracted along with its left and right context. The length of the context window can be adjusted in the
preferences. When the context window extends beyond a single event, Phonometrica joins text from adjacent
events using a separator (one space by default, configurable in the ``Separator`` field).


Metadata filters
~~~~~~~~~~~~~~~~

If your project has properties, a set of filter controls appears below the search area. Each property
category is displayed as a group of checkboxes. The search engine filters files based on the conditions
you specify:

- **Within** a category, Phonometrica uses the Boolean **OR** operator: a file matches if it has any of the checked labels.
- **Across** categories, Phonometrica uses the Boolean **AND** operator: a file must match all categories.

An additional field at the bottom allows you to filter files based on their **description** (including or
excluding files that contain a specific string).


Saving and editing queries
~~~~~~~~~~~~~~~~~~~~~~~~~~

You can **save** a query for later reuse using the ``Save`` or ``Save as...`` buttons in the query editor.
Saved queries are stored as ``.phon-query`` files and appear in the project tree. To re-run or modify a
saved query, use ``Analysis > Edit last query...`` (``Ctrl+L``) or double-click on the query in the
project tree.


.. _complex-queries:

Complex queries
---------------

A simple query searches for a pattern in one event (interval or instant) at a time. Sometimes, however,
you need to match text in several events **simultaneously** — for instance, to find a word on one layer
that is aligned with a particular part-of-speech tag on another layer. This is called a *complex query*.

Building a complex query
~~~~~~~~~~~~~~~~~~~~~~~~

Below the main search field, two buttons (``+`` and ``-``) allow you to add and remove search constraints.
Any query with more than one constraint is a complex query.

When you add one or more constraints, each constraint (except the last) is followed by a selector that
specifies the **relation** between the current constraint and the next one. Phonometrica supports the
following relations:


Alignment
^^^^^^^^^

Two events are **aligned** if they are on different layers and their left and right boundaries coincide.

*Example*: to extract all nouns from a corpus with a word layer (layer 1) and a POS layer (layer 2) that
is aligned with the word layer: set ``NOUN`` as the search pattern for layer 1, choose ``is aligned with``,
set ``.+`` as the pattern for layer 2, and choose layer 2 as the display layer. Phonometrica will return
the words on layer 2 that are exactly aligned with a ``NOUN`` item on layer 1.


Left alignment
^^^^^^^^^^^^^^

Two events are **left-aligned** if they share their left boundary (start time) but not necessarily their
right boundary. This is useful for hierarchical structures where a larger unit starts at the same point
as a smaller unit.


Right alignment
^^^^^^^^^^^^^^^

Two events are **right-aligned** if they share their right boundary (end time). This is the mirror of
left alignment.


Dominance
^^^^^^^^^

An event *a* **dominates** an event *b* if *a* and *b* are on different layers, the left boundary of *b*
is greater than or equal to that of *a*, and the right boundary of *b* is less than or equal to that of
*a*. Dominance relations encode hierarchical structures — for instance, a word dominating the syllables
it contains.


Strict dominance
^^^^^^^^^^^^^^^^

**Strict dominance** is like dominance, but requires that the boundaries are *strictly* contained: the
dominated event's boundaries must fall strictly within those of the dominating event (not coinciding
with either boundary). This is useful when you want to exclude cases where the inner event spans the
entire outer event.


Precedence (precedes)
^^^^^^^^^^^^^^^^^^^^^

Two events are in a **precedence** relation if the first one immediately precedes the second on the same
layer (i.e. the end time of the first event equals the start time of the second). You can chain
multiple constraints with this relation to search for sequences of events.

*Example*: to find all ``DET + NOUN`` sequences on a POS layer (layer 1), with the result displayed from
a word layer (layer 2): set ``DET`` for the first constraint on layer 1, choose ``precedes``, and set
``NOUN`` for the second constraint on layer 1. The display layer should be set to layer 2. Phonometrica
will return the concatenated words from layer 2 that span the matched sequence on layer 1.


Subsequence (follows)
^^^^^^^^^^^^^^^^^^^^^

The **follows** relation is the reverse of precedence: the first event immediately follows the second.


Display layer
~~~~~~~~~~~~~

Complex queries do not use the KWIC model. Instead, you choose a **display layer** at the top of the
search editor. The text displayed in the result is the concatenation of all events on the display layer
within the time span defined by the matched constraints.


.. _acoustic-queries:

Acoustic queries
----------------

In addition to text queries, Phonometrica can extract acoustic measurements from your corpus. Acoustic queries
combine the text search infrastructure (constraints, metadata filters, file selection) with acoustic analysis
algorithms. The results are displayed in a concordance view with additional measurement columns.

All acoustic queries require that the annotation files being searched are bound to sound files.


Formant queries
~~~~~~~~~~~~~~~

To run a formant query, click on ``Analysis > Measure formants...``. The formant query editor extends the
text query editor with a panel for formant analysis settings:

- **Number of formants**: the maximum number of formants to extract (typically 3 or 4).
- **Maximum frequency**: the highest frequency below which formants are expected (e.g. 5000 Hz for male
  voices, 5500 Hz for female voices).
- **Window length**: the duration of the LPC analysis window (in seconds).
- **LPC order**: the number of prediction coefficients. By default, Phonometrica uses 2 × *number of formants* + 2.

Formant values are measured at either the **midpoint** of the matched event or as an **n-point average**
over equally spaced time points. When n-point averaging is used, the result concordance can be toggled between
wide format (one row per match, with F1_1, F1_2, ... columns) and long format (one row per time point) using
the Display settings menu.

**Weenink's method**: Phonometrica implements an automatic formant selection method based on
Weenink (2015), which evaluates multiple LPC analyses with different parameter settings and selects the
formant track that best matches reference values for the vowel category. To use this method, select
*Automatic* instead of *Manual* in the formant settings panel.

Optionally, you can include **bandwidth** columns in the output. Formant values stored in Hertz can be
converted to **ERB** or **Bark** scales on the fly using the Scales menu in the concordance toolbar
(see :ref:`concordance-view`).


Pitch queries
~~~~~~~~~~~~~

To run a pitch query, click on ``Analysis > Measure pitch...``. The pitch query editor adds a panel for
pitch analysis settings:

- **Algorithm**: Phonometrica supports five pitch tracking algorithms: **REAPER** [TAL2014]_ (the default),
  **Harvest** [MOR2017]_, **RAPT** [TAL1995]_, **SWIPE** [CAM2007]_, and **Praat** [BOE1993]_. Reaper,
  Harvest, and RAPT are provided by the Speech Signal Processing Toolkit (SPTK); SWIPE and Praat are
  dedicated implementations. See :ref:`sound-view` for references.
- **Minimum pitch** and **Maximum pitch**: the expected pitch range.
- **Voicing threshold**: sensitivity to voicing detection. The valid range and default value depend on the
  selected algorithm (for example, 0.2–0.5 with default 0.3 for SWIPE, −0.5–1.6 with default 0.9 for REAPER);
  the editor updates the default automatically when you change algorithm.
- **Time step**: determines the temporal resolution of the pitch track.

When **Praat** is selected, four additional parameters are revealed, corresponding to Praat's ``To Pitch (ac)`` command:

- **Silence threshold** (default 0.03): frames below this relative amplitude are treated as silent.
- **Octave cost** (default 0.01): favors higher-frequency candidates during path selection.
- **Octave-jump cost** (default 0.35): penalty for an octave jump between adjacent frames.
- **Voiced/unvoiced cost** (default 0.45): penalty for a voiced↔unvoiced transition.

Like formant queries, pitch can be measured at the midpoint or as an n-point average. Pitch values in
Hertz can be converted to **semitones** (relative to a reference) or to **ERB rate** via the Scales menu.


Intensity queries
~~~~~~~~~~~~~~~~~

To run an intensity query, click on ``Analysis > Measure intensity...``. The intensity query editor adds
settings for:

- **Minimum intensity** and **Maximum intensity**: the expected intensity range.
- **Time step**: the temporal resolution of the intensity contour.

Intensity can be measured at the midpoint or as an n-point average.


Spectral moments queries
~~~~~~~~~~~~~~~~~~~~~~~~

**Spectral moments** characterize the shape of the spectral distribution and are widely used in phonetics
for the analysis of fricatives and other obstruents. To run a spectral moments query, click on
``Analysis > Measure spectral moments...``.

Phonometrica computes four spectral moments from the power spectrum, treating the spectrum as a
probability distribution over frequency:

- **Centre of gravity** (COG, 1st moment): the mean frequency, weighted by spectral power.
- **Spread** (2nd moment): the standard deviation of the distribution, reflecting how dispersed
  the energy is around the COG.
- **Skewness** (3rd moment): the asymmetry of the distribution. Positive skewness indicates more
  energy below the COG; negative skewness indicates more energy above it.
- **Kurtosis** (4th moment, excess): the peakedness of the distribution relative to a Gaussian.
  Positive kurtosis indicates a sharper spectral peak; negative kurtosis indicates a flatter distribution.

The spectral moments query editor extends the text query editor with a panel for analysis settings:

- **Window duration**: the length of the analysis window (in seconds). The default is 25 ms, which
  is typical for fricative analysis.
- **Window type**: the shape of the window function applied before the FFT (Gaussian by default).
- **Min frequency** and **Max frequency**: the frequency range over which the moments are computed.
  By default, the full range from 0 Hz to the Nyquist frequency is used. You can restrict the range
  to focus on a particular spectral region (e.g. 1000–11025 Hz to exclude low-frequency voicing energy).
- **Pre-emphasis**: a 6 dB/octave high-pass filter that compensates for the spectral tilt of voiced
  sounds. Enabled by default with a threshold of 50 Hz.
- **Output**: checkboxes let you select which of the four moments to include in the concordance.
  All four are enabled by default.

Like other acoustic queries, spectral moments can be measured at the **midpoint** of the matched event
or as an **n-point** measurement at user-specified percentages. When n-point measurement is selected,
the result concordance can be toggled between wide and long format.


References
----------

.. [BIR2001] Bird, Steven & Mark Liberman. 2001. A Formal Framework for Linguistic Annotation. *Speech Communication* 33(1–2). 23–60.

.. [CAM2007] Camacho, Arturo. 2007. SWIPE: A sawtooth waveform inspired pitch estimator for speech and music. PhD dissertation, University of Florida Gainesville.

.. [BOE1993] Boersma, Paul. 1993. Accurate short-term analysis of the fundamental frequency and the harmonics-to-noise ratio of a sampled sound. *Proceedings of the Institute of Phonetic Sciences, University of Amsterdam* 17. 97–110.

.. [MOR2017] Morise, Masanori. 2017. Harvest: A high-performance fundamental frequency estimator from speech signals. *Proceedings of INTERSPEECH 2017*, 2321–2325.

.. [TAL1995] Talkin, David. 1995. A robust algorithm for pitch tracking (RAPT). In W. B. Kleijn & K. K. Paliwal (eds.), *Speech Coding and Synthesis*, 495–518. Amsterdam: Elsevier.

.. [TAL2014] Talkin, David. 2014. REAPER: Robust Epoch And Pitch EstimatoR. Software, Google. https://github.com/google/REAPER.

.. [WEE2015] Weenink, David. 2015. Improved formant frequency measurements of short segments. *Proceedings of the 18th International Congress of Phonetic Sciences*. Glasgow: University of Glasgow.
