# =====================================================================
# gen_reference_nested.R — nested-RE reference using a synthetic
# school/classroom/student design.
#
# Why synthetic (and not a real corpus dataset):
#   * Need clean, balanced nesting to keep tolerances tight: 8 schools
#     × 4 classrooms × 10 students = 320 obs, fully balanced.
#   * Exercises the new (1|school/classroom) slash sugar end-to-end,
#     which has no analogue in any of the existing reference fixtures.
#   * glmmTMB and lme4 desugar (1|g1/g2) to (1|g1) + (1|g1:g2), where
#     the second group's name is the literal string "g1:g2". This is
#     exactly the synthetic-group representation Phonometrica uses
#     internally, so the ranef_name lookup matches verbatim.
#
# Three models, each fitted by glmmTMB with REML = FALSE:
#   M1 — y ~ program + (1|school)                      [non-nested baseline]
#   M2 — y ~ program + (1|school) + (1|school:classroom)
#   M3 — y ~ program + (1|school/classroom)            [identical to M2]
#
# M2 and M3 must agree to numerical noise on every fit field — that's
# the slash-sugar correctness contract. The phon test exercises this
# by fitting the slash form and checking against the M3 entry.
# =====================================================================

source("_helpers.R")
suppressPackageStartupMessages({library(glmmTMB)})

# ── Synthetic data ────────────────────────────────────────────────
set.seed(20260510)

n_schools    <- 8
n_classrooms <- 4    # per school
n_students   <- 10   # per classroom

n_obs <- n_schools * n_classrooms * n_students   # 320

school_ids    <- paste0("S", 1:n_schools)
classroom_ids <- paste0("C", 1:n_classrooms)

# School effects ~ N(0, 0.6^2); classroom-within-school effects ~ N(0, 0.4^2).
sd_school    <- 0.6
sd_classroom <- 0.4
sd_resid     <- 1.0

u_school <- setNames(rnorm(n_schools, 0, sd_school), school_ids)
# Classroom effect indexed by (school, classroom) pair.
u_classroom <- matrix(rnorm(n_schools * n_classrooms, 0, sd_classroom),
                      nrow = n_schools, ncol = n_classrooms,
                      dimnames = list(school_ids, classroom_ids))

# Build long-form data frame.
d <- expand.grid(student   = 1:n_students,
                 classroom = classroom_ids,
                 school    = school_ids,
                 KEEP.OUT.ATTRS = FALSE,
                 stringsAsFactors = FALSE)
d$program <- rep_len(c("A", "B"), n_obs)   # alternating, balanced

beta0 <- 5.0
beta_programB <- 0.8

d$y <- beta0 +
       beta_programB * (d$program == "B") +
       u_school[d$school] +
       u_classroom[cbind(d$school, d$classroom)] +
       rnorm(n_obs, 0, sd_resid)

# Phonometrica's auto-detector treats "S1", "C1", "A"/"B" as text — good.
d <- d[, c("school", "classroom", "program", "y")]

# ── Write data CSV (consumed by both R and the phon test) ────────
data_path <- file.path("..", "..", "data", "nested_synthetic.csv")
write.csv(d, data_path, row.names = FALSE)
cat("Wrote", nrow(d), "rows to", data_path, "\n")

# ── Fit all three models ─────────────────────────────────────────
m1 <- glmmTMB(y ~ program + (1|school),
              data = d, REML = FALSE)
m2 <- glmmTMB(y ~ program + (1|school) + (1|school:classroom),
              data = d, REML = FALSE)
m3 <- glmmTMB(y ~ program + (1|school/classroom),
              data = d, REML = FALSE)

# Sanity: M2 and M3 must agree on logLik to ~1e-6.
stopifnot(abs(as.numeric(logLik(m2)) - as.numeric(logLik(m3))) < 1e-6)
cat("M2 vs M3 logLik agree to <1e-6 — OK\n")

# ── Serialise ─────────────────────────────────────────────────────
models <- list(
    M1 = build_model_entry(m1, "y ~ program + (1|school)",
                           "glmmTMB", "gaussian"),
    M2 = build_model_entry(m2, "y ~ program + (1|school) + (1|school:classroom)",
                           "glmmTMB", "gaussian"),
    M3 = build_model_entry(m3, "y ~ program + (1|school/classroom)",
                           "glmmTMB", "gaussian")
)

write_reference("nested", models, "nested_synthetic.csv", "reference_nested.json")
cat("Done.\n")
