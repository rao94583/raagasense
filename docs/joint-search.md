# Diagnosing the joint raag and Sa search

RaagaSense proposes tonic candidates from held melody pitches, evaluates
every raag at every candidate, and retains the best tonic for each raag.
The standalone tonic proposal with the highest Sa/Pa score is not
necessarily the tonic in the winning raag–Sa pair.

The evaluator can now record that production search without changing its
scoring or supplying a reference tonic to it. This separates two questions:
did the candidate generator include the reference Sa, and did the scoring
prefer the reference raag–Sa pair when it was available?

## Running the diagnostic

From the repository root, with Saraga imported:

```sh
bazel run //tools:evaluate -- --seconds 120 --all-sa \
  --joint-report data/saraga/joint-tracker-120s.jsonl

bazel run //tools:evaluate -- --seconds 120 --all-sa \
  --pitch-dir data/saraga/pitch --pitch-format saraga \
  --joint-report data/saraga/joint-melodia-120s.jsonl
```

Use new report filenames for subsequent runs; existing reports are refused.
The normal accuracy report is still printed. The JSONL report contains a
settings object, one object per evaluated recording, and a final `complete`
object with recording counts. A file without that final object is an
unfinished run. Artist-held-out profile learning is unchanged.

The reference Sa is used only to label diagnostic outcomes and in the
separate supplied-Sa evaluation. With Saraga and `--all-sa`, it is an
automatically extracted reference, not a human-checked answer. Raag labels
include the accepted title labels and project mapping of Poorva to Puriya
Kalyan. The candidate search never receives these reference tonic values.

## Per-recording evidence

Each recording object includes:

| Field | Meaning |
|---|---|
| `candidates` | Every proposed Sa in original Sa/Pa-score order, with Hz and score |
| `hypotheses` | Every scored raag–Sa pair, including alternate Sas for the same raag, sorted by score |
| `winner` | The actual production winner, including its selected Sa |
| `reference_candidate_present` | Whether any proposal agrees with the reference Sa's pitch class |
| `nearest_candidate_pitch_class_error_cents` | Signed error of the closest proposal, folding octaves |
| `reference_candidate_rank` | Proposal rank of that closest candidate, if it agrees |
| `reference_pair` | Highest-scoring pair with the labeled raag and a Sa agreeing with the reference |
| `reference_pair_rank` | One plus the number of pairs with a strictly higher score |
| `reference_pair_ties` | Number of pairs tied with the reference pair, including itself |
| `reference_pair_gap` | Winner's score minus the reference pair's score |
| `runner_up_gap` | Winner's score minus the second-best pair's score |
| `supplied_sa_winner` | Winner of the separate search at the exact reference Sa |
| `supplied_sa_truth_rank` | Labeled raag's rank in that separate supplied-Sa search |

Sa agreement uses the existing metric: less than 50 cents apart with
octaves ignored. It does not validate the singer's tonic octave. Numerical
ties use a tolerance of `1e-12`; score differences are heuristic evidence,
not calibrated probabilities. Missing measurements are JSON `null`.
Scores in the supplied-Sa search omit the Sa/Pa prior and should not be
subtracted from scores in the automatic search.

## Outcome categories

Each recording receives one category, with missing proposals taking
precedence over the correctness of the winning raag:

| Outcome | Interpretation |
|---|---|
| `no_reference_sa` | No usable tonic reference; joint correctness cannot be assessed |
| `no_prediction` | No winning pair, for example because no usable held notes were extracted |
| `reference_sa_not_proposed` | Search never considered a Sa within tolerance of the reference; the winning raag might still be correct |
| `correct_raag_and_sa` | Winning raag and Sa both agree with the references |
| `correct_raag_wrong_sa` | Raag identification succeeds, but the winning Sa disagrees |
| `wrong_raag_reference_sa` | Winning Sa agrees, but the raag is wrong |
| `wrong_raag_wrong_sa` | A reference-Sa proposal exists, but another raag and Sa win |

