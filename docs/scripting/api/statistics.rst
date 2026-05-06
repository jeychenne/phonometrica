Statistical functions
=====================

This page describes the statistical functions that are available in Phonometrica.


Array statistics
----------------

.. function:: mean(x [, dim])

Returns the mean of the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the mean over the given dimension in a two-dimensional array. If ``dim`` is equal to 1, the calculation is performed
over rows. If it is equal to 2, it is performed over columns.

See also: :func:`std`, :func:`sum`, :func:`vrc`

------------

.. function:: std(x [, dim])

Returns the standard deviation of the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the standard deviation over the given dimension in a two-dimensional array. If ``dim`` is equal to 1, the calculation is performed
over rows. If it is equal to 2, it is performed over columns.

See also: :func:`vrc`, :func:`mean`

------------

.. function:: sum(x [, dim])

Returns the sum of the elements in the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the sum over the given dimension in a two-dimensional array. If ``dim`` is equal to 1, the summation is performed
over rows. If it is equal to 2, summation is performed over columns.

------------

.. function:: vrc(x [, dim])

Returns the sample variance of the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the variance over the given dimension in a two-dimensional array. If ``dim`` is equal to 1, the calculation is performed
over rows. If it is equal to 2, it is performed over columns.

See also: :func:`std`

------------


Model fitting
-------------

.. function:: fit(formula, data [, family])

Fits a frequentist statistical model from a formula string and a data table (concordance or dataset). This is the main entry
point for model fitting in Phonometrica.

``formula`` is an R-style formula string (e.g. ``"f1 ~ vowel + gender + (1|speaker)"``). ``data`` is a
``DataTable`` object (a concordance or dataset). ``family`` is an optional string specifying the
distributional family; the default is ``"gaussian"``.

Supported families:

* ``"gaussian"``: linear regression / LMM (identity link)
* ``"binomial"``: logistic regression / logistic GLMM (logit link)
* ``"poisson"``: Poisson regression / Poisson GLMM (log link)
* ``"negbin"``: negative binomial regression / NB GLMM (log link)
* ``"beta"``: beta regression / beta GLMM (logit link), for proportions in (0, 1)
* ``"student"``: Student *t* regression / *t* mixed model (identity link), for continuous
  outcomes with heavy-tailed residuals (e.g. formant measurements with tracking errors).
  The scale parameter σ and the degrees-of-freedom parameter ν are estimated jointly
  with the regression coefficients. Observations with large residuals are automatically
  down-weighted, making the estimates robust to outliers. When ν → ∞, the model reduces
  to Gaussian regression.

Returns a ``Model`` object (see Model fields below).

Example::

   let ds = load("my_data.csv")
   let m = fit("f1 ~ vowel + gender + (1|speaker)", ds)
   summarize(m)
   print "AIC = " & m.aic

   let m2 = fit("voicing ~ consonant + position + (1|speaker)", ds, "beta")
   summarize(m2)

   let m3 = fit("f1 ~ vowel + gender + (1|speaker)", ds, "student")
   summarize(m3)
   print "sigma = " & m3.sigma
   print "nu    = " & m3.nu

------------

.. function:: fit(formula, data, prior[, prior])

Fits a **Bayesian** model using approximate Bayesian inference (INLA-style). The ``prior``
argument is a ``Prior`` object (see `Prior specification`_ below) that controls the
prior distributions on all model parameters.

When ``family`` is omitted, ``"gaussian"`` is used. The function returns a ``Model`` object
with ``estimation`` set to ``"Bayesian"`` and the posterior summary fields populated
(see `Bayesian model fields`_ below).

At fit time, any prior fields left at their auto-scaled defaults are replaced by
data-dependent weakly informative priors (see `Prior specification`_).

