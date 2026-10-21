# RaagaSense

Listens to Hindustani classical music and identifies the raaga as it is
performed. Native macOS app now, iOS later; C++20 and Objective-C++, built
with Bazel. No machine learning: pitch tracking (harmonic summation, which
copes with accompaniment; YIN for clean single voices), the tonic, svaras and
raaga matching are all conventional signal processing and rules.

```sh
bazel test //...
bazel run //app/macos:RaagaSense
bazel run //tools:analyze -- recording.mp3                   # svara histogram
bazel run //tools:analyze -- --steady recording.mp3          # ...of steady notes only
bazel run //tools:analyze -- --notes recording.mp3           # steady notes, one per line
bazel run //tools:analyze -- --tonic recording.mp3           # candidates for Sa
bazel run //tools:analyze -- --tonic --svaras "S g m d n" recording.mp3  # ...knowing the raag
bazel run //tools:analyze -- --csv recording.mp3             # per-frame pitch
bazel run //tools:plot -- recording.mp3 out.png              # pitch over svara grid
bazel run //tools:evaluate                                   # check against recordings/
bazel run //tools:record -- --list                           # sound inputs
bazel run //tools:record -- --input M2 --channels 3,4 recordings/yaman.m4a
```

The tools estimate Sa from the recording; `--sa HZ` sets it instead.

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
- [x] Steady notes, apart from meend and kan
- [x] Tonic (Sa) estimate from held Sa and Pa, in the tools
- [ ] Tonic for raagas without Pa, and in the app
- [ ] Andolan, and meends as transitions between svaras
- [x] Svaras of 30 raags (29 names, one in two forms), and Sa from a known raag
- [ ] Raaga knowledge base (emphasis, phrases) and scoring
- [ ] iOS app