A missing reference-Sa candidate suggests auditing proposal generation and
the reference itself. An available but lower-ranked reference pair suggests
investigating the scoring and its input features. A score tie demonstrates
that the current model cannot distinguish those hypotheses. It does not
prove that a listener, a better model, or a longer excerpt could not do so.

The supplied-Sa comparison is a useful counterfactual. Its reference Sa
is exact, whereas an automatic proposal may merely be within 50 cents;
rounding and phrase interpretation can therefore differ even when a
candidate is classified as present.

## Inspecting a report

```python
import collections
import json
from pathlib import Path

objects = [json.loads(line) for line in
           Path("data/saraga/joint-tracker-120s.jsonl").read_text().splitlines()]
assert objects[-1]["type"] == "complete"
recordings = [item for item in objects if item["type"] == "recording"]
assert len(recordings) == objects[-1]["recordings"]
print(collections.Counter(item["outcome"] for item in recordings))

# Prioritize cases where supplying Sa fixes raag recognition.
review = [item for item in recordings
          if not item["winner_raag_correct"]
          and item["supplied_sa_truth_rank"] == 1]
for item in review:
    print(item["file"], item["truth_raag"], item["outcome"],
          item["winner"], item["reference_pair_rank"])
```

Join the recording filename to `data/saraga/manifest.json` to recover its
human-readable title, performer, and original source path.

## Saraga results, 2026-10-25

Measured on the current 89 recordings (51 raags, 11 artists), using opening
120-second excerpts, accepted title labels, the Poorva mapping, and
`--all-sa`. Scoring parameters were unchanged. Both full reports completed
and passed independent JSON, candidate coverage, pair-rank and winner
consistency checks.

| Measurement | RaagaSense tracker | Saraga Melodia |
|---|---:|---:|
| Reference Sa proposed | 88/89 | 88/89 |
| Correct raag and reference Sa win | 27 | 28 |
| Wrong raag wins with reference Sa | 35 | 30 |
| Wrong raag and Sa win despite an available reference-Sa proposal | 26 | 30 |
| Reference Sa never proposed | 1 | 1 |
| Correct raag but wrong Sa wins | 0 | 0 |
| No prediction | 0 | 0 |

The reference raag–Sa pair has rank 1 on 28 recordings in each mode. One
tracker recording has a top-score tie and the actual tie-breaking order
chooses another raag; Melodia has no reference pair tied at rank 1.
Sa in the actual winning pair agrees with the reference on 62/89 tracker
recordings and 58/89 Melodia recordings. It differs from the first
standalone tonic proposal on 31 and 37 recordings, respectively.

Supplying the exact reference Sa repairs 12 tracker raag errors but causes
two previously correct results to become incorrect (a net gain of ten).
It repairs 14 Melodia errors without a corresponding regression. These
counterfactuals are conditional on the automatic tonic references.

This points to joint scoring and its melodic input features as the main
current bottleneck, rather than insufficient tonic candidate coverage.
The next investigation should compare the winning and reference pairs'
musical evidence: phrase/movement support, held-note profiles, and
penalties for expected notes not yet heard in an opening excerpt. Better
extraction may still improve that evidence; agreement between two
extractors is not proof of accurate lead-voice transcription.

Local artifacts:

- `data/saraga/joint-tracker-120s.jsonl` and `joint-melodia-120s.jsonl`: all proposals and pairs.
- `data/saraga/joint-summary-120s.json`: aggregate counts.
- `data/saraga/joint-summary-120s.csv`: 178 recording/mode summaries with titles and performers.
- `data/saraga/joint-review-120s.csv`: 28 recording/mode rows where supplying Sa repairs the raag result or the reference Sa was not proposed. The same recording may appear for both extractors.

This is a development diagnostic on a sparse corpus with automatic tonic
references. It does not establish that any model score gap represents
perceptual certainty or that the best-scoring reference pair is the true
musical interpretation.

## Validation

The core regression test checks that enabling tracing preserves production
rankings, scores, tonic choices, and foreign-svara counts while retaining
alternative tonic hypotheses. Diagnostic tests cover proposal absence,
scoring failures, correct-raag/wrong-Sa cases, octave equivalence, numerical
ties, empty predictions, missing references, and JSON string escaping.
