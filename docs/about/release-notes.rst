Release notes
-------------



0.9.3 (10/05/2026)
~~~~~~~~~~~~~~~~~~

**General**

- All plots in analysis views are now detachable into their own window.
- Save dialogs for analyses propose a default filename derived from the source data table.
- The user manual now opens in the system web browser instead of the embedded Qt help viewer; equation rendering in the documentation has been improved.

**Sound and annotation views**

- New audio recording capability (WAV PCM_16, unbounded duration), accessible from the main window.

**Statistical analysis**

- **Nested random effects** are now supported, in either of two equivalent forms: lme4-style slash sugar ``(1 | school/classroom)``, which expands at parse time to ``(1 | school) + (1 | classroom:school)``; and explicit bare-colon synthetic groups ``(1 | g1:g2)``, which build a grouping factor over the observed pairs of values. Slash sugar follows the lme4 convention with the inner factor on the left of the synthetic name; the bare-colon form preserves the order written. The right-click menu in the analysis view now offers an *Add as nested grouping factor in...* submenu listing existing grouping factors in the formula.
- **Predicted effects** for fixed-effects, mixed-effects, and Bayesian models. Mixed-effects predictions are available in both conditional and population-averaged forms.
- **Posterior predictive checks** for Bayesian models, with a residual-diagnostics tab using frequentist DHARMa-style p-values for cross-family consistency.
- **Refit model** action re-runs the fit with the current settings without reopening the analysis.
- Improved accuracy of frequentist Student-t (robust) regression: multi-start optimisation, exact-Hessian Laplace correction with Fisher-information fallback when the Hessian is non-positive-definite.
- Improved accuracy of Bayesian negative binomial models and of Bayesian robust regression in both fixed-only and mixed forms.
- Improved GAM accuracy. GAMs are now explicitly marked as **experimental** in the documentation pending further validation.
- Validation suites against ``glmmTMB`` (frequentist) and ``brms`` (Bayesian) reference fits for the negative binomial, Student-t, and Bayesian Gaussian families.
- EDA scatter plots: the *Regression line* option now stays available when a grouping variable is selected. With grouping, one OLS line is drawn per group, in the group's colour and clipped to that group's own x-range; it can be combined freely with the existing means and confidence ellipses.
- Clicking an observation in an EDA or residual-diagnostics plot opens the corresponding row in the source data table.

**Scripting**

- New ``get_script_path()`` builtin returning the absolute path of the currently-running script.
- Numeric literals can now be written in scientific notation (e.g. ``1.5e-3``).
- Syntactic sugar for named arguments: positional arguments are passed first, followed by ``key = value`` pairs that are collected into a single ``Table`` argument.
- Fixed compound assignment on ``Module`` fields (e.g. ``module.x += 1``).
- VM instruction format widened to 16-bit operands, raising the per-function limits on constants, locals, and arguments.

**Internal**

- Updated bundled r8brain resampler to the latest upstream version.



0.9.2 (04/05/2026)
~~~~~~~~~~~~~~~~~~

**General**

- Support for drag'n drop in a running instance
- Save dialogs propose the document's label as the default filename, falling back to ``untitled`` when no label has been set.

**Sound and annotation views**

- The mouse cursor is now tracked across linked sound plots while dragging an anchor on an annotation layer.

**Concordance views**

- The tab title now reliably shows the modified-state asterisk for all concordance edits — editing match text, editing the underlying annotation event, toggling the Wide/Long layout, and cell-level changes via undo/redo.
- Fixed a bug whereby filter rules could appear duplicated (sometimes many times over) after reopening a project, and where clearing a filter via the toolbar did not persist across reopens. Filter rules are now stored exclusively in the project file; existing concordance files with stale or duplicated rules are silently cleaned up the next time they are saved.
- Fixed row removal in concordances containing auxiliary columns.

**Statistical analysis**

