.. _sound-view:

Sound visualization and analysis
================================

Phonometrica offers a dedicated environment for speech visualization and analysis. To visualize a sound file, you need to open it in a **sound view**. 
To open a sound view, double-click on a sound file in the file manager, or right-click on it and choose ``View file`` from the context menu. 
When it is opened, the sound view will display the first 10 seconds of the sound file, or the whole sound file if it is shorter than that.


Structure of sound views
------------------------

Toolbar
~~~~~~~

The toolbar is located at the top of the sound view and provides a number of buttons which can execute actions or display menus. 

Wavebar
~~~~~~~

The **wave bar** is located at the bottom of the sound view: it shows a simplified waveform of the whole sound file, and indicates
which part of the file is currently selected. You can select any portion of the wave bar to zoom in on a portion of the
sound file: The other plots (waveform, pitch track and intensity track) will be adjusted to display the portion you
have selected. You can also use the mouse wheel over the wave bar and scroll it up or down to shift the selected window
left or right, respectively.

Waveform
~~~~~~~~

The waveform displays a two-dimensional representation of the sound, with time on the *x* axis 
and amplitude on the *y* axis. The waveform is always present and cannot be hidden. 

The ``Waveform settings...`` command (available from the waveform menu |waveform| in the toolbar) allows you to alter the range of amplitudes used to display the waveform.
By default, Phonometrica uses **local magnitude**, which is the largest magnitude in the current window. As a result, the magnitude will change every time the
window changes. If you prefer to use a fixed magnitude instead, you can either choose **global magnitude**, which will use the largest magnitude in the whole 
sound file, or **fixed magnitude** to set a custom magnitude. Note that the largest possible magnitude is 1.


Spectrogram
~~~~~~~~~~~

A spectrogram offers a three-dimensional representation of the signal, with time on the *x* axis, frequency on the
*y* axis and intensity as shades of grey (the darker it is, the higher the intensity is). The appearance of the
spectrogram can be adjusted by changing the following settings, using the ``Spectrogram settings...`` command
available from the spectrogram menu |spectrogram| in the toolbar:

* ``spectrogram type``: the type of spectrogram is determined by the duration of the analysis window.
  A **wide-band spectrogram** is obtained with a short analysis window (5 ms by default): this type of spectrogram has good
  time resolution, which allows us to see individual glottal pulses as vertical striation lines, but it has poor frequency resolution. A **narrow-band spectrogram** uses a long analysis window (25 ms by default): it has poor time resolution but good frequency resolution, which allows us to see individual harmonics as thin horizontal bands. You can choose a custom window length
  (in milliseconds) if the default choices don't fit your needs.
* ``frequency range``: the range of frequencies that is displayed. If this value is higher than the Nyquist frequency for
  a given file (i.e. half its sampling frequency), Phonometrica will use the Nyquist frequency instead of this setting.
* ``dynamic range``: this value determines the degree of contrast in the spectrogram. All values that are less than
  *max_dB − dynamic_range* are displayed in white, where *max_dB* is the highest intensity in the current window.
* ``window type``: this parameter indicates the shape of the window that is applied to a segment of the sound file before
  calculating its Fast Fourier Transform.
* ``pre-emphasis threshold``: threshold of the high-pass pre-emphasis filter. The amplitude of the frequencies above this
  threshold will be increased. This value is plugged into the following equation: :math:`y[n] = x[n] - \exp(-2 \pi f \frac{1}{F_s}) x[n-1]`,
  where :math:`f` is the pre-emphasis threshold and :math:`F_s` is the sampling rate.

You can show or hide the spectrogram using the ``Show spectrogram`` command in the spectrogram menu.


Formant tracks
~~~~~~~~~~~~~~

Formant tracks are overlaid over the spectrogram, so the spectrogram must be visible to be able to display formants. By default,
Phonometrica shows the first 4 formants (F1, F2, F3, F4), if they are defined. Phonometrica's formant tracking algorithm is based
on Linear Predictive Coding (LPC). The ``Formant settings...`` command (available from the formants menu |formants| in the
toolbar) allows you to adjust the formant tracking algorithm's parameters:

