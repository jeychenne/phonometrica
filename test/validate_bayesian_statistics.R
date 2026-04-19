# ============================================================
# R validation suite for Phonometrica's BAYESIAN regression
# Mirrors validate_bayesian_statistics.phon
#
# Reference packages:
#   brms  (primary)  — MCMC, all families supported
#   INLA  (optional) — Laplace + grid, Gaussian / Binomial /
#                      Poisson only (skipped where parameterisation
#                      differs materially or the family is not natively
#                      supported)
#
# Run from the directory containing the data/ folder:
#     R CMD BATCH validate_bayesian_statistics.R
# or interactively:
#     source("validate_bayesian_statistics.R")
# ============================================================

# ── Packages ────────────────────────────────────────────────
# brms is required; INLA is optional.  INLA is not on CRAN; if you want it:
#     install.packages("INLA",
#         repos = c(getOption("repos"),
#                   INLA = "https://inla.r-inla-download.org/R/stable"),
#         dep = TRUE)

library(brms)
use_inla <- requireNamespace("INLA", quietly = TRUE)
if (use_inla) {
    library(INLA)
    cat("INLA found — running INLA checks where supported.\n")
} else {
    cat("INLA not found — skipping INLA checks. Install from\n")
    cat("  https://www.r-inla.org/download-install  if you want them.\n")
}

options(width = 120)

datadir <- "data"

# ── Priors (match Phonometrica's set_* calls in the .phon script) ──
#
# Phonometrica:          brms equivalent:
#   Normal(0, 10) on β     prior(normal(0, 10), class = b)
#                          prior(normal(0, 10), class = Intercept)
#   PC(u=1, α=0.05)        exponential(rate = -log(0.05)/1 ≈ 2.9957)
#     on random SDs        prior(..., class = sd)
#   PC(u=1, α=0.05)        prior(exponential(2.9957), class = sigma)
#     on residual SD
#   Gamma(1, 0.01) θ       prior(gamma(1, 0.01), class = shape)
#   Gamma(1, 0.01) φ       prior(gamma(1, 0.01), class = phi)
#
# brms NOTE: the `class = Intercept` prior is applied on the *centered*
# design-matrix scale.  With a wide prior (sd = 10) this is numerically
# indistinguishable from applying the prior on the natural scale, so we
# use the standard syntax.  If strict parity is required, replace
#     y ~ x1 + x2 + ...
# with
#     y ~ 0 + Intercept + x1 + x2 + ...
# and drop the `class = Intercept` prior line.

pc_rate <- -log(0.05) / 1   # ≈ 2.9957 — PC(1, 0.05) ⇒ Exp(2.9957) on σ

common_prior_fixed <- c(
    prior(normal(0, 10), class = b),
    prior(normal(0, 10), class = Intercept)
)

# Gaussian adds a residual-SD prior
prior_gaussian <- c(
    common_prior_fixed,
    prior(exponential(2.9957), class = sigma),
    prior(exponential(2.9957), class = sd)   # dropped for fixed-only models
)
prior_gaussian_fixed_only <- c(
    common_prior_fixed,
    prior(exponential(2.9957), class = sigma)
)

prior_binomial_fixed <- common_prior_fixed
prior_binomial_mixed <- c(common_prior_fixed,
                          prior(exponential(2.9957), class = sd))

prior_poisson_fixed  <- common_prior_fixed
prior_poisson_mixed  <- c(common_prior_fixed,
                          prior(exponential(2.9957), class = sd))

prior_negbin_fixed <- c(common_prior_fixed,
                        prior(gamma(1, 0.01), class = shape))
prior_negbin_mixed <- c(common_prior_fixed,
                        prior(gamma(1, 0.01), class = shape),
                        prior(exponential(2.9957), class = sd))

prior_beta_fixed <- c(common_prior_fixed,
                      prior(gamma(1, 0.01), class = phi))