- Improved accuracy and speed of binomial (logistic) regression, with further improvements to Bayesian logistic models that include random slopes.
- Improved accuracy of robust (Student-t) regression in both frequentist and Bayesian fits.
- Improved accuracy of Bayesian negative binomial models.
- Bayesian models now report a posterior summary for random-effect correlations.
- Random-slope terms can now contain interactions (e.g. ``(1 + x:y | g)``).
- Variables with parentheses in their names are now properly quoted in formulas; main effects and interactions are kept in shorthand form (e.g. ``x*y`` rather than ``x + y + x:y``) in formula summaries.
- Column-width formatting in model summary tables now adapts to the longest coefficient or hyperparameter name, so long names no longer push value columns out of alignment.

**Speech**

- Audio transcription no longer offers a translation option; transcription is performed in the source language only.

**Scripting**

- New ``try ... catch ... end`` block to catch errors to catch errors thrown by Phonometrica or by a user script with ``throw``.
- New Python-style triple-quoted string literals (``"""..."""`` and ``'''...'''``) for writing multi-line strings without manual concatenation. Line breaks and isolated occurrences of the delimiter are part of the content; the string is closed by three delimiter characters in a row. Escape sequences are processed as in single-line strings.
- Single-quoted and double-quoted strings now report a syntax error when a literal line break is encountered before the closing delimiter. The error message points to the triple-quoted form. This matches the behaviour of most mainstream scripting languages and is a one-time, intentional break in compatibility with previous versions.
- New ``get_column(datatable, name)`` to retrieve a column by name from a dataset or concordance, complementing the existing index-based form, as well as ``append(datatable, column, name)`` to add a column to a data table.
- Additional model-checking accessors exposed for analyses (fixed-effect inference, coefficient and hyperparameter names, hyperparameter posteriors, random-effects summaries).
- Documents (annotations, concordances, datasets, notes, scripts, sounds, spectra) are now explicitly non-clonable.


0.9.1 (24/04/2026)
~~~~~~~~~~~~~~~~~~~

This release features usability improvements and bug fixes.

**General**

- Settings dialogs for formants, pitch, intensity, and spectrograms are now non-modal: you can keep them open while interacting with the sound view and see parameter changes take effect immediately.
- macOS: fixed communication with Praat when launching files from the file manager, annotation views, and concordance views.
- macOS: toolbar separators in sound views are now drawn reliably, matching the appearance on other platforms.

**Sound and annotation views**

- Rewritten pitch-tracking algorithm (Praat backend) for improved accuracy and robustness across voice types, with additional parameters exposed in the pitch settings dialog.
- New keyboard shortcuts for acoustic measurements under the cursor or within a selection: ``F5`` / ``Shift+F5`` for FFT and FFT+LPC spectra; ``F6`` / ``Shift+F6`` for formants and mean formants; ``F7`` / ``Shift+F7`` for pitch and mean pitch; ``F8`` / ``Shift+F8`` for intensity and mean intensity.

**Concordance views**

- Base cells (match text and auxiliary columns) are now editable inline: double-click a cell, or use the column's context menu, to edit it in place. Edits participate in the undo/redo stack.
- New keyboard shortcuts in the concordance table: ``Space`` plays the selected match, ``Esc`` stops playback, and ``Ctrl+Return`` opens the match in its annotation.
- Protocol queries: split-field output (one column per protocol field in the concordance) is once again controlled by the coding protocol, matching the documented behaviour.
- Fixed a bug whereby imported concordance files were filed under the *Scripts* folder instead of *Concordances* in the project tree.
- Fixed serialization of ``NaN`` values in concordance and dataset files, which could prevent re-opening documents containing missing measurements.

**Bookmarks**

- Creating a bookmark from a concordance now opens a dedicated editor where you can set the bookmark's title and add free-form notes. The title is used as the label in the file manager.
- Bookmarks can now be created from annotation views: select an event on a focused layer and click the new bookmark button in the toolbar, or press ``Ctrl+B``. The selected event's text seeds the default title.
- Hovering over a bookmark in the file manager now displays a tooltip showing the file, the match (with KWIC context for concordance bookmarks, or the layer and time span for annotation-view bookmarks), and any notes you entered.

**Scripting**

- Fixed ``String::to_float()`` for a class of numeric strings that previously produced incorrect conversions or failed to parse.


0.9.0 (17/04/2026)
~~~~~~~~~~~~~

