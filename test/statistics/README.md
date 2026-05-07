# Statistics validation suite

Validates Phonometrica's regression engine against gold-standard
references. The architecture is the same across families and engines:

```
R generates a JSON reference  →  phon test loads JSON  →  phon test fits
                                                          and compares
```

R is the source of truth (frequentist: `lm` + `glmmTMB`; Bayesian:
`brms`). JSON is the interchange format — it's a strict subset of the
phon scripting language, so `load_json(read_file(path))` parses it
directly.

## Subdirectories

- `lib/assert.phon` — shared assertion harness used by every
  `test_<family>.phon`. Returns a `Module` with helpers (`check`,
  `check_fit`, `start_section`, `end_section`, `report`) and
  pass/fail counters that accumulate across imports.

- `frequentist/` — `lm` / `glmmTMB` references, six families
  (gaussian, binomial, poisson, negbin, beta, student) plus a
  real-data binomial test on `schwa_eychenne2019.csv`. See
  `frequentist/README.md`.

- `bayesian/` — placeholder; `brms` references and matching phon
  tests will be wired up in a later session, mirroring the
  frequentist layout.

## Why a JSON reference instead of cross-language IPC?

Generating a JSON reference once, committing it, and comparing against
it from phon means:

- The phon test runs in a single process — no subprocess to R, no
  PATH/version drift.
- A fresh checkout can run the validation suite without R
  installed (R is needed only when the reference needs regenerating).
- Regression bisects are straightforward — the JSON is text and
  diffs cleanly.

The cost is that references must be regenerated whenever R, the
reference packages, or the `_helpers.R` formatter changes.

## Naming conventions used by both sides

- **Coefficient names** follow R's `model.matrix` defaults:
  `(Intercept)`, `vowelI`, `genderM`, `vowelI:genderM`, …
- **Random-effect SD names** follow the format
  `sd(<term>|<group>)` plus a final `sd(residual)` for Gaussian
  mixed models — matching what Phonometrica's
  `model_get_field("ranef_names")` produces in
  `phon/application/data_table.cpp`. See `frequentist/_helpers.R`
  for the matching R-side formatter.

## Running

See `frequentist/README.md` for the full workflow. The short version:

```
cd frequentist
Rscript gen_reference_all.R                      # once, after R/glmmTMB updates
phonometrica run_all.phon                        # any time after that
```
