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

.. function:: fit(formula, data, prior)
              fit(formula, data, family, prior)

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
available from grid integration, and Bayesian fit statistics (WAIC, LPPD, log-marginal
likelihood).

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

**Bayesian models**: WAIC comparison table and log Bayes factors. Cannot be mixed with
frequentist models.

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


Diagnostics
-----------

.. function:: dharma(model)

Computes simulation-based residual diagnostics and prints the results. Three tests
are performed:

* **Uniformity test** (Kolmogorov-Smirnov): tests whether the scaled residuals follow a uniform
  distribution, as expected if the model is correctly specified.
* **Dispersion test**: checks for over- or under-dispersion by comparing the observed variance
  of the scaled residuals to its expected value.
* **Outlier test**: counts observations whose response falls entirely outside the simulated range.

For **frequentist models**, the simulations are drawn from the fitted model (DHARMa-style
conditional simulation with 1000 replicates).

For **Bayesian models**, the function performs posterior predictive checking (PPC): coefficient
vectors are drawn from the posterior (200 replicates), new response vectors are simulated, and
the test statistics are compared between observed and simulated data. The resulting *p*-values
are Bayesian *p*-values (proportion of replicates where the simulated statistic exceeds the
observed).

Example::

   let m = fit("count ~ condition + (1|subject)", ds, "poisson")
   dharma(m)


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


.. _prior-specification:

Prior specification
-------------------

A ``Prior`` object controls the prior distributions used for Bayesian model fitting. Create
one by calling the ``Prior`` constructor, then optionally configure individual priors before
passing it to :func:`fit`.

When a prior field is left at its default, Phonometrica replaces it at fit time with a
data-dependent weakly informative prior scaled to the response variable (following the
approach of brms). Setting a prior explicitly disables auto-scaling for that component.

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

Example::

   let prior = Prior()
   set_fixed(prior, 0, 5)   // N(0, 5) for all slopes

------------

.. function:: set_fixed(prior, name, mean, sd)

Sets a Normal prior for a specific coefficient identified by ``name`` (e.g.
``"(Intercept)"``, ``"age"``). This overrides the default fixed-effects prior for that
coefficient only.

Example::

   let prior = Prior()
   set_fixed(prior, "(Intercept)", 500, 100)  // informative prior for intercept (e.g. F1 in Hz)

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

   WAIC, LPPD, and log-marginal likelihood are displayed by :func:`summarize` and in the
   GUI summary and comparison panels, but are not yet exposed as individual model fields
   in the scripting API.
