# =====================================================================
# gen_data_gam_ext.R — writes test/data/gam_ext.csv, the responses for
# the beta and Student-t GAM tests (test_gam.phon), on the covariates of
# gam_test.csv (same rows, same order).
#   y_beta: beta, logit mean smooth in duration, precision phi = 15
#   y_t:    Student t (nu = 4, scale 20) around a smooth in duration
# Run from: test/statistics/frequentist/
# =====================================================================

d <- read.csv(file.path("..", "..", "data", "gam_test.csv"), sep="\t",
              stringsAsFactors = FALSE)
set.seed(20261122)
n <- nrow(d)
mu  <- plogis(-0.8 + 0.9 * sin((d$duration - 40) / 45) + 0.01 * (d$age - 40))
phi <- 15
y_beta <- round(rbeta(n, mu * phi, (1 - mu) * phi), 6)
y_beta <- pmin(pmax(y_beta, 1e-6), 1 - 1e-6)
y_t <- round(500 + 60 * sin((d$duration - 40) / 50) + 20 * rt(n, df = 4), 4)

out <- data.frame(speaker = d$speaker, vowel = d$vowel, age = d$age,
                  duration = d$duration, y_beta = y_beta, y_t = y_t)
write.table(out, file.path("..", "..", "data", "gam_ext.csv"), sep="\t",
            row.names = FALSE, quote = FALSE)
cat("Written: gam_ext.csv (n =", n, ")\n")
