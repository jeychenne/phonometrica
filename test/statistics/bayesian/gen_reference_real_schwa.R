# =====================================================================
# gen_reference_real_schwa.R — Bayesian reference for the real schwa
# dataset (schwa_eychenne2019.csv). Mirrors frequentist
# gen_reference_real_schwa.R: same recoding (schwa01 ∈ {0,1}), same
# factor levels, same five model formulas.
#
# Five models:
#   M1 — schwa01 ~ dialect * gender + task                              (no RE)
#   M2 — schwa01 ~ dialect + gender + task + (1|subject)
#   M3 — schwa01 ~ dialect + gender + task + (1+task|subject)           (random slope)
#   M4 — schwa01 ~ dialect + gender + task + (1|subject) + (1|word)     (crossed)
#   M5 — schwa01 ~ dialect + gender + task + (1+task|subject) + (1|word)
# =====================================================================

source("_helpers.R")

data_path <- resolve_data_path("schwa_eychenne2019.csv")
if (!file.exists(data_path)) stop(sprintf("%s not found.", data_path))

d <- read.delim(data_path)

# Recode response: present → 1, absent/uncertain → 0. Matches the
# frequentist generator (as.integer(d$schwa == "present")) so the
# 7787-row sample size is identical across suites.
d$schwa01 <- as.integer(d$schwa == "present")

# Factor levels match the frequentist generator so coefficient names
# line up across suites.
d$dialect <- factor(d$dialect, levels = c("Basque", "Languedocian", "Provençal"))
d$gender  <- factor(d$gender,  levels = c("female", "male"))
d$task    <- factor(d$task,    levels = c("formal", "informal", "text"))
d$subject <- factor(d$subject)
d$word    <- factor(d$word)

cat("Fitting M1 (interaction, no RE)...\n")
m1 <- fit_brms("schwa01 ~ dialect * gender + task", d, "binomial", "schwa_M1")

cat("Fitting M2 (1|subject)...\n")
m2 <- fit_brms("schwa01 ~ dialect + gender + task + (1|subject)",
               d, "binomial", "schwa_M2")

cat("Fitting M3 (1+task|subject)...\n")
m3 <- fit_brms("schwa01 ~ dialect + gender + task + (1+task|subject)",
               d, "binomial", "schwa_M3")

cat("Fitting M4 (1|subject) + (1|word)...\n")
m4 <- fit_brms("schwa01 ~ dialect + gender + task + (1|subject) + (1|word)",
               d, "binomial", "schwa_M4")

cat("Fitting M5 (1+task|subject) + (1|word)...\n")
m5 <- fit_brms("schwa01 ~ dialect + gender + task + (1+task|subject) + (1|word)",
               d, "binomial", "schwa_M5")

models <- list(
    M1 = build_model_entry_brms(m1, "schwa01 ~ dialect * gender + task",                                       "binomial"),
    M2 = build_model_entry_brms(m2, "schwa01 ~ dialect + gender + task + (1|subject)",                         "binomial"),
    M3 = build_model_entry_brms(m3, "schwa01 ~ dialect + gender + task + (1+task|subject)",                    "binomial"),
    M4 = build_model_entry_brms(m4, "schwa01 ~ dialect + gender + task + (1|subject) + (1|word)",              "binomial"),
    M5 = build_model_entry_brms(m5, "schwa01 ~ dialect + gender + task + (1+task|subject) + (1|word)",         "binomial")
)

write_reference_brms(
    "real_schwa", models, "schwa_eychenne2019.csv",
    resolve_output_path("reference_real_schwa.json")
)
cat("Done.\n")
