.. _concordance-view:

Concordances
============

When you run a query (see :ref:`queries`), the results are displayed in a **concordance view**. A concordance is
a structured table of matches extracted from your corpus, along with metadata and (for acoustic queries) measurement
data. Concordance views provide tools for browsing, playing, filtering, editing, and manipulating query results.

Concordances are saved as ``.phon-conc`` files in XML format. They are listed in the project tree under a
**Concordances** folder and can be reopened at any time.


Structure of the concordance view
---------------------------------

Toolbar
~~~~~~~

The toolbar at the top of the concordance view provides the following actions:

- |save| **Save**: save the concordance to disk.
- |csv| **Export to CSV**: export the concordance as a tab-separated text file (CSV) for use in spreadsheet software or R.
- |play| **Play** / |stop| **Stop**: play (or stop) the sound corresponding to the selected match. If the annotation is bound to a sound file, double-clicking on a row also plays the match.
- |view| **View in annotation**: open the annotation at the location of the selected match. Depending on the *Display settings* (see below), the annotation opens in a new tab or in a split view beside the concordance.
- |bookmark| **Bookmark**: bookmark the selected match for later reference. A dialog opens where you can set the bookmark's title (pre-filled with the match value) and add free-form notes. The title appears as the bookmark's label in the file manager; hovering over the bookmark in the tree shows the match, surrounding context, and any notes. Bookmarks are accessible from the project tree under the **Bookmarks** folder.
- |edit| **Edit**: edit the text of the event where the match was found in the original annotation.
- |delete| **Delete rows**: remove the selected row(s) from the concordance.
- **Open in Praat**: if Praat is configured (see :ref:`praat-integration`), open the sound file and
  annotation in Praat at the location of the selected match. This is useful for inspecting a
  measurement or verifying a match using Praat's acoustic analysis tools.

Set operations
~~~~~~~~~~~~~~

Concordances support set operations, which allow you to combine results from different queries:

- |union| **Union** (A ∪ B): combine two concordances, keeping all matches from both.
- |intersect| **Intersection** (A ∩ B): keep only the matches that appear in both concordances.
- |complement| **Complement** (A ∖ B): keep only the matches from the first concordance that do *not* appear in the second.

Each of these operations opens a dialog where you select the other concordance. The result is a new concordance.

Merge
~~~~~

The |merge| **Merge** button performs a *horizontal merge*: it adds columns from another concordance or dataset to the current
concordance, matching rows by position. This is useful, for instance, to combine formant measurements with pitch data for
the same set of tokens.

Filtering
~~~~~~~~~

The |filter| **Filter** button shows or hides the **filter bar** below the toolbar. The filter bar lets you define
one or more filter rules to restrict which rows are displayed. Each rule consists of a column, an operator, and a value.
The available operators depend on the column type:

- For text columns: *equals*, *not equals*, *contains*, *does not contain*, *matches regex*, and *is one of* (select from the unique values in the column).
- For numeric columns: *=*, *≠*, *<*, *≤*, *>*, *≥*.

Multiple filter rules are combined with AND logic: a row must satisfy all rules to be displayed.
When a filter is active, the count label at the top of the table shows both the number of visible rows and the total
number of rows. The |clearfilter| **Clear filters** button removes all filter rules.

The |subset| **Subset** button creates a new concordance containing only the currently visible (filtered) rows. This is
useful for extracting a subset of your data based on specific criteria.

Metric columns
~~~~~~~~~~~~~~

The |sigma| **Metric column** button opens a dialog that lets you compute a distance metric on a numeric column,
optionally grouped by a categorical variable. The available metrics are:

- **z-score**: (x − mean) / standard deviation.
- **Modified z-score**: based on the median and median absolute deviation (MAD), more robust to outliers.
- **Absolute z-score** and **absolute modified z-score**: unsigned versions of the above.
- **Mahalanobis distance**: multivariate distance from the group centroid.

The computed metric is added as a new column. You can optionally create an automatic filter rule (e.g. keep only
rows where the absolute z-score is less than 3) to identify and exclude outliers.

