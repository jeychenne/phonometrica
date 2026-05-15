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

These functions produce a **new** sound file on disk and return a fresh ``Sound`` handle. They do
not modify the source and do not add the result to the current project — call ``import_file(path)``
if you want the new file in the project. The on-disk format of the result is inferred from the
output path's extension (``.wav``, ``.aiff``, ``.flac``, or ``.ogg``). Data is streamed through
libsndfile, so even multi-hour files are processed in constant memory.


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


Acoustic measurement
--------------------

.. function:: get_intensity(sound as Sound, channel as Integer, time as Number)


Returns the intensity (in dB) at the given time on the specified channel.


------------

.. function:: get_mean_intensity(sound as Sound, channel as Integer, t1 as Number, t2 as Number)

Returns the mean intensity (in dB) between ``t1`` and ``t2`` on the specified channel.


------------

.. function:: get_pitch(sound as Sound, channel as Integer, time [, minimum_pitch [, maximum_pitch [, voicing_threshold]]])

Returns the pitch (in Hz) at the given time, or ``undefined`` if the sound is unvoiced at that time. Optionally, you can specify the minimum and maximum pitches, as well as the 
voicing threshold used by the pitch detection algorithm. If these optional parameters are not provided, your current settings will be used instead.


------------

.. function:: get_mean_pitch(sound as Sound, channel as Integer, t1 as Number, t2 as Number)

Returns the mean F0 value (in Hz) between ``t1`` and ``t2`` on the specified channel.


------------

.. function:: get_formants(sound as Sound, channel as Integer, time [, nformant [, maximum_frequency, [, window_length [, lpc_order]]]]])

Returns an ``Array`` containing ``nformant`` rows and 2 columns. The first column contains formant values (in Hertz), such that F1 is at index (1, 1), F2 is at index (2, 1), etc.
The second column contains the formants' bandwidths: F1's bandwidth is at index (1, 2), F2's bandwidth is at (2, 2), etc. Optionally, you can specify the number of formants to extract,
the maximum possible frequency of the last formant, the analysis window length and the LPC order. If these optional parameters are not provided, your current settings
will be used instead.


Spectrum and spectral moments
-----------------------------

.. function:: get_spectrum(sound as Sound, channel as Integer, t1 as Number, t2 as Number)

Computes an FFT spectrum from the sound between ``t1`` and ``t2`` on the specified channel and returns a ``Spectrum`` object.
The resulting spectrum can be queried for its properties (see Fields below).

Example::

   let snd = get_sounds()[1]
   let spec = get_spectrum(snd, 1, 0.5, 0.55)
   print spec.bin_count
   print spec.bandwidth

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

   let snd = get_sounds()[1]
   let m = get_spectral_moments(snd, 1, 0.5, 0.025, 1000, 10000)
   print "COG = " & m["cog"]
   print "Skewness = " & m["skewness"]


Reporting functions
-------------------

These convenience functions display acoustic measurements in the output panel for the sound loaded in the
current view. They are typically used from the console or from scripts attached to keyboard shortcuts.

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

Displays the mean formant values between ``t1`` and ``t2`` in the current view.


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