Example::

   let ds = load("my_data.csv")

   // Bayesian fit with default priors
   let prior = Prior()
   let m = fit("f1 ~ vowel + (1|speaker)", ds, prior)
   summarize(m)
   print "pd for vowel[i] = " & m.pd[2]

   // Bayesian fit with custom fixed-effects prior
   let prior2 = Prior()
   set_fixed(prior2, 0, 5)   // N(0, 5) for all slopes
   let m2 = fit("count ~ group + (1|subject)", ds, "poisson", prior2)
   summarize(m2)

------------

.. function:: summarize(model)

Prints a summary of a fitted ``Model`` object. The output adapts to the estimation method:

**Frequentist models**: fixed-effects coefficients (estimates, standard errors, z/t-values,
and *p*-values), random-effects variance components (if present), and overall fit statistics
(AIC, BIC, log-likelihood).

**Bayesian models**: fixed-effects posterior summaries (posterior mean, posterior SD, 95%
credible interval bounds, and probability of direction with significance codes), hyperparameter
posteriors (variance component SDs, dispersion parameters) with credible intervals when
available from grid integration, and Bayesian fit statistics (WAIC, LOO-IC with Pareto *k*
diagnostics, LPPD, log-marginal likelihood).

Example::

   let m = fit("f1 ~ vowel + (1|speaker)", ds)
   summarize(m)

   // Bayesian
   let prior = Prior()
   let mb = fit("f1 ~ vowel + (1|speaker)", ds, prior)
   summarize(mb)

------------

.. function:: get_coef(model)

Prints a formatted table of the estimated fixed-effects coefficients and returns the coefficient
array from a fitted model.

------------

.. function:: compare(model1, model2)

Compares two fitted models. The comparison method depends on the estimation type:

**Frequentist models**: likelihood-ratio test (LRT) with a table of information criteria
(AIC, BIC, log-likelihood, deviance) and the LRT chi-squared statistic with *p*-value.
The models should be nested (one should be a special case of the other).

**Bayesian models**: WAIC and LOO-IC comparison tables (with ΔWAIC, ΔLOO-IC and their
standard errors), Pareto *k* diagnostic summaries, and log Bayes factors. Cannot be mixed
with frequentist models.

Example::

   let m1 = fit("f1 ~ vowel + (1|speaker)", ds)
   let m2 = fit("f1 ~ vowel + gender + (1|speaker)", ds)
   compare(m1, m2)

------------

.. function:: filter(table as DataTable, expression as String [, label as String])

Returns a new dataset containing only the rows that match the filter expression.
If ``label`` is provided, the resulting dataset will have the given label.

Example::

   let ds = load("data.csv")
   let females = filter(ds, "gender == 'F'")


Post-hoc analysis
-----------------

.. function:: emmeans(model, factor [, adjustment])

Computes and prints estimated marginal means (EMMs) for the given categorical factor.
EMMs are population-averaged predictions at each level of the factor, with other categorical
factors balanced and numeric covariates held at their observed means.

If ``adjustment`` is provided (``"holm"``, ``"bonferroni"``, or ``"none"``), pairwise contrasts
between all levels of the factor are computed and printed as well.

For **Bayesian models**, the EMM table reports credible intervals instead of confidence
intervals, and the contrast table reports the probability of direction (pd) instead of
adjusted *p*-values. Multiplicity adjustment is not applied to pd values; the ``adjustment``
argument is ignored.

Example::

   let m = fit("f1 ~ vowel + gender + (1|speaker)", ds)
   emmeans(m, "vowel", "holm")

------------

.. function:: emtrends(model, factor, variable [, adjustment])

Estimates the slope (trend) of a continuous variable at each level of a categorical factor. This is
useful when the model includes an interaction between a numeric covariate and a factor (e.g.
``f2 ~ frequency * group``).

Results are reported on the link scale. If ``adjustment`` is provided, pairwise contrasts
of the slopes across factor levels are computed and printed.

Example::

   let m = fit("f2 ~ frequency * group + (1|speaker)", ds)
   emtrends(m, "group", "frequency", "holm")


Prediction
----------

.. function:: predict(model as Model)

