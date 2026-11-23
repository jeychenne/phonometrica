# =====================================================================
# gen_reference_student.R — Bayesian Student-t reference.
# Mirrors frequentist/gen_reference_student.R: same monophtongues.csv
# corpus (private), same five formulas mixing E1/E2 responses.
#
# Phonometrica exposes Student-t hyperparameters as `sigma(student)`
# and `nu(student)`. The brms-side prior on ν is gamma(2, 0.1) with
# lb = 2, matching Phonometrica's default PriorSpec::student_nu and its
# NU_MIN clamp (see phon_default_priors in _helpers.R). The May 2026
# reference used uniform(2, 200), Phonometrica's default at the time;
# it was regenerated on 2026-11-22 after the default changed.
#
# Five models:
#   M1 — E1 ~ Voyelle * Condition + Sexe + Age                          (no RE)
#   M2 — E1 ~ Voyelle * Condition + Sexe * Age + (1|Sujet) + (1|Mot)
#   M3 — E1 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)
#   M4 — E2 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)
#   M5 — E2 ~ Voyelle * Condition + Sexe + Age + (1|Sujet)
#
# Runtime caveat:
#   Random-slope `(1+Voyelle|Sujet)` with multi-level Voyelle creates
#   a large RE covariance block. With brms 2.22 each model samples in
#   1-4 min (Nov 2026); fits are cached under stan_cache/.
# =====================================================================

source("_helpers.R")

data_path <- resolve_data_path("monophtongues.csv")
if (!file.exists(data_path)) {
    stop(sprintf(paste(
        "%s not found. The Student-t reference uses the user's private",
        "monophthong corpus; see frequentist/gen_reference_student.R for",
        "context. Place the file under test/data/ before running."
    ), data_path))
}

# Auto-detect tab vs comma.
read_auto <- function(path) {
    d_tab <- tryCatch(read.delim(path), error = function(e) NULL)
    if (!is.null(d_tab) && ncol(d_tab) > 1L) return(d_tab)
    d_csv <- tryCatch(read.csv(path),    error = function(e) NULL)
    if (!is.null(d_csv) && ncol(d_csv) > 1L) return(d_csv)
    stop(sprintf("Could not parse %s as tab- or comma-separated.", path))
}

d <- read_auto(data_path)
required <- c("E1", "E2", "Voyelle", "Condition", "Sexe", "Age", "Sujet", "Mot")
missing <- setdiff(required, names(d))
if (length(missing) > 0) {
    stop(sprintf("Missing required columns: %s", paste(missing, collapse = ", ")))
}

for (col in c("Voyelle", "Condition", "Sexe", "Sujet", "Mot")) {
    if (!is.factor(d[[col]])) d[[col]] <- factor(d[[col]])
}
if (is.character(d$Age)) d$Age <- factor(d$Age)

cat("Fitting M1 (E1, no RE)...\n")
m1 <- fit_brms("E1 ~ Voyelle * Condition + Sexe + Age", d, "student", "stud_M1")

cat("Fitting M2 (E1, 1|Sujet + 1|Mot)...\n")
m2 <- fit_brms("E1 ~ Voyelle * Condition + Sexe * Age + (1|Sujet) + (1|Mot)",
               d, "student", "stud_M2")

cat("Fitting M3 (E1, 1+Voyelle|Sujet + 1|Mot)...\n")
m3 <- fit_brms("E1 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)",
               d, "student", "stud_M3")

cat("Fitting M4 (E2, 1+Voyelle|Sujet + 1|Mot)...\n")
m4 <- fit_brms("E2 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)",
               d, "student", "stud_M4")

cat("Fitting M5 (E2, 1|Sujet)...\n")
m5 <- fit_brms("E2 ~ Voyelle * Condition + Sexe + Age + (1|Sujet)",
               d, "student", "stud_M5")

models <- list(
    M1 = build_model_entry_brms(m1, "E1 ~ Voyelle * Condition + Sexe + Age",                                       "student"),
    M2 = build_model_entry_brms(m2, "E1 ~ Voyelle * Condition + Sexe * Age + (1|Sujet) + (1|Mot)",                 "student"),
    M3 = build_model_entry_brms(m3, "E1 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)",         "student"),
    M4 = build_model_entry_brms(m4, "E2 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)",         "student"),
    M5 = build_model_entry_brms(m5, "E2 ~ Voyelle * Condition + Sexe + Age + (1|Sujet)",                           "student")
)

write_reference_brms(
    "student", models, "monophtongues.csv",
    resolve_output_path("reference_student.json")
)
cat("Done.\n")
