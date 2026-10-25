# Phrase scoring with octave contour

The phrase matcher now preserves signed semitones and repeated events from
the source notation. For example, lower Ni followed by Re and Ga differs
from upper Ni followed by those same notes. An entire phrase can shift by
an octave, since the automatic tonic's octave is not a reliable reference
for the performer's madhya register. Internal octave relationships remain
fixed.

Dynamic programming finds complete occurrences with up to two intervening
events per required note. Each inserted event discounts the match by
`exp(-0.5)`. Required notes are never deleted or substituted. Matches
cannot cross a silence over 0.6 s or extend beyond 20 s. Only the last
120 s of passing notes contribute phrase evidence during a long stream;
the existing pitch histogram still accumulates over the performance.

## Evidence without learned profiles

For each tonic hypothesis, four deterministic permutations shuffle pitches
within each segment, keeping its pitch histogram and event times. The
mean phrase-match quality of these controls is subtracted from the actual
sequence's quality. Only positive surplus contributes to the raag score.
Missing phrases therefore do not become negative evidence in a partial
performance.

This is a heuristic chance correction, not a calibrated significance test.
Four controls provide a limited estimate, and the null model does not
preserve short-range melodic correlations. Its benefit is that phrase
support now works without `learned.csv`, and adapts to the current excerpt
instead of depending on the durations and identities of other recordings.
The existing phrase weight of 0.5 was retained; no weight search was run on
the diagnostic corpus.

Learning now stores held-note profiles only. Existing `profile` CSV rows
load unchanged; old `chance` rows are ignored because they describe a
different representation and null model. New exports omit those rows.

## Catalog consistency

The imported strings include templates whose notes contradict their
catalog entry. For example, `r m r n` appears under Yaman and Bhupal Todi,
although it does not fit either stored svara collection. Tanarang's
[Yaman description](https://tanarang.com/raag-yaman/) specifies tivra Ma
and otherwise shuddha svaras; its
[Bhupal Todi description](https://tanarang.com/raag-bhupal-todi/) omits Ma
and Ni. Neither page's characteristic-phrase list includes this sequence.

The loader now excludes contradictory templates, retaining the original
strings in `raag_phrases.cpp` for audit. It also removes duplicate templates.
This consistency check is not a claim that the catalog is musically
complete: some conflicts can arise from variants or missing occasional
notes. Those should be reconciled from sources before being used as
positive evidence. In particular, the source
[Puriya page](https://tanarang.com/raag-puriya/) itself has a capital Re
in one example despite specifying komal Re; conflicting notation is not
silently treated as an additional allowed svara.

## Local diagnostic results

Measured October 25, 2026, using the same 35 local recordings, excerpt
durations, labels, and artist policy as the
[checkpoint baseline](recognition-baseline.md). The paired table uses the
33 recordings with known Sa. Melody selection stays framewise by default.

| Excerpt | Model | Automatic Sa top-1 / top-3 | Labeled Sa top-1 / top-3 |
|---|---|---|---|
| 30 s | Checkpoint | 4/33 / 12/33 | 8/33 / 19/33 |
| 30 s | New phrases | 5/33 / 13/33 | 11/33 / 20/33 |
| 120 s | Checkpoint | 8/33 / 19/33 | 11/33 / 24/33 |
| 120 s | New phrases | 10/33 / 19/33 | 14/33 / 25/33 |
| 120 s | New phrases + temporal melody | 10/33 / 20/33 | 15/33 / 26/33 |

The two-minute run with `--no-phrases` reproduces the checkpoint's aggregate
results, which supports attributing the gain to phrase scoring rather
than changed pitch tracking or histogram weights. Across all 35 recordings,
automatic top-1 is 5/35 at 30 s and 10/35 at 120 s with the new phrases.

At 120 s, the Bhairav, Durga, and Marwa examples become correct automatic
top-1 predictions, while the previously correct Bilawal example drops to
rank 2. With supplied Sa, Bhairav, Marwa, and Tilak Kamod become correct;
none of the previously correct fixed-Sa predictions becomes incorrect.
The aggregate gain therefore includes a real regression, not a uniform
improvement across recordings.

```sh
bazel run //tools:evaluate -- --seconds 120 recordings
bazel run //tools:evaluate -- --no-phrases --seconds 120 recordings
bazel run //tools:evaluate -- --temporal --seconds 120 recordings
```

Unlike the first temporal-tracking experiment, phrase scoring improves
top-1 accuracy in both Sa modes and at both durations on this collection.
It is enabled by default. Temporal selection remains opt-in: without
corrected melody annotations or an independent corpus, its broader value
is still uncertain. These are modest gains on a small collection already
used for development, not a general accuracy claim.

Tests cover octave contour, repeated events, insertion costs, alternate
alignments, time limits, shuffle invariants, phrase evidence without learned
profiles, neutral absent evidence, bounded history, and loading old profiles.
Repeats still require distinct extracted note events; the steady-note
extractor can merge real rearticulations. Meend, andolan, detailed intonation,
and phrase-ending repose remain future modelling work.