Returns a ``Dataset`` of model predictions at the training rows used to fit ``model``.
Equivalent in spirit to ``predict(model)`` in R for a freshly fit model.

The result has one row per observation in the original data and four columns:

* ``Fit``: predicted mean (on the response scale by default).
* ``SE fit``: standard error of the linear predictor.
* ``CI lower``, ``CI upper``: 95% confidence interval (frequentist models) or 95% credible
  interval (Bayesian models). The bounds are computed on the link scale and then transformed
  by the inverse link, so for non-identity-link models the interval is asymmetric on the
  response scale.

This call relies on the design matrix that was built when ``model`` was fit. The design
matrix is **not persisted** in ``.phon-analysis`` files (it can be many megabytes for large
datasets), so ``predict(model)`` is only available in the same session in which the model
was fit. After saving and reloading a project, call ``predict(model, data)`` instead — see
the next overload.

Example::

   let ds = load("vowel_data.csv")
   let m = fit("f1 ~ vowel + gender", ds)
   let p = predict(m)
   print get_header(p, 1)        # "Fit"
   print get_column(p, "Fit")[1] # predicted F1 for the first row

------------

.. function:: predict(model as Model, newdata as Dataset)

Returns a ``Dataset`` of model predictions at the rows of ``newdata``. The result echoes all
of ``newdata``'s columns, followed by the four prediction columns described above.

``newdata`` must contain a column for every predictor in the model formula. Other columns are
copied through unchanged. This is the form to use after saving and reloading a project, or
to predict on a held-out dataset.

If a row in ``newdata`` cannot be predicted — for example, the value of a categorical
predictor is a level that was not seen at fit time, or a cell in a numeric predictor is
empty or non-numeric — the four prediction columns get ``NaN`` for that row, while the
echoed columns are passed through normally.

Errors are raised (no ``NaN`` fallback) for structural problems: a predictor column missing
from ``newdata``, an unparseable formula, or a model type that is not yet supported (see
**Limitations** below).

Example::

   let ds = load("vowel_data.csv")
   let m = fit("f1 ~ vowel + gender", ds)
   let p = predict(m, ds)
   # p has all of ds's columns plus Fit, SE fit, CI lower, CI upper

------------

.. function:: predict(model as Model, newdata as Dataset, options as Table)

As above, but with an ``options`` table that controls the output. All fields are optional:

* ``type`` (``String``, default ``"ci"``): the kind of interval to compute. Currently only
  ``"ci"`` is supported. ``"pi"`` (prediction intervals for a new observation, including
  residual variance) and ``"both"`` will be added in a future release.
* ``scale`` (``String``, default ``"response"``): the scale of ``Fit`` and the CI bounds.
  ``"response"`` returns predictions on the natural response scale (probabilities for
  binomial, counts for Poisson, etc.). ``"link"`` returns them on the linear-predictor
  scale (logit, log, identity, etc.). ``SE fit`` is always on the link scale.
* ``bare`` (``Boolean``, default ``false``): if ``true``, drop the echoed columns of
  ``newdata`` and return only the four prediction columns.
* ``ci_level`` (``Number``, default ``0.95``): coverage probability for the confidence /
  credible interval. Must be strictly between 0 and 1.
* ``re_form`` (``String``, default ``"none"``): how to handle random effects in mixed
  models.

  - ``"none"`` (default): **population-level prediction**. The random effects ``u`` are set
    to zero, giving ``η = X·β``. This is what ``ggpredict`` and ``predict.glmmTMB(re.form
    = NA)`` return by default.
  - ``"all"``: **conditional prediction**, summing the BLUPs across all random-effects
    groups present in the model. ``η = X·β + Σ_g Z_g·u_g``.
  - A specific group name (e.g. ``"speaker"``): conditional on that group only, with
    other random-effects groups set to ``u = 0``. This is how the **Effects** tab's
    ``Random`` drop-down builds per-speaker curves.

  For conditional prediction, ``newdata`` must contain a column named after the grouping
  factor with values matching the levels seen at fit time. Rows whose grouping cell is
  empty or names a level not present in the fitted model get ``NaN`` for that row's
  prediction. The standard error treats the BLUPs as fixed (matching ``lme4``'s
  ``predict.merMod`` default behaviour); a future release will add an option to propagate
  ``u`` uncertainty into the interval.

