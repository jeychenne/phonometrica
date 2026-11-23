# =====================================================================
# gen_reference_gam.R — generates reference_gam.json for test_gam.phon
# Dataset: test/data/gam_test.csv  (already in the repo)
# Run from: test/statistics/frequentist/
# =====================================================================

library(mgcv)
library(jsonlite)

d <- read.csv(file.path("..", "..", "data", "gam_test.csv"), sep="\t",
              stringsAsFactors = FALSE)
d$vowel <- factor(d$vowel)
d$speaker <- factor(d$speaker)
cat("Loaded gam_test.csv: n =", nrow(d), "\n")

# Smooth summary as parallel arrays — same pattern as fixef/ranef in
# the existing reference JSONs, which Phonometrica load_json handles
# correctly. log10_sp is comparable with Phonometrica's smooth_log_lambda
# because both scale the penalties the same way (mgcv's scale.penalty),
# one entry per smooth (per level for by-factor smooths) in the same order.
smooth_block <- function(m, s) {
  has_F <- "F" %in% colnames(s$s.table)
  list(
    names    = I(rownames(s$s.table)),
    edf      = I(round(as.numeric(s$edf), 4)),
    F        = I(if (has_F) round(as.numeric(s$s.table[,"F"]), 4)
               else       round(as.numeric(s$s.table[,"Chi.sq"]), 4)),
    p        = I(round(as.numeric(s$s.table[,"p-value"]), 6)),
    log10_sp = I(round(as.numeric(log10(m$sp)), 4))
  )
}

# Fit one model and package everything check_gam compares. `formula` is
# the Phonometrica spelling, stored for reference only.
#   method "GCV.Cp" (mgcv's default; Phonometrica's default): GCV for
#     Gaussian, UBRE for Poisson/binomial.
#   method "REML": Phonometrica's fit_method = "REML". mgcv also uses REML
#     for its extended families (nb, betar, scat) whatever the method.
# AIC is left out of REML references: mgcv's AIC for a REML fit counts
# edf2, the EDF corrected for smoothing-parameter uncertainty (Wood, Pya &
# Saefken 2016), which Phonometrica does not compute; its AIC uses
# sum(edf) (+1 for an estimated scale, + the extra parameters).
ref_model <- function(r_formula, formula, family = gaussian(), data = d, method = "GCV.Cp") {
  m <- gam(r_formula, data = data, family = family, method = method)
  s <- summary(m)
  fit <- list(nobs = nrow(data), loglik = round(as.numeric(logLik(m)), 4))
  if (method != "REML" && is.null(family$getTheta)) fit$aic <- round(AIC(m), 4)
  if (family$family == "gaussian") {
    fit$r2_adj <- round(s$r.sq, 4)
    fit$scale  <- round(sqrt(m$sig2), 6)
  }
  # Extra parameters of the extended families, under Phonometrica's names.
  fam <- m$family$family
  if (grepl("^Negative Binomial", fam)) fit$theta <- round(m$family$getTheta(TRUE), 5)
  if (grepl("^Beta regression", fam))   fit$phi   <- round(m$family$getTheta(TRUE), 5)
  if (grepl("^Scaled t", fam)) {
    th <- m$family$getTheta(TRUE)
    fit$nu <- round(th[1], 5); fit$sigma <- round(th[2], 5)
  }
  list(formula = formula, method = method, fit = fit,
       fixef   = list(names    = I(rownames(s$p.table)),
                      estimate = I(round(as.numeric(s$p.table[, "Estimate"]), 5)),
                      se       = I(round(as.numeric(s$p.table[, "Std. Error"]), 5))),
       smooths = smooth_block(m, s))
}

out <- list(models = list())

# M1: Gaussian, single smooth
out$models$M1 <- ref_model(y_gauss ~ s(duration, bs="cr", k=10),
                           "y_gauss ~ s(duration, bs=cr, k=10)")
# M2: Gaussian, factor + smooth
out$models$M2 <- ref_model(y_gauss ~ vowel + s(duration, bs="cr", k=10),
                           "y_gauss ~ vowel + s(duration, bs=cr, k=10)")
# M3: Gaussian, two smooths
out$models$M3 <- ref_model(y_gauss ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
                           "y_gauss ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)")
# M4: Gaussian, by-factor smooth (vowel was converted to factor at top)
out$models$M4 <- ref_model(y_gauss ~ vowel + s(duration, by=vowel, bs="cr", k=8),
                           "y_gauss ~ vowel + s(duration, by=vowel, bs=cr, k=8)")
