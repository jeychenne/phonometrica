# =====================================================================
# gen_reference_student.R — Student-t regression reference using a
# real corpus dataset (test/data/monophtongues.csv) where glmmTMB's
# t_family() converges cleanly on all five models in the suite.
#
# History. Two earlier reference attempts were rejected:
#
#   1. glmmTMB on prior synthetic F1 data (450/600 obs) — every
#      fit was nonconverged, sigma-collapsed, or refused logLik.
#      Test was disabled.
#
#   2. heavy::heavyLme (Pinheiro/Liu/Wu 2001 ECME) — installed and
#      runs, but diverges to nonsense on every random-effects fit
#      on freshly-regenerated 2700-obs synthetic data even with ν
#      fixed at the truth, while heavy's own dental example fits
#      cleanly. A heavy bug specific to that geometry.
#
#   3. mgcv::gam(scat) — fits all models cleanly but uses
#      penalised REML with effective-degrees-of-freedom counting
#      and factor-level coding for random slopes (`by=vowel`)
#      that doesn't structurally match Phonometrica's contrast-
#      coded `(1+vowel|speaker)` slopes. Approximation worked but
#      a real corpus where glmmTMB just works is a more
#      convincing reference.
#
# Now using glmmTMB on a real (corpus-derived) monophthong dataset
# the user verified converges cleanly. This puts Student-t on the
# same engine as the other frequentist families (Gaussian, Beta,
# Binomial, Poisson, NB) for engine-consistency.
#
# Five models on monophtongues.csv:
#   M1 — E1 ~ Voyelle * Condition + Sexe + Age                      (no RE)
#   M2 — E1 ~ Voyelle * Condition + Sexe * Age + (1|Sujet) + (1|Mot)
#   M3 — E1 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)
#   M4 — E2 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)
#   M5 — E2 ~ Voyelle * Condition + Sexe + Age + (1|Sujet)
#
# Variable conventions (factors get alphabetical reference levels
# unless the source data already specifies otherwise):
#   E1, E2     — formant frequencies (Hz, response)
#   Voyelle    — vowel category (factor)
#   Condition  — experimental condition (factor)
#   Sexe       — speaker gender (factor)
#   Age        — speaker age (numeric or factor; left as-is from CSV)
#   Sujet      — speaker grouping factor (random)
#   Mot        — word grouping factor (random)
# =====================================================================

source("_helpers.R")
suppressPackageStartupMessages({library(glmmTMB)})

# ── Load data ──────────────────────────────────────────────────────
# Auto-detect tab vs comma delimiter rather than forcing read.delim:
# repo convention is tab but a freshly-exported CSV may be comma-
# separated. Whichever parse yields more than one column wins.

read_auto <- function(path) {
    d_tab <- tryCatch(read.delim(path), error = function(e) NULL)
    if (!is.null(d_tab) && ncol(d_tab) > 1L) return(d_tab)
    d_csv <- tryCatch(read.csv(path), error = function(e) NULL)
    if (!is.null(d_csv) && ncol(d_csv) > 1L) return(d_csv)
    stop(sprintf("Could not parse %s as tab- or comma-separated.", path))
}

d <- read_auto(resolve_data_path("monophtongues.csv"))
cat(sprintf("Loaded %d rows × %d columns\n", nrow(d), ncol(d)))
cat(sprintf("Columns: %s\n", paste(names(d), collapse = ", ")))

# Validate up front. Without this, a misnamed column produces a
# misleading "replacement array has 0 rows" downstream because
# d[["MissingCol"]] returns NULL and factor(NULL) returns length 0.
required <- c("E1", "E2", "Voyelle", "Condition",
              "Sexe", "Age", "Sujet", "Mot")
missing  <- setdiff(required, names(d))
if (length(missing) > 0L) {
    stop(sprintf("Missing required columns: %s",
                 paste(missing, collapse = ", ")))
}

# Coerce known categorical columns to factors. Reference levels are
# alphabetical (R default) which matches Phonometrica's default.
# Age is left as-is — if it's numeric in the CSV it stays numeric;
# if it's character it gets factored below alongside the rest.
for (col in c("Voyelle", "Condition", "Sexe", "Sujet", "Mot")) {
    if (!is.factor(d[[col]])) d[[col]] <- factor(d[[col]])
}
if (is.character(d$Age)) d$Age <- factor(d$Age)

cat(sprintf("  Voyelle levels: %s\n",
            paste(levels(d$Voyelle), collapse = ", ")))
cat(sprintf("  Condition levels: %s\n",
            paste(levels(d$Condition), collapse = ", ")))
cat(sprintf("  Sexe levels: %s\n",
            paste(levels(d$Sexe), collapse = ", ")))
cat(sprintf("  Age class: %s%s\n", class(d$Age),
            if (is.factor(d$Age))
                sprintf(" (levels: %s)", paste(levels(d$Age),
                                               collapse = ", "))
            else ""))

# ── Fit ────────────────────────────────────────────────────────────

cat("\nFitting models...\n")

cat("[M1]\n")
m1 <- glmmTMB(E1 ~ Voyelle * Condition + Sexe + Age,
              family = t_family(), data = d)

cat("[M2]\n")
m2 <- glmmTMB(E1 ~ Voyelle * Condition + Sexe * Age +
                  (1 | Sujet) + (1 | Mot),
              family = t_family(), data = d)

cat("[M3]\n")
m3 <- glmmTMB(E1 ~ Voyelle * Condition + Sexe * Age +
                  (1 + Voyelle | Sujet) + (1 | Mot),
              family = t_family(), data = d)

cat("[M4]\n")
m4 <- glmmTMB(E2 ~ Voyelle * Condition + Sexe * Age +
                  (1 + Voyelle | Sujet) + (1 | Mot),
              family = t_family(), data = d)

cat("[M5]\n")
m5 <- glmmTMB(E2 ~ Voyelle * Condition + Sexe + Age + (1 | Sujet),
              family = t_family(), data = d)

# ── Build entries ──────────────────────────────────────────────────
# Formula strings are stored verbatim in the JSON and re-used by
# Phonometrica's fit() — Phonometrica accepts lme4-style syntax so
# these pass through unchanged.

models <- list(
    M1 = build_model_entry(
        m1,
        "E1 ~ Voyelle * Condition + Sexe + Age",
        "glmmTMB", "student"
    ),
    M2 = build_model_entry(
        m2,
        "E1 ~ Voyelle * Condition + Sexe * Age + (1|Sujet) + (1|Mot)",
        "glmmTMB", "student"
    ),
    M3 = build_model_entry(
        m3,
        "E1 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)",
        "glmmTMB", "student"
    ),
    M4 = build_model_entry(
        m4,
        "E2 ~ Voyelle * Condition + Sexe * Age + (1+Voyelle|Sujet) + (1|Mot)",
        "glmmTMB", "student"
    ),
    M5 = build_model_entry(
        m5,
        "E2 ~ Voyelle * Condition + Sexe + Age + (1|Sujet)",
        "glmmTMB", "student"
    )
)

write_reference("student", models, "monophtongues.csv",
                resolve_output_path("reference_student.json"))
cat("Done.\n")