.. note::

    Because the squared Mahalanobis distance (D²) follows a chi-squared distribution
    with *k* degrees of freedom — where *k* is the number of columns included in the
    computation — a principled outlier threshold can be derived from that distribution.
    For a two-dimensional vowel space (F1 × F2, *k* = 2), the 95 % and 99 % critical
    values are D ≈ 2.45 and D ≈ 3.03, respectively. For a three-dimensional space
    (F1 × F2 × F3, *k* = 3), the corresponding thresholds are D ≈ 2.80 (95 %) and
    D ≈ 3.37 (99 %). Tokens whose Mahalanobis distance exceeds the chosen threshold
    lie outside the expected ellipsoid for that confidence level and are typically
    flagged as potential measurement errors or atypical realizations. A 95 % threshold offers a
    reasonable balance between sensitivity and specificity for most phonetic datasets;
    the 99 % threshold is more conservative and should be preferred when the category
    genuinely contains peripheral tokens that should not be discarded.

Vowel normalization
~~~~~~~~~~~~~~~~~~~

The |normvowel| **Normalize vowels** button opens a dialog for applying speaker-intrinsic vowel normalization
to formant columns. Four methods are available: Lobanov, Nearey 1 (per-formant), Nearey 2 (uniform), and
Watt & Fabricius. See :ref:`vowel-normalization` for a detailed description of each method and usage
instructions.

Analyze
~~~~~~~

The |stats| **Analyze** button opens an :ref:`analysis view <analysis-view>` for the concordance, where you can fit
statistical models to your data.


Display settings
~~~~~~~~~~~~~~~~

The |display| **Display settings** menu controls which column groups are visible:

- **Match info**: file name, layer index, start time, and end time.
- **Context**: left and right context (for text concordances using the KWIC model).
- **Metadata**: file description and properties.
- **Long format** (acoustic queries with n-point data): toggle between wide format (one row per match, with
  columns F1_1, F1_2, ... for each measurement point) and long format (one row per measurement point, which is
  often more convenient for statistical analysis). This option is only available for queries that used n-point
  averaging (not midpoint).
- **Open matches in split view**: when checked, clicking *View in annotation* opens the annotation beside the
  concordance; otherwise it opens in a new tab.
- **Highlight targets**: show the target column(s) in bold red to make them easier to identify.

Scales menu
~~~~~~~~~~~

For acoustic concordances (formant, pitch, or intensity queries), a |scales| **Scales** menu appears in the toolbar.
It lets you toggle the display of values in alternative perceptual scales:

- **Formant concordances**: toggle ERB (equivalent rectangular bandwidth) and/or Bark columns, computed on the fly from the raw Hertz values.
- **Pitch concordances**: toggle semitone and/or ERB-rate columns.
- **Merged columns**: if you have added acoustic columns from another concordance via Merge, separate scale toggles appear for the merged data.


Table features
--------------

The concordance table supports the following interactions:

- **Double-click** on a row to play the match (if a sound file is bound).
- **Double-click** on a base cell (match text or auxiliary column) to edit it in place. Edits are added to the undo/redo stack and can be reverted with ``Ctrl+Z``.
- **Right-click** on a row to open a context menu with *Play match*, *View in annotation*, *Open in Praat* (if configured), *Bookmark*, *Edit event text*, and *Delete row(s)*.
- **Right-click on a column header** to access column-level operations: *Sort ascending*, *Sort descending*, *Rename column*, *Recode* (for text columns), and *Transform* (for numeric columns).

Sorting
~~~~~~~

You can sort the concordance by any column by right-clicking on the column header and choosing *Sort ascending*
or *Sort descending*.

Renaming columns
~~~~~~~~~~~~~~~~

Double-click on a column header (or right-click and choose *Rename*) to give it a custom alias. This is useful
for giving short, descriptive names to acoustic measurement columns (e.g. renaming ``F1`` to ``F1_Hz``).

Recoding columns
~~~~~~~~~~~~~~~~

Right-click on a text column header and choose **Recode...** to open the recode dialog. This creates a new column
where values have been remapped according to a mapping table. For example, you could recode detailed phonetic
transcriptions into broader phonological categories.

Transforming columns
~~~~~~~~~~~~~~~~~~~~

Right-click on a numeric column header and choose **Transform...** to open the transform dialog. This creates a
new column by applying a mathematical formula to each value. For instance, you can convert Hertz values to Bark
using ``bark(x)`` or log-transform durations using ``log(x)``. See :ref:`transform` for the full list of available
functions.

.. _applying-coding-protocols:

Applying coding protocols
~~~~~~~~~~~~~~~~~~~~~~~~~

A **coding protocol** is a description of a coding scheme (such as the numeric schwa coding used in the PFC project)
which tells Phonometrica how to split each coded value into its constituent fields and, optionally, how to replace
raw codes with human-readable labels. Protocols are stored as JSON files. See :ref:`page-plugins` for the full
specification of the protocol format.

There are two ways to attach a protocol to a concordance column. Both are available on the right-click menu of any
text column header:

- **Apply coding protocol...** opens a file picker; pick an existing ``.json`` protocol and Phonometrica creates one
  new column per field in the protocol, to the right of the existing columns. Raw codes are replaced with the labels
  defined in the protocol; values that do not match any label are kept as-is so that no information is lost.

- **Build coding protocol...** opens the protocol builder (see below). The clicked column supplies sample input for
  the live preview, and the protocol can be applied directly from the dialog when you're happy with it. This is the
  recommended entry point when you don't already have a ``.json`` protocol for your coding scheme.

If some rows do not match the protocol, or contain field values that aren't defined in the protocol, Phonometrica
will display a warning summarising how many rows are affected. Non-matching rows are left blank across all new
columns; rows with undefined field values keep the raw code in the affected columns.


Building a coding protocol
~~~~~~~~~~~~~~~~~~~~~~~~~~

The protocol builder is an interactive dialog for creating or editing a coding protocol without writing JSON by hand.
It can be launched in two ways:

- From **Plugins > Build coding protocol...** (standalone mode). The sample area starts empty; type or paste coded
  values directly into it to test the protocol as you build it.

- From a concordance column's right-click menu, via **Build coding protocol...** The first ten values of the column
  are copied into the sample area automatically, and the dialog's **Apply** button becomes available to apply the
  protocol directly to that column.

The dialog is divided into three panes. The **field list** on the left lets you add, remove, and reorder fields.
The **field editor** in the middle configures the selected field: its name, the ``match_all`` regular expression
(which must match every possible value of that field), and a table of ``match`` / ``label`` pairs that translate raw
codes into human-readable labels. The **preview** on the right shows the sample text split into columns according
to the current protocol. Each row is marked ✓ (fully translated), ⚠ (matched the protocol but contains one or more
values not defined in the label table), or ✗ (did not match the protocol at all).

Above the preview you can set the protocol's name, the **separator** that appears between fields in a coded value
(leave empty when fields are concatenated directly, as in ``1412``), and whether the protocol is **case-sensitive**.

At the bottom of the dialog, **Load...** reads an existing protocol into the builder; the menu lists every protocol
installed under a plugin, plus a **Browse file system...** entry for arbitrary paths. **Save...** writes the current
protocol to a ``.json`` file via a file dialog. **Apply...** is enabled only when the dialog was launched from a
concordance column and applies the current protocol to that column.


Keyboard shortcuts
------------------

Playback, stop, and row deletion in concordance views are available via the toolbar, the right-click
context menu, and double-click (which plays the match when a sound file is bound). No dedicated key
bindings are currently registered for these actions.


.. |save| image:: ../icons/save.svg
    :height: 16px
    :width: 16px

.. |csv| image:: ../icons/file-spreadsheet.svg
    :height: 16px
    :width: 16px

.. |play| image:: ../icons/play.svg
    :height: 16px
    :width: 16px

.. |stop| image:: ../icons/square.svg
    :height: 16px
    :width: 16px

.. |view| image:: ../icons/file-search-corner.svg
    :height: 16px
    :width: 16px

.. |bookmark| image:: ../icons/book-marked.svg
    :height: 16px
    :width: 16px

.. |edit| image:: ../icons/pencil-line.svg
    :height: 16px
    :width: 16px

.. |delete| image:: ../icons/grid-2x2-x.svg
    :height: 16px
    :width: 16px

.. |union| image:: ../icons/set-union.svg
    :height: 16px
    :width: 16px

.. |intersect| image:: ../icons/set-intersection.svg
    :height: 16px
    :width: 16px

.. |complement| image:: ../icons/set-complement.svg
    :height: 16px
    :width: 16px

.. |merge| image:: ../icons/merge-tables.svg
    :height: 16px
    :width: 16px

.. |filter| image:: ../icons/filter.svg
    :height: 16px
    :width: 16px

.. |clearfilter| image:: ../icons/filter-x.svg
    :height: 16px
    :width: 16px

.. |subset| image:: ../icons/scissors.svg
    :height: 16px
    :width: 16px

.. |sigma| image:: ../icons/sigma.svg
    :height: 16px
    :width: 16px

.. |stats| image:: ../icons/statistics.svg
    :height: 16px
    :width: 16px

.. |display| image:: ../icons/display.svg
    :height: 16px
    :width: 16px

.. |scales| image:: ../icons/ruler.svg
    :height: 16px
    :width: 16px

.. |normvowel| image:: ../icons/vowel-space.svg
    :height: 16px
    :width: 16px