Example::

   let ds = load("vowel_data.csv")
   let m = fit("schwa ~ position + gender", ds, "binomial")

   # Prediction on the link (logit) scale, 99% CI, no echoed columns
   let opts = {}
   opts["scale"] = "link"
   opts["ci_level"] = 0.99
   opts["bare"] = true
   let p = predict(m, ds, opts)

Example (conditional prediction)::

   let ds = load("schwa.csv")
   let m = fit("realized ~ position + (1|speaker)", ds, "binomial")

   # Population-level: predicted probability for an "average" speaker
   let p_pop = predict(m, ds)

   # Conditional on speaker: per-speaker BLUPs added to η
   let opts = {}
   opts["re_form"] = "speaker"
   let p_cond = predict(m, ds, opts)

   # For training rows, p_cond["Fit"] matches m.fitted exactly.
   # The same call on a held-out dataset gives per-speaker predicted
   # probabilities for whichever speakers appear in newdata.

------------

**Mixed-effects models.** With the default ``re_form = "none"``, ``predict()`` returns the
**population-level** prediction: ``η = X·β`` with the random effects set to zero. This is
what ``ggpredict`` returns by default, and what most users want for "what does the model
say in general?". The ``Fit`` values do **not** match ``model.fitted``, which is the
conditional mean including the BLUP contribution per group. With a non-default
``re_form`` (``"all"`` or a specific group name), the BLUPs are folded in and ``Fit`` on
the response scale matches ``model.fitted`` at training rows.

**Bayesian models.** The same arithmetic produces the posterior mean of the predicted value
and the posterior SD, because for Bayesian fits ``model.beta`` and ``model.vcov`` hold the
posterior mean and posterior covariance respectively. The ``CI lower`` and ``CI upper``
columns are then 95% **credible intervals**. The column names stay the same so script code
does not need to branch on estimation type — the interpretation comes from the model.

**Limitations.** The function refuses cleanly, with an explanatory error message, in the
following cases:

* Models with **by-factor smooths** (``s(x, by=...)``) or **random-effect smooths**
  (``s(g, bs="re")``).
* ``opts["type"] = "pi"`` or ``"both"``: prediction intervals are not yet implemented.
* ``opts["re_form"]`` set to anything other than ``"none"``, ``"all"``, or the name of a
  random-effects group present in the fitted model.

For visualizing predictions interactively, use the **Effects** tab in the analysis view
(see :ref:`analysis-view`); it calls into ``predict()`` internally, including for
conditional per-group prediction.


Diagnostics
-----------

.. function:: test_residuals(model)

Computes simulation-based residual diagnostics similar to the DHARMa package in R [HAR2022]_ and prints the results. Three tests
are performed:

* **Uniformity test** (Kolmogorov-Smirnov): tests whether the scaled residuals follow a uniform
  distribution, as expected if the model is correctly specified.
* **Dispersion test**: checks for over- or under-dispersion by comparing the observed variance
  of the scaled residuals to its expected value.
* **Outlier test**: counts observations whose response falls entirely outside the simulated range.

For **frequentist models**, the simulations are drawn from the fitted model (conditional
simulation with 1000 replicates).

For **Bayesian models**, the same DHARMa-style diagnostics are used: scaled residuals are
computed from the marginal predictive distribution at the posterior mean (with random effects
re-drawn from their estimated covariance for mixed-effects models), and the reported
*p*-values are frequentist p-values from the simulation reference distribution. The
interpretation is the same as for frequentist fits.

Example::

   let m = fit("count ~ condition + (1|subject)", ds, "poisson")
   test_residuals(m)


