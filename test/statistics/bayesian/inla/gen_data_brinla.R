# =====================================================================
# gen_data_brinla.R
# =====================================================================
# Convert the brinla GPL-3 .rda datasets (and boot::nitrofen, for the
# nitrofen Poisson GLMM; mgcv::gamSim for the GAM example) into
# tab-separated, unquoted CSVs under test/data/. Run once whenever:
#   * a new brinla release ships,
#   * a new model in the suite needs a different dataset, or
#   * factor-encoding conventions change.
#
# Generated files are committed; the phon-side tests don't need R or
# brinla installed.
#
# Why tab-separated and quote-free:
#   Phonometrica's load() auto-detects column types. Strings vs.
#   numbers are decided by whether all non-header rows parse as
#   numeric. Categorical columns coded as integers ("1","2","3") get
#   picked up as Number and the formula
#       Relief ~ PainLevel + ...
#   would fit one slope per categorical predictor instead of a factor
#   expansion. To force factor treatment we recode such columns to
#   non-numeric strings with a fixed-letter prefix (e.g. P1, P2, ...).
#   The same labels are picked up by R's factor() so the level names
#   line up between INLA and Phonometrica.
# =====================================================================

suppressPackageStartupMessages({
    library(brinla)
    library(boot)       # for boot::nitrofen
    library(dplyr)      # for nitrofen pivot
    library(tidyr)
    library(mgcv)       # for gamSim
})

# This file lives in bayesian/inla/; test/data/ is THREE levels up.
script_dir <- function() {
    args <- commandArgs(trailingOnly = FALSE)
    file_arg <- grep("^--file=", args, value = TRUE)
    if (length(file_arg) > 0L) {
        return(normalizePath(dirname(sub("^--file=", "", file_arg[1]))))
    }
    getwd()
}
DATA_DIR <- file.path(script_dir(), "..", "..", "..", "data")
dir.create(DATA_DIR, showWarnings = FALSE, recursive = TRUE)

write_csv_phon <- function(df, name) {
    path <- file.path(DATA_DIR, name)
    write.table(df, path, sep = "\t", quote = FALSE,
                row.names = FALSE, na = "NA")
    cat(sprintf("Wrote %d rows to %s\n", nrow(df), path))
}

# ── usair (Ch3 — B1, B2, B3) ──────────────────────────────────────────
# SO2 ~ negtemp + manuf + wind + precip + days   (gaussian + Student-t)
# All numeric columns; no recoding needed.
data(usair, package = "brinla")
write_csv_phon(usair, "usair_brinla.csv")

# ── painrelief (Ch3 — B4) ─────────────────────────────────────────────
# Relief ~ PainLevel + Codeine*Acupuncture       (gaussian)
# brinla codes the three factor predictors as integers (1..8 / 1..2 /
# 1..2). Recode to alphabetic-prefixed strings so Phon treats them as
# factors. R's factor() picks up the same labels.
data(painrelief, package = "brinla")
pr <- painrelief
pr$PainLevel   <- paste0("L", pr$PainLevel)     # L1..L8
pr$Codeine     <- paste0("C", pr$Codeine)       # C1, C2
pr$Acupuncture <- paste0("A", pr$Acupuncture)   # A1, A2
write_csv_phon(pr, "painrelief_brinla.csv")

# ── lowbwt (Ch4 — G1, G2) ─────────────────────────────────────────────
# LOW ~ AGE + LWT + RACE + SMOKE + HT + UI + FTV   (binomial)
# brinla stores RACE/SMOKE/HT/UI as factors with integer labels.
# Recode RACE to semantic labels (Hosmer & Lemeshow, *Applied Logistic
# Regression*, 2nd ed., Table 1.1) and the three binary factors to
# "no"/"yes" so Phon treats them as factors rather than continuous.
data(lowbwt, package = "brinla")
lb <- lowbwt
lb$RACE  <- c("1" = "white", "2" = "black", "3" = "other")[as.character(lb$RACE)]
lb$SMOKE <- c("0" = "no",    "1" = "yes")[as.character(lb$SMOKE)]
lb$HT    <- c("0" = "no",    "1" = "yes")[as.character(lb$HT)]
lb$UI    <- c("0" = "no",    "1" = "yes")[as.character(lb$UI)]
write_csv_phon(lb, "lowbwt_brinla.csv")

