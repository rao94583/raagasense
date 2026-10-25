# Temporal melody selection experiment

The fixed-Sa/evaluation checkpoint is commit `9bcbb35`. This experiment
keeps its pitch salience and percussion mask, and adds bounded temporal
selection of competing pitch candidates. Classifier weights and tonic
estimation rules are unchanged.

At each frame, up to eight local salience maxima above 15% of the strongest
peak are retained. A fixed-lag Viterbi search compares paths through these
candidates and an unvoiced state. Its score combines log salience relative
to the existing voicing threshold with a pitch-change cost of 0.4 per
semitone and a voiced/unvoiced switching cost of 0.3. These settings were
checked on synthetic mixtures, not selected by searching raag accuracy.

With `tracking_frames = 15` and the default 10 ms hop, the selector waits
150 ms beyond the percussion filter's 80 ms lookahead. Committed timestamps
are unchanged. The search window and candidate memory are bounded;
`Finish` flushes pending frames exactly once. No learned model or external
dependency is used.

## Local recognition results

Measured October 25, 2026, on the same recordings and evaluation policy
as [the checkpoint baseline](recognition-baseline.md). The table uses the
33 recordings with known Sa for paired comparisons. Default framewise
selection reproduced the checkpoint's 30-second results.

| Excerpt | Melody selection | Automatic Sa top-1 / top-3 | Labeled Sa top-1 / top-3 |
|---|---|---|---|
| 30 s | Framewise | 4/33 / 12/33 | 8/33 / 19/33 |
| 30 s | Temporal | 4/33 / 14/33 | 9/33 / 19/33 |
| 120 s | Framewise | 8/33 / 19/33 | 11/33 / 24/33 |
| 120 s | Temporal | 6/33 / 19/33 | 12/33 / 24/33 |

At 120 s, automatic top-1 changes from correct to incorrect for the Adana
and Bilawal examples. With labeled Sa, Bhimpalasi changes from incorrect
to correct. Some other rankings improve and others regress. This small,
previously used corpus is diagnostic evidence, not an independent estimate
of generalization.

Because the automatic-Sa top-1 result regresses, temporal selection remains
**opt-in**. `MelodyOptions::tracking_frames` defaults to 0; the app's behavior
with respect to melody selection is unchanged. `analyze --temporal` and
`evaluate --temporal` set it to 15.
`--temporal` cannot be combined with `evaluate --pitch-dir`, since supplied
tracks bypass melody extraction entirely.

Subsequent [phrase-scoring changes](phrase-scoring.md) improve recognition
with either melody selector. The table above isolates the original temporal
experiment before those changes; use `--no-phrases` when reproducing it with
the current evaluator.

## What the tests establish

- The temporal selector follows the lead through a brief accompaniment
  surge more accurately than independent peak selection in the constructed
  mixture, and rejects the constructed tabla interference.
- It retains a sustained octave leap, centers of notes changing every
  100 ms, a 500-cent meend, and a broad oscillation over a weaker drone.
- It leaves silence between phrases unvoiced, preserves output under
  different input chunk sizes, bounds latency, and flushes without
  duplicating frames.

These are signal-level regression checks, not proof of real lead-melody
accuracy. The algorithm still has no reliable source identity: a strong,
continuous drone or accompaniment can itself form the smoothest path.
There is no drone subtraction, and a steady lead is not automatically
penalized as a drone. The confidence value remains a salience-strength
measure, not a calibrated probability that the selected source is the lead.

The next evidence needed is corrected lead-pitch annotations for both the
regressing excerpts and a small varied set of recordings. This will help
separate actual melody errors from sensitivity of steady-note extraction,
tonic estimation, and raag ranking. Continuity alone is insufficient to
justify replacing the existing tracker.
