.. _analysis-view:

Statistical analysis
====================

Phonometrica provides a dedicated environment for fitting and comparing statistical models.
To open an analysis, create one from a concordance or dataset using the **Analyze** command,
or open an existing ``.phon-analysis`` file. The analysis view is divided into three areas:
a **top bar** for entering formulas, a **left panel** with column and model lists, and a
**right panel** with tabs for results, diagnostics, and exploratory plots.


Top bar
-------

The top bar contains the following elements:

- **Formula**: an editable text field where you enter your model formula using R-style syntax
  (e.g. ``F1 ~ vowel + context``). Press Enter or click **Fit** to fit the model.
- **Family**: a drop-down menu to select the distributional family for the response variable:
  Gaussian (for continuous data), Binomial (for binary outcomes), Poisson (for count data),
  or Negative binomial (for overdispersed counts).
- **Fit**: fits the model described by the current formula and family. The fitted model is
  added to the model list.
- **Help** (|help|): opens this documentation page.


Formula syntax
--------------

Phonometrica uses R-style formula syntax. The left-hand side is the response variable, and
the right-hand side lists the predictors separated by ``+``.

Fixed effects
~~~~~~~~~~~~~

- ``y ~ a + b``: additive effects of *a* and *b*
- ``y ~ a * b``: equivalent to ``y ~ a + b + a:b`` (main effects plus their interaction)
- ``y ~ a:b``: interaction only, without the main effects
- ``y ~ 0 + a`` or ``y ~ a - 1``: remove the intercept

Smooth terms (GAM)
~~~~~~~~~~~~~~~~~~

- ``y ~ s(x)``: penalized regression spline of the numeric variable *x* (default: 10 knots, cubic regression spline basis)
- ``y ~ s(x, k=15)``: spline with 15 knots
- ``y ~ s(x, by=group)``: a separate smooth for each level of the factor *group*

Random effects
~~~~~~~~~~~~~~

- ``y ~ a + (1 | speaker)``: random intercept for *speaker*
- ``y ~ a + (1 + a | speaker)``: random intercept and correlated random slope for *a*
- ``y ~ a + (0 + a | speaker)``: random slope only (no random intercept)

You can combine any number of fixed, smooth, and random terms in a single formula.


Columns panel
-------------

The left panel lists all the columns available in the source data.

- **Double-click** a column to add it as a fixed-effects predictor.
- **Right-click** a column to open a context menu with the following options:

  - *Set as response*: place this column on the left-hand side of the formula.
  - *Remove from formula*: remove this variable from the formula.
  - *Add as predictor*: add as a fixed-effects term.
  - *Add with main effects and interaction*: create an ``a * b`` term with another variable already in the formula.
  - *Add interaction only with...*: create an ``a:b`` interaction term without adding main effects.
  - *Add as smooth*: (numeric columns only) add a smooth term ``s(x)`` with preset or custom *k*.
    A submenu offers *with by variable* to create ``s(x, by=factor)`` terms.
  - *Add as grouping factor*: add a random intercept ``(1 | group)``.
  - *Add correlated slope in...*: add this variable as a random slope inside an existing random term
    (e.g. ``(1 + a | speaker)``).
  - *Add independent slope in...*: add this variable as a random slope in a new random term
    for the same group (e.g. ``(0 + a | speaker)``).
  - *Set reference level...*: (categorical columns only) choose which level is used as the
    reference for treatment contrasts. By default, the alphabetically first level is used.

A small check mark appears next to columns that are currently used in the formula.


Models panel
------------

Below the columns panel, the **Models** list shows all fitted models. Clicking a model displays
its summary, diagnostics, and EDA plots. You can select multiple models using Ctrl+click or
Shift+click.

- **Delete**: remove the selected model from the analysis.
- **Compare**: run pairwise likelihood ratio tests on the selected models (or all models if
  fewer than two are selected). See `Model comparison`_ below.


Summary tab
-----------

The Summary tab displays detailed results for the selected model:

- **Coefficient table**: estimated coefficients, standard errors, test statistics (*t*-values
  for Gaussian models, *z*-values for GLMs), and *p*-values with significance codes.
- **Goodness of fit**: AIC, BIC, and log-likelihood.
- **Gaussian models**: residual standard error, R², and adjusted R².
- **Smooth terms** (GAM): a separate table showing effective degrees of freedom (EDF),
  reference degrees of freedom, *F*-statistic, and approximate *p*-value for each smooth.
- **Random effects** (mixed models): variance and standard deviation for each random term,
  plus the covariance structure (correlations between random slopes).
- **Pseudo R²** (mixed models): Nakagawa & Schielzeth (2013) marginal and conditional R².
  The marginal R² measures the proportion of variance explained by fixed effects alone;
  the conditional R² measures the proportion explained by both fixed and random effects.
  Computed for all families (Gaussian, Binomial, Poisson, Negative binomial) using the
  appropriate distribution-specific variance.

The toolbar above the summary provides:

- **Copy to clipboard**: copy the summary text.
- **Save as text**: save the summary to a plain text file.
- **Save as LaTeX table**: export the coefficient table in LaTeX format.


Model comparison
----------------

Click **Compare** with two or more models selected (or with fewer than two selected to compare
all models). The comparison output has two parts:

**Information criteria table**

