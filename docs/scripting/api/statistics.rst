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

Fits a statistical model from a formula string and a data table (concordance or dataset). This is the main entry
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

.. function:: summarize(model)

Prints a summary of a fitted ``Model`` object, including fixed-effects coefficients (estimates,
standard errors, z/t-values, and p-values), random-effects variance components (if present),
and overall fit statistics (AIC, BIC, log-likelihood).

Example::

   let m = fit("f1 ~ vowel + (1|speaker)", ds)
   summarize(m)

------------

.. function:: get_coef(model)

Prints a formatted table of the estimated fixed-effects coefficients and returns the coefficient
array from a fitted model.

------------

.. function:: compare(model1, model2)

Compares two fitted models using a likelihood-ratio test (LRT). Prints a table of information
criteria (AIC, BIC, log-likelihood, deviance) and the LRT chi-squared statistic with p-value.
The models should be nested (one should be a special case of the other).

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

Computes DHARMa-style simulation-based residual diagnostics and prints the results. Three tests
are performed:

* **Uniformity test** (Kolmogorov-Smirnov): tests whether the scaled residuals follow a uniform
  distribution, as expected if the model is correctly specified.
* **Dispersion test**: checks for over- or under-dispersion by comparing the observed variance
  of the scaled residuals to its expected value.
* **Outlier test**: counts observations whose response falls entirely outside the simulated range.

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
