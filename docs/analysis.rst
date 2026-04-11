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

Smooth terms fit penalized regression splines, which capture nonlinear relationships without
requiring you to specify the shape of the curve in advance. They are useful when a predictor's
effect on the response is not a straight line — for instance, formant values that change
nonlinearly over the course of a vowel.

Basic syntax:

- ``y ~ s(x)``: penalized regression spline of the numeric variable *x* (default: 10 knots, cubic regression spline basis)
- ``y ~ s(x, k=15)``: spline with 15 knots (increase *k* if you expect a highly wiggly pattern)
- ``y ~ s(x, by=group)``: a separate smooth for each level of the factor *group*

Basis types
^^^^^^^^^^^

Phonometrica supports two basis types, selected with the ``bs`` argument:

**Cubic regression splines** (``bs=cr``, the default). A ``bs=cr`` smooth models a nonlinear
relationship between a numeric covariate and the response. The basis consists of natural
cubic spline functions evaluated at *k* knots placed at quantiles of the covariate. The
penalty is the integrated squared second derivative of the curve, which measures
*wiggliness*: higher smoothing parameters produce smoother curves, and in the limit the
smooth reduces to a straight line. The penalty has rank *k* − 2, because the penalty null
space — the set of functions with zero second derivative — is the family of straight lines
(intercept + slope), which are left unpenalized. An identifiability constraint (sum-to-zero
across the data) is absorbed into the basis, reducing the effective dimension from *k* to
*k* − 1. This ensures the smooth is identifiable in the presence of a model intercept.

For example::

   F1 ~ vowel + s(duration)
   F1 ~ vowel + s(duration, k=20)

**Random effects** (``bs=re``). A ``bs=re`` smooth models group-level deviations for a
categorical factor. The basis is an indicator matrix (one column per level), and the
penalty is the identity matrix, which shrinks all group adjustments toward zero. This is
mathematically equivalent to a random effect with variance σ²_u = σ²_ε / λ, where λ is
the smoothing parameter estimated by GCV (Generalized Cross-Validation, an efficient
approximation to leave-one-out cross-validation that selects the degree of smoothing by
minimizing prediction error). The key difference from ``bs=cr`` is that
``bs=re`` does not model a smooth curve — it models a set of discrete group-level
adjustments.

Random intercepts and random slopes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``bs=re`` basis supports two kinds of random effects:

**Random intercepts**: ``s(speaker, bs=re)`` gives each speaker a constant adjustment to the
response. This is the GAM equivalent of ``(1|speaker)`` in a mixed model.

**Random slopes**: ``s(speaker, by=duration, bs=re)`` gives each speaker a different slope
for the numeric variable *duration*. This is the GAM equivalent of ``(0 + duration | speaker)``
in a mixed model. Note that the ``by`` argument has a different meaning here than for
``bs=cr``: for ``bs=cr``, ``by=factor`` fits a separate smooth per factor level; for
``bs=re``, ``by=numeric`` scales the group indicators by the covariate value.

To model both random intercepts and random slopes for the same grouping variable, include
two separate ``bs=re`` terms. This gives **uncorrelated** random effects (each with its own
smoothing parameter)::

   F1 ~ vowel + s(duration) + s(speaker, bs=re) + s(speaker, by=duration, bs=re)

.. note::

   **Why two terms instead of one?** Unlike the mixed-model syntax where
   ``(1 + duration | speaker)`` bundles intercept and slope into a single term, the GAM
   framework requires separate smooth terms because each one has its own smoothing parameter
   (and therefore its own variance component), estimated independently by GCV. The formula
   ``s(speaker, bs=re) + s(speaker, by=duration, bs=re)`` is the standard way to express
   this in mgcv and in Phonometrica. Removing the slope variable from the formula (e.g.
   right-clicking *duration* and choosing *Remove from formula*) will remove the slope term
   while leaving the random intercept in place.

You can also cross grouping factors::

   F1 ~ vowel + s(duration) + s(speaker, bs=re) + s(item, bs=re)

