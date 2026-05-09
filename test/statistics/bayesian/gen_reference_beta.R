# =====================================================================
# gen_reference_beta.R — Bayesian beta-regression reference.
# Mirrors frequentist/gen_reference_beta.R: same beta_accuracy.csv
# (synthetic, 4475 obs, accuracy ∈ (0,1)), same five model formulas.
#
# Phonometrica's hyper for beta is `phi(beta)` (precision parameter).
# brms calls it `phi`; the prior translation uses Phonometrica's
# default Gamma(1, 0.01).
#
# Five models:
#   M1 — accuracy ~ difficulty * domain                                   (interaction, no RE)
#   M2 — accuracy ~ difficulty + domain + (1|subject)
#   M3 — accuracy ~ difficulty + domain + (1+difficulty|subject)          (random slope)
#   M4 — accuracy ~ difficulty + domain + (1|subject) + (1|item)          (crossed)
#   M5 — accuracy ~ difficulty + domain + (1+difficulty|subject) + (1|item)
# =====================================================================

source("_helpers.R")

data_path <- resolve_data_path("beta_accuracy.csv")
if (!file.exists(data_path)) stop(sprintf("%s not found.", data_path))

d <- read.delim(data_path)
d$difficulty <- factor(d$difficulty, levels = c("easy", "hard"))
d$domain     <- factor(d$domain,     levels = c("math", "verbal", "spatial"))
d$subject    <- factor(d$subject)
d$item       <- factor(d$item)

cat("Fitting M1 (interaction, no RE)...\n")
m1 <- fit_brms("accuracy ~ difficulty * domain", d, "beta", "beta_M1")

cat("Fitting M2 (1|subject)...\n")
m2 <- fit_brms("accuracy ~ difficulty + domain + (1|subject)", d, "beta", "beta_M2")

cat("Fitting M3 (1+difficulty|subject)...\n")
m3 <- fit_brms("accuracy ~ difficulty + domain + (1+difficulty|subject)", d, "beta", "beta_M3")

cat("Fitting M4 (1|subject) + (1|item)...\n")
m4 <- fit_brms("accuracy ~ difficulty + domain + (1|subject) + (1|item)", d, "beta", "beta_M4")

cat("Fitting M5 (1+difficulty|subject) + (1|item)...\n")
m5 <- fit_brms("accuracy ~ difficulty + domain + (1+difficulty|subject) + (1|item)", d, "beta", "beta_M5")

models <- list(
    M1 = build_model_entry_brms(m1, "accuracy ~ difficulty * domain",                                          "beta"),
    M2 = build_model_entry_brms(m2, "accuracy ~ difficulty + domain + (1|subject)",                            "beta"),
    M3 = build_model_entry_brms(m3, "accuracy ~ difficulty + domain + (1+difficulty|subject)",                 "beta"),
    M4 = build_model_entry_brms(m4, "accuracy ~ difficulty + domain + (1|subject) + (1|item)",                 "beta"),
    M5 = build_model_entry_brms(m5, "accuracy ~ difficulty + domain + (1+difficulty|subject) + (1|item)",      "beta")
)

write_reference_brms(
    "beta", models, "beta_accuracy.csv",
    resolve_output_path("reference_beta.json")
)
cat("Done.\n")