Model fields
------------

A ``Model`` object returned by :func:`fit` has the following fields:

.. attribute:: formula

The formula string used to fit the model.

.. attribute:: family

The family name (e.g. ``"gaussian"``, ``"binomial"``, ``"poisson"``, ``"negbin"``, ``"beta"``, ``"student"``).

.. attribute:: link

The link function name (e.g. ``"identity"``, ``"logit"``, ``"log"``).

.. attribute:: nobs

Number of observations.

.. attribute:: aic

Akaike Information Criterion.

.. attribute:: bic

Bayesian Information Criterion.

.. attribute:: loglik

Log-likelihood at convergence.

.. attribute:: deviance

Residual deviance.

.. attribute:: r2

R² (Gaussian fixed-effects models only).

.. attribute:: adj_r2

Adjusted R² (Gaussian fixed-effects models only).

.. attribute:: r2_marginal

Nakagawa marginal R² (mixed models only).

.. attribute:: r2_conditional

Nakagawa conditional R² (mixed models only).

.. attribute:: rse

Residual standard error (Gaussian only).

.. attribute:: df

Residual degrees of freedom.

.. attribute:: theta

Overdispersion parameter (negative binomial only; 0 otherwise).

.. attribute:: phi

Precision parameter (beta only; 0 otherwise).

.. attribute:: sigma

Scale parameter (Student *t* only; 0 otherwise).

.. attribute:: nu

Degrees of freedom (Student *t* only; 0 otherwise).

.. attribute:: converged

Boolean indicating whether the optimizer converged.

.. attribute:: niter

Number of iterations (0 for OLS).

.. attribute:: fitted

Array of fitted values from the model.

.. attribute:: residuals

Array of response residuals (observed − fitted) from the model.

.. attribute:: estimation

A string indicating the estimation method: ``"Frequentist"`` or ``"Bayesian"``.
Available on all models.

.. attribute:: waic

WAIC (Watanabe–Akaike information criterion). NaN for frequentist models.

.. attribute:: loo_ic

LOO-IC (PSIS leave-one-out information criterion). NaN for frequentist models.

.. attribute:: p_waic

Effective number of parameters from WAIC. NaN for frequentist models.

.. attribute:: p_loo

Effective number of parameters from LOO-IC. NaN for frequentist models.

.. attribute:: se_waic

Standard error of WAIC. NaN for frequentist models.

.. attribute:: se_loo

Standard error of LOO-IC. NaN for frequentist models.

.. attribute:: pareto_k

Array of per-observation Pareto *k* diagnostics from PSIS-LOO. Empty for frequentist
models. Values below 0.7 indicate that LOO-IC is reliable; values above 0.7 suggest
that the LOO approximation may be poor for those observations.

.. attribute:: log_marginal

Log-marginal likelihood (Laplace approximation). NaN for frequentist models.


.. _prior-specification:

Prior specification
-------------------

A ``Prior`` object controls the prior distributions used for Bayesian model fitting. Create
one by calling the ``Prior`` constructor, then optionally configure individual priors before
passing it to :func:`fit`.

When a prior field is left at its default, Phonometrica replaces it at fit time with a
data-dependent weakly informative prior scaled to the response variable (following the
approach of brms). Setting a prior explicitly disables auto-scaling for that component.

.. _prior-scale-awareness:

.. warning::

   **Match the prior scale to the response scale.**  Custom priors supplied via
   :func:`set_fixed`, :func:`set_variance`, and :func:`set_residual` are applied on the
   *link scale* of the model, which for identity-link families (Gaussian, Student-t) is
   the raw response scale.  Data on a Hz scale (e.g. F1 formant values in the hundreds)
   will have slope coefficients of similar magnitude, so a tight prior like ``N(0, 10)``
   acts as a very strong shrinkage toward zero — pulling coefficients away from their
   data-supported values and, for Student-t specifically, causing the optimizer to
   inflate ``sigma`` and push ``nu`` to its upper bound as it compensates.

   For logit-link families (Binomial, Beta) and log-link families (Poisson, Negative
   Binomial), coefficients live on a transformed scale and are typically O(1), so
   ``N(0, 10)`` is a loose prior by default.  Custom priors on these families can
   usually use moderate scales without issue.

   When in doubt, omit the ``set_fixed`` call and let the auto-scaled defaults apply
   (the constructor default is to auto-scale every field).  Phonometrica will emit a
   ``prior_warning`` on the fitted model when the residual scale of an identity-link
   fit exceeds 1.5 × sd(*y*), which reliably flags this class of misconfiguration.

.. function:: Prior()

Creates a new prior specification with all fields set to auto-scaled defaults:

* Fixed effects (slopes): Normal(0, 2.5 × sd(*y*))
* Intercept: Normal(mean(*y*), 2.5 × sd(*y*))
* Variance components: PC(2.5 × sd(*y*), 0.05)
* Residual SD: PC(2.5 × sd(*y*), 0.05)
* NB θ: Gamma(1, 0.01)
* Beta φ: Gamma(1, 0.01)

Example::

   let prior = Prior()

------------

.. function:: set_fixed(prior, mean, sd)

Sets the default Normal prior for all fixed-effect slope coefficients to Normal(``mean``,
``sd``). The intercept receives a separate prior centered on the response mean (this is
handled automatically). Disables auto-scaling for fixed effects.

.. note::

   The ``sd`` value is on the link scale of the model.  For identity-link families
   (Gaussian, Student-t) fit to data on a Hz or dB scale, a small ``sd`` such as ``10``
   strongly shrinks coefficients toward the prior mean and may produce a degenerate
   fit.  See :ref:`prior-scale-awareness` for details.

Example::

   let prior = Prior()
   set_fixed(prior, 0, 5)   // N(0, 5) for all slopes

------------

.. function:: set_fixed(prior, name, mean, sd)

Sets a Normal prior for a specific coefficient identified by ``name`` (e.g.
``"Intercept"``, ``"age"``). This overrides the default fixed-effects prior for that
coefficient only.

Example::

   let prior = Prior()
   set_fixed(prior, "Intercept", 500, 100)  // informative prior for intercept (e.g. F1 in Hz)

------------

.. function:: set_variance(prior, type, scale)
              set_variance(prior, type, param1, param2)

Sets the prior for random-effect standard deviations (variance components). ``type`` is one
of ``"pc"`` (penalized complexity), ``"half_cauchy"`` (Half-Cauchy), or ``"half_normal"``
(Half-Normal). Disables auto-scaling for variance components.

For ``"pc"``, two parameters are required: ``param1`` is the upper bound *u* and ``param2``
is the tail probability α, such that P(σ > *u*) = α. For ``"half_cauchy"`` and
``"half_normal"``, only ``scale`` (the scale parameter) is needed.

Example::

   let prior = Prior()
   set_variance(prior, "pc", 50, 0.05)         // PC prior: P(σ > 50) = 0.05
   set_variance(prior, "half_cauchy", 25)       // Half-Cauchy(25)

------------

.. function:: set_residual(prior, type, scale)
              set_residual(prior, type, param1, param2)

Sets the prior for the residual standard deviation (Gaussian family only). Same syntax as
:func:`set_variance`. Disables auto-scaling for the residual prior.

Example::

   let prior = Prior()
   set_residual(prior, "pc", 100, 0.05)

------------

.. function:: set_negbin_theta(prior, shape, rate)

Sets a Gamma(``shape``, ``rate``) prior for the negative binomial overdispersion parameter θ.

Example::

   let prior = Prior()
   set_negbin_theta(prior, 2, 0.1)  // Gamma(2, 0.1) — more informative than default

------------

.. function:: set_beta_phi(prior, shape, rate)

Sets a Gamma(``shape``, ``rate``) prior for the beta regression precision parameter φ.

