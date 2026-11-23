# =====================================================================
# gen_reference_gam_predict.R — generates reference_gam_predict.json
# and ../../data/gam_newdata.csv for test_gam_predict.phon, which checks
# predict() for GAMs against mgcv's predict.gam(type = "link",
# se.fit = TRUE).
# Dataset: test/data/gam_test.csv (the data of gen_reference_gam.R)
# Run from: test/statistics/frequentist/
#
# What is tested is predict(), not the λ search: test_gam.phon already
# compares the fits. The two programs can stop at slightly different λ
# (M4 level u is on a flat GCV stretch where Phonometrica finds the lower
# score; M6 and M12 differ in the 4th decimal of log10 λ), and those small
# λ differences move the predictions by up to 0.4% (fit) and 11% (SE),
# swamping anything predict() itself might get wrong. So mgcv is refitted
# here with sp fixed at Phonometrica's own smoothing parameters (LOG10_SP
# below, copied from model.smooth_log_lambda); predict.gam then evaluates
# exactly the model Phonometrica fitted. test_gam_predict.phon first
# checks that Phonometrica still selects these λ: if the fitter changes,
# update LOG10_SP and rerun this script.
#
# The new data cross every vowel, four speakers (S99 is unseen: both
# programs give it a zero random effect, i.e. a population-level
# prediction; mgcv warns "factor levels S99 not in original fit") and
# durations that include values outside the observed range [40.71,
# 299.44], where both programs extrapolate the cr basis linearly.
# For models with random-effect smooths, fit_pop/se_pop are the
# population-level predictions (predict.gam's `exclude` of every bs="re"
# term), Phonometrica's predict() option re_smooths = "none".
# =====================================================================

library(mgcv)
library(jsonlite)

d <- read.csv(file.path("..", "..", "data", "gam_test.csv"), sep="\t",
              stringsAsFactors = FALSE)
d$vowel <- factor(d$vowel)
d$speaker <- factor(d$speaker)

nd <- expand.grid(duration = c(20, 40.71, 75, 120, 170, 230, 299.44, 330),
                  vowel = c("a", "i", "u"),
                  speaker = c("S00", "S07", "S29", "S99"),
                  stringsAsFactors = FALSE)
nd <- nd[, c("speaker", "vowel", "duration")]
write.table(nd, file.path("..", "..", "data", "gam_newdata.csv"), sep = "\t",
            quote = FALSE, row.names = FALSE)

# Phonometrica's log10(λ), one per smoothing parameter (per level for
# the by-factor smooth), in model.smooth_log_lambda order.
LOG10_SP <- list(
  M4  = c(3.09590160921612, 11.4667752150256, 3.62107065229506),
  M6  = 0.53515625,
  M7  = c(0.535078886268821, 12),
  M8  = c(3.72289220830426, 0.267342512123287),
  M12 = 0.9921875
)

ref_predict <- function(tag, r_formula, formula, family = gaussian()) {
  m <- gam(r_formula, data = d, family = family, sp = 10^LOG10_SP[[tag]])
  p <- suppressWarnings(predict(m, nd, type = "link", se.fit = TRUE))
  out <- list(formula = formula, family = family$family,
              log10_sp = I(LOG10_SP[[tag]]),
              fit = I(as.numeric(p$fit)), se = I(as.numeric(p$se.fit)))
  # Population level (Phonometrica's re_smooths = "none"): every bs="re"
  # term excluded.
  re_terms <- vapply(m$smooth, function(sm) sm$label, "")[
    vapply(m$smooth, function(sm) inherits(sm, "random.effect"), TRUE)]
  if (length(re_terms) > 0) {
    q <- predict(m, nd, type = "link", se.fit = TRUE, exclude = re_terms)
    out$fit_pop <- I(as.numeric(q$fit))
    out$se_pop  <- I(as.numeric(q$se.fit))
  }
  out
}

out <- list(models = list())
out$models$M4 <- ref_predict("M4", y_gauss ~ vowel + s(duration, by=vowel, bs="cr", k=8),
                             "y_gauss ~ vowel + s(duration, by=vowel, bs=cr, k=8)")
out$models$M6 <- ref_predict("M6", y_gauss ~ duration + s(speaker, bs="re"),
                             "y_gauss ~ duration + s(speaker, bs=re)")
out$models$M7 <- ref_predict("M7", y_gauss ~ duration + s(speaker, bs="re") + s(speaker, by=duration, bs="re"),
                             "y_gauss ~ duration + s(speaker, bs=re) + s(speaker, by=duration, bs=re)")
out$models$M8 <- ref_predict("M8", y_gauss ~ vowel + s(duration, bs="cr", k=8) + s(speaker, bs="re"),
                             "y_gauss ~ vowel + s(duration, bs=cr, k=8) + s(speaker, bs=re)")
out$models$M12 <- ref_predict("M12", y_count ~ duration + s(speaker, bs="re"),
                              "y_count ~ duration + s(speaker, bs=re)", poisson())

out$meta <- list(engine="mgcv", version=as.character(packageVersion("mgcv")),
                 R_version=R.version$version.string, dataset="gam_test.csv",
                 newdata="gam_newdata.csv",
                 generated=format(Sys.time(),"%Y-%m-%d %H:%M:%S %Z"))
write(toJSON(out, pretty=TRUE, auto_unbox=TRUE, digits=NA), "reference_gam_predict.json")
cat("Written: reference_gam_predict.json and ../../data/gam_newdata.csv\n")
