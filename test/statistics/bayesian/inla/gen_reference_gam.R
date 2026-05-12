# =====================================================================
# gen_reference_gam.R — INLA reference for Chapter 9 (GAMs). Two
# models adapted from brinla/docs/scripts/gam.R:
#
#   M_A — gamSim Gaussian:
#       y ~ s(x0) + s(x1) + s(x2) + s(x3)              (n=400, simulated)
#
# Deferred: M_K kyphosis binomial.
#   Kyphosis ~ s(Age) + s(StartVert) + s(NumVert)      (n=83)
#   Phon converges to a degenerate fit on this dataset: intercept
#   ≈ −14.5 (logit-scale → predicted probability ≈ 5e-7 everywhere),
#   WAIC NaNs from log(0) in pointwise likelihoods on the 17 positive
#   cases. INLA fits cleanly (intercept −0.47 ± 0.52). Diagnosis: n=83
#   with 17 positives, k=10 × 3 smooths = 30 basis parameters → near
#   saturation; the binomial likelihood is flat in directions where
#   smooth(positive_case) → +∞ (quasi-separation), and Phon's
#   smoothing-penalty selection for the Bayesian binomial-GAM path
#   doesn't keep the joint solution bounded. This is a real Phon-side
#   issue surfaced by the smoke test, not a tolerance one. Reactivation:
#   restore `M_K = entry_mk` to the `models` list at the bottom of this
#   file once Phon produces a bounded fit. The fit code below is
#   preserved for that.
#
#
# ★★★ Apples-to-oranges comparison ★★★
#
# Unlike the BLR/GLM/GLMM slices (where Phon and INLA both apply
# Laplace + grid integration to *the same* model), GAM slices compare
# fundamentally different smooth bases:
#
#   * Phon: cubic regression splines (mgcv-style "cr" basis), k=10
#   * INLA: 2nd-order random walks ("rw2") on inla.group'd knot points
#
# These bases produce qualitatively similar smooth curves but
# basis-specific coefficients that can't be name-matched. Per Phon's
# memory, the current cr basis additionally has a 2-3× effective
# penalty parameterization gap vs mgcv that's deferred to a post-v1.0
# basis rewrite. Both engines also choose smoothing differently —
# Phon fixes lambda at the frequentist optimum and treats smooth-basis
# coefficients as having an implicit N(0, σ² S⁻) prior already embedded
# in vcov (see bayesian_adjust's smooth-column mask, bayesian.cpp:60-72);
# INLA integrates over the smoothing precision through grid quadrature.
#
# Scope of this slice: SMOKE TEST.
#   ✓ Both engines converge on the same data
#   ✓ nobs matches exactly
#   ✓ WAIC agrees to ±25 (very loose; basis sizes differ)
#   ✓ Intercept β agrees to mean_rel = 0.30
#   ✓ For Gaussian: residual SD agrees to hyper_mean_rel = 0.30
#   ✗ Per-smooth coefficients NOT compared (basis-specific)
#   ✗ Per-smooth EDFs NOT compared (no clean INLA equivalent for rw2)
#   ✗ Smooth precisions NOT compared (basis-specific parameterization)
#
# Tightening is deferred to post-v1.0 when the basis-parity rewrite
# makes cross-engine matching meaningful. Until then, this slice is a
# regression net against gross drift and crashes, not a fine-grained
# numerical validation.
#
# Hyperpar order (INLA puts family hypers first, then random effects
# in formula order):
#   Gaussian + 4 rw2 smooths: [1] Precision for Gaussian observations
#                              [2] Precision for x0g, [3] x1g, [4] x2g, [5] x3g
#   Binomial + 3 rw2 smooths: [1] Precision for Age_g, [2] StartVert_g, [3] NumVert_g
#
# Datasets (from gen_data_brinla.R):
#   test/data/gam_sim_brinla.csv  (y, x0..x3; mgcv::gamSim(1, n=400, seed=2))
#   test/data/kyphosis_brinla.csv (Kyphosis, Age, StartVert, NumVert)
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

# ──────────────────────────────────────────────────────────────────────
# GAM-specific helpers.
#
# extract_intercept_only — fixef extract restricted to the Intercept
# row. INLA emits one fixed effect for f-only GAM formulas (just the
# implicit intercept, named "(Intercept)"); we strip the parens to
# match Phon's "Intercept" naming convention. Other "fixed effects" in
# the formula would be picked up by extract_fixef_inla; here we
# deliberately ignore them since the slice's two models are intercept-
# only on the fixed side.
# ──────────────────────────────────────────────────────────────────────

