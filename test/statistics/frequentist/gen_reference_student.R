# =====================================================================
# gen_reference_student.R — produces reference_student.json
# =====================================================================
# Run:    Rscript gen_reference_student.R
#
# Two synthetic datasets:
#   student_f1_robust.csv (600 obs, 20 spk, 10 wrd, heavy tails)
#   student_f1_mild.csv   (450 obs, 30 spk,  5 wrd, milder tails)
#
# Student-t convergence note. glmmTMB's t_family() can struggle.
# We pass a starting log(nu) and a longer iteration budget through
# glmmTMBControl so models converge reliably across runs. See the
# project memory entry on Student-t Bayesian for context on the
# Phonometrica side.
#
# Per-dataset structure: 5 models on each (M1 fixed-only with
# vowel * gender interaction, then M2-M5 with random structure
# growing to crossed RE + random slope). The robust dataset is
# heavier-tailed and pushes nu lower; the mild dataset (nu ≈ 10)
# is closer to Gaussian and a sanity check that the engine isn't
# overfitting tail behaviour.
# =====================================================================

source("_helpers.R")

ctrl <- glmmTMBControl(optCtrl = list(iter.max = 500, eval.max = 1000))

# Helper: fit a Student-t glmmTMB with sane defaults.
fit_student <- function(formula, data, start_nu = 5) {
    glmmTMB(
        formula,
        data        = data,
        family      = t_family(),
        dispformula = ~1,
        start       = list(psi = log(start_nu)),
        control     = ctrl
    )
}

# ──────────────────────────────────────────────────────────────────
# 1. ROBUST (heavy tails, nu small)
# ──────────────────────────────────────────────────────────────────

d <- read.delim(resolve_data_path("student_f1_robust.csv"))
d$vowel  <- factor(d$vowel,  levels = c("a", "i", "u"))
d$gender <- factor(d$gender, levels = c("F", "M"))

models_robust <- list()

m1 <- fit_student(f1 ~ vowel * gender, d)
models_robust$M1 <- build_model_entry(
    m1, "f1 ~ vowel * gender", "glmmTMB", "student"
)

m2 <- fit_student(f1 ~ vowel + gender + (1 | speaker), d)
models_robust$M2 <- build_model_entry(
    m2, "f1 ~ vowel + gender + (1|speaker)", "glmmTMB", "student"
)

m3 <- fit_student(f1 ~ vowel + gender + (1 + vowel | speaker), d)
models_robust$M3 <- build_model_entry(
    m3, "f1 ~ vowel + gender + (1+vowel|speaker)", "glmmTMB", "student"
)

m4 <- fit_student(f1 ~ vowel + gender + (1 | speaker) + (1 | word), d)
models_robust$M4 <- build_model_entry(
    m4, "f1 ~ vowel + gender + (1|speaker) + (1|word)", "glmmTMB", "student"
)

m5 <- fit_student(f1 ~ vowel + gender + (1 + vowel | speaker) + (1 | word), d)
models_robust$M5 <- build_model_entry(
    m5, "f1 ~ vowel + gender + (1+vowel|speaker) + (1|word)", "glmmTMB", "student"
)

# ──────────────────────────────────────────────────────────────────
# 2. MILD (nu ≈ 10)
# ──────────────────────────────────────────────────────────────────

d2 <- read.delim(resolve_data_path("student_f1_mild.csv"))
d2$vowel  <- factor(d2$vowel,  levels = c("a", "i", "u"))
d2$gender <- factor(d2$gender, levels = c("F", "M"))

models_mild <- list()

m1 <- fit_student(f1 ~ vowel * gender, d2, start_nu = 10)
models_mild$M1 <- build_model_entry(
    m1, "f1 ~ vowel * gender", "glmmTMB", "student"
)

m2 <- fit_student(f1 ~ vowel + gender + (1 | speaker), d2, start_nu = 10)
models_mild$M2 <- build_model_entry(
    m2, "f1 ~ vowel + gender + (1|speaker)", "glmmTMB", "student"
)

m3 <- fit_student(f1 ~ vowel + gender + (1 + vowel | speaker), d2, start_nu = 10)
models_mild$M3 <- build_model_entry(
    m3, "f1 ~ vowel + gender + (1+vowel|speaker)", "glmmTMB", "student"
)

m4 <- fit_student(f1 ~ vowel + gender + (1 | speaker) + (1 | word), d2, start_nu = 10)
models_mild$M4 <- build_model_entry(
    m4, "f1 ~ vowel + gender + (1|speaker) + (1|word)", "glmmTMB", "student"
)

m5 <- fit_student(f1 ~ vowel + gender + (1 + vowel | speaker) + (1 | word),
                  d2, start_nu = 10)
models_mild$M5 <- build_model_entry(
    m5, "f1 ~ vowel + gender + (1+vowel|speaker) + (1|word)", "glmmTMB", "student"
)

# ──────────────────────────────────────────────────────────────────
# Write reference.
# Two datasets share one JSON; the test_student.phon file dispatches
# on the top-level "robust" / "mild" keys.
# ──────────────────────────────────────────────────────────────────

ref <- list(
    family          = "student",
    datasets        = list(robust = "student_f1_robust.csv",
                           mild   = "student_f1_mild.csv"),
    generated_at    = format(Sys.time(), "%Y-%m-%d %H:%M:%S %Z"),
    R_version       = R.version.string,
    glmmTMB_version = as.character(packageVersion("glmmTMB")),
    robust          = list(models = models_robust),
    mild            = list(models = models_mild)
)

json <- toJSON(ref,
               auto_unbox = TRUE, null = "null", na = "null",
               pretty = TRUE, digits = 12)
writeLines(json, resolve_output_path("reference_student.json"))
cat(sprintf("Wrote reference_student.json (%d + %d models)\n",
            length(models_robust), length(models_mild)))
