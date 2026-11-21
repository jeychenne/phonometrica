# Export to R — self-test

`export_r()` (phon/analysis/export_r.cpp) writes a folder with the data, an R
script that refits each exported model, and Phonometrica's estimates. This test
runs the generated scripts in R and fails if R disagrees with Phonometrica.
It is the inverse of `../frequentist/`, which generates R references and
compares inward.

```
test/statistics/export_r/run_export_r.sh path/to/phon_stats
```

Requires R with glmmTMB (>= 1.1.8). Exports go to `out/` (git-ignored, recreated
on each run).

| File | Role |
|---|---|
| `test_export_r.phon` | Fits one model per case and exports it to `out/<case>/`. Also checks that Bayesian fits, GAMs and non-hierarchical formulas are refused. |
| `check_export.R` | Runs one `fit.R` as a user would and fails unless every comparison row, for every model, is `ok` and every R fit converged. |
| `run_export_r.sh` | Runs both halves. |

The tolerances are the generated script's own, so the test validates the very
thresholds a user sees. The fixtures are chosen to be well identified: a case
where R does not converge (for example a random slope with no between-group
variance) tests R, not the exporter.

Cases that are there on purpose, because nothing else would catch them:

- `odd_names_and_reference_levels`: non-syntactic and non-ASCII column names,
  user-set reference levels.
- `odd_slope_numeric`, `odd_slope_categorical`: non-syntactic random-slope
  names, which glmmTMB cannot fit, so the script must alias them; a categorical
  variable used *only* as a random slope, whose level order is not in
  `Model::variable_info`.
- `gaussian_reml` (Subject codes such as 308 must stay strings) and
  `gaussian_nested` (`(1 | classroom:school)`).
- `student_fixed`, `student_mixed`: a Hz-scale response, which exercises the
  standardize-and-back-transform path.
- `multi_model_rows_and_skip`: several models in one export, fitted on
  different rows (a column missing in some rows), with a factor level that
  only occurs in rows one model does not use (so its block must drop it), and
  a Bayesian model that must be left out without stopping the export.
