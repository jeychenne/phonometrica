# =====================================================================
# gen_reference_blr.R — INLA reference for Chapter 3 (Bayesian Linear
# Regression). Four models adapted from brinla/docs/scripts/blr.R:
#
#   B1 — usair_full:     SO2 ~ negtemp + manuf + wind + precip + days  (gaussian)
#   B2 — usair_reduced:  SO2 ~ negtemp + manuf + wind + precip         (gaussian)
#   B4 — painrelief:     Relief ~ PainLevel + Codeine*Acupuncture      (gaussian)
#
# Dropped from this slice:
#   B3 — usair_t (Student-t robust). On the current INLA `devel` build,
#   no working configuration exists: `inla.mode = "compact"` (the
#   default) and `"experimental"` crash on the Q-matrix non-PD
#   assertion (tabulate-Qfunc.c:121); `"classic"` completes without
#   crashing but silently returns a degenerate fit (intercept ≈ 0.05
#   on data with mean(SO2) ≈ 30, all slopes ≈ 10⁻³, σ ≈ 37 > sd(y),
#   ν posterior essentially uninformative). Phon's Student-t engine
#   is already validated against brms in
#   test/statistics/bayesian/test_student.phon — losing the INLA
#   reference for Student-t doesn't reduce coverage. Re-add when
#   INLA fixes the upstream regression.
#
# Skipped from Ch3 (and why):
#   * Ridge regression with INLA copies — INLA-only formulation.
#   * AR(1)-error models — INLA-only random effect.
#   * The custom-prior usair2 variant — B1 with default-matched priors
#     already exercises the prior path on the Phon side; B1's prior
#     overrides via control.fixed already deviate from brinla's
#     default-INLA-prior usair1 fit (see README "Prior matching").
#
# Datasets: test/data/usair_brinla.csv, test/data/painrelief_brinla.csv
# (generated once by gen_data_brinla.R; committed to the repo).
#
# Runtime: ~10 seconds total on a modern machine. All four fits are
# small (n=41 / n=32) and INLA converges in <2s each.
# =====================================================================

source("_helpers.R")

# ── Common setup ─────────────────────────────────────────────────────
# Both datasets stay as data.frames after read.delim. usair is all
# numeric. painrelief's factor columns are loaded as character; we
# convert them to factor explicitly so INLA uses alphabetical level
# ordering (which matches Phon's default reference-level choice).

usair_path <- resolve_data_path("usair_brinla.csv")
painrelief_path <- resolve_data_path("painrelief_brinla.csv")

if (!file.exists(usair_path)) {
    stop(sprintf("%s not found. Run gen_data_brinla.R first.", usair_path))
}
if (!file.exists(painrelief_path)) {
    stop(sprintf("%s not found. Run gen_data_brinla.R first.", painrelief_path))
}

usair <- read.delim(usair_path)

painrelief <- read.delim(painrelief_path)
painrelief$PainLevel   <- factor(painrelief$PainLevel)
painrelief$Codeine     <- factor(painrelief$Codeine)
painrelief$Acupuncture <- factor(painrelief$Acupuncture)

# ── B1 usair_full ────────────────────────────────────────────────────
pri_g <- phon_inla_priors(usair$SO2, "gaussian")

f1 <- SO2 ~ negtemp + manuf + wind + precip + days
cat("Fitting B1 (usair_full)...\n")
fit_b1 <- fit_inla(f1, usair, "gaussian", pri_g)

# Hyper: residual precision → sd(residual)
hyper_resid_only <- list(
    list(phon_name = "sd(residual)", inla_idx = 1, transform = "sd")
)

m_b1 <- build_model_entry_inla(
    fit_b1,
    "SO2 ~ negtemp + manuf + wind + precip + days",
    "gaussian", usair, "SO2",
    hyper_resid_only
)

# ── B2 usair_reduced ─────────────────────────────────────────────────
f2 <- SO2 ~ negtemp + manuf + wind + precip
cat("Fitting B2 (usair_reduced)...\n")
fit_b2 <- fit_inla(f2, usair, "gaussian", pri_g)

m_b2 <- build_model_entry_inla(
    fit_b2,
    "SO2 ~ negtemp + manuf + wind + precip",
    "gaussian", usair, "SO2",
    hyper_resid_only
)

# ── B3 usair_t — DROPPED from this slice ─────────────────────────────
# See header comment for the diagnostic. Code preserved here for easy
# reactivation once INLA's Student-t bug is fixed upstream:
#
#   pri_t <- inla_defaults()
#   fit_b3 <- fit_inla(f2, usair, "T", pri_t)
#   hyper_student <- list(
#       list(phon_name = "sigma(student)", inla_idx = 1, transform = "sd"),
#       list(phon_name = "nu(student)",    inla_idx = 2, transform = "id")
#   )
#   m_b3 <- build_model_entry_inla(
#       fit_b3, "SO2 ~ negtemp + manuf + wind + precip",
#       "student", usair, "SO2", hyper_student
#   )

# ── B4 painrelief (two-factor ANOVA structure) ──────────────────────
pri_p <- phon_inla_priors(painrelief$Relief, "gaussian")
f4 <- Relief ~ PainLevel + Codeine * Acupuncture
cat("Fitting B4 (painrelief)...\n")
fit_b4 <- fit_inla(f4, painrelief, "gaussian", pri_p)

m_b4 <- build_model_entry_inla(
    fit_b4,
    "Relief ~ PainLevel + Codeine*Acupuncture",
    "gaussian", painrelief, "Relief",
    hyper_resid_only
)

# ── Serialise ─────────────────────────────────────────────────────────
models <- list(B1 = m_b1, B2 = m_b2, B4 = m_b4)

write_reference_inla(
    "blr", models,
    "usair_brinla.csv + painrelief_brinla.csv",
    resolve_output_path("reference_blr.json")
)
cat("Done.\n")