.. note::

   **When to use** ``bs=re`` **vs** ``(1|group)``. Both express random intercepts, but they
   go through different estimation paths:

   - ``s(speaker, bs=re)`` is estimated in the **penalized regression** (GAM) framework via GCV.
     Use this when your model already includes smooth terms like ``s(duration)``.
   - ``(1|speaker)`` is estimated in the **mixed model** framework via Laplace approximation.
     Use this when your model has only parametric fixed effects (no smooth terms).

   Combining ``s()`` smooth terms with ``(1|group)`` random effects in the same formula is not
   currently supported. If you need both a nonlinear smooth and grouping structure, use
   ``bs=re`` for the grouping.

.. note::

   **Correlated vs uncorrelated random effects.** Two separate ``bs=re`` terms for the same
   group (one for intercepts, one for slopes) produce uncorrelated random effects — the
   correlation between a speaker's intercept adjustment and slope adjustment is assumed to be
   zero. This is analogous to fitting two separate ``(0 + ... | group)`` terms in lme4. If
   you need correlated random intercepts and slopes, use the mixed-model syntax
   ``(1 + x | group)`` with parametric fixed effects instead of smooth terms.

.. note::

   **Choosing** *k* **for spline smooths.** The default of *k* = 10 works well in most
   situations. If you suspect the relationship is very wiggly (e.g. a formant contour with
   multiple turning points), increase *k* to 15 or 20. Setting *k* too high is not harmful —
   the penalty will prevent overfitting — but it makes computation slower. Setting *k* too low
   can prevent the model from capturing genuine patterns. A useful rule of thumb: if the EDF
   reported in the summary is close to *k* − 1, consider increasing *k*.

Random effects
~~~~~~~~~~~~~~

- ``y ~ a + (1 | speaker)``: random intercept for *speaker*
- ``y ~ a + (1 + a | speaker)``: random intercept and correlated random slope for *a*
- ``y ~ a + (0 + a | speaker)``: random slope only (no random intercept)

You can combine fixed effects with smooth terms, or fixed effects with random effects.
To combine smooth terms with grouping structure, use ``s(group, bs=re)`` (see above).


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
  - *Add as grouping factor*: add a random intercept ``(1 | group)`` for mixed-effects models.
  - *Add smooth for grouping*: (categorical columns only) add a penalized random intercept
    ``s(group, bs=re)`` for use in GAM models.
  - *Add smooth for random slope in...*: (categorical columns only) add a penalized random
    slope ``s(group, by=numeric, bs=re)`` for use in GAM models. A submenu lists the
    available numeric columns as slope variables.
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
- **Scaled Residuals vs Fitted**: uses simulation-based scaled residuals (see below), which
  should be uniformly distributed between 0 and 1 regardless of the model family.
- **Scaled Residuals Q-Q**: a Q-Q plot of the scaled residuals against the uniform distribution.


Scaled residuals
~~~~~~~~~~~~~~~~

Traditional residuals (e.g. Pearson or deviance residuals) are difficult to interpret for
non-Gaussian models: their expected distribution depends on the response family and the
fitted values, so it is hard to tell whether an unusual pattern reflects a genuine model
problem or is simply an artefact of the distributional shape. Simulation-based *scaled
residuals* solve this problem by comparing each observed value to a set of values simulated
from the fitted model. Under a correctly specified model, the scaled residuals are uniformly
distributed between 0 and 1 regardless of the family, which makes them easy to interpret
visually and with formal tests.

Phonometrica computes scaled residuals using a procedure inspired by the DHARMa package in R
(Hartig, 2020). The steps are as follows:

1. For each of 1000 replicates, a new response vector is simulated from the fitted model.
   For mixed-effects models, the simulation is *conditional* on the estimated random effects
   (BLUPs), meaning that the per-observation fitted values — which already include the
   random effects — are used as the mean for the simulation. Only the residual noise is
   re-drawn.
2. For each observation, the observed value is ranked among its 1000 simulated counterparts.
   An observation that falls in the middle of the simulated range receives a residual near
   0.5; one that falls near the edge receives a residual close to 0 or 1.
