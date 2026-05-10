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
cat("Loaded gam_test.csv: n =", nrow(d), "\n")

# Smooth summary as parallel arrays — same pattern as fixef/ranef in
# the existing reference JSONs, which Phonometrica load_json handles
# correctly.
smooth_block <- function(s) {
  has_F <- "F" %in% colnames(s$s.table)
  list(
    names = I(rownames(s$s.table)),
    edf   = I(round(as.numeric(s$edf), 4)),
    F     = I(if (has_F) round(as.numeric(s$s.table[,"F"]), 4)
            else       round(as.numeric(s$s.table[,"Chi.sq"]), 4)),
    p     = I(round(as.numeric(s$s.table[,"p-value"]), 6))
  )
}

out <- list(models = list())

# M1: Gaussian, single smooth
m1 <- gam(y_gauss ~ s(duration, bs="cr", k=10), data=d, method="GCV.Cp")
s1 <- summary(m1)
out$models$M1 <- list(
  formula = "y_gauss ~ s(duration, bs=cr, k=10)",
  fit     = list(nobs=nrow(d), loglik=round(as.numeric(logLik(m1)),4),
                 aic=round(AIC(m1),4), r2_adj=round(s1$r.sq,4),
                 scale=round(sqrt(m1$sig2),6)),
  fixef   = list(names=I("(Intercept)"),
                 estimate=I(round(coef(m1)["(Intercept)"],4)),
                 se=I(round(as.numeric(s1$p.table["(Intercept)","Std. Error"]),4))),
  smooths = smooth_block(s1))

# M2: Gaussian, factor + smooth
m2 <- gam(y_gauss ~ vowel + s(duration, bs="cr", k=10), data=d, method="GCV.Cp")
s2 <- summary(m2)
out$models$M2 <- list(
  formula = "y_gauss ~ vowel + s(duration, bs=cr, k=10)",
  fit     = list(nobs=nrow(d), loglik=round(as.numeric(logLik(m2)),4),
                 aic=round(AIC(m2),4), r2_adj=round(s2$r.sq,4),
                 scale=round(sqrt(m2$sig2),6)),
  fixef   = list(names=I(rownames(s2$p.table)),
                 estimate=I(round(as.numeric(s2$p.table[,"Estimate"]),4)),
                 se=I(round(as.numeric(s2$p.table[,"Std. Error"]),4))),
  smooths = smooth_block(s2))

# M3: Gaussian, two smooths
m3 <- gam(y_gauss ~ s(duration, bs="cr", k=8) + s(age, bs="cr", k=8),
          data=d, method="GCV.Cp")
s3 <- summary(m3)
out$models$M3 <- list(
  formula = "y_gauss ~ s(duration, bs=cr, k=8) + s(age, bs=cr, k=8)",
  fit     = list(nobs=nrow(d), loglik=round(as.numeric(logLik(m3)),4),
                 aic=round(AIC(m3),4), r2_adj=round(s3$r.sq,4),
                 scale=round(sqrt(m3$sig2),6)),
  fixef   = list(names=I("(Intercept)"),
                 estimate=I(round(coef(m3)["(Intercept)"],4)),
                 se=I(round(as.numeric(s3$p.table["(Intercept)","Std. Error"]),4))),
  smooths = smooth_block(s3))

# M4: Gaussian, by-factor smooth (vowel was converted to factor at top)
m4 <- gam(y_gauss ~ vowel + s(duration, by=vowel, bs="cr", k=8),
          data=d, method="GCV.Cp")
s4 <- summary(m4)
out$models$M4 <- list(
  formula = "y_gauss ~ vowel + s(duration, by=vowel, bs=cr, k=8)",
  fit     = list(nobs=nrow(d), loglik=round(as.numeric(logLik(m4)),4),
                 aic=round(AIC(m4),4), r2_adj=round(s4$r.sq,4),
                 scale=round(sqrt(m4$sig2),6)),
  fixef   = list(names=I(rownames(s4$p.table)),
                 estimate=I(round(as.numeric(s4$p.table[,"Estimate"]),4)),
                 se=I(round(as.numeric(s4$p.table[,"Std. Error"]),4))),
  smooths = smooth_block(s4))

# M5: Poisson, single smooth
m5 <- gam(y_count ~ s(duration, bs="cr", k=8), data=d,
          family=poisson(), method="GCV.Cp")
s5 <- summary(m5)
out$models$M5 <- list(
  formula = "y_count ~ s(duration, bs=cr, k=8)",
  fit     = list(nobs=nrow(d), loglik=round(as.numeric(logLik(m5)),4),
                 aic=round(AIC(m5),4)),
  fixef   = list(names=I("(Intercept)"),
                 estimate=I(round(coef(m5)["(Intercept)"],4)),
                 se=I(round(as.numeric(s5$p.table["(Intercept)","Std. Error"]),4))),
  smooths = smooth_block(s5))

out$meta <- list(engine="mgcv", version=as.character(packageVersion("mgcv")),
                 R_version=R.version$version.string, dataset="gam_test.csv",
                 generated=format(Sys.time(),"%Y-%m-%d %H:%M:%S %Z"))
cat(sprintf("sp: %.4f", log10(m5$sp)))


coef(m5)

write(toJSON(out, pretty=TRUE, auto_unbox=TRUE), "reference_gam.json")
cat("Written: reference_gam.json\n\n")
for (nm in names(out$models)) {
  mo <- out$models[[nm]]
  cat(sprintf("  %s  AIC=%.1f  EDF[1]=%.2f  p=%.4f\n",
              nm, mo$fit$aic, mo$smooths$edf[1], mo$smooths$p[1]))
}
