# RaagaSense

Listens to Hindustani classical music and identifies the raaga as it is
performed. Native macOS app now, iOS later; C++20 and Objective-C++, built
with Bazel. No machine learning: pitch tracking (harmonic summation, which
copes with accompaniment; YIN for clean single voices), the tonic, svaras and
raaga matching are all conventional signal processing and rules.

```sh
bazel test //...
bazel run //app/macos:RaagaSense                             # Record Sample… (⌘R) collects labeled samples
bazel run //app/macos:RaagaSense -- --annotate recordings    # Label performers; optionally review short pitch excerpts
bazel run //tools:analyze -- recording.mp3                   # svara histogram
bazel run //tools:analyze -- --steady recording.mp3          # ...of steady notes only
bazel run //tools:analyze -- --notes recording.mp3           # steady notes, one per line
bazel run //tools:analyze -- --tonic recording.mp3           # candidates for Sa
bazel run //tools:analyze -- --tonic --svaras "S g m d n" recording.mp3  # ...knowing the raag
bazel run //tools:analyze -- --csv recording.mp3             # per-frame pitch
bazel run //tools:plot -- recording.mp3 out.png              # pitch over svara grid
bazel run //tools:evaluate -- --seconds 120 --all-sa         # primary Saraga corpus after import (automatic Sa references)
bazel run //tools:evaluate -- recordings ~/Music/RaagaSense\ Samples  # explicitly evaluate older samples
bazel run //tools:evaluate -- --all-sa --learn-to ~/Music/RaagaSense\ Samples/learned.csv \
    recordings ~/Music/RaagaSense\ Samples                   # ...and save what's learned for the app
bazel run //tools:record -- --list                           # sound inputs
bazel run //tools:record -- --input M2 --channels 3,4 recordings/yaman.m4a
```

The analyze and plot tools estimate Sa from the recording; `--sa HZ` sets
it instead. In the app, choosing Sa fixes it for both the pitch grid and
raag identification; Auto estimates it while ranking raags.

## Primary corpus: Saraga Hindustani

Saraga Hindustani 1.5 is now the primary development corpus. Import an
extracted download once (Python 3, standard library only):

```sh
mkdir -p data
bazel run //tools:raag_catalog > data/raag-catalog.tsv
python3 tools/import_saraga.py /path/to/saraga1.5_hindustani --catalog data/raag-catalog.tsv
```

The importer links to the original media and annotations, preserving all
108 recordings in `data/saraga/collection`. The evaluation view in
`data/saraga/recordings` contains 89 recordings of 51 supported raags by
11 artists. When raag metadata is missing, the importer trusts a raag
named in the recording title and records `raag_source=title`. This recovers
ten labels, nine of which are usable in evaluation. Poorva is mapped to
Puriya Kalyan by project decision, with the original label and mapping
recorded in the import. Sawani is explicitly excluded from evaluation.
Missing labels, unsupported raags, and explicit exclusions are listed in
`data/saraga/excluded.csv`; they remain available in the full collection.
`manifest.json` preserves source identifiers, label mappings, artist
provenance, and the import summary. Generated data is excluded from Git;
keep the source volume mounted. Re-import into a new output folder to
preserve any local label edits.

With no directory argument, `evaluate` uses the imported Saraga evaluation
view when present, falling back to `recordings` before import. The old
recordings remain available when explicitly selected.

Saraga's pitch tracks and tonic values are **automatically extracted**;
they are comparison features, not human-checked ground truth. Imported Sa
values remain unchecked; `--all-sa` explicitly includes them for provisional
fixed-Sa comparisons and profile learning. Compare our tracker against
Saraga's Melodia tracks on the same 120-second prefixes:

```sh
bazel run //tools:evaluate -- --seconds 120 --all-sa
bazel run //tools:evaluate -- --seconds 120 --all-sa --pitch-dir data/saraga/pitch --pitch-format saraga
bazel run //app/macos:RaagaSense -- --annotate data/saraga/recordings
```

Manual phrase and section annotations are linked under `annotations/` for
future work. Keep evaluation separated by artist; 27 of the supported
raags occur under only one artist, so this remains a sparse development
benchmark. See [the Saraga import and baseline notes](docs/saraga.md) for
details, attribution, and how the excluded recordings can still help.

## Label recordings without transcribing them

