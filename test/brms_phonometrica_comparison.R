# ============================================================================
#  brms vs Phonometrica Bayesian comparison
# ============================================================================
#
#  Compares Phonometrica's Laplace + CCD-grid Bayesian engine against full
#  HMC sampling via brms/Stan, on two random-slope binomial GLMMs from the
#  schwa_eychenne2019 dataset.
#
#  Priors are matched as closely as the two engines allow:
#
#    Phonometrica                      brms equivalent
#    ----------------------------      -------------------------------------
#    PC(1, 0.05) on each sd            exponential(rate = -log(0.05) / 1)
#                                      = exponential(2.9957)   on class="sd"
#    LKJ(η = 1) on correlation         lkj_corr_cholesky(1)    on class="L"
#    N(0, 10) on β                     normal(0, 10)           on class="b"
#                                      and on class="Intercept"
#    no residual prior (binomial)      —
#
#  Notes on the PC↔exponential equivalence:
#
#    The penalised-complexity prior on σ with U(σ > σ_0) = α has density
#    λ exp(−λσ) where λ = −log(α)/σ_0. With σ_0 = 1 and α = 0.05 we get
#    λ = −log(0.05) ≈ 2.9957. So PC(1, 0.05) and Exp(2.9957) are literally
#    the same distribution; brms just doesn't have a prior keyword for it.
#
#  Output: side-by-side tables of posterior mean, SD, and 95% CI for the
#  random-effect SDs, the correlation parameters, and the fixed effects;
#  plus WAIC for predictive comparison.
# ============================================================================

library(brms)
library(dplyr)
library(tibble)

# ── Setup ──────────────────────────────────────────────────────────────────

set.seed(20260502L)
options(mc.cores = parallel::detectCores())
rstan::rstan_options(auto_write = TRUE)

DATA_PATH   <- "/home/julien/OneDrive/Université/Publications/schwa_eychenne2019.csv"   # adjust if needed
N_CHAINS    <- 4
N_ITER      <- 2000     # per chain, half discarded as warmup
N_WARMUP    <- 1000
ADAPT_DELTA <- 0.95     # high enough to clear divergent transitions on
                        # boundary-leaning posteriors (PC priors)

# ── Load and prepare data ─────────────────────────────────────────────────

d <- read.csv(DATA_PATH, fileEncoding = "UTF-8")

# Ensure factor coding matches what the Phonometrica run used.
d <- d %>%
  mutate(
    schwa01 = as.integer(schwa == "present"),                 # 1 = present, 0 = absent
    left    = factor(left,
                     levels = c("consonant",
                                "simplified cluster",
                                "vowel")),
    right   = factor(right,   levels = c("IP edge", "consonant")),
    dialect = factor(dialect, levels = c("Basque", "Languedocian", "Provençal")),
    task    = factor(task,    levels = c("formal", "informal", "text")),
    subject = factor(subject)
  )

cat(sprintf("Data: n=%d, subjects=%d, words=%d\n",
            nrow(d), nlevels(d$subject), length(unique(d$word))))

# ── Shared priors ──────────────────────────────────────────────────────────

# λ for PC(1, 0.05) → exponential(λ)
lambda_pc <- -log(0.05)   # ≈ 2.9957

priors_shared <- c(
  prior_string(sprintf("exponential(%.6f)", lambda_pc),  class = "sd"),
  prior(lkj_corr_cholesky(1),                            class = "L"),
  prior(normal(0, 10),                                   class = "b"),
  prior(normal(0, 10),                                   class = "Intercept")
)

# ── Model 4: (1 + left | subject) ─────────────────────────────────────────
#
#  This is the headline comparison. Phonometrica reports:
#    sd(Intercept|subject)               = 0.6254 (0.41, 0.84)
#    sd(left[simplified cluster]|subj)   = 0.2965 (0.00, 0.75)
#    sd(left[vowel]|subj)                = 0.4468 (0.27, 0.63)
#    correlations: -0.03, +0.49, +0.11
#    WAIC = 6422.6
#
#  Expectation under matched priors: brms sd(simp) posterior mean somewhere
#  in 0.25–0.40, brms WAIC within ±5 of Phonometrica's. CI for σ_simp will
#  reach to zero in both engines — that's the prior shape, not a bug.

cat("\n========== Model 4: (1 + left | subject) ==========\n")

m4_brms <- brm(
  formula = schwa01 ~ left + right + dialect + (1 + left | subject),
  data    = d,
  family  = bernoulli(link = "logit"),
  prior   = priors_shared,
  chains  = N_CHAINS, iter = N_ITER, warmup = N_WARMUP,
  control = list(adapt_delta = ADAPT_DELTA, max_treedepth = 12),
  seed    = 20260502L,
  refresh = 200
)

cat("\n--- Model 4 brms summary ---\n")
print(summary(m4_brms))

cat("\n--- Model 4 random-effect SDs and correlations ---\n")
m4_post <- as_draws_df(m4_brms)
m4_re_pars <- grep("^(sd_|cor_)", colnames(m4_post), value = TRUE)
print(posterior::summarise_draws(m4_post[, m4_re_pars],
                                 mean, sd,
                                 ~quantile(.x, c(0.025, 0.975))))

cat("\n--- Model 4 WAIC ---\n")
m4_waic <- waic(m4_brms)
print(m4_waic)

