.. _analysis-view:

Statistical analysis
====================

Phonometrica provides a dedicated environment for fitting and comparing statistical models.
To open an analysis, create one from a concordance or dataset using the **Analyze** command,
or open an existing ``.phon-analysis`` file. The analysis view is divided into three areas:
a **top bar** for entering formulas, a **left panel** with column and model lists, and a
**right panel** with tabs for results, post-hoc tests, diagnostics, and exploratory plots.


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
- **Show random effects**: (enabled only for mixed-effects models) when checked, the summary
  includes a table of conditional modes (BLUPs) for each grouping factor. Each row shows one
  level of the grouping variable (e.g. one speaker) and the estimated deviation from the
  population mean for each random term (intercept, slopes).

  Conditional modes are best linear unbiased predictors (BLUPs) of the random effects, also
  known as *ranef* in R's lme4 terminology. They represent each group's estimated deviation
  from the population-level fixed effects. For example, a positive random intercept for
  speaker *A* means that speaker *A*'s response is estimated to be higher than the population
  mean, after accounting for the fixed effects.

  .. note::

     Conditional modes are shrinkage estimates: they are pulled toward zero relative to
     simple group-level means, with more shrinkage for groups with fewer observations or
     higher residual variance. This is a desirable property — it reduces overfitting to
     small groups.


Post-hoc tab
------------

The Post-hoc tab provides estimated marginal means (EMMs) and pairwise contrasts for the
categorical factors in the selected model. This is the standard approach for post-hoc
analysis in linear and generalized linear models.

The toolbar at the top of the tab contains the following controls:

- **Factor**: select the categorical variable for which to compute EMMs or trends.
- **Trend**: select a numeric variable to estimate its slope at each level of the factor
  (emtrends mode). Leave as "(None)" for standard estimated marginal means.
- **Adjustment**: the method used to correct *p*-values for multiple comparisons:

  - *Holm* (default): Holm's step-down procedure. Uniformly more powerful than Bonferroni
    while still controlling the family-wise error rate.
  - *Bonferroni*: multiply each *p*-value by the number of tests.
  - *None*: no adjustment (raw *p*-values).

- **Confidence**: the confidence level for intervals (default: 0.95).

Estimated marginal means
~~~~~~~~~~~~~~~~~~~~~~~~~

EMMs are population-averaged predictions at each level of the target factor. Other categorical
factors in the model are balanced (equal weight across their levels), and numeric covariates
are held at their observed means. This ensures that the estimated means are not biased by
unequal sample sizes or covariate imbalances.

The upper table displays:

- **Level**: each level of the selected factor.
- **EMM**: the estimated marginal mean on the response scale.
- **SE**: the standard error.
- **Lower CI** / **Upper CI**: confidence interval bounds.

For models with a non-identity link function (e.g. logistic or Poisson regression), two
additional columns show the link-scale EMM and SE. The response-scale CIs are obtained by
back-transforming the link-scale CI endpoints through the inverse link function, which
produces asymmetric but more accurate intervals than the delta method.

Emtrends mode
~~~~~~~~~~~~~

When a numeric variable is selected in the **Trend** dropdown, the tab switches to
*emtrends* mode. Instead of estimated marginal means, it computes the slope (trend) of
the selected continuous variable at each level of the factor.

This is useful for testing whether a continuous effect differs across groups. For example,
with the model ``F2 ~ frequency * group``, selecting Factor = "group" and Trend = "frequency"
will estimate the slope of frequency for each group. The pairwise contrasts then test
whether these slopes differ significantly.

Trends are reported on the link scale. For Gaussian models with identity link, these are
the response-scale slopes (e.g. Hz per unit of frequency). For models with a log or logit
link, they represent the change in the linear predictor per unit of the trend variable
(e.g. change in log-odds per unit of frequency for logistic regression).

Pairwise contrasts
~~~~~~~~~~~~~~~~~~

The lower table shows all pairwise differences between levels, with columns for:

- **Contrast**: the pair being compared (e.g. "a - i").
- **Estimate**: the difference between the two EMMs (or trends) on the link scale.
- **SE**: the standard error of the contrast, accounting for the covariance between EMMs.
- **t value** or **z value**: the Wald test statistic (t for Gaussian fixed-effects
  models, z for GLMs and mixed models).
- **p value**: the *p*-value after adjustment for multiple comparisons.

Significant contrasts (*p* < 0.05) are highlighted in bold. Significance codes
(\*\*\* < 0.001, \*\* < 0.01, \* < 0.05, . < 0.1) appear in the rightmost column.

Mathematical background
~~~~~~~~~~~~~~~~~~~~~~~

EMMs are computed following Searle, Speed & Milliken (1980), using the terminology
of Lenth (2016). For a model with fixed-effects coefficient vector **β** and covariance
matrix **V**, the EMM for each level is a linear function **Lβ** where **L** is a
prediction matrix (the "reference grid"). Standard errors are obtained from
SE = √diag(**L V L'**), and confidence intervals use the appropriate *t* or *z*
quantiles.

For generalized linear models, response-scale standard errors are computed via the
delta method, and confidence intervals use endpoint back-transformation through the
inverse link function.

.. note::

   EMMs for mixed-effects models use the fixed-effects coefficients only. Random effects
   integrate out to zero at the population level and do not contribute to the EMMs.
   The covariance matrix is the conditional variance Var(β̂ | θ̂) from the Henderson
   system, which is the same quantity reported by lme4 and glmmTMB in R.


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
- After fitting a model with a categorical predictor, switch to the **Post-hoc** tab to
  see which levels differ from each other. The Holm adjustment (default) controls the
  family-wise error rate while being more powerful than Bonferroni.
- For interaction models (e.g. ``F2 ~ frequency * group``), use the **Trend** dropdown in
  the Post-hoc tab to test whether the slope of a continuous variable differs across groups.
- For mixed-effects models, check **Show random effects** in the Summary toolbar to inspect
  the conditional modes (BLUPs) for each speaker, item, or other grouping factor. Large
  deviations from zero may indicate influential groups worth investigating.


References
----------

- Bates, D., Mächler, M., Bolker, B. & Walker, S. (2015). Fitting linear mixed-effects
  models using lme4. *Journal of Statistical Software*, 67(1), 1–48.
- Holm, S. (1979). A simple sequentially rejective multiple test procedure.
  *Scandinavian Journal of Statistics*, 6(2), 65–70.
- Lenth, R.V. (2016). Least-squares means: the R package lsmeans.
  *Journal of Statistical Software*, 69(1), 1–33.
- Nakagawa, S. & Schielzeth, H. (2013). A general and simple method for obtaining
  R² from generalized linear mixed-effects models. *Methods in Ecology and Evolution*,
  4(2), 133–142.
- Searle, S.R., Speed, F.M. & Milliken, G.A. (1980). Population marginal means in the
  linear model: an alternative to least squares means. *The American Statistician*,
  34(4), 216–221.


.. |help| image:: ../icons/circle-help.svg
    :height: 16px
    :width: 16px
