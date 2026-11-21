# =====================================================================
# check_export.R -- R half of the "Export to R" self-test.
#
#     Rscript check_export.R <export-folder>
#
# Runs the generated fit.R exactly as a user would (working directory =
# the export folder) and inspects the `cmp` comparison table it leaves
# behind. Exits non-zero if R's fit did not converge, or if any row is
# anything but "ok" (for any model in the script): "CHECK" is a numerical disagreement beyond the
# script's own tolerance, "not in R" is a name-mapping bug in the
# generator, "NA in R" is an R fit too degenerate to compare.
#
# The fixtures in test_export_r.phon are chosen to be well identified,
# so on them every row must be "ok". The tolerances are the generated
# script's own -- the self-test validates the very thresholds a user sees.
# =====================================================================

args <- commandArgs(trailingOnly = TRUE)
if (length(args) != 1L) stop("usage: Rscript check_export.R <export-folder>")
dir <- args[[1]]
tag <- basename(dir)

setwd(dir)
out <- capture.output(res <- tryCatch({
    source("fit.R", echo = FALSE, local = globalenv())
    NULL
}, error = function(e) conditionMessage(e)))

if (!is.null(res)) {
    cat(sprintf("FAIL %-32s fit.R raised an error: %s\n", tag, res))
    quit(status = 1)
}
if (!exists("cmp") || !is.data.frame(cmp) || nrow(cmp) == 0L) {
    cat(sprintf("FAIL %-32s fit.R did not produce a comparison table\n", tag))
    quit(status = 1)
}

bad <- cmp[cmp$status != "ok", , drop = FALSE]
if (!all(cmp$converged)) {
    cat(sprintf("FAIL %-32s R's fit did not converge for: %s\n", tag,
                paste(unique(cmp$label[!cmp$converged]), collapse = ", ")))
    quit(status = 1)
}
if (nrow(bad) > 0L) {
    cat(sprintf("FAIL %-32s %d of %d rows disagree:\n", tag, nrow(bad), nrow(cmp)))
    print(format(bad[c("label", "term", "quantity", "phonometrica", "R", "rel.difference", "status")],
                 digits = 6), row.names = FALSE)
    quit(status = 1)
}

worst <- max(abs(cmp$rel.difference), na.rm = TRUE)
cat(sprintf("ok   %-32s %d model(s), %3d values, max |rel.difference| = %.2e\n", tag,
            length(unique(cmp$model)), nrow(cmp), worst))
