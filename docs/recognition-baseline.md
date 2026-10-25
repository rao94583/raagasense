# Recognition diagnostic baseline

Measured October 25, 2026, using the 35 local recordings and their existing
labels, with the classifier/scoring constants from `live-pitch` commit
`0a11e6f`. This establishes a starting point for algorithm changes; it is
not an independent test-set estimate or a before/after improvement claim.
Audio and labels are local, ignored files and are not included in Git.

Commands, from the repository root:

```sh
bazel run //tools:evaluate -- --seconds 30 recordings
bazel run //tools:evaluate -- --seconds 120 recordings
```

Only the first 30 or 120 seconds are analyzed. Two recordings have unknown
Sa and are excluded from the paired comparison. The remaining 33 have
supplied, checked Sa labels. Both modes use the same extracted melody,
note extraction, and evaluation learning policy.

| Excerpt | Sa for classification | Top-1 | Top-3 |
|---|---|---|---|
| 30 s | Automatic | 4/33 (12.1%) | 12/33 (36.4%) |
| 30 s | Labeled | 8/33 (24.2%) | 19/33 (57.6%) |
| 120 s | Automatic | 8/33 (24.2%) | 19/33 (57.6%) |
| 120 s | Labeled | 11/33 (33.3%) | 24/33 (72.7%) |

With all 35 recordings included, automatic-Sa top-1/top-3 is 4/35 and
12/35 at 30 s, and 8/35 and 19/35 at 120 s.

The standalone Sa estimator is correct on 15/33 at 30 s and 19/33 at
120 s, using a pitch-class tolerance of 50 cents. When given the true
raag's svaras, tonic estimation is correct on 28/33 and 33/33 respectively.
The latter is a diagnostic with privileged information, not a deployable
recognition result. At 120 s, the candidate set therefore includes the
correct tonic for every recording in this labeled subset; joint scoring
still often selects an incorrect raag/tonic combination.

Supplying Sa helps substantially, but many errors remain even with it.
Without corrected melody annotations, these runs cannot distinguish melody
extraction errors from limitations of note extraction and raag scoring.
The next diagnostic is to correct a small, varied set of extracted tracks
and rerun with `--pitch-dir` and the same Sa labels/durations.

## Evaluation limitations and policy changes

- The labels use Windows line endings. The old CSV reader retained `\r`
  in the `sa_checked` header, preventing the check from being recognized.
  The new reader handles both line endings and escaped quotes.
- 31 of the 35 recordings lack explicit artist metadata. These use rules
  alone and cannot train evaluation profiles. For the other four, learning
  uses only explicitly named, different artists. This is more conservative
  than treating each unnamed file as a distinct artist. Complete consistent
  artist identities before evaluating learned models.
- Missing predictions count as misses; missing or invalid supplied pitch
  files fail a run. Per-raag recall and leading confusion pairs are printed
  alongside aggregate accuracy.
- This small collection has already informed earlier model weights. Keep a
  separate artist-disjoint test set before making general accuracy claims.
- The fixed-Sa path omits the Sa/Pa prior because it is constant across
  raags for a fixed tonic. Automatic classification otherwise retains the
  original scoring model.
