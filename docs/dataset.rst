.. _dataset-view:

Datasets
========

A **dataset** is a tabular data file that can be opened, explored, and analyzed within Phonometrica. Datasets
are typically CSV files that have been imported into the project, but they can also be created from concordance
results. The dataset view provides tools for inspecting, filtering, transforming, and preparing your data
for statistical analysis.

Datasets are stored as plain ``.csv`` (or ``.tsv``) files on disk. They are listed in the project tree under a
**Datasets** folder, and any metadata associated with a dataset (column aliases, filter rules, properties, etc.)
is kept in the project file alongside the dataset.


Opening a dataset
-----------------

To import a CSV file as a dataset, use **File > Add files to project...** and select your CSV file. When
you add a CSV file, Phonometrica will ask you to specify the column separator (tab, comma, or semicolon) and
will automatically detect column types (numeric vs. text).

You can also create a dataset from a concordance view by exporting it to CSV and reimporting, or by using
the scripting engine.

To open a dataset, double-click on it in the file manager or right-click and choose ``View file``.


Structure of the dataset view
-----------------------------

Toolbar
~~~~~~~

The toolbar at the top of the dataset view provides the following actions:

- |save| **Save**: save the dataset to disk.
- |csv| **Export to CSV**: export the dataset as a tab-separated text file.
- |delrow| **Delete rows**: remove the selected row(s) from the dataset.
- |delcol| **Delete columns**: remove the selected column(s) from the dataset.

Filtering
~~~~~~~~~

The dataset view provides the same filtering capabilities as the concordance view:

- |filter| **Filter**: show or hide the filter bar.
- |clearfilter| **Clear filters**: remove all filter rules.
- |subset| **Subset**: create a new dataset from the visible (filtered) rows.
- |sigma| **Metric column**: compute a distance metric (z-score, modified z-score, etc.) on a numeric column for outlier detection.
- |normvowel| **Normalize vowels**: apply vowel normalization to formant columns (see :ref:`vowel-normalization`).

For details on filter rules, see :ref:`concordance-view`.

Analyze
~~~~~~~

The |stats| **Analyze** button opens an :ref:`analysis view <analysis-view>` for the dataset, where you can
fit statistical models to your data.

Set operations
~~~~~~~~~~~~~~

Like concordances, datasets support set operations on rows:

- |union| **Union** (A ∪ B): combine two datasets.
- |intersect| **Intersection** (A ∩ B): keep only matching rows.
- |complement| **Complement** (A ∖ B): keep rows that do not appear in the other dataset.
- |merge| **Merge**: horizontal merge — add columns from another dataset, matching by row position.


Column operations
-----------------

Right-click on a column header to access column-level operations:

- **Sort ascending** / **Sort descending**: sort the dataset by this column.
- **Rename**: give the column a new name.
- **Recode...** (text columns): create a new column with remapped category labels.
- **Transform...** (numeric columns): create a new column by applying a mathematical formula (see :ref:`transform`).
- **Duplicate**: create a copy of the column.
- **Move left** / **Move right**: change the column's position.
- **Delete**: remove the column.


Tips
----

- Use **Filter** + **Subset** to extract subsets of your data based on specific criteria before running analyses.
- Use **Recode** to simplify categorical variables (e.g. merging several phonetic variants into a single phonological category).
- Use **Transform** to convert acoustic values to perceptual scales (e.g. ``bark(x)``, ``erb(x)``, ``st(x)``).
- Use **Metric column** followed by filtering to identify and exclude outliers before modeling.
- Click **Analyze** to open the analysis view directly from your prepared dataset.


.. |save| image:: ../icons/save.svg
    :height: 16px
    :width: 16px

.. |csv| image:: ../icons/file-spreadsheet.svg
    :height: 16px
    :width: 16px

.. |delrow| image:: ../icons/grid-2x2-x.svg
    :height: 16px
    :width: 16px

.. |delcol| image:: ../icons/circle-minus.svg
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

.. |union| image:: ../icons/set-union.svg
    :height: 16px
    :width: 16px

.. |intersect| image:: ../icons/set-intersection.svg
    :height: 16px
    :width: 16px

.. |complement| image:: ../icons/set-complement.svg
    :height: 16px
    :width: 16px

.. |merge| image:: ../icons/layers.svg
    :height: 16px
    :width: 16px

.. |normvowel| image:: ../icons/vowel-space.svg
    :height: 16px
    :width: 16px
