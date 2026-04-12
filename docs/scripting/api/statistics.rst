Statistical functions
=====================

This page describes the statistical functions that are available in Phonometrica.


Global functions
----------------

.. function:: chi2_test(X)

Computes Pearson's chi-squared (:math:`\chi^2`) test on ``X``, which must be a two-dimensional array. The *m* rows in the array represent
the *m* levels of a categorical variable, and the *n* columns represent the *n* levels of another categorical variable.
Each cell represents the unnormalized frequency count for the combination of the two variables. This test evaluates the
null hypothesis that the two variables are independent.

This function returns an object with the following fields:

* ``chi2``: the :math:`\chi^2` value
* ``df``: the number of degrees of freedom
* ``p``: the p-value

See also: :func:`report_chi2`

------------

.. function:: corr(x, y)

Calculates Pearson's correlation coefficient between samples ``x`` and ``y``, which must be one-dimensional arrays with the same size.

------------

.. function:: cov(x, y)

Calculates the covariance between samples ``x`` and ``y``, which must be one-dimensional arrays with the same size.

------------

.. function:: f_test(x, y [, alternative])

Computes the F-test on ``x`` and ``y`` which must be one-dimensional arrays. This test evaluates the null hypothesis that samples
``x`` and ``y`` have the same variance.

If ``alternative`` is specified, it must be one of the following strings: ``"two-tailed"`` performs a two-tailed test (default), ``"less"`` performs a lef-tailed
test and ``"greater"`` performs a right-tailed test.
This function returns an object with the following fields:

* ``f``: the *F* statistic, which is the ratio between the variance of ``x`` and the variance of ``y``
* ``df``: the number of degrees of freedom
* ``p``: the p-value

------------

.. function:: fit(formula, data [, family])

Fits a statistical model from a formula string and a data table (concordance or dataset). This is the main entry
point for model fitting in Phonometrica.

``formula`` is an R-style formula string (e.g. ``"f1 ~ vowel + gender + (1|speaker)"``). ``data`` is a
:doc:`DataTable <table>` object (a concordance or dataset). ``family`` is an optional string specifying the
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

Returns a Model object with the following fields:

* ``formula``: the formula string
* ``family``: the family name (e.g. ``"gaussian"``, ``"beta"``, ``"student"``)
* ``link``: the link function name (e.g. ``"identity"``, ``"logit"``, ``"log"``)
* ``nobs``: number of observations
* ``aic``: Akaike Information Criterion
* ``bic``: Bayesian Information Criterion
* ``loglik``: log-likelihood at convergence
* ``deviance``: residual deviance
* ``r2``: R² (Gaussian fixed-effects models only)
* ``adj_r2``: adjusted R² (Gaussian fixed-effects models only)
* ``r2_marginal``: Nakagawa marginal R² (mixed models only)
* ``r2_conditional``: Nakagawa conditional R² (mixed models only)
* ``rse``: residual standard error (Gaussian only)
* ``df``: residual degrees of freedom
* ``theta``: overdispersion parameter (negative binomial only; 0 otherwise)
* ``phi``: precision parameter (beta only; 0 otherwise)
* ``sigma``: scale parameter (Student *t* only; 0 otherwise)
* ``nu``: degrees of freedom (Student *t* only; 0 otherwise)
* ``converged``: Boolean indicating whether the optimizer converged
* ``niter``: number of iterations (0 for OLS)

Example::

   let ds = get_dataset("my_data")
   let m = fit("f1 ~ vowel + gender + (1|speaker)", ds)
   summarize m
   print "AIC = " & m.aic

   let m2 = fit("voicing ~ consonant + position + (1|speaker)", ds, "beta")
   summarize m2
   print "phi = " & m2.phi

   let m3 = fit("f1 ~ vowel + gender + (1|speaker)", ds, "student")
   summarize m3
   print "sigma = " & m3.sigma
   print "nu    = " & m3.nu

------------

.. function:: summarize(model)

