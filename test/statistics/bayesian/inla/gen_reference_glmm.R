# =====================================================================
# gen_reference_glmm.R — INLA reference for Chapter 5 (GLMMs). Four
# models adapted from brinla/docs/scripts/glmm.R:
#
#   R1 — reeds (Gaussian + RE intercept):
#       nitrogen ~ 1 + (1|site)                                  (n=15,   3 sites)
#   R2 — reading (Gaussian + RE intercept):
#       piat ~ agegrp + (1|id)                                   (n=233,  89 ids)
#   R4 — nitrofen (Poisson + RE intercept):
#       live ~ conc_scaled * brood + (1|id)                      (n=150,  50 ids)
#   R5 — ohio (Binomial + RE intercept):
#       resp ~ age + smoke + (1|id)                              (n=2148, 537 ids)
#
# Deferred from this slice:
#   R3 — reading (Gaussian + correlated RE slope+intercept):
#       piat ~ cagegrp + (cagegrp|id)
#   The INLA-side construction is iid2d:
#       f(id, model="iid2d", n=2*N) + f(id2, cagegrp, copy="id")
#   with a Wishart-like prior on the joint covariance. Matched PC
#   priors on (sd_int, sd_slope) independently don't have a clean
#   iid2d parameterization. The suite's purpose is to validate Phon's
#   INLA-method engine, not to reproduce R-INLA's iid2d prior
#   parameterization quirks. Reactivation if/when wanted: add an R3
#   fit with carve-out hyper tolerance on the two SDs and skip the
#   correlation, matching the brms-helper policy.
#
# Solver paths exercised:
#   R1, R2 → inla_grid_integrate_gaussian
#   R4, R5 → inla_grid_integrate_pirls (validates PIRLS step-halving
#                                       and mixed-effects SLA)
#
# Hyperpar order (verified against brinla docs; index lookup is robust
# as long as INLA's documented "family hypers first, then REs in
# formula order" convention holds):
#   Gaussian + iid RE: [1] Precision for Gaussian observations,
#                      [2] Precision for <group>
#   Poisson + iid RE:  [1] Precision for <group>
#   Binomial + iid RE: [1] Precision for <group>
#
# Datasets (from gen_data_brinla.R):
#   test/data/reeds_brinla.csv     (site is char factor A/B/C, no recode)
#   test/data/reading_brinla.csv   (id S1..S89, cagegrp = agegrp - 8.5 precomputed)
#   test/data/nitrofen_brinla.csv  (long-form, conc_scaled = conc/300, brood b1/b2/b3)
#   test/data/ohio_brinla.csv      (id S1..S537, age -2..1, smoke 0/1)
# =====================================================================

# This file lives in bayesian/inla/, so _helpers.R is a sibling.
SCRIPT_DIR <- (function() {
    args <- commandArgs(trailingOnly = FALSE)
    file_arg <- grep("^--file=", args, value = TRUE)
    if (length(file_arg) > 0L) {
        return(normalizePath(dirname(sub("^--file=", "", file_arg[1]))))
    }
    getwd()
})()
source(file.path(SCRIPT_DIR, "_helpers.R"))

read_phon_csv <- function(rel) {
    path <- resolve_data_path(rel)
    read.table(path, sep = "\t", header = TRUE,
               stringsAsFactors = FALSE, na.strings = "NA")
}

# ── R1: reeds (Gaussian + RE intercept) ──────────────────────────────
# Only 3 sites — sd(Intercept|site) posterior is prior-dominated; the
# test side widens hyper tolerance for this model.
reeds <- read_phon_csv("reeds_brinla.csv")
pri_r1 <- phon_inla_priors(reeds$nitrogen, "gaussian")
m_r1 <- fit_inla(
    nitrogen ~ 1 + f(site, model = "iid",
                     hyper = pc_re_hyper(pri_r1$pc.prec.scale)),
    data = reeds, family = "gaussian", pri = pri_r1
)
entry_r1 <- build_model_entry_inla(
    m_r1, "nitrogen ~ 1 + (1|site)", "gaussian", reeds, "nitrogen",
    list(
        list(phon_name = "sd(residual)",       inla_idx = 1, transform = "sd"),
        list(phon_name = "sd(Intercept|site)", inla_idx = 2, transform = "sd")
    )
)

# ── R2: reading (Gaussian + RE intercept) ────────────────────────────
reading <- read_phon_csv("reading_brinla.csv")
pri_r2 <- phon_inla_priors(reading$piat, "gaussian")
m_r2 <- fit_inla(
    piat ~ agegrp + f(id, model = "iid",
                      hyper = pc_re_hyper(pri_r2$pc.prec.scale)),
    data = reading, family = "gaussian", pri = pri_r2
)
entry_r2 <- build_model_entry_inla(
    m_r2, "piat ~ agegrp + (1|id)", "gaussian", reading, "piat",
    list(
        list(phon_name = "sd(residual)",     inla_idx = 1, transform = "sd"),
        list(phon_name = "sd(Intercept|id)", inla_idx = 2, transform = "sd")
    )
)

# ── R4: nitrofen (Poisson + RE intercept) ────────────────────────────
# `conc_scaled * brood` expands lme4-style on both sides:
#   conc_scaled + brood + conc_scaled:brood
# Verified Phon's parser does this expansion (formula.cpp expand_term_chain).
nitrofen <- read_phon_csv("nitrofen_brinla.csv")
pri_r4 <- phon_inla_priors(nitrofen$live, "poisson")
m_r4 <- fit_inla(
    live ~ conc_scaled * brood + f(id, model = "iid",
                                   hyper = pc_re_hyper(pri_r4$pc.prec.scale)),
    data = nitrofen, family = "poisson", pri = pri_r4
)
entry_r4 <- build_model_entry_inla(
    m_r4, "live ~ conc_scaled * brood + (1|id)", "poisson", nitrofen, "live",
    list(
        list(phon_name = "sd(Intercept|id)", inla_idx = 1, transform = "sd")
    )
)

# ── R5: ohio (Binomial + RE intercept) ───────────────────────────────
# `resp` is Bernoulli (0/1) so Ntrials = 1. INLA's default for
# family="binomial" without Ntrials is 1, but we pass it explicitly
# for clarity and version-robustness.
ohio <- read_phon_csv("ohio_brinla.csv")
pri_r5 <- phon_inla_priors(ohio$resp, "binomial")
m_r5 <- fit_inla(
    resp ~ age + smoke + f(id, model = "iid",
                           hyper = pc_re_hyper(pri_r5$pc.prec.scale)),
    data = ohio, family = "binomial", pri = pri_r5,
    Ntrials = rep(1, nrow(ohio))
)
entry_r5 <- build_model_entry_inla(
    m_r5, "resp ~ age + smoke + (1|id)", "binomial", ohio, "resp",
    list(
        list(phon_name = "sd(Intercept|id)", inla_idx = 1, transform = "sd")
    )
)

# ── Serialise ─────────────────────────────────────────────────────────
# R3 is deferred — see header comment. Reactivation: add an R3 fit
# above and append `R3 = entry_r3,` to this list.
models <- list(
    R1 = entry_r1,
    R2 = entry_r2,
    R4 = entry_r4,
    R5 = entry_r5
)

write_reference_inla(
    chapter = "Ch5",
    models  = models,
    dataset = "brinla::reeds, brinla::reading, boot::nitrofen, brinla::ohio",
    path    = resolve_output_path("reference_glmm.json")
)
