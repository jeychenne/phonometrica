.. _speech-menu:

Speech
======

The ``Speech`` menu groups tools that process the speech signal itself to help populate or
enrich annotations. It currently contains two commands:

- **Find silences**, which detects speech regions in a sound and builds an annotation with
  alternating silence and speech intervals.
- **Transcribe audio**, which runs automatic speech recognition on a sound and builds an
  annotation whose intervals contain the recognized text.

Both tools produce a new annotation that is opened in a new tab in the viewer. The
annotation is bound to the source sound but is not automatically added to the current
project: use ``File > Save as...`` (or ``Ctrl+S``, then pick a location) if you want
to keep the result.

The two tools are complementary. **Find silences** is the tool of choice when you
already know what was said (for example, a word list read from a prompt sheet) and you
just need interval boundaries to fill in by hand or with a script. **Transcribe audio**
is the tool of choice when you need both the boundaries and the text in one step and a
rough transcription is enough to start from.

All processing runs locally on your computer. No data is sent over the network by either
tool.


.. _find-silences:

Find silences
-------------

**Find silences** splits the audio into alternating silence and speech regions based on
short-term energy, and produces an annotation layer that reflects that segmentation. It is
a convenient first step for a number of workflows, including:

- annotating a read word list or a controlled elicitation recording, where you only need
  the boundaries around each word and the text will be typed in by hand;
- cleaning up long recordings where you want to discard silent stretches before running
  further analyses;
- giving yourself a rough segmentation of a long interview that you will refine manually.

Open the dialog from ``Speech > Find silences...``. The sound must be in the current
project.


How the detector works
~~~~~~~~~~~~~~~~~~~~~~

The detector computes the short-term energy of the signal in 25 ms frames with a 10 ms
hop, takes the peak frame energy across the whole file as the reference level, and
classifies each frame as silent or speech by thresholding against that peak. Two
smoothing passes then clean up the mask: short silent stretches inside a speech region
are absorbed back into the speech (so the detector does not split a word on a plosive
closure), and short isolated speech stretches are dropped (so isolated clicks, coughs
or taps are not treated as speech). Finally, every detected speech region is padded
on both sides and any overlapping regions are merged.

This is deliberately a simple detector. It trusts the dynamic range of the recording:
it does not distinguish speech from other energy-bearing sounds. It works well on clean
field recordings and read speech, less well on recordings with substantial continuous
background noise close to the threshold. In the latter case, the threshold and the
duration parameters can be tuned manually, or the sound can be pre-processed in a
separate tool before being analyzed in Phonometrica.


Dialog
~~~~~~

**Sound file**: the sound from the current project to analyze.

**Layer name**: the label given to the interval layer in the output annotation. The
default is ``silences``.

**Detection parameters**

- **Silence threshold (dB)**: the level below the peak short-term power at or below
  which a frame is considered silent. The default is -25 dB, following Praat's
  ``To TextGrid (silences)`` command. More negative values are more lenient (more audio
  counts as speech); less negative values are stricter (more audio counts as silence).
  For a clean recording with well-separated words, -25 dB is usually a reasonable
  starting point; for a recording with background noise close to the target speech
  level, try values between -20 and -15 dB.

- **Min. silence duration (s)**: the shortest silent stretch that counts as a split
  between two speech regions. Silences shorter than this are absorbed into the
  surrounding speech. The default is 0.7 s, which is conservative enough to avoid
  splitting a word on a plosive closure or a short intra-word gap. For a word list with
  sharp pauses between words, you can often lower this to around 0.3-0.4 s and get
  cleaner boundaries.

- **Min. speech duration (s)**: the shortest isolated speech region to keep. Runs
  shorter than this are treated as noise and discarded. The default is 0.1 s.

- **Padding (s)**: how much audio to keep on either side of each detected speech region.
  This avoids clipping plosive bursts and final offsets. The default is 0.1 s.

**Interval labels**

- **Silence label**: the text put in each silence interval. Leave empty for an unlabeled
  interval.
- **Speech label**: the text put in each speech interval. Leave empty when you plan to
  fill in each interval by hand (for example, with the words from a word list). You can
  also put a placeholder value (``speech``, ``?``, etc.) to make the intervals easier to
  spot while you work.


Output
~~~~~~