prior_beta_mixed <- c(common_prior_fixed,
                      prior(gamma(1, 0.01), class = phi),
                      prior(exponential(2.9957), class = sd))

# Student-t: leave sigma/nu to brms defaults (mirrors .phon which has no
# setter for these).  Fixed + random-effect SDs share the common prior.
prior_student_fixed <- common_prior_fixed
prior_student_mixed <- c(common_prior_fixed,
                         prior(exponential(2.9957), class = sd))

# ── MCMC defaults ────────────────────────────────────────────
# 4 chains × 2000 iter (1000 warmup + 1000 sampling) = 4000 post-warmup draws
SEED <- 42
CHAINS <- 4
ITER <- 2000
CORES <- 4
ADAPT_DELTA <- 0.95

# ── INLA PC prior helper ─────────────────────────────────────
# pc.prec is parameterised as P(SD > u) = alpha, matching Phonometrica.
pc_hyper <- list(prec = list(prior = "pc.prec", param = c(1, 0.05)))

# ── Helpers ──────────────────────────────────────────────────

print_header <- function(family, model) {
    cat("\n")
    cat(strrep("=", 70), "\n")
    cat(sprintf(" %s — %s\n", family, model))
    cat(strrep("=", 70), "\n\n")
}

print_subheader <- function(label) {
    cat("\n--- ", label, " ---\n", sep = "")
}

fit_brms <- function(formula, data, family, prior, ...) {
    tryCatch(
        brm(formula, data = data, family = family, prior = prior,
            chains = CHAINS, iter = ITER, cores = CORES, seed = SEED,
            control = list(adapt_delta = ADAPT_DELTA),
            refresh = 0, silent = 2, ...),
        error = function(e) {
            cat("brms FAILED:", conditionMessage(e), "\n")
            NULL
        }
    )
}

print_brms <- function(m) {
    if (is.null(m)) return(invisible(NULL))
    print(summary(m))
    # WAIC / LOO with SEs (the brms wrappers dispatch to loo::waic / loo::loo)
    w <- tryCatch(waic(m), error = function(e) NULL)
    l <- tryCatch(loo(m),  error = function(e) NULL)
    if (!is.null(w)) {
        est <- w$estimates
        cat(sprintf("WAIC   = %.1f  SE = %.1f  p_WAIC = %.1f\n",
                    est["waic","Estimate"],   est["waic","SE"],
                    est["p_waic","Estimate"]))
    }
    if (!is.null(l)) {
        est <- l$estimates
        cat(sprintf("LOO-IC = %.1f  SE = %.1f  p_LOO  = %.1f\n",
                    est["looic","Estimate"],  est["looic","SE"],
                    est["p_loo","Estimate"]))
        pk <- l$diagnostics$pareto_k
        cat(sprintf("Pareto k: good (<0.5): %d  ok (0.5-0.7): %d  bad (>0.7): %d\n",
                    sum(pk < 0.5), sum(pk >= 0.5 & pk < 0.7), sum(pk >= 0.7)))
    }
    cat("\n")
}

# INLA wrapper: linear predictor built from a formula with + f(grp, model="iid", ...)
# Caller passes the FULL formula already, including f() terms.
fit_inla <- function(formula, data, family, control.family = list(),
                     Ntrials = NULL) {
    ctrl.fixed <- list(mean = 0, prec = 1/100,
                       mean.intercept = 0, prec.intercept = 1/100)
    ctrl.compute <- list(waic = TRUE, dic = TRUE, cpo = TRUE,
                         config = TRUE, return.marginals = TRUE)
    args <- list(formula = formula, data = data, family = family,
                 control.fixed = ctrl.fixed,
                 control.compute = ctrl.compute,
                 control.family = control.family,
                 verbose = FALSE)
    if (!is.null(Ntrials)) args$Ntrials <- Ntrials
    tryCatch(do.call(INLA::inla, args),
             error = function(e) {
                 cat("INLA FAILED:", conditionMessage(e), "\n"); NULL })
}