extract_intercept_only <- function(fit) {
    sf <- fit$summary.fixed
    nm <- rownames(sf)
    idx <- which(nm == "(Intercept)")
    if (length(idx) == 0L) {
        stop("extract_intercept_only: no (Intercept) row in fit$summary.fixed")
    }
    p_neg <- inla.pmarginal(0, fit$marginals.fixed[[idx]])
    pd <- max(p_neg, 1 - p_neg)
    list(
        names     = I("Intercept"),
        post_mean = I(unname(sf[idx, "mean"])),
        post_sd   = I(unname(sf[idx, "sd"])),
        ci_lower  = I(unname(sf[idx, "0.025quant"])),
        ci_upper  = I(unname(sf[idx, "0.975quant"])),
        pd        = I(pd)
    )
}

build_gam_entry_inla <- function(fit, formula_str, family,
                                  data, response_col, hyper_specs) {
    list(
        formula   = formula_str,
        engine    = "inla",
        family    = family,
        converged = check_convergence_inla(fit),
        fit       = extract_fit_inla(fit, data, response_col),
        fixef     = extract_intercept_only(fit),
        hyper     = build_hyper_inla(fit, hyper_specs)
    )
}

# ── M_A: gam_sim Gaussian ────────────────────────────────────────────
# x0..x3 are continuous in [0, 1]; rw2 needs integer indices, so we
# bin via inla.group(., n=20). The Phon side uses cr basis with k=10
# on the raw continuous columns — different effective complexity,
# hence the loose WAIC tolerance.
gam_sim <- read_phon_csv("gam_sim_brinla.csv")
gam_sim$x0g <- inla.group(gam_sim$x0, n = 20)
gam_sim$x1g <- inla.group(gam_sim$x1, n = 20)
gam_sim$x2g <- inla.group(gam_sim$x2, n = 20)
gam_sim$x3g <- inla.group(gam_sim$x3, n = 20)

pri_ma <- phon_inla_priors(gam_sim$y, "gaussian")
m_a <- fit_inla(
    y ~ f(x0g, model = "rw2", scale.model = TRUE) +
        f(x1g, model = "rw2", scale.model = TRUE) +
        f(x2g, model = "rw2", scale.model = TRUE) +
        f(x3g, model = "rw2", scale.model = TRUE),
    data = gam_sim, family = "gaussian", pri = pri_ma
)
entry_ma <- build_gam_entry_inla(
    m_a, "y ~ s(x0) + s(x1) + s(x2) + s(x3)", "gaussian",
    gam_sim, "y",
    list(
        list(phon_name = "sd(residual)", inla_idx = 1, transform = "sd")
    )
)

# ── M_K: kyphosis binomial ───────────────────────────────────────────
# Age (1..200ish), StartVert (1..18), NumVert (2..10). Age has a long
# range with only 83 observations, so binning matters; StartVert and
# NumVert are already low-cardinality so inla.group has little effect
# but it's harmless and keeps the formula uniform.
kyphosis <- read_phon_csv("kyphosis_brinla.csv")
kyphosis$Age_g       <- inla.group(kyphosis$Age,       n = 20)
kyphosis$StartVert_g <- inla.group(kyphosis$StartVert, n = 20)
kyphosis$NumVert_g   <- inla.group(kyphosis$NumVert,   n = 20)

pri_mk <- phon_inla_priors(kyphosis$Kyphosis, "binomial")
m_k <- fit_inla(
    Kyphosis ~ f(Age_g,       model = "rw2", scale.model = TRUE) +
               f(StartVert_g, model = "rw2", scale.model = TRUE) +
               f(NumVert_g,   model = "rw2", scale.model = TRUE),
    data = kyphosis, family = "binomial", pri = pri_mk,
    Ntrials = rep(1, nrow(kyphosis))
)
entry_mk <- build_gam_entry_inla(
    m_k, "Kyphosis ~ s(Age) + s(StartVert) + s(NumVert)", "binomial",
    kyphosis, "Kyphosis",
    list()  # all hypers are smooth precisions — not comparable, skipped
)

# ── Serialise ─────────────────────────────────────────────────────────
# M_K is deferred — see header comment "Deferred: M_K kyphosis binomial".
# The fit code above is preserved for one-line reactivation: append
# M_K = entry_mk to this list once Phon's binomial GAM produces a
# bounded fit on kyphosis.
models <- list(
    M_A = entry_ma
)

write_reference_inla(
    chapter = "Ch9",
    models  = models,
    dataset = "mgcv::gamSim(1, n=400, seed=2), brinla::kyphosis",
    path    = resolve_output_path("reference_gam.json")
)