Find silences creates a new annotation with a single interval layer. The layer covers
the full duration of the sound: a leading silence interval (if the first speech region
does not start at 0), then a speech interval for each detected region, separated by
silence intervals, then a trailing silence interval (if the last speech region does not
end at the sound's duration). When nothing is detected as speech — for example, on a
completely silent recording — the layer contains a single silence interval covering the
whole sound.

The resulting annotation is a regular Phonometrica annotation. You can edit the
boundaries by dragging anchors, split or merge intervals, fill in text, change labels,
export the result to a Praat TextGrid, or run any of the query and extraction tools on
it. See :ref:`sound_annot` for details.


Typical workflow: annotating a word list
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Add the sound file for your word list recording to the project.
2. Run ``Speech > Find silences...`` with the default parameters. Use empty labels for
   both silence and speech, so that every interval is ready to be edited.
3. Review the resulting annotation. Most boundaries should be correct; adjust the ones
   that are not by dragging anchors, adding anchors where the detector missed a boundary,
   or removing anchors where it inserted an extra one.
4. Fill in the words by clicking on each speech interval and typing. Use the right
   arrow and the enter key to move efficiently through the layer.

If the detector produced too many splits (a word was broken into two intervals), the
two most useful parameters to raise are **Min. silence duration** (to bridge short
intra-word gaps) and, less often, **Silence threshold** (to make the detector less
sensitive). Conversely, if two words were merged into one interval, lower **Min. silence
duration** or make the threshold less negative.


.. _transcribe-audio:

Transcribe audio
----------------

**Transcribe audio** runs automatic speech recognition on a sound and produces an
annotation whose intervals contain the recognized text. Transcription is powered by the
whisper.cpp port of OpenAI's Whisper model and runs entirely on your machine on the CPU.
The model file is not bundled with Phonometrica and must be downloaded separately
(see below).

Open the dialog from ``Speech > Transcribe audio...``. The sound must be in the current
project.


Dialog
~~~~~~

**Sound file**: the sound from the current project to transcribe.

**Model**: the path to a Whisper model file in ``ggml`` format. Whisper models come in
several sizes (``tiny``, ``base``, ``small``, ``medium``, ``large``), each offering a
different speed/quality trade-off. The ``base`` model is a reasonable starting point for
clean recordings in a well-resourced language such as English or French; for more
demanding material (noisy audio, less-resourced languages, very long files) you will
likely want ``small`` or ``medium``, at the cost of longer processing times.

Phonometrica does not bundle a model. The dialog provides a link to the official
``ggml``-format models hosted on Hugging Face
(https://huggingface.co/ggerganov/whisper.cpp/tree/main) — ``ggml-base.bin`` is a good
first download. Once you have a model file on disk, point the ``Model`` field at it;
Phonometrica remembers your choice between sessions.

**Language**: the language of the recording. Leaving this at ``Auto-detect`` lets Whisper
guess from the first seconds of audio; this usually works but can be wrong on short or
ambiguous material. If you know the language, select it explicitly — this is faster and
more reliable.

**Translate to English**: when checked, Whisper will translate the speech to English as
it transcribes. Leave this off to transcribe in the spoken language.

**Layer name**: the label given to the interval layer in the output annotation. The
default is ``transcription``.


Output
~~~~~~

Transcribe audio creates a new annotation with a single interval layer. Whisper produces
one segment per utterance-like unit (not per word), so the resulting intervals cover
chunks that typically span several words to several seconds. The layer only contains the
transcribed intervals; regions of the sound that Whisper considers silent are left as
gaps.

The transcription is a good starting point for further work but should not be relied on
verbatim. Whisper can hallucinate, drop short utterances, insert filler words that were
not said, or misrecognize proper names and technical vocabulary. Review the resulting
annotation and correct mistakes by clicking on each interval and editing the text. You
can also adjust the boundaries by dragging anchors.


Tips and caveats
~~~~~~~~~~~~~~~~

- **First run is slower**: loading the model and running the first inference is
  noticeably slower than subsequent runs in the same session, because Phonometrica
  keeps the model in memory and reuses it for the rest of the session.
- **Long files**: Whisper is designed around 30-second chunks internally, but this is
  handled automatically — you can transcribe files of arbitrary length. Expect roughly
  one to several times real-time on a modern laptop with the ``base`` model, and slower
  for larger models.
- **Cancellation**: the progress dialog has a **Cancel** button. Cancelling leaves the
  project untouched; no annotation is created.
- **Combining with Find silences**: a future version of Phonometrica may allow combining
  silence detection with transcription directly. For the time being, if you want tighter
  boundaries than Whisper produces, run **Find silences** on the same sound afterwards
  (on a separate layer) to obtain a more fine-grained segmentation.