# ── Model 6: (1 + task | subject) ─────────────────────────────────────────
#
#  This is a cleaner case — glmmTMB converged, so we have three engines to
#  compare. Phonometrica reports:
#    sd(Intercept|subject)         = 0.9611 (0.77, 1.16)
#    sd(task[informal]|subj)       = 0.1951 (0.00, 0.39)
#    sd(task[text]|subj)           = 0.4885 (0.32, 0.66)
#    correlations: +0.02, -0.31, -0.01
#    WAIC = 6354.1
#
#  Expectation: tighter agreement than Model 4 because identifiability is
#  better (task is balanced across subjects, unlike `left = simplified
#  cluster` which is rare).

cat("\n========== Model 6: (1 + task | subject) ==========\n")

m6_brms <- brm(
  formula = schwa01 ~ left + right + dialect + task + (1 + task | subject),
  data    = d,
  family  = bernoulli(link = "logit"),
  prior   = priors_shared,
  chains  = N_CHAINS, iter = N_ITER, warmup = N_WARMUP,
  control = list(adapt_delta = ADAPT_DELTA, max_treedepth = 12),
  seed    = 20260502L,
  refresh = 200
)

cat("\n--- Model 6 brms summary ---\n")
print(summary(m6_brms))

cat("\n--- Model 6 random-effect SDs and correlations ---\n")
m6_post <- as_draws_df(m6_brms)
m6_re_pars <- grep("^(sd_|cor_)", colnames(m6_post), value = TRUE)
print(posterior::summarise_draws(m6_post[, m6_re_pars],
                                 mean, sd,
                                 ~quantile(.x, c(0.025, 0.975))))

cat("\n--- Model 6 WAIC ---\n")
m6_waic <- waic(m6_brms)
print(m6_waic)

# ── Side-by-side comparison tables ────────────────────────────────────────
#
# Hand-paste from the Phonometrica output for the three columns
# (Phon.Mean, Phon.SD, Phon.CI). brms numbers come from the fitted
# objects above.

cat("\n========== Side-by-side comparison ==========\n")

# Helper to extract a single posterior summary row.
post_summary <- function(draws, par) {
  x <- draws[[par]]
  c(mean = mean(x),
    sd   = sd(x),
    q025 = unname(quantile(x, 0.025)),
    q975 = unname(quantile(x, 0.975)))
}

# Phonometrica reference (typed in from the user's run output).
phon_m4 <- tibble::tribble(
  ~param,                                 ~Phon.Mean, ~Phon.SD, ~Phon.q025, ~Phon.q975,
  "sd(Intercept|subject)",                0.6254,     0.1101,   0.4097,     0.8412,
  "sd(left[simplified cluster]|subject)", 0.2965,     0.2319,   0.0000,     0.7511,
  "sd(left[vowel]|subject)",              0.4468,     0.0910,   0.2686,     0.6251
)

phon_m6 <- tibble::tribble(
  ~param,                                ~Phon.Mean, ~Phon.SD, ~Phon.q025, ~Phon.q975,
  "sd(Intercept|subject)",               0.9611,     0.0999,   0.7653,     1.1569,
  "sd(task[informal]|subject)",          0.1951,     0.1000,   0.0000,     0.3911,
  "sd(task[text]|subject)",              0.4885,     0.0867,   0.3186,     0.6585
)

# brms summaries on the matching parameter names. Update the strings on the
# right side if your brms version uses different parameter names — newer
# versions use `sd_subject__Intercept` etc.
brms_m4 <- rbind(
  post_summary(m4_post, "sd_subject__Intercept"),
  post_summary(m4_post, "sd_subject__leftsimplifiedcluster"),
  post_summary(m4_post, "sd_subject__leftvowel")
)

brms_m6 <- rbind(
  post_summary(m6_post, "sd_subject__Intercept"),
  post_summary(m6_post, "sd_subject__taskinformal"),
  post_summary(m6_post, "sd_subject__tasktext")
)

cmp_m4 <- cbind(phon_m4, brms.Mean = brms_m4[, "mean"],
                          brms.SD   = brms_m4[, "sd"],
                          brms.q025 = brms_m4[, "q025"],
                          brms.q975 = brms_m4[, "q975"])

cmp_m6 <- cbind(phon_m6, brms.Mean = brms_m6[, "mean"],
                          brms.SD   = brms_m6[, "sd"],
                          brms.q025 = brms_m6[, "q025"],
                          brms.q975 = brms_m6[, "q975"])

cat("\n--- Model 4 SD comparison ---\n")
print(cmp_m4, digits = 4)

cat("\n--- Model 6 SD comparison ---\n")
print(cmp_m6, digits = 4)

cat(sprintf("\n--- WAIC comparison ---\n"))
cat(sprintf("Model 4: Phonometrica = 6422.6   brms = %.1f\n",
            m4_waic$estimates["waic", "Estimate"]))
cat(sprintf("Model 6: Phonometrica = 6354.1   brms = %.1f\n",
            m6_waic$estimates["waic", "Estimate"]))

# ── Save fitted objects ───────────────────────────────────────────────────

saveRDS(list(m4_brms = m4_brms, m6_brms = m6_brms,
             cmp_m4 = cmp_m4,   cmp_m6 = cmp_m6),
        file = "brms_phon_comparison.rds")

cat("\nDone. Saved to brms_phon_comparison.rds\n")
