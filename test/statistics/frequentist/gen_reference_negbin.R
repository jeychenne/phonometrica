# =====================================================================
# gen_reference_negbin.R — Negative-binomial reference using synthetic
# count data with crossed REs and moderate random-slope variance.
#
# Why synthetic instead of glmmTMB::Owls:
#   The Owls dataset (the canonical glmmTMB NB tutorial example) has
#   a very strong food-treatment slope variance (SD ≈ 1.06, larger
#   than the intercept SD of 0.16). On reference fits this is fine
#   for glmmTMB, but it sits right in Phonometrica's documented
#   weak spot: the joint (β, θ) L-BFGS optimiser lacks step-halving
#   for cold-start û = 0 with strong RE variance, and throws an
#   L-BFGS non-finite-state on M3. That's a real engine limitation
#   tracked as a post-v1.0 hardening item — not something the
#   v1.0 frequentist suite should fail on.
#
#   Salamanders has crossed RE structure (spp × site) but boundary
#   convergence on the cover slope. Same problem we're avoiding.
#
#   Going synthetic keeps tolerance tight on the well-tested core
#   path: moderate slope variance (SD ≈ 0.3, ratio ~0.7 to the
#   intercept) lets glmmTMB and Phonometrica both converge to the
#   same interior optimum.
#
# Design:
#   * 200 subjects × 50 items, crossed.
#   * condition: baseline / primed (within-subject).
#   * context:   formal / casual / narrative (within-subject).
#   * count: NB(μ=exp(η), θ=2), where η has true random intercepts
#     and a true random slope for condition|subject.
#   * Seed = 2026, fully reproducible.
# =====================================================================

source("_helpers.R")
suppressPackageStartupMessages({library(glmmTMB)})

# ── Generate the dataset ───────────────────────────────────────────
set.seed(2026)
n_subj <- 200
n_item <- 50
subjects <- paste0("S", sprintf("%03d", 1:n_subj))
items    <- paste0("W", sprintf("%02d", 1:n_item))
conditions <- c("baseline", "primed")           # alphabetical: ref = "baseline"
contexts   <- c("casual", "formal", "narrative") # alphabetical: ref = "casual"
# Both factor declarations below use these alphabetical orderings so
# that R's reference level matches Phonometrica's auto-detected one
# (Phonometrica uses alphabetical-first as reference by default).
# Mismatched references manifest as "name not found" failures on
# the contrast that R uses but Phonometrica reduces away, plus
# numerical discrepancies on every other coefficient that absorbs
# the reference-level shift.

rows <- list()
for (s in subjects) {
    n_obs <- sample(20:30, 1)
    rows[[s]] <- data.frame(
        subject   = s,
        item      = sample(items, n_obs, replace = TRUE),
        condition = sample(conditions, n_obs, replace = TRUE),
        context   = sample(contexts, n_obs, replace = TRUE),
        stringsAsFactors = FALSE
    )
}
d <- do.call(rbind, rows)
d$condition <- factor(d$condition, levels = conditions)
d$context   <- factor(d$context,   levels = contexts)

# True generating parameters (log scale)
b0            <-  1.5
b_primed      <-  0.4
b_casual      <-  0.2
b_narr        <- -0.3
b_pri_cas     <-  0.2
b_pri_narr    <- -0.1
sd_subj_int   <-  0.4
sd_subj_slope <-  0.3   # ← moderate slope variance (deliberately not extreme)
sd_item_int   <-  0.25
theta         <-  2.0   # NB dispersion: var = mu + mu^2/theta

re_subj <- data.frame(subject = subjects,
                      u0 = rnorm(n_subj, 0, sd_subj_int),
                      u1 = rnorm(n_subj, 0, sd_subj_slope))
re_item <- data.frame(item = items, w0 = rnorm(n_item, 0, sd_item_int))
d <- merge(d, re_subj, by = "subject")
d <- merge(d, re_item, by = "item")

eta <- with(d,
    b0 +
    b_primed   * (condition == "primed") +
    b_casual   * (context == "casual") +
    b_narr     * (context == "narrative") +
    b_pri_cas  * (condition == "primed" & context == "casual") +
    b_pri_narr * (condition == "primed" & context == "narrative") +
    u0 + u1 * (condition == "primed") +
    w0)
mu <- exp(eta)
d$count <- rnbinom(nrow(d), mu = mu, size = theta)

d <- d[, c("subject", "item", "condition", "context", "count")]
data_path <- file.path("..", "..", "data", "negbin_counts.csv")
write.csv(d, data_path, row.names = FALSE)
cat("Wrote", nrow(d), "rows to", data_path, "\n")

# ── Fit and serialise ──────────────────────────────────────────────
m1 <- glmmTMB(count ~ condition * context,
              family = nbinom2, data = d)
m2 <- glmmTMB(count ~ condition + context + (1|subject),
              family = nbinom2, data = d)
m3 <- glmmTMB(count ~ condition + context + (1+condition|subject),
              family = nbinom2, data = d)
m4 <- glmmTMB(count ~ condition + context + (1|subject) + (1|item),
              family = nbinom2, data = d)
m5 <- glmmTMB(count ~ condition + context + (1+condition|subject) + (1|item),
              family = nbinom2, data = d)

models <- list(
    M1 = build_model_entry(m1, "count ~ condition * context",
                           "glmmTMB", "negbin"),
    M2 = build_model_entry(m2, "count ~ condition + context + (1|subject)",
                           "glmmTMB", "negbin"),
    M3 = build_model_entry(m3, "count ~ condition + context + (1+condition|subject)",
                           "glmmTMB", "negbin"),
    M4 = build_model_entry(m4, "count ~ condition + context + (1|subject) + (1|item)",
                           "glmmTMB", "negbin"),
    M5 = build_model_entry(m5, "count ~ condition + context + (1+condition|subject) + (1|item)",
                           "glmmTMB", "negbin")
)

write_reference("negbin", models, "negbin_counts.csv", "reference_negbin.json")
cat("Done.\n")
