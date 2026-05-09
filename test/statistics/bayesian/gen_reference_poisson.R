# =====================================================================
# gen_reference_poisson.R — Bayesian Poisson reference (brms HMC).
# Mirrors frequentist/gen_reference_poisson.R: poisson_disfluency.csv,
# same five model formulas.
#
# Five models:
#   M1 — count ~ task * age                              (interaction, no RE)
#   M2 — count ~ task + age + (1|speaker)
#   M3 — count ~ task + age + (1+task|speaker)           (random slope)
#   M4 — count ~ task + age + (1|speaker) + (1|word)     (crossed)
#   M5 — count ~ task + age + (1+task|speaker) + (1|word)
# =====================================================================

source("_helpers.R")

data_path <- resolve_data_path("poisson_disfluency.csv")
if (!file.exists(data_path)) stop(sprintf("%s not found.", data_path))

d <- read.delim(data_path)
d$task    <- factor(d$task, levels = c("conversation", "reading"))
d$age     <- factor(d$age,  levels = c("old", "young"))
d$speaker <- factor(d$speaker)
d$word    <- factor(d$word)

cat("Fitting M1 (interaction, no RE)...\n")
m1 <- fit_brms("count ~ task * age", d, "poisson", "pois_M1")

cat("Fitting M2 (1|speaker)...\n")
m2 <- fit_brms("count ~ task + age + (1|speaker)", d, "poisson", "pois_M2")

cat("Fitting M3 (1+task|speaker)...\n")
m3 <- fit_brms("count ~ task + age + (1+task|speaker)", d, "poisson", "pois_M3")

cat("Fitting M4 (1|speaker) + (1|word)...\n")
m4 <- fit_brms("count ~ task + age + (1|speaker) + (1|word)", d, "poisson", "pois_M4")

cat("Fitting M5 (1+task|speaker) + (1|word)...\n")
m5 <- fit_brms("count ~ task + age + (1+task|speaker) + (1|word)", d, "poisson", "pois_M5")

models <- list(
    M1 = build_model_entry_brms(m1, "count ~ task * age",                                  "poisson"),
    M2 = build_model_entry_brms(m2, "count ~ task + age + (1|speaker)",                    "poisson"),
    M3 = build_model_entry_brms(m3, "count ~ task + age + (1+task|speaker)",               "poisson"),
    M4 = build_model_entry_brms(m4, "count ~ task + age + (1|speaker) + (1|word)",         "poisson"),
    M5 = build_model_entry_brms(m5, "count ~ task + age + (1+task|speaker) + (1|word)",    "poisson")
)

write_reference_brms(
    "poisson", models, "poisson_disfluency.csv",
    resolve_output_path("reference_poisson.json")
)
cat("Done.\n")
