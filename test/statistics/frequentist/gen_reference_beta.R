# =====================================================================
# gen_reference_beta.R — Beta regression reference using a synthetic
# task-accuracy dataset.
#
# Why synthetic (not a real dataset):
#   Real mixed-effects beta-regression datasets are rare in standard
#   R packages. The classic betareg::* datasets (GasolineYield,
#   ReadingSkills, MockJurors, FoodExpenditure) are all small (< 50
#   obs) and non-mixed. Rather than shoehorn a non-mixed dataset
#   into a mixed-effects test or use phonetic data where the slope
#   variance happens to be zero, we generate clean data where every
#   model in the suite converges at an interior optimum.
#
# Design:
#   * 150 subjects × 40 items, crossed.
#   * difficulty: easy / hard (within-subject, varies row-to-row).
#   * domain:     math / verbal / spatial (within-subject).
#   * accuracy: continuous response in (0,1), generated from a
#     beta(μφ, (1−μ)φ) with φ=30 and μ from a logit linear
#     predictor with true random intercepts and a true random slope
#     for difficulty|subject.
#   * Seed = 2026, fully reproducible.
# =====================================================================

source("_helpers.R")
suppressPackageStartupMessages({library(glmmTMB)})

# ── Generate the dataset ───────────────────────────────────────────
set.seed(2026)
n_subj <- 150
n_item <- 40
subjects <- paste0("S", sprintf("%03d", 1:n_subj))
items    <- paste0("I", sprintf("%02d", 1:n_item))
difficulties <- c("easy", "hard")
domains      <- c("math", "verbal", "spatial")

rows <- list()
for (s in subjects) {
    n_obs <- sample(25:35, 1)
    rows[[s]] <- data.frame(
        subject    = s,
        item       = sample(items, n_obs, replace = TRUE),
        difficulty = sample(difficulties, n_obs, replace = TRUE),
        domain     = sample(domains, n_obs, replace = TRUE),
        stringsAsFactors = FALSE
    )
}
d <- do.call(rbind, rows)
d$difficulty <- factor(d$difficulty, levels = difficulties)
d$domain     <- factor(d$domain,     levels = domains)

# True generating parameters (logit scale unless noted)
b0             <-  0.5
b_hard         <- -0.8
b_verbal       <-  0.2
b_spatial      <- -0.1
b_hard_verbal  <-  0.3
b_hard_spatial <- -0.2
sd_subj_int    <-  0.5
sd_subj_hard   <-  0.4   # ← real non-zero slope variance
sd_item_int    <-  0.3
phi            <- 30     # beta precision

re_subj <- data.frame(subject = subjects,
                      u0 = rnorm(n_subj, 0, sd_subj_int),
                      u1 = rnorm(n_subj, 0, sd_subj_hard))
re_item <- data.frame(item = items, w0 = rnorm(n_item, 0, sd_item_int))
d <- merge(d, re_subj, by = "subject")
d <- merge(d, re_item, by = "item")

eta <- with(d,
    b0 +
    b_hard    * (difficulty == "hard") +
    b_verbal  * (domain == "verbal") +
    b_spatial * (domain == "spatial") +
    b_hard_verbal  * (difficulty == "hard" & domain == "verbal") +
    b_hard_spatial * (difficulty == "hard" & domain == "spatial") +
    u0 + u1 * (difficulty == "hard") +
    w0)
mu <- 1 / (1 + exp(-eta))
d$accuracy <- rbeta(nrow(d), mu * phi, (1 - mu) * phi)

d <- d[, c("subject", "item", "difficulty", "domain", "accuracy")]
data_path <- file.path("..", "..", "data", "beta_accuracy.csv")
write.csv(d, data_path, row.names = FALSE)
cat("Wrote", nrow(d), "rows to", data_path, "\n")

# ── Fit and serialise ──────────────────────────────────────────────
m1 <- glmmTMB(accuracy ~ difficulty * domain,
              family = beta_family, data = d)
m2 <- glmmTMB(accuracy ~ difficulty + domain + (1|subject),
              family = beta_family, data = d)
m3 <- glmmTMB(accuracy ~ difficulty + domain + (1+difficulty|subject),
              family = beta_family, data = d)
m4 <- glmmTMB(accuracy ~ difficulty + domain + (1|subject) + (1|item),
              family = beta_family, data = d)
m5 <- glmmTMB(accuracy ~ difficulty + domain + (1+difficulty|subject) + (1|item),
              family = beta_family, data = d)

models <- list(
    M1 = build_model_entry(m1, "accuracy ~ difficulty * domain",
                           "glmmTMB", "beta"),
    M2 = build_model_entry(m2, "accuracy ~ difficulty + domain + (1|subject)",
                           "glmmTMB", "beta"),
    M3 = build_model_entry(m3, "accuracy ~ difficulty + domain + (1+difficulty|subject)",
                           "glmmTMB", "beta"),
    M4 = build_model_entry(m4, "accuracy ~ difficulty + domain + (1|subject) + (1|item)",
                           "glmmTMB", "beta"),
    M5 = build_model_entry(m5, "accuracy ~ difficulty + domain + (1+difficulty|subject) + (1|item)",
                           "glmmTMB", "beta")
)

write_reference("beta", models, "beta_accuracy.csv", "reference_beta.json")
cat("Done.\n")
