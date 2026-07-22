.. _sound-type:

Sounds
======

This page documents the ``Sound`` type, which represents a sound file loaded in memory, and related functions. ``Sound`` is :ref:`non-clonable <clonability>`.


Functions
---------


.. function:: get_sounds()

Return a list of all the sounds in the current project.

------------

.. function:: get_sound(path)

Return the ``Sound`` object from the current project whose path is ``path``, or ``null`` if there is no such
sound. If the object exists but is not a sound, an error is thrown.

------------

.. function:: get_current_sound()

Return the ``Sound`` object loaded in the current view, or ``null`` if the current view is neither an annotation view
nor a sound view.

------------

.. function:: get_window_duration()


Return the duration of the visible window in the current annotation or sound view.

------------

.. function:: get_selection_duration()

Return the duration of the selection in the current annotation or sound view, or 0 if there is no selection.

------------

.. function:: get_visible_channels()

Return a list of the visible channel indices in the current annotation or sound view.


Structural transformations
--------------------------

These functions produce a **new** sound file on disk and return a fresh ``Sound`` handle (except
``convert``, which returns nothing). They do not modify the source and do not add the result to
the current project — call ``phon.project.add_file(path)`` if you want the new file in the project. For
``extract_sound_slice`` and ``concatenate_sounds``, the on-disk format is inferred from the output
path's extension (``.wav``, ``.aiff``, ``.flac``, ``.ogg``, or ``.mp3``). For ``convert``, the
format is passed explicitly as a string so you can write to a path whose extension doesn't match
the desired container, or write to an extensionless path. Data is streamed through libsndfile,
so even multi-hour files are processed in constant memory.

Support for ``.mp3`` (and equivalently the ``"mp3"`` format string) depends on the libsndfile
build Phonometrica was linked against. If your platform's libsndfile lacks MPEG support, any
attempt to write an MP3 raises a clear ``[I/O error]`` rather than silently producing a broken
file.


.. function:: extract_sound_slice(sound as Sound, t_start as Number, t_end as Number, path as String)

Extracts the samples in ``[t_start, t_end]`` (in seconds) from ``sound`` into a new sound file at
``path`` and returns the resulting ``Sound``. Times must satisfy ``0 <= t_start < t_end <=
duration``. Sample rate and channel count are preserved.

------------

.. function:: concatenate_sounds(sources as List, path as String)

Concatenates the sounds in ``sources`` end-to-end into a new sound file at ``path`` and returns
it. All sources must share the same sample rate and channel count; any mismatch raises an error
identifying the first offending file. The output keeps the common rate and channel count, with
the format determined by ``path``'s extension.

------------

.. function:: convert(sound as Sound, path as String, format as String [, sample_rate as Number])

Writes ``sound`` to ``path`` in the given ``format``, optionally resampling to ``sample_rate``
(in Hz). When the rate argument is omitted, the source's sample rate is preserved and the data
is streamed straight through libsndfile (a fast path that performs no resampling).

``format`` is a case-insensitive string. The recognised names are ``"wav"``, ``"aiff"`` (also
``"aif"``), ``"flac"``, ``"ogg"``, and ``"mp3"``. A leading dot is allowed, so ``".wav"`` and
``"wav"`` behave identically. Unknown names raise an ``[Argument error]``; names that are known
but unavailable in this libsndfile build raise an ``[I/O error]``.

Channel count is always preserved. When ``sample_rate`` is given, each channel is resampled
independently using the r8brain CDSPResampler24, so stereo (and any higher channel count) is
handled correctly. The output bit depth follows the source where the target container allows it:
PCM_24 stays PCM_24 for WAV/AIFF/FLAC, FLOAT stays FLOAT on WAV, and everything else falls back
to PCM_16; OGG always writes Vorbis and MP3 always writes Layer III.

``convert`` returns no value. If you want the new file to appear in the current project, call
``phon.project.add_file(path)`` after the conversion.

Examples::

   var s = get_current_sound()

   # Re-encode without changing the sample rate.
   convert(s, "/tmp/copy.flac", "flac")

   # Downsample to 16 kHz, write as a WAV.
   convert(s, "/tmp/16k.wav", "wav", 16000)

   # The format string takes precedence over the extension.
   convert(s, "/tmp/take_001.audio", "flac")

------------


Acoustic measurement
--------------------

.. function:: get_intensity(sound as Sound, channel as Integer, time as Number)


Returns the intensity (in dB) at the given time on the specified channel.


------------

.. function:: get_mean_intensity(sound as Sound, channel as Integer, t1 as Number, t2 as Number)

Returns the mean intensity (in dB) between ``t1`` and ``t2`` on the specified channel.


------------

.. function:: get_pitch(sound as Sound, channel as Integer, time as Number [, options as Table])