Open **Label & Review…** (⌘O) and choose your recordings folder. Fill in the
lead performer's name, raag, and Sa if known, then **Save & next recording**.
Consistent performer names let evaluation keep one artist's recordings
together. Unknown names can stay blank; original notes and other CSV
columns are preserved, and the tool makes a backup before its first label
update. Existing custom raag names are retained. **Hear Sa** plays a quiet
reference tone to help check the tonic; an automatic suggestion stays
unverified until you explicitly check it by ear.
Beside Sa in Hz, the panel shows the nearest concert-pitch note and octave
plus cents above (+) or below (−) it, using A4 = 440 Hz.

Pitch review is optional. Choose a **5, 10, or 15 second** excerpt, analyze
it automatically, then compare **Listen**, **Pitch guide**, or **Both**.
Drag to select a section and correct it with **Up/Down an octave**, **No
lead**, or **Set held note**. Octave corrections preserve the contour;
assign a fixed note only when the performer really holds it. Undo and
Restore estimate are available. Skip uncertain passages instead of guessing.

Only tick **I listened through this whole excerpt and checked the pitch
guide** after reviewing the entire excerpt, including gaps and moving
notes. The tool then enables export. You do not need to review the rest of
the recording. New edits clear the check, and the tool asks before discarding
unexported edits or checked excerpts. It does not save pitch drafts between
sessions; export the checked passage before leaving, or skip it.

Exports contain a new mono WAV copy, a matching pitch CSV, and labels;
the original audio is unchanged. In the chosen export folder:

```text
recordings/<source>__<start-ms>-<end-ms>.wav
recordings/labels.csv
corrected-pitch/<same filename>.wav.csv
```

Times in the pitch CSV start at the excerpt's beginning. Existing exported
files are not overwritten. Artist and source information follow the
excerpt; Sa is trusted only when explicitly checked by ear.

```sh
bazel run //tools:evaluate -- --pitch-dir /path/to/reviewed/corrected-pitch /path/to/reviewed/recordings
```

Start with performer labels and just a couple of clear 10-second excerpts.
This supplies useful diagnostics without frame-by-frame transcription.
Short checked excerpts help assess pitch extraction. Keep the longer source
recordings for assessing raag identification over a performance.

## Recognition diagnostics

`evaluate` reports automatic-Sa accuracy on all labeled recordings, and
automatic-Sa versus supplied-Sa accuracy on the same subset with known Sa.
Each report includes top-1/top-3 accuracy, per-raag recall, and leading
confusions. Recordings with no usable notes count as misses. `--seconds N`
uses the first N seconds (or the whole recording if shorter); omit it to
evaluate complete recordings.

`--joint-report FILE.jsonl` records every proposed Sa and scored raag–Sa
pair, the actual winner's Sa, and the reference pair's rank, score gap and
ties. It distinguishes a missing tonic candidate from a scoring failure;
the standalone "Sa alone" estimate is not necessarily the winner's Sa.
Existing reports are not overwritten. See [joint-search diagnostics](docs/joint-search.md).

Characteristic phrases retain octave contour and repeated notes. Matches
are discounted for inserted notes and checked against shuffled versions of
the current note sequence, so phrase scoring works without `learned.csv`.
`evaluate --no-phrases` disables this evidence for comparison. Existing
learned pitch profiles still load; legacy phrase-chance rows are ignored.
See [phrase scoring and its diagnostic results](docs/phrase-scoring.md).

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

Temporal melody selection is available experimentally in `analyze` and
`evaluate` with `--temporal`. It keeps competing pitch peaks and commits a
path with 150 ms of lookahead. The app and tools retain framewise selection
by default: the prototype improves some synthetic mixtures and fixed-Sa
results but regresses automatic-Sa accuracy on the local recordings. See
[the comparison and limitations](docs/melody-tracking.md).

```sh
bazel run //tools:evaluate -- --seconds 120 recordings
bazel run //tools:evaluate -- --temporal --seconds 120 recordings
bazel run //tools:analyze -- --temporal --sa 220 --csv recording.mp3
```

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
- [x] Characteristic phrases from tanarang.com, with octave contour and an excerpt-specific chance correction
- [x] Raag profiles learned from labeled recordings
- [ ] Learned movement between svaras, without favoring raags that have recordings
- [x] Live raag guesses and an Auto Sa in the app, from `learned.csv`
- [x] Fixed-Sa identification and paired tonic/melody diagnostics
- [ ] iOS app