This is a major release. Phonometrica has gained substantial new functionality for acoustic analysis, data exploration, and statistical modeling.

**General**

- New user interface based on Qt 6.
- Start view with quick-access buttons (Open Project, Add Files, New Annotation, Analyze Data, Documentation) and a recent projects list.
- Preferences dialog with three tabs (General, Measurement, Appearance): configure Praat path, default estimation method, query context settings, display precision, and monospaced font.
- IPA panel for inserting phonetic symbols into annotations and scripts.
- Output panel for script execution output, separate from the interactive console.
- Batch property editing: select multiple files in the file manager and edit their properties at once through the information panel, with a union property table showing coverage counts.
- Context-sensitive help: each view has a Help button that links to the relevant documentation page.
- Research notes: create and edit rich-text notes (bold, italic, headings, lists) within your project, stored as HTML (``.phon-note``).
- Praat integration: configure the Praat path in preferences; "Open in Praat" from the file manager, annotation views, and concordance views; auto-detection of Praat on macOS and Windows.
- Bidirectional TextGrid ↔ native annotation conversion with property, description, and sound binding transfer.

**Sound visualization**

- Spectral slice (power spectrum): view the FFT and/or LPC spectral envelope at the cursor position; export to PNG, PDF, or SVG.
- Spectral moments: compute centre of gravity, spread, skewness, and kurtosis at the cursor position or within a selection.
- Plot export improvements: waveforms, spectrograms, pitch tracks, and other plots can be exported with Retina-quality rendering.

**Queries**

- New query relation types: *left alignment*, *right alignment*, *strict dominance*, *precedes*, and *follows*, in addition to the existing *alignment*, *dominance*, and *precedence*.
- **Formant queries**: extract formant measurements (F1–F5 and bandwidths) at the midpoint or as n-point averages; automatic formant selection using Weenink's method; on-the-fly ERB and Bark conversion.
- **Pitch queries**: extract pitch (F0) measurements using one of five algorithms (REAPER, Harvest, RAPT, SWIPE, Praat); semitone and ERB conversion.
- **Intensity queries**: extract intensity (dB) measurements at the midpoint or as n-point averages.
- **Spectral moments queries**: extract centre of gravity, spread, skewness, and kurtosis from the power spectrum; configurable window duration, window type, frequency range, and pre-emphasis.

**Concordance views**

- New concordance view with toolbar for playback, annotation viewing, bookmarking, editing, and CSV export.
- Set operations on concordances: union, intersection, and complement.
- Horizontal merge: add columns from another concordance or dataset.
- Column-level operations: rename, sort, recode (categorical), and transform (numeric).
- Filter bar: define filter rules on any column (text or numeric) with operators such as *equals*, *contains*, *matches regex*, *is one of*, comparison operators.
- Subset creation from filtered rows.
- Metric columns for outlier detection: z-score, modified z-score, Mahalanobis distance.
- Wide/long format toggle for n-point acoustic data.
- Scales menu for toggling ERB, Bark, and semitone columns.
- Vowel normalization: Lobanov, Nearey 1, Nearey 2, and Watt & Fabricius methods, accessible from concordance and dataset views.
- Target highlighting (bold red).
- Split view: option to open annotations beside the concordance.

**Dataset views**

- New dataset view for tabular data (CSV import).
- Same filtering, set operation, merge, recode, transform, and metric column features as concordance views.
- Column operations: rename, duplicate, move, delete.

**Statistical analysis**