print_inla <- function(m) {
    if (is.null(m)) return(invisible(NULL))
    cat("INLA fixed effects (posterior mean and 95% CrI):\n")
    print(round(m$summary.fixed[, c("mean", "sd", "0.025quant", "0.975quant")], 4))
    if (length(m$summary.hyperpar) > 0) {
        cat("\nINLA hyperparameters (post. mean / 95% CrI):\n")
        print(round(m$summary.hyperpar[, c("mean", "sd", "0.025quant", "0.975quant")], 4))
    }
    if (!is.null(m$waic)) {
        cat(sprintf("\nWAIC = %.1f  p_eff = %.1f\n",
                    m$waic$waic, m$waic$p.eff))
    }
    # INLA does not report PSIS-LOO natively; we report mlik and a CPO-LPD.
    if (!is.null(m$mlik)) {
        cat(sprintf("log marginal (Gaussian integration) = %.2f\n",
                    m$mlik[1, 1]))
    }
    if (!is.null(m$cpo) && !is.null(m$cpo$cpo)) {
        cpo <- m$cpo$cpo
        cpo <- cpo[is.finite(cpo) & cpo > 0]
        if (length(cpo) > 0) {
            cat(sprintf("Sum log CPO (proxy for LPPD) = %.2f\n",
                        sum(log(cpo))))
        }
    }
    cat("\n")
}

# =====================================================================
# 1. GAUSSIAN — F1 values
# =====================================================================

d <- read.delim(file.path(datadir, "gaussian_f1.csv"))
d$vowel  <- factor(d$vowel,  levels = c("a", "i", "u"))
d$gender <- factor(d$gender, levels = c("F", "M"))

print_header("GAUSSIAN (brms)", "M1: f1 ~ vowel + gender")
m <- fit_brms(f1 ~ vowel + gender, d, gaussian(), prior_gaussian_fixed_only)
print_brms(m)

if (use_inla) {
    print_header("GAUSSIAN (INLA)", "M1: f1 ~ vowel + gender")
    mI <- fit_inla(f1 ~ vowel + gender, d, "gaussian",
                   control.family = list(hyper = pc_hyper))
    print_inla(mI)
}

print_header("GAUSSIAN (brms)", "M2: f1 ~ vowel + gender + (1|speaker)")
m <- fit_brms(f1 ~ vowel + gender + (1 | speaker), d, gaussian(), prior_gaussian)
print_brms(m)

if (use_inla) {
    print_header("GAUSSIAN (INLA)", "M2: f1 ~ vowel + gender + (1|speaker)")
    mI <- fit_inla(f1 ~ vowel + gender + f(speaker, model = "iid",
                                            hyper = pc_hyper),
                   d, "gaussian",
                   control.family = list(hyper = pc_hyper))
    print_inla(mI)
}

print_header("GAUSSIAN (brms)", "M3: f1 ~ vowel + gender + (1|speaker) + (1|word)")
m <- fit_brms(f1 ~ vowel + gender + (1 | speaker) + (1 | word),
              d, gaussian(), prior_gaussian)
print_brms(m)

if (use_inla) {
    print_header("GAUSSIAN (INLA)", "M3: f1 ~ vowel + gender + (1|speaker) + (1|word)")
    mI <- fit_inla(f1 ~ vowel + gender +
                        f(speaker, model = "iid", hyper = pc_hyper) +
                        f(word,    model = "iid", hyper = pc_hyper),
                   d, "gaussian",
                   control.family = list(hyper = pc_hyper))
    print_inla(mI)
}

print_header("GAUSSIAN (brms)", "M4: f1 ~ vowel + (1 + gender|speaker)")
m <- fit_brms(f1 ~ vowel + (1 + gender | speaker), d, gaussian(), prior_gaussian)
print_brms(m)

