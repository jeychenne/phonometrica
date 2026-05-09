# =====================================================================
# gen_reference_negbin.R — Bayesian negative binomial reference.
# Mirrors frequentist/gen_reference_negbin.R: same negbin_counts.csv
# (synthetic, 5000 obs), same five model formulas.
#
# brms calls the dispersion parameter `shape`; that's the same role
# as glmmTMB's `theta` in nbinom2 and as Phonometrica's `theta(NB)`
# hyper. Phonometrica's default Gamma(1, 0.01) prior is matched.
#
# Five models:
#   M1 — count ~ condition * context                            (interaction, no RE)
#   M2 — count ~ condition + context + (1|subject)
#   M3 — count ~ condition + context + (1+condition|subject)    (random slope)
#   M4 — count ~ condition + context + (1|subject) + (1|item)   (crossed)
#   M5 — count ~ condition + context + (1+condition|subject) + (1|item)
# =====================================================================

source("_helpers.R")

data_path <- resolve_data_path("negbin_counts.csv")
if (!file.exists(data_path)) stop(sprintf("%s not found.", data_path))

d <- read.delim(data_path)
d$condition <- factor(d$condition, levels = c("baseline", "primed"))
d$context   <- factor(d$context,   levels = c("casual", "formal", "narrative"))
d$subject   <- factor(d$subject)
d$item      <- factor(d$item)

cat("Fitting M1 (interaction, no RE)...\n")
m1 <- fit_brms("count ~ condition * context", d, "negbin", "nb_M1")

cat("Fitting M2 (1|subject)...\n")
m2 <- fit_brms("count ~ condition + context + (1|subject)", d, "negbin", "nb_M2")

cat("Fitting M3 (1+condition|subject)...\n")
m3 <- fit_brms("count ~ condition + context + (1+condition|subject)", d, "negbin", "nb_M3")

cat("Fitting M4 (1|subject) + (1|item)...\n")
m4 <- fit_brms("count ~ condition + context + (1|subject) + (1|item)", d, "negbin", "nb_M4")

cat("Fitting M5 (1+condition|subject) + (1|item)...\n")
m5 <- fit_brms("count ~ condition + context + (1+condition|subject) + (1|item)", d, "negbin", "nb_M5")

models <- list(
    M1 = build_model_entry_brms(m1, "count ~ condition * context",                                       "negbin"),
    M2 = build_model_entry_brms(m2, "count ~ condition + context + (1|subject)",                         "negbin"),
    M3 = build_model_entry_brms(m3, "count ~ condition + context + (1+condition|subject)",              "negbin"),
    M4 = build_model_entry_brms(m4, "count ~ condition + context + (1|subject) + (1|item)",             "negbin"),
    M5 = build_model_entry_brms(m5, "count ~ condition + context + (1+condition|subject) + (1|item)",   "negbin")
)

write_reference_brms(
    "negbin", models, "negbin_counts.csv",
    resolve_output_path("reference_negbin.json")
)
cat("Done.\n")