A table showing each model's formula, number of parameters, AIC, BIC, and log-likelihood,
sorted from simplest to most complex. Lower AIC and BIC indicate a better balance between
fit and parsimony. These criteria can be used for any pair of models, whether nested or not.

**Pairwise likelihood ratio tests**

For every pair of models, a likelihood ratio test (LRT) is computed:

- **Df**: difference in number of parameters between the two models.
- **Chisq**: the chi-squared test statistic (−2 × (logLik\ :sub:`simple` − logLik\ :sub:`complex`)).
- **Pr(>Chisq)**: the *p*-value from the chi-squared distribution.

The LRT is only valid when the simpler model is **nested** within the more complex one
(i.e. the simpler model's terms are a subset of the more complex model's terms). Phonometrica
checks this automatically and issues a warning if models do not appear to be nested, suggesting
AIC or BIC instead.

When two models have the same number of parameters but different terms, the LRT cannot be
computed (Df = 0). The table shows ``--`` for these pairs.

.. note::

   The nestedness check is heuristic — it compares formulas at the symbolic level. In rare
   edge cases (e.g. aliased interactions, different contrast coding), it may produce false
   warnings. If you know your models are nested, you can safely ignore the warning.


Diagnostics tab
---------------

The Diagnostics tab helps you check whether the model's assumptions are met. A drop-down
menu lets you choose between four plot types:

- **Residuals vs Fitted**: plots raw residuals against fitted values. Look for random scatter
  around zero; patterns (e.g. a funnel shape) suggest violated assumptions.
- **Normal Q-Q**: compares residual quantiles to theoretical normal quantiles. Points close
  to the diagonal indicate normally distributed residuals.
- **Scaled Residuals vs Fitted**: uses simulation-based (DHARMa-style) scaled residuals, which
  should be uniformly distributed between 0 and 1 regardless of the model family.
- **Scaled Residuals Q-Q**: a Q-Q plot of the scaled residuals against the uniform distribution.

When a scaled residual plot is shown, a **Residual tests** panel appears below the plot with
three formal tests:

- **Kolmogorov–Smirnov test**: tests whether the scaled residuals are uniformly distributed.
- **Dispersion test**: checks for over- or underdispersion.
- **Outlier test**: detects observations with extreme residual values.

The **Export** button saves the current diagnostic plot to a file (PNG, PDF, or SVG).


EDA tab
-------

The Exploratory Data Analysis tab lets you visualize your data before fitting a model.

Plot types
~~~~~~~~~~

The plot type is determined automatically by the types of the selected variables:

- **Numeric × Numeric**: scatter plot with optional regression line.
- **Numeric × (none)**: histogram with adjustable bin count and optional kernel density curve.
- **Numeric × Categorical**: grouped scatter plot (strip chart) colored by group.

Scatter plot options
~~~~~~~~~~~~~~~~~~~~

- **X** and **Y**: select the variables for each axis.
- **Group**: color points by a categorical variable.
- **Pool by**: average X and Y values within each (group, pool) cell before plotting
  (e.g. pool by speaker to get one point per speaker per vowel).
- **Label**: render the value of a variable as text at each data point.
- **Regression line**: overlay an OLS regression line.
- **Means**: show the mean of each group as a marker.
- **Ellipses**: draw confidence ellipses around each group, with an adjustable confidence
  level (default: 68% ≈ 1σ).
- **Formant chart**: reverse both axes so that high values appear at the bottom-left, as is
  conventional for F1 × F2 vowel plots.

Histogram options
~~~~~~~~~~~~~~~~~

- **Bins**: number of histogram bins (0 = automatic, using Sturges' rule).
- **Density curve**: overlay a kernel density estimate.
- **Smoothing**: bandwidth adjustment factor for the density curve (1.00 = Silverman's rule
  of thumb).

Summary table
~~~~~~~~~~~~~

Below the plot, a summary table shows descriptive statistics (count, mean, standard deviation,
min, max) for the plotted variables, broken down by group if applicable.

The EDA plot can be **detached** into a resizable floating window using the maximize button
in the toolbar. It can be exported to PNG, PDF, or SVG via the **Save as...** menu.


Supported model types
---------------------

Phonometrica's statistical engine supports the following model families:

- **Gaussian** (identity link): linear regression and linear mixed models (LMM).
- **Binomial** (logit link): logistic regression and logistic mixed models.
- **Poisson** (log link): Poisson regression and Poisson mixed models.
- **Negative binomial** (log link, NB2 parameterization): for overdispersed count data.
- **GAM**: generalized additive models with penalized regression splines, including by-variable
  smooths and per-smooth significance tests.

All model types support random intercepts and random slopes (mixed-effects models).


Tips
----

- Use the **Family** dropdown to match your response variable: Gaussian for continuous
  measurements (e.g. formant frequencies, durations), Binomial for binary outcomes (e.g.
  correct/incorrect), Poisson or Negative binomial for count data.
- Start with a simple model and build up complexity. Use **Compare** to test whether
  additional terms improve the fit.
- Check the **Diagnostics** tab after fitting. Poor residual patterns suggest the model
  may need a different family, additional predictors, or data transformation.
- For vowel formant plots, use the **Formant chart** checkbox in the EDA tab to reverse
  both axes.


.. |help| image:: ../icons/circle-help.svg
    :height: 16px
    :width: 16px
