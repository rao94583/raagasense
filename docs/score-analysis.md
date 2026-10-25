# Score attribution and a note-presence experiment

Checkpoint `3b05bde` preserves the annotation tools, Saraga import and the
first joint-search diagnostics. The next investigation attributes the
existing score to its inputs and tests a specific change to missing-note
evidence. These are development experiments, not a final test-set evaluation.

## Score breakdown

Version 2 joint reports add weighted `components` to every traced match:

| Component | Evidence |
|---|---|
| `tonic_prior` | Held time interpreted as Sa or Pa |
| `rare_prior` | Existing penalty for a rare catalog raag |
| `held_fit` | Likelihood of the held-note distribution under catalog svaras and weights |
| `missing_notes` | Expected non-occasional svaras with less than 1% of held time |
| `foreign_notes` | Out-of-raag svaras accounting for at least 1% of held time |
| `direction` | Ascending and descending passing-note evidence |
| `learned_profile` | Additional fit to profiles learned from other artists |
| `phrases` | Characteristic phrase evidence above the shuffled-note comparison |

The components sum to the total score within floating-point rounding.
They are captured for every automatic pair and every supplied-Sa match.
Tracing is optional; ordinary app results do not store the breakdown.
On both 89-recording baseline runs, adding attribution preserved every
winning raag and Sa, with score differences below `3e-15`.

```sh
bazel run //tools:evaluate -- --seconds 120 --all-sa \
  --joint-report data/saraga/joint-tracker-components-120s.jsonl

python3 tools/score_ablation.py data/saraga/joint-tracker-components-120s.jsonl \
  --manifest data/saraga/manifest.json --output data/saraga/score-ablation-tracker-120s
```

For the Melodia comparison, add
`--pitch-dir data/saraga/pitch --pitch-format saraga` and choose different
report/output names. Existing outputs are refused.

The replay removes one component at a time, reselects the best Sa for
every raag from all recorded hypotheses, and ranks raags again. Candidate
generation, pitch extraction and fitted profiles remain fixed. No weights
are fitted or searched. This is a counterfactual score intervention, not a
refitted probabilistic model.

Reports include `summary.json` and `contributions.csv`, which attributes
the winner's advantage over the reference pair in each wrong-raag case.
The reference pair uses the labeled raag and a proposed Sa within 50 cents
of the reference, ignoring octave. With Saraga, the Sa reference is still
automatically extracted. A component can favor many wrong answers while
also being essential to correct answers, so attribution alone is not a
reason to delete it.

## Component-removal results

Measured 2026-10-25 on the same 89 Saraga recordings and opening 120 seconds,
with artist-held-out profile learning, phrase evidence enabled and `--all-sa`.
Numbers are top-1 lower–upper bounds: uniquely correct through correct
under some ordering of tied raags. Numerical tie tolerance is `1e-12`.
Only the best Sa per raag counts toward a tie; alternative Sas do not
duplicate a raag's credit. The unchanged C++ baseline has actual automatic
top-1 counts of 27 and 28, and supplied-Sa counts of 37 and 42.

| Removed component | Tracker automatic | Tracker supplied Sa | Melodia automatic | Melodia supplied Sa |
|---|---:|---:|---:|---:|
| None | 27–28 | 36–37 | 28 | 41–42 |
| Tonic prior | 24–25 | 36–37 | 28 | 41–42 |
| Rare prior | 27–28 | 35–36 | 27–28 | 40–41 |
| Held fit | 21–23 | 33–35 | 24 | 38 |
| Missing-note penalty | 33–34 | 36–37 | 26 | 37 |
| Foreign-note penalty | 30 | 38 | 22 | 36 |
| Direction | 23–24 | 34–35 | 29–30 | 42–44 |
| Learned profile | 19–20 | 29–30 | 21 | 31–32 |
| Phrases | 24–25 | 34–35 | 29 | 42–43 |

Missing-note penalties supply the largest individual wrong-pair advantage
in 29 tracker cases and 27 Melodia cases. Yet removing them helps automatic
recognition with our tracker and hurts recognition with Melodia. Removing
direction or phrase evidence also has opposite effects across the two
extractors. Learned profiles help both. These differences argue against
choosing new weights solely to maximize one of these tables.

## Passing notes as evidence of presence

The existing missing-note term uses held time. A svara that was visited
briefly can therefore be penalized as missing. The focused experiment
`--passing-note-presence` also accepts a stable note from `PassingNotes`
as evidence that an expected svara was heard. `PassingNotes` already
requires a stable stretch of at least 30 ms. Completely unobserved notes
retain their penalty; foreign-note penalties, held-note likelihoods,
profiles, phrases, direction evidence and tonic proposals are unchanged.

```sh
bazel run //tools:evaluate -- --seconds 120 --all-sa --passing-note-presence
```

The option is experimental and disabled by default. It does not create
new tonic candidates or require a reference Sa during automatic inference.

Measured against the same baseline, without tuning thresholds or weights:

| Excerpt / source | Automatic top-1, baseline → experiment | Automatic top-3 | Supplied Sa top-1 | Supplied Sa top-3 |
|---|---:|---:|---:|---:|
| 30 s / tracker | 12 → 10 | 22 → 25 | 18 → 17 | 29 → 29 |
| 30 s / Melodia | 8 → 7 | 17 → 18 | 14 → 16 | 28 → 28 |
| 120 s / tracker | 27 → 29 | 46 → 49 | 37 → 36 | 55 → 51 |
| 120 s / Melodia | 28 → 27 | 42 → 48 | 42 → 37 | 52 → 53 |

All counts have denominator 89. The tracker two-minute automatic gain is
six recovered cases and four regressions; the shorter tracker run gains
none and loses two. Melodia at two minutes gains four and loses five.
The tracker's two-minute mean per-raag recall stays at 27.8% despite the
increase in overall top-1. The option is therefore **not enabled by default**.

Across all four comparisons, independent checks verified identical tonic
proposals and changes confined to the missing-note score contribution
(within floating-point tolerance) for every automatic and supplied-Sa pair.
This isolates the effect of the rule; it does not establish its musical
validity or generalization beyond these recordings.

The result distinguishes presence from prominence: accepting any short
stable visit as sufficient evidence does not consistently improve
discrimination. Future work should inspect the gains and regressions with
musical context before assigning stronger evidence to brief visits. Changes
to phrase or direction weights would also need cross-extractor and
independent-artist checks, given the opposite component-removal results.

Full local reports use these filename patterns:

```text
data/saraga/joint-{tracker,melodia}-components-{30,120}s.jsonl
data/saraga/joint-{tracker,melodia}-passing-presence-{30,120}s.jsonl
data/saraga/{components,passing-presence}-{tracker,melodia}-{30,120}s.txt
data/saraga/score-ablation-{tracker,melodia}-120s/{summary.json,contributions.csv}
data/saraga/passing-presence-changes.csv
```

The final CSV lists 53 gain/regression rows across the two excerpt lengths,
two extractors and automatic/supplied-Sa modes, with titles and performers
for targeted listening. Rows from the same recording are not independent
evaluation samples.

Reports are development diagnostics using opening excerpts and automatic
Sa references. Selecting an approach on these recordings does not provide
an untouched test-set accuracy estimate.

## Validation

```sh
bazel test //...
python3 -m unittest discover -s tools -p '*_test.py' -v
```

Tests cover score sums and preserved automatic/supplied-Sa rankings, tracing
with learned profiles and phrases, separating missing and foreign notes,
the effect of passing-note presence on only the missing-note component,
reselecting alternative Sas during replay, ties, gains and regressions,
and rejection of incomplete or inconsistent reports.