# INLA skipped for M4 — random slopes need iid2d/iidkd which has a
# different parameterisation of the covariance prior than Phonometrica's
# LKJ(η=1) + PC(σ) setup.  Not a meaningful cross-check.
if (use_inla) {
    cat("(INLA skipped for M4 — random-slope covariance parameterisation differs.)\n\n")
}

# =====================================================================
# 2. BINOMIAL — Schwa realization
# =====================================================================

d <- read.delim(file.path(datadir, "binomial_schwa.csv"))
d$position <- factor(d$position, levels = c("final", "initial", "medial"))
d$style    <- factor(d$style,    levels = c("casual", "formal"))

print_header("BINOMIAL (brms)", "M1: realized ~ position + style")
m <- fit_brms(realized ~ position + style, d, bernoulli(), prior_binomial_fixed)
print_brms(m)

if (use_inla) {
    print_header("BINOMIAL (INLA)", "M1: realized ~ position + style")
    mI <- fit_inla(realized ~ position + style, d, "binomial", Ntrials = 1)
    print_inla(mI)
}

print_header("BINOMIAL (brms)", "M2: realized ~ position + style + (1|speaker)")
m <- fit_brms(realized ~ position + style + (1 | speaker),
              d, bernoulli(), prior_binomial_mixed)
print_brms(m)

if (use_inla) {
    print_header("BINOMIAL (INLA)", "M2: realized ~ position + style + (1|speaker)")
    mI <- fit_inla(realized ~ position + style +
                        f(speaker, model = "iid", hyper = pc_hyper),
                   d, "binomial", Ntrials = 1)
    print_inla(mI)
}

print_header("BINOMIAL (brms)", "M3: realized ~ position + style + (1|speaker) + (1|word)")
m <- fit_brms(realized ~ position + style + (1 | speaker) + (1 | word),
              d, bernoulli(), prior_binomial_mixed)
print_brms(m)

if (use_inla) {
    print_header("BINOMIAL (INLA)", "M3: realized ~ position + style + (1|speaker) + (1|word)")
    mI <- fit_inla(realized ~ position + style +
                        f(speaker, model = "iid", hyper = pc_hyper) +
                        f(word,    model = "iid", hyper = pc_hyper),
                   d, "binomial", Ntrials = 1)
    print_inla(mI)
}

print_header("BINOMIAL (brms)", "M4: realized ~ position + (1 + style|speaker)")
m <- fit_brms(realized ~ position + (1 + style | speaker),
              d, bernoulli(), prior_binomial_mixed)
print_brms(m)

if (use_inla) {
    cat("(INLA skipped for M4 — random-slope covariance parameterisation differs.)\n\n")
}

# =====================================================================
# 3. POISSON — Disfluency counts
# =====================================================================

d <- read.delim(file.path(datadir, "poisson_disfluency.csv"))
d$task <- factor(d$task, levels = c("conversation", "reading"))
d$age  <- factor(d$age,  levels = c("old", "young"))

print_header("POISSON (brms)", "M1: count ~ task + age")
m <- fit_brms(count ~ task + age, d, poisson(), prior_poisson_fixed)
print_brms(m)

if (use_inla) {
    print_header("POISSON (INLA)", "M1: count ~ task + age")
    mI <- fit_inla(count ~ task + age, d, "poisson")
    print_inla(mI)
}

print_header("POISSON (brms)", "M2: count ~ task + age + (1|speaker)")
m <- fit_brms(count ~ task + age + (1 | speaker),
              d, poisson(), prior_poisson_mixed)
print_brms(m)

if (use_inla) {
    print_header("POISSON (INLA)", "M2: count ~ task + age + (1|speaker)")
    mI <- fit_inla(count ~ task + age +
                        f(speaker, model = "iid", hyper = pc_hyper),
                   d, "poisson")
    print_inla(mI)
}

print_header("POISSON (brms)", "M3: count ~ task + age + (1|speaker) + (1|word)")
m <- fit_brms(count ~ task + age + (1 | speaker) + (1 | word),
              d, poisson(), prior_poisson_mixed)
