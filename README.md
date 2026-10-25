# RaagaSense

Listens to Hindustani classical music and identifies the raaga as it is
performed. Native macOS app now, iOS later; C++20 and Objective-C++, built
with Bazel. No machine learning: pitch tracking (harmonic summation, which
copes with accompaniment; YIN for clean single voices), the tonic, svaras and
raaga matching are all conventional signal processing and rules.

```sh
bazel test //...
bazel run //app/macos:RaagaSense                             # Record Sample… (⌘R) collects labeled samples
bazel run //tools:analyze -- recording.mp3                   # svara histogram
bazel run //tools:analyze -- --steady recording.mp3          # ...of steady notes only
bazel run //tools:analyze -- --notes recording.mp3           # steady notes, one per line
bazel run //tools:analyze -- --tonic recording.mp3           # candidates for Sa
bazel run //tools:analyze -- --tonic --svaras "S g m d n" recording.mp3  # ...knowing the raag
bazel run //tools:analyze -- --csv recording.mp3             # per-frame pitch
bazel run //tools:plot -- recording.mp3 out.png              # pitch over svara grid
bazel run //tools:evaluate -- recordings ~/Music/RaagaSense\ Samples  # check against labeled folders
bazel run //tools:evaluate -- --seconds 30 recordings         # compare auto and labeled Sa on 30 s prefixes
bazel run //tools:evaluate -- --all-sa --learn-to ~/Music/RaagaSense\ Samples/learned.csv \
    recordings ~/Music/RaagaSense\ Samples                   # ...and save what's learned for the app
bazel run //tools:record -- --list                           # sound inputs
bazel run //tools:record -- --input M2 --channels 3,4 recordings/yaman.m4a
```

The analyze and plot tools estimate Sa from the recording; `--sa HZ` sets
it instead. In the app, choosing Sa fixes it for both the pitch grid and
raag identification; Auto estimates it while ranking raags.

## Recognition diagnostics

`evaluate` reports automatic-Sa accuracy on all labeled recordings, and
automatic-Sa versus supplied-Sa accuracy on the same subset with known Sa.
Each report includes top-1/top-3 accuracy, per-raag recall, and leading
confusions. Recordings with no usable notes count as misses. `--seconds N`
uses the first N seconds (or the whole recording if shorter); omit it to
evaluate complete recordings.

Folders can contain `labels.csv` with required `file,raag,sa_hz` columns.
Sa 0 means unknown. If `sa_checked` exists, only `yes` admits a tonic label;
`--all-sa` explicitly includes unchecked values. Without `labels.csv`, the
existing filename conventions provide raag and optional Sa labels.

For evaluation, learning uses other explicitly named artists only. Supply
an `artist` column, or `artist NAME` in `notes` (ending at a semicolon).
Recordings without artist metadata use rules alone and do not train the
evaluation profiles. Artist identities should be spelled consistently;
group excerpts and versions of a performance under the same artist. The
`--learn-to` export uses all eligible recordings for deployment, including
those without artist metadata; it is not an evaluation model.

To isolate melody extraction errors, export and correct pitch tracks, then
evaluate them using the same tonic labels and excerpt duration:

```sh
mkdir -p corrected-pitch
bazel run //tools:analyze -- --sa 220 --csv recordings/example.wav > corrected-pitch/example.wav.csv
# Correct the time/frequency track against the recording before evaluating.
bazel run //tools:evaluate -- --seconds 30 --pitch-dir corrected-pitch recordings
```

Provide one `<audio filename>.csv` per evaluated recording, including the
audio extension. Missing or malformed tracks fail the run. Columns `time`
(seconds) and `frequency` (Hz) are required; blank or zero frequency means
unvoiced. Optional `confidence` defaults to 1. Extra columns from analyze
are ignored. Times must increase strictly; frames beyond the evaluated
audio duration are omitted. Corrected tracks bypass MelodyTracker and
still go through the same note extraction and scoring.

Compare extracted pitch + automatic Sa, extracted pitch + labeled Sa, and
corrected pitch + labeled Sa on the same recordings. These comparisons
separate tonic errors from melody extraction and raag-model limitations.
Keep a separate test set for final accuracy estimates; do not select
scoring weights on it.

## Layout

| Directory | Contents | Depends on |
|---|---|---|
| `core/` | Pitch tracking, svaras, steady notes, tonic, the raags' svaras; later raaga matching | C++ standard library only |
| `platform/` | Audio files, sound inputs, the microphone, the pitch plot | Core Audio, AVFoundation, Core Graphics; no AppKit or UIKit |
| `app/macos/` | The macOS app | AppKit |
| `tools/` | Command-line tools for working on recordings | macOS |

An iOS app goes in `app/ios/`: a UIKit front end over the same `core` and
`platform` libraries (the microphone code already sets up the iOS audio
session, and the plot draws into any Core Graphics context).

## Status

- [x] YIN pitch tracking, streaming and per frame
- [x] Melody pitch tracking over accompaniment (harmonic summation)
- [x] Pitch to svara, relative to a chosen Sa
- [x] Live pitch display on macOS
- [x] Recording labeled samples, with Sa found live from the raag
- [x] Steady notes, apart from meend and kan
- [x] Tonic (Sa) estimate from held Sa and Pa, in the tools
- [ ] Tonic for raagas without Pa, and in the app
- [ ] Andolan, and meends as transitions between svaras
- [x] Svaras of 122 raags (the NCPA AUTRIM list, common raags from tanarang.com, and more; 28 marked rare), and Sa from a known raag
- [x] Naming the raag from held notes: svaras, occasional svaras, vadi
- [x] Svaras left out going up or down (39 raags so far)
- [x] Characteristic phrases (tanarang.com's, for 105 raags), scored above chance
- [x] Raag profiles learned from labeled recordings
- [ ] Learned movement between svaras, without favoring raags that have recordings
- [x] Live raag guesses and an Auto Sa in the app, from `learned.csv`
- [x] Fixed-Sa identification and paired tonic/melody diagnostics
- [ ] iOS app