Returns the F0 value (in Hz) at the given time on the specified channel, or ``undefined`` if the
sound is unvoiced at that time. When the ``options`` table is omitted, all tracker settings come
from your current pitch-tracking preferences.

The ``options`` table can be written as a literal (``{ "min_pitch": 80, "max_pitch": 400 }``) or
built up with the :ref:`named-argument syntax <named_arguments>`
(``min_pitch = 80, max_pitch = 400`` as trailing arguments). Both forms are exactly equivalent.
Validation is strict: any unknown key raises an error rather than being silently ignored, so a
typo like ``"min_picth"`` does not leave you wondering why your override had no effect. Keys you
do not supply fall back to your global pitch-tracking settings.

Supported keys:

* ``method`` (string): pitch tracker to use (one of ``"praat"`` (default), ``"harvest"``, ``"rapt"``, ``"reaper"``, ``"swipe"``).
* ``min_pitch`` (number): lower bound on the candidate F0, in Hz.
* ``max_pitch`` (number): upper bound on the candidate F0, in Hz.
* ``threshold`` (number): voicing threshold used by the tracker.
* ``octave_jump_cost`` (number): penalty applied to large frame-to-frame F0 jumps.
* ``voicing_cost`` (number): penalty controlling the voiced/unvoiced decision.
* ``silence_threshold`` (number): amplitude below which frames are treated as silent.
* ``octave_cost`` (number): bias toward higher candidates within each frame.
* ``use_gaussian`` (boolean): if ``true``, apply a Gaussian window to the analysis frames.

Example::

   var snd = get_sounds()[1]

   # Defaults from settings.
   var f0 = get_pitch(snd, 1, 0.5)

   # Override the search range. Named-argument form:
   var f0b = get_pitch(snd, 1, 0.5, min_pitch = 80, max_pitch = 400)

   # Same call, table-literal form:
   var f0c = get_pitch(snd, 1, 0.5, { "min_pitch": 80, "max_pitch": 400 })


------------

.. function:: get_mean_pitch(sound as Sound, channel as Integer, t1 as Number, t2 as Number [, options as Table])

Returns the mean F0 value (in Hz) between ``t1`` and ``t2`` on the specified channel, averaged over
the voiced frames in that interval. When the ``options`` table is omitted, all tracker settings
come from your current pitch-tracking preferences.

``options`` behaves exactly as for :func:`get_pitch`, with the same strict validation and the same
two equivalent call forms (table literal or :ref:`named arguments <named_arguments>`). All keys
listed for ``get_pitch`` are accepted, plus:

* ``time_step`` (number): frame step in seconds for the underlying pitch tracker.

Example::

   var snd = get_sounds()[1]

   var m = get_mean_pitch(snd, 1, 0.5, 1.2, min_pitch = 80, max_pitch = 400)


------------

.. function:: get_formants(sound as Sound, channel as Integer, time as Number [, options as Table])

Returns an ``Array`` containing ``nformant`` rows and 2 columns. The first column contains formant
values (in Hertz), such that F1 is at index (1, 1), F2 is at index (2, 1), etc. The second column
contains the formants' bandwidths: F1's bandwidth is at index (1, 2), F2's bandwidth is at (2, 2),
etc.

When the ``options`` table is omitted, all analysis parameters come from your current formant
settings. As for :func:`get_pitch`, ``options`` can be written as a literal
(``{ "nformant": 5, "lpc_order": 12 }``) or with the
:ref:`named-argument syntax <named_arguments>`
(``nformant = 5, lpc_order = 12``). Unknown keys raise an error.

Supported keys:

* ``nformant`` (integer): number of formants to return.
* ``nyquist`` (number): maximum frequency considered for the topmost formant, in Hz. A common
  choice is 5000 Hz for adult male voices and 5500 Hz for adult female voices.
* ``window_size`` (number): analysis window duration, in seconds.
* ``lpc_order`` (integer): order of the LPC analysis.

Example::

   var snd = get_sounds()[1]

   # Defaults from settings.
   var f = get_formants(snd, 1, 0.5)

   # Female-voice band, 5 formants:
   var f2 = get_formants(snd, 1, 0.5, nformant = 5, nyquist = 5500, lpc_order = 12)


------------

.. function:: get_voice_report(sound as Sound, channel as Integer, t1 as Number, t2 as Number [, options as Table])

Computes the full voice-quality battery (jitter, shimmer, harmonics-to-noise ratio, plus a pulse summary) over the half-open
time interval ``[t1, t2)`` on the specified channel, and returns the result as a ``Table``. When ``channel`` is 0, the per-frame
mean across channels is analysed (the "average" view).

When the ``options`` table is omitted, F0 search bounds default to 75 Hz and 600 Hz, matching Praat's voice-report defaults.
``options`` can be written as a literal (``{ "f0_min": 100, "f0_max": 500 }``) or with the
:ref:`named-argument syntax <named_arguments>` (``f0_min = 100, f0_max = 500``). Unknown keys raise an error.