# M5: Poisson, single smooth
out$models$M5 <- ref_model(y_count ~ s(duration, bs="cr", k=8),
                           "y_count ~ s(duration, bs=cr, k=8)", poisson())
# M6: Gaussian, random-intercept smooth
out$models$M6 <- ref_model(y_gauss ~ duration + s(speaker, bs="re"),
                           "y_gauss ~ duration + s(speaker, bs=re)")
# M7: Gaussian, random intercepts + uncorrelated random slopes
out$models$M7 <- ref_model(y_gauss ~ duration + s(speaker, bs="re") + s(speaker, by=duration, bs="re"),
                           "y_gauss ~ duration + s(speaker, bs=re) + s(speaker, by=duration, bs=re)")
# M8: Gaussian, smooth + random-intercept smooth
out$models$M8 <- ref_model(y_gauss ~ vowel + s(duration, bs="cr", k=8) + s(speaker, bs="re"),
                           "y_gauss ~ vowel + s(duration, bs=cr, k=8) + s(speaker, bs=re)")
# M9: Binomial, single smooth
out$models$M9 <- ref_model(y_bin ~ s(duration, bs="cr", k=8),
                           "y_bin ~ s(duration, bs=cr, k=8)", binomial())

# M10: Poisson, two smooths (one smoothing parameter each)
out$models$M10 <- ref_model(y_count ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
                            "y_count ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)", poisson())
# M11: Binomial, two smooths (one at its linear limit, one wiggly)
out$models$M11 <- ref_model(y_bin ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
                            "y_bin ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)", binomial())
# M12: Poisson, random-intercept smooth
out$models$M12 <- ref_model(y_count ~ duration + s(speaker, bs="re"),
                            "y_count ~ duration + s(speaker, bs=re)", poisson())
# ── REML (fit_method = "REML") ──────────────────────────────────────
out$models$R1 <- ref_model(y_gauss ~ s(duration, bs="cr", k=10),
                           "y_gauss ~ s(duration, bs=cr, k=10)", method = "REML")
out$models$R2 <- ref_model(y_gauss ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
                           "y_gauss ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)", method = "REML")
out$models$R3 <- ref_model(y_gauss ~ duration + s(speaker, bs="re"),
                           "y_gauss ~ duration + s(speaker, bs=re)", method = "REML")
out$models$R4 <- ref_model(y_count ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
                           "y_count ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)", poisson(), method = "REML")
out$models$R5 <- ref_model(y_bin ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
                           "y_bin ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)", binomial(), method = "REML")

# ── Extended families: REML, extra parameters estimated jointly ───────
# Beta and Student-t responses come from gam_ext.csv (gen_data_gam_ext.R).
e <- read.csv(file.path("..", "..", "data", "gam_ext.csv"), sep="\t",
              stringsAsFactors = FALSE)
out$models$X1 <- ref_model(y_nb ~ s(duration, bs="cr", k=8),
                           "y_nb ~ s(duration, bs=cr, k=8)", nb(), method = "REML")
out$models$X2 <- ref_model(y_beta ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
                           "y_beta ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)", betar(),
                           data = e, method = "REML")
out$models$X3 <- ref_model(y_t ~ s(duration, bs="cr", k=8),
                           "y_t ~ s(duration, bs=cr, k=8)", scat(), data = e, method = "REML")

# Not included: y_count ~ vowel + s(duration, by=vowel, k=8) (Poisson).
# For level i its UBRE has two minima of almost equal height (EDF 4.9 at
# log10(sp) 2.45, and a straight line as sp -> Inf); mgcv's Newton search
# stops at the first, Phonometrica's grid finds the second, which is
# slightly lower (by 2.5e-4). Both are correct answers to the criterion.

out$meta <- list(engine="mgcv", version=as.character(packageVersion("mgcv")),
                 R_version=R.version$version.string, dataset="gam_test.csv",
                 generated=format(Sys.time(),"%Y-%m-%d %H:%M:%S %Z"))
write(toJSON(out, pretty=TRUE, auto_unbox=TRUE), "reference_gam.json")
cat("Written: reference_gam.json\n\n")
for (nm in names(out$models)) {
  mo <- out$models[[nm]]
  cat(sprintf("  %-4s %-6s loglik=%.3f  EDF[1]=%.3f  log10(sp)[1]=%.3f\n",
              nm, mo$method, mo$fit$loglik, mo$smooths$edf[1], mo$smooths$log10_sp[1]))
}