print_brms(m)

if (use_inla) {
    print_header("POISSON (INLA)", "M3: count ~ task + age + (1|speaker) + (1|word)")
    mI <- fit_inla(count ~ task + age +
                        f(speaker, model = "iid", hyper = pc_hyper) +
                        f(word,    model = "iid", hyper = pc_hyper),
                   d, "poisson")
    print_inla(mI)
}

print_header("POISSON (brms)", "M4: count ~ task + (1 + age|speaker)")
m <- fit_brms(count ~ task + (1 + age | speaker),
              d, poisson(), prior_poisson_mixed)
print_brms(m)

if (use_inla) {
    cat("(INLA skipped for M4 — random-slope covariance parameterisation differs.)\n\n")
}

# =====================================================================
# 4. NEGATIVE BINOMIAL — Hesitation counts
# =====================================================================
#
# INLA is skipped for NB: its default hyperparameter is log(1/size) with a
# loggamma prior, which cannot be made numerically equivalent to
# Phonometrica's Gamma(1, 0.01) on θ without a Jacobian-adjusted custom
# prior.  brms's `shape` matches Phonometrica's θ directly.

d <- read.delim(file.path(datadir, "negbin_hesitation.csv"))
d$complexity <- factor(d$complexity, levels = c("complex", "simple"))
d$stress     <- factor(d$stress,     levels = c("stressed", "unstressed"))

print_header("NEGBIN (brms)", "M1: hesitations ~ complexity + stress")
m <- fit_brms(hesitations ~ complexity + stress,
              d, negbinomial(), prior_negbin_fixed)
print_brms(m)

print_header("NEGBIN (brms)", "M2: hesitations ~ complexity + stress + (1|speaker)")
m <- fit_brms(hesitations ~ complexity + stress + (1 | speaker),
              d, negbinomial(), prior_negbin_mixed)
print_brms(m)

print_header("NEGBIN (brms)", "M3: hesitations ~ complexity + stress + (1|speaker) + (1|word)")
m <- fit_brms(hesitations ~ complexity + stress + (1 | speaker) + (1 | word),
              d, negbinomial(), prior_negbin_mixed)
print_brms(m)

print_header("NEGBIN (brms)", "M4: hesitations ~ complexity + (1 + stress|speaker)")
m <- fit_brms(hesitations ~ complexity + (1 + stress | speaker),
              d, negbinomial(), prior_negbin_mixed)
print_brms(m)

if (use_inla) {
    cat("(INLA skipped for NB — hyperparameter parameterisation differs from Phonometrica.)\n\n")
}

# =====================================================================
# 5. BETA — Voicing proportion
# =====================================================================
#
# INLA's "beta" family parameterises the precision on the log scale with a
# loggamma prior — not a Gamma(1, 0.01) on φ — so it is skipped.

d <- read.delim(file.path(datadir, "beta_voicing.csv"))
d$consonant <- factor(d$consonant, levels = c("b", "d", "p", "t"))
d$position  <- factor(d$position,  levels = c("final", "initial", "medial"))

print_header("BETA (brms)", "M1: voicing ~ consonant + position")
m <- fit_brms(voicing ~ consonant + position, d, Beta(), prior_beta_fixed)
print_brms(m)

print_header("BETA (brms)", "M2: voicing ~ consonant + position + (1|speaker)")
m <- fit_brms(voicing ~ consonant + position + (1 | speaker),
              d, Beta(), prior_beta_mixed)
print_brms(m)

print_header("BETA (brms)", "M3: voicing ~ consonant + position + (1|speaker) + (1|word)")
m <- fit_brms(voicing ~ consonant + position + (1 | speaker) + (1 | word),
              d, Beta(), prior_beta_mixed)
print_brms(m)