- Analysis view: visual workspace for fitting, comparing, and diagnosing statistical models.
- Visual formula builder with right-click context menus on columns.
- Fixed-effects GLMs: Gaussian, binomial, Poisson, and negative binomial (NB2) regression.
- Beta regression (logit link) for proportion response variables.
- Student *t* regression (identity link) for robust estimation with heavy-tailed residuals; jointly estimates scale σ and degrees-of-freedom ν parameters.
- Mixed-effects models (LMM/GLMM): support for random intercepts and correlated random slopes; crossed random effects.
- Generalized additive models (GAM): support for penalized cubic regression splines, per-smooth GCV, by-variable smooths, EDF and F-statistics.
- Random intercepts and random slopes via ``s(group, bs=re)`` and ``s(group, by=x, bs=re)`` in GAMs.
- Model comparison: AIC/BIC table, pairwise likelihood ratio tests with automatic nestedness checking.
- Approximate Bayesian inference (INLA-style): data-dependent weakly informative priors, posterior summaries (mean, credible intervals, probability of direction), customizable priors.
- Bayesian model comparison: WAIC, PSIS-LOO-IC with Pareto *k* diagnostics, log Bayes factors.
- Estimated marginal means (EMMs) and pairwise contrasts with Holm/Bonferroni adjustment; emtrends mode for testing slopes across groups. Bayesian EMMs report credible intervals and probability of direction.
- Reference level setting for categorical variables.
- DHARMa-style simulation-based scaled residuals for all model families.
- Pseudo R² (Nakagawa & Schielzeth) for mixed-effects models.
- Diagnostic plots: residuals vs. fitted, Normal Q-Q, scaled residual plots with Kolmogorov–Smirnov, dispersion, and outlier tests.
- Exploratory data analysis (EDA): scatter plots, histograms with kernel density, grouped strip charts; confidence ellipses; formant chart mode (reversed axes); pooled means; regression lines; detachable plot window.
- Summary export to plain text, clipboard, and LaTeX.
- Analysis documents saved in XML format with full-precision coefficients.

**Column transformations**

- Transform dialog with real-time preview: apply mathematical formulas to create new numeric columns.
- Built-in phonetic scale functions: ``bark()``, ``erb()``, ``mel()``, ``st()``.
- Standard math functions: ``log()``, ``log10()``, ``log2()``, ``sqrt()``, ``abs()``, ``exp()``, ``pow()``, ``round()``, ``floor()``, ``ceil()``.

**Plugins**

- Plugin management from the GUI: install and uninstall plugins via the Plugins menu.
- Plugin actions appear as submenus in the Plugins menu.


0.8.0 (2020)
~~~~~~~~~~~~~

This is an internal development version that was not released publicly.

- new scripting engine
- faster hash table based on Robin Hood hashing
- save annotation in annotation view with ``Ctrl+S``


0.7.6 (18/05/2020)
~~~~~~~~~~~~~~~~~~~

- properties are now displayed in a table in the information panel
- better error reporting
- bug fixes

0.7.5 (09/11/2019)
~~~~~~~~~~~~~~~~~~~

- n-point average in formant analysis
- ability to remove rows in query views
- fix navigation with arrows in annotation views when some layers are hidden
- return empty label instead of crashing in formant analysis when intervals are misaligned
- ``get_selected_annotations()`` and ``get_selected_sounds()`` functions

0.7.4 (14/11/2019)
~~~~~~~~~~~~~~~~~~~

- robust sandwich variance estimator for Poisson regression
- fix project finalization when views are modified
- ``covrc()`` was renamed to ``cov()``
- ``Run script...`` is now in the ``Tools`` menu

0.7.3 (10/11/2019)
~~~~~~~~~~~~~~~~~~~

- ``poisson()`` function for Poisson regression
- negative numbers are now parsed correctly in ``String`` to ``Number`` conversion
- fix ``split()`` method in ``String`` type

0.7.2 (09/11/2019)
~~~~~~~~~~~~~~~~~~~

- ``lm()`` function for linear regression
- ``logit()`` function for logistic regression
- ``read_matrix()`` and ``write_matrix()`` functions to read/write a numeric array to/from a text file
- DFT now uses double precision
- ``slice()`` method to obtain a slice of an ``Array``

0.7.1 (07/11/2019)
~~~~~~~~~~~~~~~~~~~

- license is now GPL 3
- Gaussian window for spectrograms and LPC analysis
- improved LPC analysis
- experimental automatic formant selection using Weenink's method in formant queries

0.7.0 (05/11/2019)
~~~~~~~~~~~~~~~~~~~

- formant queries (``Analysis > Analyze formants...``)
- fix formant bandwidth estimation
- ``maximum bandwidth`` parameter in formant analysis
- fix path compression in project files

0.6.3 (02/11/2019)
~~~~~~~~~~~~~~~~~~~