Prints a summary of a fitted Model object, including fixed-effects coefficients (estimates,
standard errors, z/t-values, and p-values), random-effects variance components (if present),
and overall fit statistics (AIC, BIC, log-likelihood).

Example::

   let m = fit("f1 ~ vowel + (1|speaker)", ds)
   summarize m

------------

.. function:: compare(model1, model2)

Compares two fitted models using a likelihood-ratio test (LRT). Prints a table of information
criteria (AIC, BIC, log-likelihood, deviance) and the LRT chi-squared statistic with p-value.
The models should be nested (one should be a special case of the other).

Example::

   let m1 = fit("f1 ~ vowel + (1|speaker)", ds)
   let m2 = fit("f1 ~ vowel + gender + (1|speaker)", ds)
   compare m1 m2

------------

.. function:: coef(model)

Returns the array of estimated fixed-effects coefficients from a fitted model.

------------

.. function:: nobs(model)

Returns the number of observations used to fit the model.

------------

.. function:: aic(model)

Returns the Akaike Information Criterion of a fitted model.

------------

.. function:: bic(model)

Returns the Bayesian Information Criterion of a fitted model.

------------

.. function:: loglik(model)

Returns the log-likelihood at convergence of a fitted model.

------------

.. function:: fitted(model)

Returns the array of fitted values from a model.

------------

.. function:: residuals(model)

Returns the array of response residuals (observed − fitted) from a model.

------------

.. function:: lm(y, X)

