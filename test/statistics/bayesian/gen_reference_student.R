# =====================================================================
# gen_reference_student.R — Bayesian Student-t reference.
# Mirrors frequentist/gen_reference_student.R: same monophtongues.csv
# corpus (private), same five formulas mixing E1/E2 responses.
#
# Phonometrica exposes Student-t hyperparameters as `sigma(student)`
# and `nu(student)`. The brms-side prior on ν is uniform(2, 200) to
# match Phonometrica's effectively-flat ν prior within its [2, 200]
# clamp range — the May 2026 Student-t validation used the same flat
# ν prior.
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
#   a large RE covariance block, slow to sample. Plan for ~30-90 min
#   per model on M3-M5. Stan models are cached.
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