- fix regression in ``report_formants()`` due to the new array indexing syntax
- documentation for the ``List`` type
- anchors are now only edited on the visible layers
- new statistical functions: ``covrc()`` (covariance) and ``corr()`` (Pearson's correlation coefficient)

0.6.2 (31/10/2019)
~~~~~~~~~~~~~~~~~~~

This release brings improvements to the scripting engine, statistical functions, and fixes a regression.

- new concatenation operator ``&``
- improved ``Array`` type
- the multiplication and division operators can now operate on two arrays, or on an array and a scalar value
- mathematical functions for numbers and arrays
- statistical functions: sum, mean, variance, standard deviation and hypothesis testing (chi-squared test, F-test, one-sample t-test, two sample independent t-test with or without equal variance)
- ``to_string()`` method for lists, arrays and objects
- comment/uncomment selection in script views
- bug fixes

0.6.1 (27/10/2019)
~~~~~~~~~~~~~~~~~~~

- improved LPC analysis
- scripting functions to convert between Hertz and bark, ERB units, mel and semitones (see :ref:`sound-type`). These functions accept a ``Number`` or an ``Array``.
- ``get_annotations()`` and ``get_sounds()`` now return a sorted list
- automatic indentation in script views

0.6.0 (25/10/2019)
~~~~~~~~~~~~~~~~~~~

This release brings more sound visualization and analysis options, as well as a number of enhancements and bug fixes.

- spectrogram in sound and annotation views
- LPC-based formant tracking
- waveform scaling using global, local or fixed magnitude
- intensity settings
- click on middle button (wheel) to zoom on the active selection
- user dialogs
- uninstall plugin (``Tools > uninstall plugin``)
- new resampler
- resample and/or convert sound to WAV, AIFF or FLAC
- Sound objects are now accessible from the scripting engine
- measure pitch, intensity and formants under the cursor
- show/hide layers in annotation views
- ``Export annotation(s) to plain text...`` (in ``File > Export``)
- updated documentation


0.5.2 (04/10/2019)
~~~~~~~~~~~~~~~~~~~

- new import dialog for metadata
- bug fixes


0.5.1 (29/09/2019)
~~~~~~~~~~~~~~~~~~~

-  new regular expression engine based on PCRE2
-  faster loading time for TextGrid annotations (~ 23%) thanks to the new regex engine


0.5.0 (27/09/2019)
~~~~~~~~~~~~~~~~~~~

-  new website at http://www.phonometrica-ling.org
-  create and edit annotations
-  scripting API to access the content of annotations
-  export metadata to CSV
-  bookmarks
-  fix initialization on Windows when the user's directory contains non-ASCII characters


0.4.1 (21/09/2019)
~~~~~~~~~~~~~~~~~~~

-  fix communication with Praat on Windows when the user's directory contains non-ASCII characters
-  better monospace font on Windows
-  improved preferences dialog


0.4.0 (20/09/2019)
~~~~~~~~~~~~~~~~~~~

This is the first functional version of Phonometrica. It brings the following features:

-  project management
-  native format for annotations based on annotation graphs
-  conversion between Praat TextGrids and Phonometrica annotations
-  typed properties (Boolean, numeric or textual)
-  query editor for single layer queries
-  query protocols
-  plugins


0.3.0 (30/08/2019)
~~~~~~~~~~~~~~~~~~~

-  initial implementation of annotation views


0.2.0 (17/03/2019)
~~~~~~~~~~~~~~~~~~~

-  project management, with support for metadata
-  script editor and scripting console
-  basic interaction with Praat
-  initial documentation
-  installers for Windows, macOS and Linux (Debian/Ubuntu)


0.1.0 (26/02/2019)
~~~~~~~~~~~~~~~~~~~

-  Scripting engine based on MuJS 1.0.5.


Phonometrica is partly based on Dolmen, developed and maintained by Julien Eychenne from 2010 to 2018. A python
proof-of-concept of Dolmen was sketched out in April/May 2010. Dolmen was a complete redesign of the PFC
platform (2006/2008), a concordancer implemented in Python and specifically written for the PFC project
(www.projet-pfc.net).