* ``number of formants``: the maximum number of formants to extract and display over the spectrogram.
* ``maximum frequency``: the highest frequency below which formants are expected to be found. For
  vowel analysis, a good rule of thumb is to use 5000 Hz for male voices and 5500 Hz for female voices.
* ``maximum bandwidth``: candidate formants whose bandwidth exceeds this threshold (400 Hz by default) will be discarded. If you don't
  want this behavior, set this value to a high value such as ``maximum frequency``.
* ``window length``: the duration (in seconds) of the analysis window used to calculate prediction coefficients.
* ``LPC order``: the number of prediction coefficients used for LPC analysis. By default, Phonometrica applies the following
  formula:  :math:`LPC\ order = 2n + 2`, where *n* is the expected number of formants.

Pitch track
~~~~~~~~~~~

The pitch track is a two-dimensional representation of the sound which shows how pitch (measured in Hertz) changes over time. Phonometrica supports five pitch
tracking algorithms: **REAPER** [TAL2014]_ (the default), **Harvest** [MOR2017]_, **RAPT** [TAL1995]_, **SWIPE** [CAM2007]_, and **Praat** [BOE1993]_.
Reaper, Harvest, and RAPT come from the Speech Signal Processing Toolkit (SPTK); SWIPE and Praat are dedicated implementations.

The ``Pitch settings...`` command (available from the pitch menu |pitch| in the toolbar) allows you to choose the algorithm and adjust its parameters:

* ``method``: the pitch tracking algorithm to use (Reaper, Harvest, RAPT, SWIPE, or Praat).
* ``minimum pitch``: the lowest pitch value expected to be found in the sound.
* ``maximum pitch``: the highest pitch value expected to be found in the sound.
* ``time step``: this determines the number of points used to estimate pitch in the current window.
* ``voicing threshold``: sensitivity of the algorithm to voicing detection. The valid range and default value depend on the chosen method (for example, 0.2–0.5 with default 0.3 for SWIPE, −0.5–1.6 with default 0.9 for REAPER).

When **Praat** is selected, four additional parameters are exposed, matching Praat's ``To Pitch (ac)`` command:

* ``silence threshold`` (default 0.03): frames below this relative amplitude are treated as silent.
* ``octave cost`` (default 0.01): favors higher-frequency candidates during path selection.
* ``octave-jump cost`` (default 0.35): penalty for an octave jump between adjacent frames.
* ``voiced/unvoiced cost`` (default 0.14): penalty for a voiced↔unvoiced transition.

You can show or hide the pitch track using the ``Show pitch`` command in the pitch menu.

Intensity track
~~~~~~~~~~~~~~~

The intensity track is a two-dimensional representation of the sound which shows how intensity (measured in decibels) changes over time. The ``Intensity settings...`` command
(available from the intensity menu |intensity| in the toolbar) allows you to adjust intensity settings:

* ``minimum intensity``: the lowest intensity value expected to be found in the sound.
* ``maximum intensity``: the highest intensity value expected to be found in the sound.
* ``time step``: this determines the number of points used to estimate intensity in the current window.

You can show or hide the intensity track using the ``Show intensity`` command in the intensity menu.


Voice quality
~~~~~~~~~~~~~

The voice quality menu |voice| in the toolbar gives access to perturbation measures of a voiced
signal. Two commands are available:

* ``Show glottal pulses`` overlays the glottal-closure instants (GCIs) detected by REAPER [TAL2014]_
  on top of each waveform. Pulses inside voiced regions are drawn as short vertical ticks; pulses
  REAPER would otherwise insert across unvoiced gaps are filtered out.
* ``Get voice report`` (shortcut ``F9``) computes a full voice-quality report — jitter, shimmer and
  harmonics-to-noise ratio — over the current selection. See :ref:`voice-report` below.

All voice-quality measures use the pitch range from the pitch settings (``minimum pitch`` /
``maximum pitch``) to bound REAPER's period search and to filter out-of-range periods from the
perturbation aggregates.


How to use sound views
----------------------


