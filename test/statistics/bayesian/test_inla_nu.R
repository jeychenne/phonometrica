# ============================================================
# Does R-INLA underestimate the ν posterior spread for
# near-Gaussian data, the same way Phonometrica does?
#
# Setup
# -----
# 500 observations from a truly Gaussian linear model (so ν is
# unidentifiable from the data — the posterior should be wide).
# We fit a Student-t regression with:
#   (a) brms  — full MCMC, used as ground truth
#   (b) R-INLA — Laplace approximation, the same class of method
#               as Phonometrica's Bayesian engine
#
# Expected result if the limitation is structural to INLA
# -------------------------------------------------------
# brms  : ν posterior wide, e.g. 95% CI ≈ [5, 180]
#          mean ≈ 70–120 (dominated by the Uniform(2,200) prior
#          since the Gaussian data barely identifies ν)
# R-INLA: ν posterior narrow, e.g. 95% CI ≈ [10, 50]
#          same qualitative underdispersion as Phonometrica
# ============================================================

set.seed(42)

# ── Simulate near-Gaussian data ──────────────────────────────
n   <- 500
x1  <- rnorm(n)                           # continuous predictor
x2  <- factor(rbinom(n, 1, 0.5))          # binary predictor
y   <- 2 + 0.8 * x1 - 0.4 * (x2 == 1) +
        rnorm(n, sd = 1.2)                # truly Gaussian residuals

dat <- data.frame(y = y, x1 = x1, x2 = x2)

# ── (a) brms – MCMC ground truth ─────────────────────────────
library(brms)

fit_brms <- brm(
  y ~ x1 + x2,
  data   = dat,
  family = student(),
  prior  = c(
    prior(uniform(2, 200), class = nu, lb = 2, ub = 200)
  ),
  chains  = 4,
  iter    = 4000,
  warmup  = 1000,
  seed    = 42,
  refresh = 500
)

nu_brms <- posterior_summary(fit_brms, variable = "nu")
cat("\n── brms ν posterior ──────────────────────────────────────\n")
cat(sprintf("  mean = %.1f\n",  nu_brms["Estimate"]))
cat(sprintf("  sd   = %.1f\n",  nu_brms["Est.Error"]))
cat(sprintf("  95%% CI = [%.1f, %.1f]\n",
            nu_brms["Q2.5"], nu_brms["Q97.5"]))

# ── (b) R-INLA – Laplace approximation ───────────────────────
library(INLA)

# The Student-t family in R-INLA uses two hyperparameters:
#   1. Precision  τ = 1/σ²  (log-scale internally)
#   2. Degrees of freedom ν (log-scale internally)
#
# We use:
#   τ prior : loggamma(1, 5e-5)  – vague, matches typical defaults
#   ν prior : pc.dof(u=10, α=0.5) – PC prior saying
#             P(ν < 10) = 0.5, which is weakly informative and the
#             R-INLA default for this family.
#
# NOTE: The PC prior on ν is different from brms's Uniform(2,200),
# so the posteriors will not be directly comparable in absolute
# terms.  What matters for this test is the WIDTH of the ν
# posterior: a well-calibrated method should give a wide CI
# (because Gaussian data barely identifies ν), while a method
# that underestimates posterior spread gives a narrow CI.

fit_inla <- inla(
  y ~ x1 + x2,
  data   = dat,
  family = "T",
  control.family = list(
    hyper = list(
      prec = list(prior = "loggamma", param = c(1, 5e-5)),
      dof  = list(prior = "pc.dof",   param = c(10, 0.5))
    )
  ),
  control.compute = list(config = TRUE)
)

# Print available hyperparameter names so you can verify
cat("\nR-INLA hyperparameter names:\n")
print(names(fit_inla$marginals.hyperpar))

# Extract ν marginal.  The exact name varies by INLA version;
# adjust the index or string below to match what is printed above.
# Typically it is "dof for the T observations" or similar.
nu_name <- grep("dof|Degrees|df", names(fit_inla$marginals.hyperpar),
                value = TRUE, ignore.case = TRUE)[1]

if (is.na(nu_name)) {
  cat("\nCould not identify the ν hyperparameter automatically.\n")
  cat("Inspect fit_inla$marginals.hyperpar manually.\n")
} else {
  nu_marg <- fit_inla$marginals.hyperpar[[nu_name]]
  nu_mean  <- inla.emarginal(function(x) x, nu_marg)
  nu_ci    <- inla.qmarginal(c(0.025, 0.975), nu_marg)
  # approximate SD from the marginal
  nu_var   <- inla.emarginal(function(x) (x - nu_mean)^2, nu_marg)

  cat("\n── R-INLA ν posterior ────────────────────────────────────\n")
  cat(sprintf("  mean = %.1f\n",  nu_mean))
  cat(sprintf("  sd   ≈ %.1f\n",  sqrt(nu_var)))
  cat(sprintf("  95%% CI = [%.1f, %.1f]\n", nu_ci[1], nu_ci[2]))
}

# ── Summary ───────────────────────────────────────────────────
cat("\n── Interpretation ────────────────────────────────────────\n")
cat("The data are truly Gaussian, so ν is barely identified.\n")
cat("A correct method should give a wide ν posterior.\n")
cat("If R-INLA's 95% CI is substantially narrower than brms's,\n")
cat("this confirms that the underestimation is structural to\n")
cat("the INLA Laplace approximation, not specific to Phonometrica.\n")