3. For discrete response families (Binomial, Poisson, Negative binomial), a small random
   jitter is applied to break ties, following the *randomized quantile residuals* approach
   of Dunn & Smyth (1996). This ensures that the residuals are continuous and uniformly
   distributed even for discrete data.

When a scaled residual plot is shown, a **Residual tests** panel appears below the plot with
three formal tests:

- **Kolmogorov–Smirnov test**: tests whether the scaled residuals follow a uniform
  distribution. A significant result (small *p*-value) suggests that the model does not
  adequately describe the data.
- **Dispersion test**: compares the observed variance of the residuals to the theoretical
  variance of a uniform distribution (1/12). Values greater than 1 suggest overdispersion
  (more variability than the model predicts), while values less than 1 suggest
  underdispersion.
- **Outlier test**: counts how many residuals fall in the extreme tails (below 1/1001 or
  above 1000/1001 for 1000 simulations). Under a correct model, the expected number of outliers
  is approximately *n* × 2/1001, where *n* is the number of observations. A significant
  excess of outliers may indicate influential observations or model misfit.

.. note::

   **Interpreting the tests.** Because the residuals are based on only 1000 simulation
   replicates, the tests have limited precision and their *p*-values may fluctuate across
   runs. A single borderline-significant result should not be cause for concern. Instead,
   look at the overall picture: do all three tests agree? Does the Q-Q plot show a clear
   pattern? A well-specified model will typically show non-significant results on all three
   tests, a dispersion ratio close to 1, and a Q-Q plot with points falling along the
   diagonal.

.. note::

   **Comparison with R.** Phonometrica's scaled residuals use conditional simulation for
   mixed-effects models: the random effects are held fixed at their estimated values (BLUPs),
   and only the residual noise is re-simulated. In R, the DHARMa package computes scaled
   residuals via ``simulateResiduals()``, which calls the model's ``simulate()`` method.
   The default behaviour depends on the package used to fit the model:

   - **lme4** (``lmer``/``glmer``): by default, ``simulate()`` draws *new* random effects
     from the estimated distribution (unconditional simulation). To match Phonometrica's
     conditional approach, pass ``re.form = NULL``::

        sim <- simulateResiduals(model, re.form = NULL)

   - **glmmTMB**: ``simulate()`` always draws new random effects. Conditional simulation
     is not currently supported (as of glmmTMB 1.1.x). Diagnostic values may therefore
     differ from those reported by Phonometrica, though both approaches should agree on
     whether the model is well-specified.

   Because of differences in random number generators and the limited number of simulation
   replicates, individual test statistics and *p*-values are not expected to match exactly
   between Phonometrica and R. They should, however, lead to the same qualitative
   conclusions.

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
  smooths, per-smooth significance tests, and random intercepts and random slopes via ``bs=re``
  to account for speaker/item grouping.

All model types support random intercepts and random slopes (mixed-effects models).
GAM models support grouping structure through ``s(group, bs=re)`` smooth terms (random
intercepts) and ``s(group, by=x, bs=re)`` terms (random slopes).


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
- When fitting a GAM with speaker or item effects, use ``s(speaker, bs=re)`` rather than
  a fixed effect for the grouping variable. This estimates the between-group variance and
  shrinks group estimates toward the population mean, reducing overfitting — especially
  when some groups have few observations. If the effect of a covariate varies by speaker,
  add a random slope: ``s(speaker, by=duration, bs=re)``.
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
- Dunn, P.K. & Smyth, G.K. (1996). Randomized quantile residuals. *Journal of
  Computational and Graphical Statistics*, 5(3), 236–244.
- Hartig, F. (2020). DHARMa: Residual diagnostics for hierarchical (multi-level / mixed)
  regression models. R package. https://CRAN.R-project.org/package=DHARMa
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
- Wood, S.N. (2017). *Generalized Additive Models: An Introduction with R* (2nd ed.).
  CRC Press.


.. |help| image:: ../icons/circle-help.svg
    :height: 16px
    :width: 16px
