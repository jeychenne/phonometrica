# probe_b3.R — bisect the family="T" + usair INLA crash.
# Run from anywhere with:  Rscript probe_b3.R
# Prints OK / CRASH for each configuration; stop after the first
# CRASH and look at the preceding OK to see which flag is the
# trigger.

suppressPackageStartupMessages({
    library(INLA)
    library(brinla)
})

data(usair)
f <- SO2 ~ negtemp + manuf + wind + precip

probe <- function(label, ...) {
    cat(sprintf("[%-50s] ... ", label))
    res <- tryCatch(
        inla(f, family = "T", data = usair, ...),
        error = function(e) list(error = e)
    )
    if (!is.null(res$error) || is.null(res$summary.fixed)) {
        cat("CRASH\n")
        return(invisible(FALSE))
    }
    cat(sprintf("OK  (intercept mean = %6.2f)\n",
                res$summary.fixed["(Intercept)", "mean"]))
    invisible(TRUE)
}

cat("\n=== Bisecting control.compute flags ===\n\n")

probe("brinla baseline: dic + cpo",
      control.compute = list(dic = TRUE, cpo = TRUE))

probe("+ waic",
      control.compute = list(dic = TRUE, cpo = TRUE, waic = TRUE))

probe("+ return.marginals",
      control.compute = list(dic = TRUE, cpo = TRUE, return.marginals = TRUE))

probe("our full set: dic + cpo + waic + return.marginals",
      control.compute = list(dic = TRUE, cpo = TRUE, waic = TRUE,
                             return.marginals = TRUE))

cat("\n=== Trying alternative INLA modes (full flags) ===\n\n")

probe("full + inla.mode = 'classic'",
      control.compute = list(dic = TRUE, cpo = TRUE, waic = TRUE,
                             return.marginals = TRUE),
      inla.mode = "classic")

probe("full + inla.mode = 'experimental'",
      control.compute = list(dic = TRUE, cpo = TRUE, waic = TRUE,
                             return.marginals = TRUE),
      inla.mode = "experimental")

cat("\n=== Trying classic (non-MKL) binary, if available ===\n\n")

old_call <- inla.getOption("inla.call")
binary <- tryCatch({
    inla.setOption(inla.call = "inla")
    "ok"
}, error = function(e) "not available")
if (binary == "ok") {
    probe("full / classic binary",
          control.compute = list(dic = TRUE, cpo = TRUE, waic = TRUE,
                                 return.marginals = TRUE))
    inla.setOption(inla.call = old_call)
} else {
    cat("(classic non-MKL binary not installed — skipping)\n")
}

cat("\nDone.\n")