print_header("BETA (brms)", "M4: voicing ~ consonant + (1 + position|speaker)")
m <- fit_brms(voicing ~ consonant + (1 + position | speaker),
              d, Beta(), prior_beta_mixed)
print_brms(m)

if (use_inla) {
    cat("(INLA skipped for Beta — precision prior parameterisation differs.)\n\n")
}

# =====================================================================
# 6. STUDENT T (ROBUST) — F1 values with tracking errors
# =====================================================================
#
# INLA has no native Student-t likelihood for the response (only as a
# latent model), so it is skipped.  Priors on sigma and nu use brms's
# defaults (student_t(3, 0, 2.5) on sigma, gamma(2, 0.1) on nu), mirroring
# the .phon script which also uses defaults for these parameters.

d <- read.delim(file.path(datadir, "student_f1_robust.csv"))
d$vowel  <- factor(d$vowel,  levels = c("a", "i", "u"))
d$gender <- factor(d$gender, levels = c("F", "M"))

print_header("STUDENT (brms)", "M1: f1 ~ vowel + gender")
m <- fit_brms(f1 ~ vowel + gender, d, student(), prior_student_fixed)
print_brms(m)

print_header("STUDENT (brms)", "M2: f1 ~ vowel + gender + (1|speaker)")
m <- fit_brms(f1 ~ vowel + gender + (1 | speaker),
              d, student(), prior_student_mixed)
print_brms(m)

print_header("STUDENT (brms)", "M3: f1 ~ vowel + gender + (1|speaker) + (1|word)")
m <- fit_brms(f1 ~ vowel + gender + (1 | speaker) + (1 | word),
              d, student(), prior_student_mixed)
print_brms(m)

print_header("STUDENT (brms)", "M4: f1 ~ vowel + (1 + gender|speaker)")
m <- fit_brms(f1 ~ vowel + (1 + gender | speaker),
              d, student(), prior_student_mixed)
print_brms(m)

if (use_inla) {
    cat("(INLA skipped for Student-t — no native response-level family.)\n\n")
}

# 6b. STUDENT T MILD (nu=10) — same design, milder tails
# =====================================================================

d2 <- read.delim(file.path(datadir, "student_f1_mild.csv"))
d2$vowel  <- factor(d2$vowel,  levels = c("a", "i", "u"))
d2$gender <- factor(d2$gender, levels = c("F", "M"))

print_header("STUDENT MILD (brms)", "M1: f1 ~ vowel + gender")
m <- fit_brms(f1 ~ vowel + gender, d2, student(), prior_student_fixed)
print_brms(m)

print_header("STUDENT MILD (brms)", "M2: f1 ~ vowel + gender + (1|speaker)")
m <- fit_brms(f1 ~ vowel + gender + (1 | speaker),
              d2, student(), prior_student_mixed)
print_brms(m)

cat("\n====================================================================\n")
cat(" Bayesian validation suite — complete.\n")
cat("====================================================================\n")

# ── Tolerance guidance (for the human reader) ───────────────
#
# Expected agreement between Phonometrica's INLA-style grid integration
# and the reference implementations (MCMC via brms, Laplace/grid via INLA):
#
#   * Fixed-effect posterior means        : within  ~2% or 0.1·SE
#   * Fixed-effect posterior SDs          : within  ~5%
#   * 95% credible-interval bounds        : within  ~5%
#   * Random-effect SD posterior means    : within ~10%
#     (most-sensitive quantity; a larger gap here is expected for
#      sparse data or near-zero variance components because the grid
#      integration approximates the marginal more coarsely in the tail)
#   * WAIC                                : within ~1·SE(WAIC)
#   * LOO-IC vs brms LOO                  : within ~1·SE(LOO)
#   * Pareto-k > 0.7 counts               : should agree on order of
#                                           magnitude; exact counts differ
#                                           because the underlying draw
#                                           mechanism differs (posterior
#                                           grid vs. NUTS samples)
#
# Larger discrepancies flag a genuine issue worth investigating.