Playing a sound
~~~~~~~~~~~~~~~

To play a sound, you can use the play button |play|: if there is a selection in the current window, Phonometrica will only play this selection, otherwise it will play the 
whole window. Once playing has started, a moving cursor will track the approximate time which is currently being played. The play button will turn into a pause button |pause|, 
which allows you to pause (and then later resume) playing. You can also stop playing using the stop button |stop|.


Changing the current window
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Phonometrica offers a number of ways to navigate through the file, using the wavebar, the navigation buttons in the toolbar, or the mouse.

First, you can select any part of the wavebar to display it as the current window. If you would like to keep the same window size and shift the sound left or right, you can hover
the mouse over the wavebar and use the scroll wheel: scrolling down will shift the current window forward, and scrolling up will shift it backward.

Once you have selected a portion of the file, you can change it using the toolbar's buttons. The forward |forward| and backward |backward| buttons will shift the current window by 
a small amount, right or left, respectively. You can also zoom in |zoomin| or zoom out |zoomout| on the 
current window, which allows you to view the sound file with varying degrees of detail. If you would like to zoom in on a specific part of the current window, click where you would 
like your selection to start, and drag the mouse until the end of the selection. You can change the current window to this selection by clicking on the ``Zoom to selection``
button |zoomsel|, or by clicking on the middle button of the mouse (i.e. the scroll wheel).

Finally, the ``View whole file`` button |zoomall| allows you to set the current window to the whole file, and the ``Select window`` button |select| allows you to select a specific 
part of the sound file by setting its start and end points manually.


Acoustic measurements
~~~~~~~~~~~~~~~~~~~~~

In order to perform manual acoustic measurements, you must first enable **mouse tracking** by clicking on the ``Enable mouse tracking`` button |mouse| in the toolbar. Once mouse tracking
is activated, a vertical line will follow the cursor whenever you move the mouse over one of the sound plots. This moving cursor keeps track of the current time in the waveform 
plot. If you click on the left button anywhere in one of the sound plots, a **persistent cursor** will be displayed. (You can remove the persistent cursor by clicking on the right
button.)

Once a persistent cursor is visible, you can perform acoustic measurements by using the dedicated commands. These commands will print their output in the console:

* The ``Get pitch`` command in the pitch menu |pitch| prints the pitch under the cursor.
* The ``Get intensity`` command in the intensity menu |intensity| prints the intensity under the cursor.
* The ``Get formants`` command in the formants menu |formants| prints the value of the visible formants, as well as their respective bandwidth, under the cursor.
* The ``Get spectral moments`` command in the spectrogram menu |spectrogram| prints the centre of gravity, spread, skewness, and kurtosis at the cursor position (see :ref:`spectral-moments`).
* The ``Get voice report`` command in the voice quality menu |voice| computes jitter, shimmer and HNR over a *time-span* selection (see :ref:`voice-report`). Unlike the other commands, it requires a span, not a cursor.

Note that for these commands to work, the corresponding plot must be visible (e.g. the pitch plot must be visible if you want to measure pitch).


.. _spectral-slice:

Spectral slice
~~~~~~~~~~~~~~

Phonometrica can display a **spectral slice** (power spectrum) at the current cursor position. This is similar to
Praat's "View spectral slice" feature. To view a spectral slice, place a persistent cursor on the sound (by clicking
with mouse tracking enabled) and then choose ``View spectral slice`` from the spectrogram menu |spectrogram| in the toolbar.

A new window will open showing the power spectrum as a frequency-versus-power line plot. The spectral slice
supports three display modes:

* **FFT only**: the traditional power spectrum computed via Fast Fourier Transform (shown as a blue curve).
* **LPC only**: a smooth spectral envelope derived from LPC analysis (shown as a red curve).
* **FFT + LPC**: both the FFT spectrum and the LPC envelope superimposed.

You can hover over the plot to read frequency and power values at the cursor position. The spectral slice
can be exported to **PNG**, **PDF**, or **SVG** using the toolbar buttons in the spectrum window.


.. _spectral-moments:

Spectral moments
~~~~~~~~~~~~~~~~

Phonometrica can compute **spectral moments** (centre of gravity, spread, skewness, and kurtosis) at the
current cursor position or within a selected time span. These four moments characterize the shape of the
spectral energy distribution and are commonly used in phonetics for the analysis of fricatives and other
obstruents.

To compute spectral moments, place a persistent cursor on the sound (or select a portion of the signal)
and choose ``Get spectral moments`` from the spectrogram menu |spectrogram| in the toolbar. If a time span
is selected, the entire selection is used as the analysis window. If only a cursor is placed, Phonometrica
prompts you for a window duration (default: 25 ms) and centres the window around the cursor.

The analysis uses the spectrogram settings for the window type, pre-emphasis, and frequency range. The
results are printed in the output panel:

- **COG** (centre of gravity): the mean frequency in Hz.
- **Spread**: the standard deviation of the spectral distribution in Hz.
- **Skewness**: the asymmetry of the distribution (dimensionless).
- **Kurtosis**: the peakedness relative to a Gaussian (excess kurtosis, dimensionless).

To extract spectral moments systematically from a corpus, use a spectral moments query
(see :ref:`acoustic-queries`).


.. _voice-report:

Voice report
~~~~~~~~~~~~

The **voice report** is a one-shot summary of voice-quality measures over a selected time span.
It mirrors Praat's "Voice report" so values can be cross-checked against Praat. To compute it,
select a portion of the signal that contains continuous voiced speech (e.g. a single sustained
vowel), then choose ``Get voice report`` from the voice quality menu |voice| in the toolbar, or
press ``F9``. A single-point cursor is not enough — jitter, shimmer and HNR all require a span.

The report contains four blocks per visible channel:

- **Pulses**: the number of glottal-closure instants detected by REAPER [TAL2014]_ in voiced
  regions of the selection, together with the mean period and the corresponding mean F0. The
  mean is taken over periods inside the pitch range only.
- **Jitter**: five period-perturbation measures.

  - *Local*: mean absolute difference between consecutive periods, normalised by the mean
    period (a relative measure, printed as a percentage).
  - *Local, absolute*: mean absolute difference between consecutive periods, in microseconds.
  - *RAP*: relative average perturbation over a 3-period window.
  - *PPQ5*: period perturbation quotient over a 5-period window.
  - *DDP*: difference of differences of periods (= 3 × RAP by construction).
- **Shimmer**: five amplitude-perturbation measures, defined analogously to the jitter family
  on peak amplitudes around each glottal pulse.

  - *Local*: relative mean absolute amplitude difference (percentage).
  - *Local, in dB*: mean absolute difference of consecutive amplitudes in decibels.
  - *APQ3*, *APQ5*, *APQ11*: amplitude perturbation quotient over a 3-, 5- or 11-period window.
- **HNR**: harmonics-to-noise ratio, in dB, averaged over voiced frames. Derived from the
  normalised autocorrelation strength of the Praat-style pitch tracker [BOE1993]_:
  :math:`\mathrm{HNR}_{\mathrm{dB}} = 10\,\log_{10} \frac{r}{1 - r}`, where :math:`r` is the
  per-frame chosen-path strength.

Each measure displays ``(undefined)`` when there are too few valid pulses or no voiced frames
to compute it.

Jitter and shimmer reject periods that fall outside the pitch range, pairs whose period ratio
exceeds 1.3, and (for shimmer) pairs whose amplitude ratio exceeds 1.6. These filters match
Praat's defaults; they are essential on real speech because a single misplaced pulse —
typically at a voicing boundary — would otherwise dominate the aggregate. The same selection
can therefore yield a meaningful report even when REAPER inserts a few spurious pulses near
the edges of the voiced segment.

The same computation is also available from the scripting engine through the
:func:`get_voice_report` function, which returns a ``Table`` whose keys mirror the fields in
the printed report; see the :ref:`Sound API <sound-type>` page.


.. _sound-file-operations:

Sound file operations
---------------------

