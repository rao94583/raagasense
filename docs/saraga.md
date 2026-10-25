# Saraga Hindustani development corpus

RaagaSense uses the local Saraga Hindustani **1.5** download as its primary
development corpus. The earlier personal recordings remain available for
optional checks; their manual recovery is no longer a prerequisite.

Saraga is by B. Bozkurt, A. Srinivasamurthy, S. Gulati, and X. Serra,
Music Technology Group, Universitat Pompeu Fabra. The dataset is distributed
under CC BY-NC-SA 4.0. See the [version 1.5 dataset record](https://zenodo.org/records/4301737),
[project website](https://mtg.github.io/saraga/), and
[file-format documentation](https://mtg.github.io/saraga/organization.html).
The local import retains metadata and links to media; this repository does
not redistribute the recordings.

## Import

From the repository root:

```sh
mkdir -p data
bazel run //tools:raag_catalog > data/raag-catalog.tsv
python3 tools/import_saraga.py /path/to/saraga1.5_hindustani --catalog data/raag-catalog.tsv
```

Add `--dry-run` to inspect without importing, or `--output /another/path`
to create an independent view. Existing destinations are refused to avoid
overwriting locally corrected labels. The import is staged and renamed
into place only after successful completion. Original files are never
written. Moving or unmounting the source volume breaks the links; keep it
available when using the dataset.

The importer reads the supplied `file_paths.csv` and metadata JSON files,
cross-checks recording IDs and inventory, and validates required sidecars.
It uses the compiled RaagaSense catalog and aliases, plus three explicit
spelling conversions: Bhairabi → Bhairavi, Jogia → Jogiya, and Miya malhar
→ Miyan ki Malhar. When raag metadata is missing, titles are accepted as
accurate raag labels, as requested for this project. The importer
recognizes complete catalog names and titles starting with Raag/Raga/Rāga;
it matches the whole name and records `raag_source=title` in both labels
and the manifest. Metadata labels have `raag_source=metadata` and take
precedence. Introductions mentioning a raag are not treated as titled raag
performances, and multiple metadata labels remain excluded. Compound and
Mishra raags retain their complete names.

For this dataset, **Poorva → Puriya Kalyan** is an explicit project decision.
The original `source_raag=Poorva` and title remain intact; `raag_mapping`
records the decision separately from spelling aliases. This changes the
Saraga import rather than adding a general alias to the app's catalog.
Sawani is explicitly excluded from evaluation (`excluded_raag`) and
retained in the complete collection, including if a future catalog adds it.

Artists come from the track's lead artist, a unique voice performer if the
lead flag is absent, or a unique album artist if neither is present.
`artist_source` records the choice. MusicBrainz artist IDs keep spellings
consistent within the import. Release-level fallbacks deserve a spot check
before treating this as a definitive benchmark.

## Local inventory

Import after accepting title labels and the Poorva mapping on 2026-10-25:

| Item | Count |
|---|---:|
| All recordings | 108 |
| Total duration from metadata | 43.59 hours |
| Eligible recordings | 89 |
| Eligible duration | 38.58 hours |
| Supported raags represented | 51 |
| Artists in eligible view | 11 |
| Missing raag metadata | 15 |
| Labels recovered from titles | 10 |
| Recovered labels eligible for evaluation | 9 |
| No raag label available from metadata or title | 5 |
| Unsupported labels, excluding explicit exclusions | 13 |
| Explicitly excluded Sawani recordings | 1 |
| Supported raags represented by only one artist | 27 |

The 13 unsupported labels are Basanti kedar, Dagori, Gawti, Hindol Pancham,
Jait Kalyan, Khokar, Lagan Gandhar, Lalit pancham, Maajh khamaj,
Mishra kalingada, Mishra piloo, Nat Kamod, and Triveni gauri.
Each has one recording in this download. Catalog expansion needs musical
definitions and validation; dropping distinctive words would create wrong
labels.

Generated layout (audio filenames are stable recording MBIDs):

```text
data/saraga/
  collection/labels.csv       all recordings, including Unknown labels
  collection/<mbid>.mp3      links to all original recordings
  recordings/labels.csv      eligible raags and performer identities
  recordings/<mbid>.mp3      evaluation view
  pitch/<mbid>.mp3.tsv       links to original Melodia pitch tracks
  metadata/<mbid>.json       links to original editorial metadata
  annotations/<mbid>/        original tonic, pitch and available manual files
  excluded.csv              explicit reasons for exclusions
  manifest.json             source paths, titles, identifiers and import summary
```

Local edits in `recordings/labels.csv` and `collection/labels.csv` are
independent. Review the eligible view when improving benchmark labels;
keep the manifest as a record of the original import.

## Unlabeled recordings

Ten of the 15 recordings missing raag metadata have titles naming a raag.
Those titles are trusted as labels without requiring another listening
check. Paraj, Bhimpalasi, Sohani (canonical Sohini), Kedar, Bahar, Gaud
Malhar, Bhoopali, and Puriya join evaluation. Poorva also joins evaluation
under Puriya Kalyan, following the project's mapping decision. Sawani
retains its title label and remains explicitly excluded. No guessed raag
UUID is assigned to title labels.

The remaining five entries include introductions, lyrics, and
demonstrations. They remain in the collection without raag labels and can
later help test speech handling and abstention. Unsupported but explicitly
labeled raags can help evaluate unfamiliar-raag handling or catalog
expansion. Accepting a raag title does not change the separate unchecked
status of the automatically extracted tonic.

## Evaluation protocol

```sh
# RaagaSense melody extraction; imported automatic tonic references enabled.
bazel run //tools:evaluate -- --seconds 120 --all-sa

# Same recordings and duration, using Saraga's automatic Melodia extraction.
bazel run //tools:evaluate -- --seconds 120 --all-sa --pitch-dir data/saraga/pitch --pitch-format saraga

# Legacy corpus remains explicitly selectable.
bazel run //tools:evaluate -- --seconds 120 recordings
```

The default dataset is `data/saraga/recordings` if imported; otherwise it is
`recordings`. Evaluation continues to leave out all recordings by the
evaluated artist when learning profiles. For raags represented by only one
artist, that fold has no learned profile for the true raag. Recordings are
the evaluation units; overlapping excerpts should never be counted as
independent tracks or split across training and validation artists.

Saraga's tonic and frame-level pitch are automatically extracted. Every
imported tonic has `sa_checked=no`; `--all-sa` deliberately opts into those
references for provisional comparisons and learning. Fixed-Sa results
condition on those references being correct. Agreement with Melodia is
not a measurement of true lead-voice pitch accuracy. Its missing confidence
is represented internally by a processing weight of 1, with zero Hz
preserved as unvoiced. Manual phrase annotations are sparse symbolic
phrases, not complete frame-level pitch corrections.

Use these comparisons to locate likely bottlenecks. The first 120 seconds
may contain introductions or a limited alap, so this is an initial prefix
baseline, not full-performance accuracy. A later benchmark should select
musical windows using the section annotations, keep an untouched artist
test set, audit a small number of tonic references, and report the number
of raags with adequate artist coverage. Do not tune on the whole corpus
and then call its cross-validation result a final test score.

## Current 89-recording baseline and joint-search diagnosis

Measured 2026-10-25 after accepting the Poorva mapping, with the same
parameters and artist-held-out learning. Optional joint-search tracing
captures existing decisions without changing predictions. Both runs use
120-second prefixes and `--all-sa`.

| Pitch source | Automatic Sa top-1 | Automatic Sa top-3 | Supplied Sa top-1 | Supplied Sa top-3 |
|---|---:|---:|---:|---:|
| RaagaSense tracker | 27/89 (30.3%) | 46/89 (51.7%) | 37/89 (41.6%) | 55/89 (61.8%) |
| Saraga Melodia | 28/89 (31.5%) | 42/89 (47.2%) | 42/89 (47.2%) | 52/89 (58.4%) |

Both modes propose a Sa agreeing with the reference on 88/89 recordings.
With our tracker, 35 wrong predictions use the reference Sa and another
26 choose another raag and Sa despite a reference-Sa proposal being
available. Melodia has 30 recordings in each of those groups. This suggests
prioritizing the joint scoring and its melodic features over widening the
tonic candidate set. See [joint-search diagnostics](joint-search.md) for
the full interpretation, limitations, and review artifacts.

Full accuracy reports are
`data/saraga/baseline-tracker-89-recordings-120s.txt` and
`data/saraga/baseline-melodia-89-recordings-120s.txt`.

## Measured 88-recording baseline with title labels

Refreshed 2026-10-25 on 88 recordings, including eight accepted title
labels, with the same algorithm, parameters and artist-held-out protocol.
Both modes use `--all-sa`; tonic references remain automatically extracted.
These results predate the Poorva mapping; the current 89-recording results
are reported above.

| Pitch source | Automatic Sa top-1 | Automatic Sa top-3 | Supplied Sa top-1 | Supplied Sa top-3 |
|---|---:|---:|---:|---:|
| RaagaSense tracker | 27/88 (30.7%) | 46/88 (52.3%) | 38/88 (43.2%) | 55/88 (62.5%) |
| Saraga Melodia | 28/88 (31.8%) | 42/88 (47.7%) | 42/88 (47.7%) | 52/88 (59.1%) |

Mean per-raag top-1 recall is 28.3% automatic / 40.5% supplied Sa with our
tracker, and 28.9% / 47.3% with Melodia. Independent tonic agreement is
58/88 and 50/88, respectively; knowing the true raag raises it to 75/88
and 77/88. As before, that last diagnostic uses the answer.

Full reports are `data/saraga/baseline-tracker-88-recordings-120s.txt` and
`data/saraga/baseline-melodia-88-recordings-120s.txt`. The denominator and training pool
changed from the initial 80 recordings, so compare extractors within each
table rather than attributing differences between tables to the algorithm.
The previous import is preserved locally as
`data/saraga-before-title-labels-20261025T165027Z`.

## Initial metadata-only 120-second baseline

Before accepting title labels, measured 2026-10-25 on 80 eligible recordings,
with the current working
tree's framewise tracker, phrase evidence enabled, and artist-held-out
profile learning. Both runs used `--all-sa`. Predictions use the full app
catalog, rather than restricting candidates to the 48 raags in this view.
No scoring weights were changed for this corpus.

| Pitch source | Automatic Sa top-1 | Automatic Sa top-3 | Supplied Sa top-1 | Supplied Sa top-3 |
|---|---:|---:|---:|---:|
| RaagaSense tracker | 24/80 (30.0%) | 39/80 (48.8%) | 34/80 (42.5%) | 47/80 (58.8%) |
| Saraga Melodia | 25/80 (31.2%) | 38/80 (47.5%) | 37/80 (46.2%) | 46/80 (57.5%) |

Mean per-raag top-1 recall was 27.1% automatic / 37.6% supplied Sa with our
tracker, and 27.0% / 44.1% with Melodia. Independent tonic estimation agreed
with the supplied tonic's pitch class within 50 cents on 54/80 recordings
using our tracker and 45/80 using Melodia. Knowing the true raag raised
that agreement to 67/80 and 70/80, respectively; that diagnostic uses the
answer and is not an inference-time accuracy score.

Full per-recording ranks, recall and confusions are in the local files
`data/saraga/baseline-tracker-80-recordings-120s.txt` and
`data/saraga/baseline-melodia-80-recordings-120s.txt`. Each mode learns its held-note
profiles from its own pitch representation, so this compares complete
pipelines rather than varying only the test recording's extraction.

Supplying Sa helped both pipelines more than switching pitch extractors.
The modest Melodia advantage suggests that tonic inference and raag
representation deserve attention alongside extraction. This is a
development diagnostic with uncertain tonic references, sparse artist
coverage, and opening excerpts; it does not establish a final accuracy or
prove that either extractor has more accurate pitch contours.

## Validation

```sh
python3 -m unittest discover -s tools -p 'import_saraga_test.py' -v
bazel test //...
```

Synthetic import tests cover stable identifiers, spelling mappings, quoted
CSV fields, preserved sources, automatic-feature provenance, missing
labels, title fallback and its provenance, whole-name matching, metadata
precedence, ambiguous artists, inventory mismatches, path boundaries, invalid
tonics, and protection of existing imports. C++ tests cover Saraga pitch
timing/voicing, invalid frames, and decoding only the requested audio prefix.
Import tests also check the Poorva mapping and its provenance, and Sawani's
explicit exclusion even if the catalog later supports it.
