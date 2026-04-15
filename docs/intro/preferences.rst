.. _preferences:

Preferences
===========

The **Preferences** dialog lets you customize Phonometrica's behavior. To open it, go to
``Edit > Preferences...`` (on Windows and Linux) or ``Phonometrica > Preferences...`` (on macOS).

The dialog is organized into three tabs: General, Measurement, and Appearance. A **Reset to defaults**
button at the bottom restores all settings to their factory values.


General
-------

The General tab contains the following options:

**Project and session**

- **Load most recent project on startup**: when checked, Phonometrica automatically opens the last
  project you were working on when it starts. This is convenient if you work on the same project
  across multiple sessions. Disabled by default.
- **Restore open views on startup**: when checked (and when auto-load is also enabled), Phonometrica
  reopens the tabs (annotations, concordances, analyses, etc.) that were open when you last closed
  the application. Disabled by default.
- **Automatically save project on exit**: when checked, Phonometrica saves the project and all
  modified files automatically when you quit, without asking for confirmation. Disabled by default.
- **Activate syntax hints in script views by default**: when checked, the script editor shows
  auto-completion hints and function tooltips as you type. Enabled by default.
- **Discard empty queries**: when checked, queries that return no matches are silently discarded
  instead of creating an empty concordance. Enabled by default.

**Praat integration**

- **Path to Praat**: the file system path to the Praat executable on your computer. Phonometrica
  needs this to use the "Open in Praat" feature (see :ref:`praat-integration`). Click **Browse...**
  to locate the Praat executable on your system. On macOS, Phonometrica automatically detects Praat
  if it is installed in the ``/Applications`` folder; on Windows, Phonometrica checks the default
  installation directory. If Praat is installed elsewhere, you need to set the path manually.

**Statistics**

- **Default estimation method**: choose between **Frequentist** (maximum likelihood, the default)
  and **Bayesian** (approximate Bayesian inference with weakly informative priors). This controls
  which estimation method is pre-selected in the analysis view's **Estimation** dropdown when you
  open a new analysis. You can always switch between the two methods in the analysis view itself.


Measurement
-----------

The Measurement tab controls defaults for queries and display precision.

**Default query context**

These options control what context information is extracted around each match when you run a text
query (see :ref:`text-queries`). The setting chosen here becomes the default for new queries; you
can override it in the query editor for individual queries.

- **No context**: extract only the matched event, with no surrounding text. Useful for acoustic
  queries where you only need the measurement columns.
- **Surrounding labels**: extract the text of the events immediately to the left and right of the
  matched event, rather than a fixed number of characters. Useful when you want to see the
  neighboring intervals or instants.
- **Number of characters** (KWIC): extract a fixed number of characters to the left and right
  of the match (default: 40). This is the standard Key Word In Context model and is the default.

**Display**

- **Decimal places for Hz values**: the number of decimal digits shown for frequency values
  (formants, pitch, bandwidths) in concordances and measurement output. The default is 0, which
  rounds to the nearest Hz. ERB and Bark values automatically use 2 additional decimal places beyond
  this setting.


Appearance
----------

The Appearance tab lets you configure the font used in scripts, the console, and other
fixed-width text areas.

- **Monospaced (fixed-width) font**: select a monospaced font from those available on your system
  and set its size (default: platform-dependent, typically 12 pt). The font change takes effect
  when script views are next opened or reloaded.
