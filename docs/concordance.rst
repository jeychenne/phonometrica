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
- |bookmark| **Bookmark**: bookmark the selected match for later reference. Bookmarks are accessible from the bookmark panel in the bottom left corner of the main window.
- |edit| **Edit**: edit the text of the event where the match was found in the original annotation.
- |delete| **Delete rows**: remove the selected row(s) from the concordance.

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
- **Right-click** on a row to open a context menu with *Play match*, *View in annotation*, *Bookmark*, *Edit event text*, and *Delete row(s)*.
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


Keyboard shortcuts
------------------

- **Space**: play the selected match.
- **Escape**: stop playback.
- **Delete**: delete the selected rows.


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