Phonometrica lets you transform sound files as files: pulling out a time range, and concatenating
several sounds end-to-end. Both operations produce a **new** sound file on disk and add it to the
current project. They are accessible from the context menu in the file manager.

Extract slice
~~~~~~~~~~~~~

Right-click a sound and choose **Extract slice...**. The dialog has plain text fields for the start
and end times in seconds and a format selector (WAV, AIFF, FLAC, or OGG). Sample rate and channel
count are preserved. Data is streamed through libsndfile, so extracting a 10-second slice from a
multi-hour recording is fast and memory-light.

This same dialog appears, with additional options, when you right-click an annotation that is
bound to a sound — in that case you can extract the sound, the annotation, or both, and the "both"
mode automatically binds the new annotation to the new sound (see :ref:`annotation-file-operations`).

Concatenate sounds
~~~~~~~~~~~~~~~~~~

To glue sounds end-to-end, multi-select two or more sounds in the file manager, right-click, and
choose **Concatenate sounds...**. The dialog shows the sources in a list you can reorder by drag
and drop, plus an output path and format selector.

All sources must share the same **sample rate** and **channel count**. Any mismatch is reported
in red at the top of the dialog and the operation is blocked until you cancel — resampling the
inputs to a common rate is a separate step you need to do beforehand (a future version may offer
resample-on-concatenate). The output keeps the common rate and channel count, with the chosen
container format. Data is streamed frame by frame so the cost is linear in the total duration.


References
----------

.. [BOE1993] Boersma, Paul. 1993. Accurate short-term analysis of the fundamental frequency and the harmonics-to-noise ratio of a sampled sound. *Proceedings of the Institute of Phonetic Sciences, University of Amsterdam* 17. 97–110.

.. [CAM2007] Camacho, Arturo. 2007. SWIPE: A sawtooth waveform inspired pitch estimator for speech and music. PhD dissertation, University of Florida Gainesville.

.. [MOR2017] Morise, Masanori. 2017. Harvest: A high-performance fundamental frequency estimator from speech signals. *Proceedings of INTERSPEECH 2017*, 2321–2325.

.. [TAL1995] Talkin, David. 1995. A robust algorithm for pitch tracking (RAPT). In W. B. Kleijn & K. K. Paliwal (eds.), *Speech Coding and Synthesis*, 495–518. Amsterdam: Elsevier.

.. [TAL2014] Talkin, David. 2014. REAPER: Robust Epoch And Pitch EstimatoR. Software, Google. https://github.com/google/REAPER.




.. |waveform| image:: ../icons/waveform.svg
    :height: 16px
    :width: 16px

.. |spectrogram| image:: ../icons/spectrum.svg
    :height: 16px
    :width: 16px

.. |pitch| image:: ../icons/pitch.svg
    :height: 16px
    :width: 16px    

.. |intensity| image:: ../icons/ear.svg
    :height: 16px
    :width: 16px

.. |formants| image:: ../icons/waves.svg
    :height: 16px
    :width: 16px

.. |voice| image:: ../icons/voice.svg
    :height: 16px
    :width: 16px

.. |play| image:: ../icons/play.svg
    :height: 16px
    :width: 16px

.. |pause| image:: ../icons/pause.svg
    :height: 16px
    :width: 16px

.. |stop| image:: ../icons/square.svg
    :height: 16px
    :width: 16px

.. |forward| image:: ../icons/arrow-right.svg
    :height: 16px
    :width: 16px

.. |backward| image:: ../icons/arrow-left.svg
    :height: 16px
    :width: 16px

.. |zoomin| image:: ../icons/zoom-in.svg
    :height: 16px
    :width: 16px

.. |zoomout| image:: ../icons/zoom-out.svg
    :height: 16px
    :width: 16px

.. |zoomsel| image:: ../icons/minimize.svg
    :height: 16px
    :width: 16px

.. |zoomall| image:: ../icons/maximize.svg
    :height: 16px
    :width: 16px

.. |select| image:: ../icons/select-window.svg
    :height: 16px
    :width: 16px

.. |mouse| image:: ../icons/mouse.svg
    :height: 16px
    :width: 16px
