# =====================================================================
# gen_reference_all.R — regenerate every reference_<family>.json
# =====================================================================
# Run:    Rscript gen_reference_all.R
#
# Convenience wrapper for first-time setup or after a wholesale
# change to _helpers.R. For day-to-day work, run individual
# gen_reference_<family>.R scripts to avoid re-fitting the slow ones
# (beta, real schwa) when you only care about a single family.
# =====================================================================

scripts <- c(
    "gen_reference_gaussian.R",
    "gen_reference_binomial.R",
    "gen_reference_poisson.R",
    "gen_reference_negbin.R",
    "gen_reference_beta.R",
    "gen_reference_student.R",
    "gen_reference_nested.R",
    "gen_reference_real_schwa.R"
)

# Resolve relative to this script's directory so the runner works
# from any CWD.
self_dir <- (function() {
    args <- commandArgs(trailingOnly = FALSE)
    file_arg <- grep("^--file=", args, value = TRUE)
    if (length(file_arg) > 0L) {
        return(normalizePath(dirname(sub("^--file=", "", file_arg[1]))))
    }
    getwd()
})()

t0 <- Sys.time()
for (s in scripts) {
    cat(sprintf("\n=== Running %s ===\n", s))
    t1 <- Sys.time()
    source(file.path(self_dir, s), local = TRUE)
    cat(sprintf("    [%s done in %.1fs]\n", s,
                as.numeric(difftime(Sys.time(), t1, units = "secs"))))
}
cat(sprintf("\nTotal time: %.1fs\n",
            as.numeric(difftime(Sys.time(), t0, units = "secs"))))
