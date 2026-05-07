# Frequentist statistics validation

Validates Phonometrica's frequentist regression engine against `lm`
(Gaussian fixed-only, closed-form OLS) and `glmmTMB` (everything
else, ML estimation).

## Layout

```
frequentist/
├── README.md
├── _helpers.R                       ← shared R helpers (jsonlite, glmmTMB)
├── gen_reference_<family>.R         ← per-family reference generators
├── gen_reference_all.R              ← runs all of the above
├── reference_<family>.json          ← generated artifacts (committed)
├── test_<family>.phon               ← per-family validation tests
└── run_all.phon                     ← orchestrator
```

## Naming convention

R names categorical coefficients as `paste0(var, level)` and the
intercept as `(Intercept)`. Phonometrica uses brackets and no parens:

| R                      | Phonometrica            |
|------------------------|-------------------------|
| `(Intercept)`          | `Intercept`             |
| `voweli`               | `vowel[i]`              |
| `voweli:genderM`       | `vowel[i]:gender[M]`    |
| `sd((Intercept)\|s)`   | `sd(Intercept\|s)`      |
| `sd(voweli\|s)`        | `sd(vowel[i]\|s)`       |
| `sd(residual)`         | `sd(residual)` (same)   |

`_helpers.R` translates names from R's convention to Phonometrica's
before serialisation (`translate_fixef_name`, `build_cat_levels`).
The JSON references therefore contain Phonometrica-format names
directly, which is also what `summarize(model)` prints in the GUI,
and the phon-side harness looks up by name without knowing R's
quirks.

If you regenerate references with an `_helpers.R` that doesn't do
the translation (anything pre-May 2026), the phon-side lookups will
all miss and you'll see `name not found` failures for every fixed
effect — re-running `Rscript gen_reference_all.R` with the current
helpers fixes it.

## Workflow

### 1. Generate the references (R)

Once, after a fresh clone or after R/`glmmTMB`/`_helpers.R` updates:

```
Rscript gen_reference_all.R
```

Or per family — useful when only one family changed:

```
Rscript gen_reference_gaussian.R
Rscript gen_reference_beta.R
```

The slow ones are `gen_reference_beta.R` (large dataset, 7200 obs,
4×3 interaction, multiple random-slope models) and
`gen_reference_real_schwa.R` (1211-level word random intercept on
real data). On a modern machine the full set takes ~1-2 minutes.

### 2. Run the phon tests

```
phonometrica run_all.phon
```

Or per family — same surgical iteration:

```
phonometrica test_gaussian.phon
```

`run_all.phon` accumulates pass/fail counters via the shared
`assert.phon` module and asserts a clean run at the end (non-zero
exit code on any failure, suitable for CI).

## Models

Each family/dataset is exercised with five models in the same shape:

| Tag | Structure                                   | What it tests                                                  |
|-----|---------------------------------------------|----------------------------------------------------------------|
| M1  | fixed only, with interaction (`*`)          | Closed-form / IRLS for the simplest case                       |
| M2  | + (1\|speaker)                              | Single random intercept                                        |
| M3  | + (1+slope_var\|speaker)                    | Single cluster, random slope (covariance estimation)           |
| M4  | + (1\|speaker) + (1\|word)                  | Crossed random intercepts                                      |
| M5  | + (1+slope_var\|speaker) + (1\|word)        | Crossed RE + slope (full menagerie)                            |

The slope variable per family is the within-speaker covariate that
varies most cleanly:

| Family   | Dataset                       | Interaction in M1     | Slope variable |
|----------|-------------------------------|-----------------------|----------------|
| Gaussian | gaussian_f1.csv               | vowel × gender        | vowel (3 lvl)  |
| Binomial | binomial_schwa.csv            | position × style      | style (2 lvl)  |
| Poisson  | poisson_disfluency.csv        | task × age            | task (2 lvl)   |
| NegBin   | negbin_hesitation.csv         | complexity × stress   | stress (2 lvl) |
| Beta     | beta_voicing.csv              | consonant × position  | position (3)   |
| Student  | student_f1_robust + mild      | vowel × gender        | vowel (3 lvl)  |
| Real     | schwa_eychenne2019.csv        | dialect × gender      | task (3 lvl)   |

## Tolerances

Tolerances are tuned per-family in each `test_<family>.phon`. The
core `check_fit` helper takes three:

- `rtol_fit` — applied to scalar fit summaries (loglik, AIC, BIC,
  RSE, R², theta, phi, sigma, nu).
- `rtol_coef` — applied to fixed-effect estimates and SEs.
- `rtol_ranef` — applied to random-effect SDs (with a 1.0 absolute
  floor so near-zero SDs don't fail spuriously).

Each tolerance has a small absolute floor in `check_fit` itself
(typically 1e-3 to 1e-2 depending on the quantity).

Notable per-family deviations from the default tight tolerances:

- **Gaussian M1** uses near-machine-precision tolerances (1e-6 on
  fit, 1e-5 on coef) since both engines compute closed-form OLS.
- **Negative binomial** uses a wider `rtol_fit` (0.02) because of a
  known ~1.3-unit additive offset in Phonometrica's NB log-likelihood
  vs glmmTMB's; the fits sit on the same surface but the constant
  reporting offset propagates into AIC/BIC.
- **Student-t** uses the loosest tolerances of any family because
  the dispersion parameter ν is weakly identified when the tails
  are mild.

See the comment block at the top of each `test_<family>.phon` for
the rationale behind specific values.

## Adding a new family or model

1. Write `gen_reference_<family>.R` next to its peers; source
   `_helpers.R` and call `build_model_entry()` for each model.
2. Add a `Rscript gen_reference_<family>.R` line (or a new
   `source(...)` entry) to `gen_reference_all.R`.
3. Write `test_<family>.phon`. The pattern is fixed: import
   `../lib/assert`, load the JSON, fit each model, call
   `A.check_fit("M<n>", model, refs["M<n>"], rtol_fit, rtol_coef, rtol_ranef)`.
4. Add the family to `run_all.phon`'s `import()` list.
5. Commit the regenerated `reference_<family>.json` alongside the
   code.

If your reference needs a non-standard fit summary field (e.g. a
custom dispersion parameter), extend `check_fit` in
`../lib/assert.phon` — the existing scaffolding (loglik, AIC, BIC,
rse, r2, adj_r2, theta, phi, sigma, nu) covers every family in this
suite.