Supported keys:

* ``f0_min`` (number): lower bound on the pitch tracker's periodicity search and the period filter, in Hz.
* ``f0_max`` (number): upper bound on the pitch tracker's periodicity search and the period filter, in Hz.

The returned table has 15 fields. ``num_pulses`` is the number of pulses derived from the Praat-style pitch contour
inside the selection. All other fields are ``Number`` values, and equal ``undefined`` (NaN) when there are not enough
valid pulses or voiced frames to compute the corresponding measure.

============================  =====================================================================================
Field                         Description
============================  =====================================================================================
``num_pulses``                Number of voiced pulses (integer).
``mean_period``               Mean period over in-range pulses, in seconds.
``voiced_frame_fraction``     Fraction of analysed frames classified as voiced, in [0, 1].
``mean_f0``                   Mean F0 over voiced frames, in hertz (Praat's "Mean pitch").
``jitter_local``              Mean ``|T(i+1) − T(i)| / mean(T)``, dimensionless (multiply by 100 for "percent").
``jitter_local_abs``          Mean ``|T(i+1) − T(i)|`` in seconds.
``jitter_rap``                3-point relative average perturbation (dimensionless).
``jitter_ppq5``               5-point period perturbation quotient (dimensionless).
``jitter_ddp``                Difference of differences of periods, equal to ``3 × jitter_rap``.
``shimmer_local``             Relative shimmer (dimensionless).
``shimmer_local_db``          Mean ``|20 · log10(A(i+1)/A(i))|`` in decibels.
``shimmer_apq3``              3-point amplitude perturbation quotient (dimensionless).
``shimmer_apq5``              5-point amplitude perturbation quotient (dimensionless).
``shimmer_apq11``             11-point amplitude perturbation quotient (dimensionless).
``hnr``                       Harmonics-to-noise ratio, mean over voiced frames, in decibels.
============================  =====================================================================================

Pulses, voicing decisions, mean F0 and HNR are all derived from a single pass of the Praat-style autocorrelation pitch
tracker [BOE1993]_ over the selection. Pulse times are produced by Praat's ``Sound & Pitch: To PointProcess (cc)``
algorithm — for each voiced run, the contour is walked forward at intervals of the local period and each predicted
pulse time is snapped to the nearest signal extremum within a quarter-period window. Jitter and shimmer aggregates
apply the same period (1.3) and amplitude (1.6) ratio filters as Praat's voice report. On short selections, the
surrounding context is padded so the tracker has enough Viterbi neighbourhood to make confident voicing decisions;
reported measures are still restricted to ``[t1, t2)``. See :ref:`voice-report` in the sound view documentation for
full definitions.

Example::

   var snd = get_sounds()[1]
   var r = get_voice_report(snd, 1, 0.5, 1.2)
   print("Pulses found: {r.num_pulses}")
   print("Local jitter: {100 * r.jitter_local} %")
   print("HNR: {r.hnr} dB")

When a measure is undefined (e.g. on an unvoiced selection), the corresponding field holds NaN and prints as ``nan`` (or
``undefined`` when serialised through JSON). A NaN field can be tested with the standard ``x != x`` idiom, since NaN
compares unequal to itself.


Spectrum and spectral moments
-----------------------------

.. function:: get_spectrum(sound as Sound, channel as Integer, t1 as Number, t2 as Number)

Computes an FFT spectrum from the sound between ``t1`` and ``t2`` on the specified channel and returns a ``Spectrum`` object.
The resulting spectrum can be queried for its properties (see Fields below).

Example::

   var snd = get_sounds()[1]
   var spec = get_spectrum(snd, 1, 0.5, 0.55)
   print(spec.bin_count)
   print(spec.bandwidth)

------------

.. function:: get_spectral_moments(sound as Sound, channel as Integer, time as Number, window as Number, min_freq as Number, max_freq as Number)

Computes the four spectral moments at the given time on the specified channel. ``window`` is the analysis
window duration (in seconds), and ``min_freq``/``max_freq`` define the frequency range (in Hz).

Returns a ``Table`` with the following keys:

* ``cog``: centre of gravity (1st moment), in Hz
* ``spread``: standard deviation (2nd moment), in Hz
* ``skewness``: skewness (3rd moment), dimensionless
* ``kurtosis``: excess kurtosis (4th moment), dimensionless

Example::

   var snd = get_sounds()[1]
   var m = get_spectral_moments(snd, 1, 0.5, 0.025, 1000, 10000)
   print("COG = " & m["cog"])
   print("Skewness = " & m["skewness"])


Reporting functions
-------------------

These convenience functions display acoustic measurements in the output panel for the sound loaded in the
current view. They are typically used from the console or from scripts attached to keyboard shortcuts. They are
implemented in the standard library (``speech_analysis``) on top of the measurement functions above, and are only
available when Phonometrica runs with its graphical interface.

.. function:: report_intensity(time as Number)

Displays the intensity at the given time in the current view.

------------

.. function:: report_mean_intensity(t1 as Number, t2 as Number)

Displays the mean intensity between ``t1`` and ``t2`` in the current view.

------------

.. function:: report_pitch(time as Number)

Displays the pitch at the given time in the current view.

------------

.. function:: report_mean_pitch(t1 as Number, t2 as Number)

Displays the mean pitch between ``t1`` and ``t2`` in the current view.

------------

.. function:: report_formants(time as Number)

Displays the values of the visible formants at the given time in the current view.

------------

.. function:: report_mean_formants(t1 as Number, t2 as Number)

Displays the mean formant values between ``t1`` and ``t2`` in the current view. (Not implemented yet: the current
version displays a "Not implemented yet!" message.)


Frequency conversion
--------------------

.. function:: hertz_to_bark(f)

Converts frequency ``f`` (in Hertz) to bark. See [TRA1990]_.

Note: if ``f`` is an ``Array``, the conversion is applied to all the elements in the array.

------------

.. function:: bark_to_hertz(z)

Converts frequency ``z`` (in bark) to Hertz. See [TRA1990]_.

Note: if ``z`` is an ``Array``, the conversion is applied to all the elements in the array.

------------

.. function:: hertz_to_erb(f)

Converts frequency ``f`` (in Hertz) to ERB units. See [GLA1990]_.

Note: if ``f`` is an ``Array``, the conversion is applied to all the elements in the array.

------------

.. function:: erb_to_hertz(e)

Converts frequency ``e`` (in ERB units) to Hertz. See [GLA1990]_.

Note: if ``e`` is an ``Array``, the conversion is applied to all the elements in the array.

------------

.. function:: hertz_to_mel(f)

Converts frequency ``f`` (in Hertz) to mel.

Note: if ``f`` is an ``Array``, the conversion is applied to all the elements in the array.

------------

.. function:: mel_to_hertz(mel)

Converts frequency ``mel`` (in mel) to Hertz.

Note: if ``mel`` is an ``Array``, the conversion is applied to all the elements in the array.

------------

.. function:: hertz_to_semitones(f0 [, ref])

Converts frequency ``f0`` (in Hertz) to semitones, using ``ref`` as a reference frequency (in Hertz). If ``ref`` is not provided,
it is equal to 100 Hz.

Note: if ``f0`` is an ``Array``, the conversion is applied to all the elements in the array.

------------

.. function:: semitones_to_hertz(st [, ref])

Converts the number of semitones ``st`` to Hertz, using ``ref`` as a reference frequency (in Hertz). If ``ref`` is not provided,
it is equal to 100 Hz.

Note: if ``st`` is an ``Array``, the conversion is applied to all the elements in the array.


Sound fields
------------


.. attribute:: path

Returns the path of the sound file.

------------

.. attribute:: duration

Returns the duration of the file in seconds.

------------

.. attribute:: sample_rate

Returns the sample rate of the file in Hertz.

------------

.. attribute:: nchannel

Returns the number of channels in the file.


Spectrum fields
---------------

.. attribute:: path

Returns the path of the spectrum file, or an empty string for a spectrum computed on the fly with ``get_spectrum``.

------------

.. attribute:: bin_count

Returns the number of frequency bins in the spectrum.

------------

.. attribute:: sample_rate

Returns the sample rate (in Hz) of the sound from which the spectrum was computed.

------------

.. attribute:: bandwidth

Returns the bandwidth (frequency resolution) of the spectrum in Hz.

------------

.. attribute:: max_frequency

Returns the maximum frequency in the spectrum (in Hz).

------------

.. attribute:: start_time

Returns the start time (in seconds) of the analysis window.

------------

.. attribute:: end_time

Returns the end time (in seconds) of the analysis window.

------------

.. attribute:: peak_dB

Returns the peak power level in dB.

------------

.. attribute:: floor_dB

Returns the floor power level in dB.

------------

.. attribute:: lpc_order

Returns the LPC order used for spectral envelope estimation, or 0 if no LPC was computed.

------------

.. attribute:: has_lpc

Returns ``true`` if an LPC spectral envelope has been computed.


------------

.. [GLA1990] Glasberg, Brian R & Brian C.J Moore. 1990. Derivation of auditory filter shapes from notched-noise data. *Hearing Research* 47(1–2). 103–138.

.. [TRA1990] Traunmüller, Hartmut. 1990. Analytical expressions for the tonotopic sensory scale. *The Journal of the Acoustical Society of America* 88(1). 97–100.
