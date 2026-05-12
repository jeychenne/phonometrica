# =====================================================================
# gen_reference_glm.R — INLA reference for Chapter 4 (GLMs). Six
# models adapted from brinla/docs/scripts/glm.R:
#
#   G1 — lowbwt full:    LOW ~ AGE + LWT + RACE + SMOKE + HT + UI + FTV  (binomial, n=189)
#   G2 — lowbwt reduced: LOW ~ LWT + RACE + SMOKE + HT + UI               (binomial, n=189)
#   G3 — AIDS linear:    DEATHS ~ TIME                                    (poisson, n=14)
#   G4 — AIDS log:       DEATHS ~ log_time   (log_time precomputed)       (poisson, n=14)
#   G5 — crab NB:        SATELLITES ~ COLOR + SPINE + WIDTH                (nbinomial, n=173)
#   G6 — crab Poisson:   SATELLITES ~ COLOR + SPINE + WIDTH                (poisson, n=173)
#
# Skipped from Ch4 (and why):
#   * Insurance Poisson with offset — separate dataset, marginal value
#     for engine validation
#   * Gamma regression (wafer) — Phon doesn't support family="gamma"
#   * Beta regression (gas) — not covered in this slice
#   * Zero-inflated Poisson/NB (articles) — INLA-only families
#
# Carve-outs:
#   G5 (NB) — INLA uses its default loggamma prior on size = θ; Phon
#   uses Gamma(1, 0.01) on theta(NB). β stays prior-matched via
#   phon_inla_priors. test_glm.phon's tol_g5 widens hyper tolerance.
#
# Datasets (from gen_data_brinla.R):
#   test/data/lowbwt_brinla.csv
#   test/data/aids_brinla.csv     (TIME, DEATHS, log_time precomputed)
#   test/data/crab_brinla.csv     (COLOR, SPINE recoded to strings)
# =====================================================================

source("_helpers.R")

# ── Load and prepare data ────────────────────────────────────────────
lowbwt_path <- resolve_data_path("lowbwt_brinla.csv")
aids_path   <- resolve_data_path("aids_brinla.csv")
crab_path   <- resolve_data_path("crab_brinla.csv")

for (p in c(lowbwt_path, aids_path, crab_path)) {
    if (!file.exists(p)) {
        stop(sprintf("%s missing. Run gen_data_brinla.R first.", p))
    }
}

lowbwt <- read.delim(lowbwt_path)
lowbwt$RACE  <- factor(lowbwt$RACE)
lowbwt$SMOKE <- factor(lowbwt$SMOKE)
lowbwt$HT    <- factor(lowbwt$HT)
lowbwt$UI    <- factor(lowbwt$UI)

aids <- read.delim(aids_path)

crab <- read.delim(crab_path)
crab$COLOR <- factor(crab$COLOR)
crab$SPINE <- factor(crab$SPINE)

# ── G1 lowbwt full ───────────────────────────────────────────────────
# LOW ~ AGE + LWT + RACE + SMOKE + HT + UI + FTV   (binomial Bernoulli)
# brinla uses Ntrials = 1 (scalar, broadcast to all rows). Phon
# implicitly assumes Bernoulli per row, so the configurations align.
pri_lb <- phon_inla_priors(lowbwt$LOW, "binomial")
f_g1 <- LOW ~ AGE + LWT + RACE + SMOKE + HT + UI + FTV
cat("Fitting G1 (lowbwt_full)...\n")
fit_g1 <- fit_inla(f_g1, lowbwt, "binomial", pri_lb, Ntrials = 1)

m_g1 <- build_model_entry_inla(
    fit_g1, "LOW ~ AGE + LWT + RACE + SMOKE + HT + UI + FTV",
    "binomial", lowbwt, "LOW",
    list()  # binomial has no family-level dispersion hyperparameter
)

# ── G2 lowbwt reduced ────────────────────────────────────────────────
f_g2 <- LOW ~ LWT + RACE + SMOKE + HT + UI
cat("Fitting G2 (lowbwt_reduced)...\n")
fit_g2 <- fit_inla(f_g2, lowbwt, "binomial", pri_lb, Ntrials = 1)

m_g2 <- build_model_entry_inla(
    fit_g2, "LOW ~ LWT + RACE + SMOKE + HT + UI",
    "binomial", lowbwt, "LOW",
    list()
)

# ── G3 AIDS linear-TIME ──────────────────────────────────────────────
pri_aids <- phon_inla_priors(aids$DEATHS, "poisson")
f_g3 <- DEATHS ~ TIME
cat("Fitting G3 (AIDS_linear)...\n")
fit_g3 <- fit_inla(f_g3, aids, "poisson", pri_aids)

m_g3 <- build_model_entry_inla(
    fit_g3, "DEATHS ~ TIME",
    "poisson", aids, "DEATHS",
    list()  # poisson has no family-level dispersion hyperparameter
)

# ── G4 AIDS log-TIME ────────────────────────────────────────────────
# Phon's formula parser doesn't accept log(TIME) directly; the
# log_time column was precomputed by gen_data_brinla.R. Both engines
# read the same precomputed column for the matched comparison.
f_g4 <- DEATHS ~ log_time
cat("Fitting G4 (AIDS_log)...\n")
fit_g4 <- fit_inla(f_g4, aids, "poisson", pri_aids)

m_g4 <- build_model_entry_inla(
    fit_g4, "DEATHS ~ log_time",
    "poisson", aids, "DEATHS",
    list()
)

# ── G5 crab NB ───────────────────────────────────────────────────────
# INLA labels the NB hyperparameter "size", which equals glmmTMB θ,
# brms shape, and Phon's theta(NB) — no transform needed. The prior
# on size is INLA's default loggamma (the NB carve-out); Phon's
# default is Gamma(1, 0.01). test_glm.phon's tol_g5 widens hyper
# tolerance to absorb the resulting θ-posterior shift. β stays
# matched.
pri_crab <- phon_inla_priors(crab$SATELLITES, "negbin")
f_g5 <- SATELLITES ~ COLOR + SPINE + WIDTH
cat("Fitting G5 (crab_NB)...\n")
fit_g5 <- fit_inla(f_g5, crab, "nbinomial", pri_crab)

hyper_nb <- list(
    list(phon_name = "theta(NB)", inla_idx = 1, transform = "id")
)
m_g5 <- build_model_entry_inla(
    fit_g5, "SATELLITES ~ COLOR + SPINE + WIDTH",
    "negbin", crab, "SATELLITES",
    hyper_nb
)

# ── G6 crab Poisson ──────────────────────────────────────────────────
pri_crab_p <- phon_inla_priors(crab$SATELLITES, "poisson")
cat("Fitting G6 (crab_Poisson)...\n")
fit_g6 <- fit_inla(f_g5, crab, "poisson", pri_crab_p)

m_g6 <- build_model_entry_inla(
    fit_g6, "SATELLITES ~ COLOR + SPINE + WIDTH",
    "poisson", crab, "SATELLITES",
    list()
)

# ── Serialise ─────────────────────────────────────────────────────────
models <- list(
    G1 = m_g1, G2 = m_g2,
    G3 = m_g3, G4 = m_g4,
    G5 = m_g5, G6 = m_g6
)

write_reference_inla(
    "glm", models,
    "lowbwt_brinla.csv + aids_brinla.csv + crab_brinla.csv",
    resolve_output_path("reference_glm.json")
)
cat("Done.\n")