Example::

   let prior = Prior()
   set_beta_phi(prior, 1, 0.01)

------------

.. function:: set_lkj(prior, eta)

Sets an LKJ prior on the correlation matrix of correlated random effects (e.g. a random
intercept and random slope for the same grouping factor). The density is

.. math::

   p(R \mid \eta) \propto |R|^{\eta - 1}

where *R* is the correlation matrix derived from the random-effect covariance.

``eta`` must be strictly positive. The default is ``1.0``, which is uniform over
correlation matrices (equivalent to placing no prior on the correlation structure).
Values greater than 1 concentrate posterior mass toward the identity (favouring
independent random terms); values less than 1 push toward strongly correlated random
terms. ``eta = 2`` is a common mildly regularising choice — it downweights extreme
correlations (±1) that would indicate a degenerate covariance — and matches the
convention used in brms and in Vasishth et al. (2018).

The prior is only active when a grouping factor has two or more random terms (e.g. an
intercept plus a slope); for intercept-only random effects it has no effect.

Reference: Lewandowski, Kurowicka & Joe (2009), *J. Multivariate Anal.*

Example::

   let prior = Prior()
   set_lkj(prior, 2)   // LKJ(2): mildly regularising


.. _bayesian-model-fields:

Bayesian model fields
---------------------

When a model is fitted with Bayesian estimation (by passing a ``Prior`` to :func:`fit`),
the following additional fields are available on the ``Model`` object. These fields are
empty arrays for frequentist models.

.. attribute:: estimation

A string indicating the estimation method: ``"Frequentist"`` or ``"Bayesian"``.

.. attribute:: posterior_mean

Array of posterior means for each fixed-effect coefficient (length = number of fixed effects).
This is the quantity reported as "Estimate" in the Bayesian summary.

.. attribute:: posterior_mode

Array of posterior modes (MAP estimates) for each fixed-effect coefficient. For models with
grid integration, this is the conditional mode at the posterior mode of the hyperparameters
θ*. For symmetric posteriors, the mode equals the mean; they may differ for small samples.

.. attribute:: posterior_median

Array of posterior medians (0.5 quantile of the marginal posterior) for each fixed-effect
coefficient. Computed from the mixture CDF for grid-integrated models.

.. attribute:: posterior_sd

Array of posterior standard deviations for each fixed-effect coefficient. This is the
quantity reported as "Est.Error" in the Bayesian summary.

.. attribute:: ci_lower

Array of lower bounds of the 95% credible interval for each fixed-effect coefficient.

.. attribute:: ci_upper

Array of upper bounds of the 95% credible interval for each fixed-effect coefficient.

.. attribute:: pd

Array of probability-of-direction values for each fixed-effect coefficient. The pd is
defined as max(P(β > 0), P(β < 0)) and ranges from 0.5 (no evidence for either direction)
to 1.0 (certainty). It is the Bayesian counterpart to a two-sided *p*-value.

Example::

   let prior = Prior()
   let m = fit("f1 ~ vowel + gender + (1|speaker)", ds, prior)

   print "Estimation: " & m.estimation           // "Bayesian"
   print "Intercept posterior mean: " & m.posterior_mean[1]
   print "Intercept 95% CrI: [" & m.ci_lower[1] & ", " & m.ci_upper[1] & "]"
   print "Intercept pd: " & m.pd[1]

   // Frequentist fields (AIC, BIC, loglik) remain available
   print "AIC = " & m.aic

.. note::

   WAIC, LOO-IC, and log-marginal likelihood are available both through :func:`summarize`
   and as individual model fields (``waic``, ``loo_ic``, ``log_marginal``, etc.; see
   `Model fields`_).


References
----------

.. [HAR2022] Hartig, Florian. 2022. DHARMa: Residual Diagnostics for Hierarchical (Multi-Level / Mixed) Regression Models. R package. https://CRAN.R-project.org/package=DHARMa.
