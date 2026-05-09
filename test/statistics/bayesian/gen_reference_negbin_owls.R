# =====================================================================
# gen_reference_negbin_owls.R — Bayesian NB reference on the owls
# dataset (Roulin & Bersier 2007 / Zuur et al. 2009). Mirrors
# frequentist/gen_reference_negbin_owls_lme4.R: same owls.csv
# (single nest grouping factor, 599 obs), same five model formulas.
#
# Five models:
#   M1 — calls ~ food * sex                                  (interaction, no RE)
#   M2 — calls ~ food + sex + (1|nest)
#   M3 — calls ~ food + sex + (1+food|nest)                  (random slope)
#   M4 — calls ~ food + sex + arrival + (1|nest)
#   M5 — calls ~ food + sex + arrival + (1+food|nest)
# =====================================================================

source("_helpers.R")

data_path <- resolve_data_path("owls.csv")
if (!file.exists(data_path)) stop(sprintf("%s not found.", data_path))

d <- read.delim(data_path)
d$food <- factor(d$food, levels = c("Deprived", "Satiated"))
d$sex  <- factor(d$sex,  levels = c("Female",   "Male"))
d$nest <- factor(d$nest)

cat("Fitting M1 (food * sex, no RE)...\n")
m1 <- fit_brms("calls ~ food * sex", d, "negbin", "owls_M1")

cat("Fitting M2 (1|nest)...\n")
m2 <- fit_brms("calls ~ food + sex + (1|nest)", d, "negbin", "owls_M2")

cat("Fitting M3 (1+food|nest)...\n")
m3 <- fit_brms("calls ~ food + sex + (1+food|nest)", d, "negbin", "owls_M3")

cat("Fitting M4 (with arrival, 1|nest)...\n")
m4 <- fit_brms("calls ~ food + sex + arrival + (1|nest)", d, "negbin", "owls_M4")

cat("Fitting M5 (with arrival, 1+food|nest)...\n")
m5 <- fit_brms("calls ~ food + sex + arrival + (1+food|nest)", d, "negbin", "owls_M5")

models <- list(
    M1 = build_model_entry_brms(m1, "calls ~ food * sex",                                  "negbin"),
    M2 = build_model_entry_brms(m2, "calls ~ food + sex + (1|nest)",                       "negbin"),
    M3 = build_model_entry_brms(m3, "calls ~ food + sex + (1+food|nest)",                  "negbin"),
    M4 = build_model_entry_brms(m4, "calls ~ food + sex + arrival + (1|nest)",             "negbin"),
    M5 = build_model_entry_brms(m5, "calls ~ food + sex + arrival + (1+food|nest)",        "negbin")
)

write_reference_brms(
    "negbin_owls", models, "owls.csv",
    resolve_output_path("reference_negbin_owls.json")
)
cat("Done.\n")