Fits a linear regression model. ``y`` is a set of N observations for a continuous outcome, and ``X`` is an N by M matrix for a model with M regression
coefficients, including the intercept which must be the first coefficient. (In general, it should be a column of 1's.)

This function returns an object with the following fields:

* ``beta``: an array of estimates for the regression coefficients. The first entry is the intercept
* ``se``: an array representing the standard errors of the regression coefficients
* ``t``: an array of t-values for the regression coefficients (``t[i]`` is the t-value for ``beta[i]``)
* ``p``: an array of p-values for a t-test which evaluates the null hypothesis that each regression coefficient is equal to 0 (``p[i]`` is the p-value for ``beta[i]``)
* ``r2``: the :math:`R^2` value, which is the proportion of variance explained by the model
* ``adj_r2``: the adjusted :math:`R^2` value, which takes into account the number of predictors in the model.

Note: the model is estimated by minimizing the sum of squared errors. It is fitted analytically using Singular Value Decomposition.

See also: :func:`logit`, :func:`poisson`

------------

.. function:: logit(y, X [, max_iter])

Fits a logistic regression model. ``y`` is a set of N binary observations (either 0 or 1), and ``X`` is an N by M matrix for a model with M regression
coefficients, including the intercept which must be the first coefficient. (In general, it should be a column of 1's.)
If ``max_iter`` is provided, it indicates the maximum number of iterations that the solver should perform to estimate the coefficients (200 by default).

This function returns an object with the following fields:

* ``beta``: an array of estimates for the regression coefficients. The first entry is the intercept
* ``se``: an array representing the standard errors of the regression coefficients
* ``z``: an array of z-values for the regression coefficients (``z[i]`` is the z-value for ``beta[i]``)
* ``p``: an array of p-values for a Wald test which evaluates the null hypothesis that each regression coefficient is equal to 0 (``p[i]`` is the p-value for ``beta[i]``)
* ``niter``: the number of iterations performed by the numerical solver
* ``converged``: a Boolean value indicating whether the solver has converged to a solution. It is ``true`` if ``niter < max_iter``

Note: the model is fitted numerically using the Limited-memory Broyden–Fletcher–Goldfarb–Shanno (L-BFGS) approximation method.

See also: :func:`lm`, :func:`poisson`

------------

.. function:: mean(x [, dim])

Returns the mean of the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the mean over the given dimension in a two dimension array. If dim is equal to 1, the calculation is performed
over rows. If it is equal to 2, it is performed over columns.

------------

.. function:: poisson(y, X [, robust [, max_iter]])

Fits a Poisson regression model. ``y`` is a set of N observations which represent count data (i.e. non-negative integers), and ``X`` is an N by M matrix for a model with M regression
coefficients, including the intercept which must be the first coefficient. (In general, it should be a column of 1's.) If ``robust`` is
``true`` (it is ``false`` by default), Phonometrica will use the so-called "robust variance sandwich estimator" to adjust the standard errors for mild violations of the assumption that the mean is equal to the variance.
If ``max_iter`` is provided, it indicates the maximum number of iterations that the solver should perform to estimate the coefficients (200 by default).

This function returns an object with the following fields:

* ``beta``: an array of estimates for the regression coefficients. The first entry is the intercept
* ``se``: an array representing the standard errors of the regression coefficients
* ``z``: an array of z-values for the regression coefficients (``z[i]`` is the z-value for ``beta[i]``)
* ``p``: an array of p-values for a Wald test which evaluates the null hypothesis that each regression coefficient is equal to 0 (``p[i]`` is the p-value for ``beta[i]``)
* ``niter``: the number of iterations performed by the numerical solver
* ``converged``: a Boolean value indicating whether the solver has converged to a solution. It is ``true`` if ``niter < max_iter``

Note: the model is fitted numerically using the Limited-memory Broyden–Fletcher–Goldfarb–Shanno (L-BFGS) approximation method.

See also: :func:`lm`, :func:`logit`

------------

.. function:: report_chi2(X)

Computes and reports Pearson's chi-squared test on ``X``, which must be a two-dimensional array. This is a convenience wrapper
over ``chi2_test()``.

See also: :func:`chi2_test`

------------

.. function:: std(x [, dim])

Returns the standard deviation of the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the standard deviation over the given dimension in a two dimension array. If dim is equal to 1, the calculation is performed
over rows. If it is equal to 2, it is performed over columns.

See also: :func:`vrc`, :func:`mean`

------------

.. function:: sum(x [, dim])

Returns the sum of the elements in the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the sum over the given dimension in a two dimension array. If dim is equal to 1, the summation is performed
over rows. If it is equal to 2, summation is performed over columns.

------------

.. function:: t_test(x, y [, equal_variance, [, alternative]])

Computes a two-sample independent t-test for the mean between the samples ``x`` and ``y``, which must be one-dimensional
arrays. This test evaluates the null hypothesis that samples ``x`` and ``y`` have equal means.

If ``equal_variance`` is true, the variance of the two samples is assumed to be equal and Student's t-test is calculated,
using the pooled standard error. If ``equal_variance`` is false (default), Welch's t-test is used instead.

If ``alternative`` is specified, it must be one of the following strings: ``"two-tailed"`` performs a two-tailed test (default),
``"less"`` performs a lef-tailed test and ``"greater"`` performs a right-tailed test.
This function returns an object with the following fields:

* ``t``: the *t* statistic
* ``df1``: the number of degrees of freedom of ``x``
* ``df2``: the number of degrees of freedom of ``y``
* ``p``: the p-value


See also: :func:`t_test1`

------------

.. function:: t_test1(x, mu [, alternative])

Computes a one-sample t-test for the sample ``x``, which must be a one-dimensional array. This test evaluates the null
 hypothesis that the mean of sample ``x`` is equal to the theoretical mean ``mu``.

If ``alternative`` is specified, it must be one of the following strings: ``"two-tailed"`` performs a two-tailed test (default),
``"less"`` performs a lef-tailed test and ``"greater"`` performs a right-tailed test.
This function returns an object with the following fields:

* ``t``: the *t* statistic
* ``df``: the number of degrees of freedom
* ``p``: the p-value

See also: :func:`t_test`

------------

.. function:: vrc(x [, dim])

Returns the sample variance of the array ``x``. If ``dim`` is specified, returns an ``Array`` in which each element
represents the variance over the given dimension in a two dimension array. If dim is equal to 1, the calculation is performed
over rows. If it is equal to 2, it is performed over columns.

See also: :func:`std`

------------