# ── AIDS (Ch4 — G3, G4) ───────────────────────────────────────────────
# DEATHS ~ TIME / DEATHS ~ log(TIME)              (poisson, n=14)
# Phon's formula parser doesn't support function calls like log(); we
# precompute log_time as a column. INLA-side reads the same precomputed
# column for the matched comparison.
data(AIDS, package = "brinla")
ad <- AIDS
ad$log_time <- log(ad$TIME)
write_csv_phon(ad, "aids_brinla.csv")

# ── crab (Ch4 — G5, G6) ───────────────────────────────────────────────
# SATELLITES ~ COLOR + SPINE + WIDTH              (negbin + poisson)
data(crab, package = "brinla")
cr <- crab
cr$COLOR <- paste0("col", cr$COLOR)             # col2..col5
cr$SPINE <- paste0("sp",  cr$SPINE)             # sp1..sp3
write_csv_phon(cr, "crab_brinla.csv")

# ── reeds (Ch5 — R1) ──────────────────────────────────────────────────
# nitrogen ~ 1 + (1|site)                         (gaussian + RE int.)
# `site` is already a factor with character levels (A, B, C); no recode.
data(reeds, package = "brinla")
write_csv_phon(reeds, "reeds_brinla.csv")

# ── reading (Ch5 — R2, R3) ────────────────────────────────────────────
#   R2: piat ~ agegrp + (1|id)                    (uses agegrp)
#   R3: piat ~ cagegrp + (cagegrp|id)             (uses centered cagegrp)
# Pre-compute cagegrp (= agegrp - 8.5) once so both engines see the
# identical predictor.
data(reading, package = "brinla")
rd <- reading
rd$id <- paste0("S", as.character(rd$id))       # S1..S89
rd$cagegrp <- rd$agegrp - 8.5
write_csv_phon(rd, "reading_brinla.csv")

# ── nitrofen (Ch5 — R4, from boot package) ────────────────────────────
# live ~ conc_scaled*brood + (1|id)               (poisson + RE int.)
# boot::nitrofen is wide (brood1, brood2, brood3, total). Pivot to long
# and precompute conc_scaled (= conc/300) since Phon doesn't support
# I() in formulas.
data(nitrofen, package = "boot")
nf <- nitrofen
nf$id <- seq_len(nrow(nf))
lnf <- nf %>%
    select(-total) %>%
    pivot_longer(c(brood1, brood2, brood3),
                 names_to = "brood", values_to = "live") %>%
    arrange(id)
lnf$brood <- factor(lnf$brood, labels = c("b1", "b2", "b3"))
lnf$id    <- paste0("S", lnf$id)
lnf$conc_scaled <- lnf$conc / 300
lnf <- lnf[, c("id", "conc", "conc_scaled", "brood", "live")]
write_csv_phon(lnf, "nitrofen_brinla.csv")

# ── ohio (Ch5 — R5) ───────────────────────────────────────────────────
# resp ~ age + smoke + (1|id)                     (binomial + RE int.)
# age (-2,-1,0,1) and smoke (0,1) are numeric in brinla's formula
# (single slope each), so we keep them as integers. id is a grouping
# factor; prefix it to force String typing.
data(ohio, package = "brinla")
oh <- ohio
oh$id <- paste0("S", oh$id)
write_csv_phon(oh, "ohio_brinla.csv")

# ── kyphosis (Ch9 — A2) ───────────────────────────────────────────────
# Kyphosis ~ s(Age) + s(StartVert) + s(NumVert)   (binomial GAM)
# All numeric; Kyphosis (response) is 0/1.
data(kyphosis, package = "brinla")
write_csv_phon(kyphosis, "kyphosis_brinla.csv")

# ── gam_sim (Ch9 — A1) ────────────────────────────────────────────────
# y ~ s(x0) + s(x1) + s(x2) + s(x3)               (gaussian GAM, sim.)
# brinla/docs/scripts/gam.R uses set.seed(2) before gamSim(); we
# replicate so the CSV is bit-reproducible across regenerations.
set.seed(2)
gs <- gamSim(1, n = 400, dist = "normal", scale = 2)
gs <- gs[, c("y", "x0", "x1", "x2", "x3")]
write_csv_phon(gs, "gam_sim_brinla.csv")

cat("Done.\n")
