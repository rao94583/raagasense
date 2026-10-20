# RaagaSense

Listens to Hindustani classical music and identifies the raaga as it is
performed. Native macOS app now, iOS later; C++20 and Objective-C++, built
with Bazel. No machine learning: pitch tracking (YIN), the tonic, svaras and
raaga matching are all conventional signal processing and rules.

```sh
bazel test //...
bazel run //app/macos:RaagaSense
bazel run //tools:analyze -- --sa 146.83 recording.mp3         # svara histogram
bazel run //tools:analyze -- --sa 146.83 --steady recording.mp3  # ...of steady notes only
bazel run //tools:analyze -- --sa 146.83 --notes recording.mp3   # steady notes, one per line
bazel run //tools:analyze -- --sa 146.83 --csv recording.mp3   # per-frame pitch
bazel run //tools:plot -- --sa 146.83 recording.mp3 out.png    # pitch over svara grid
```

## Layout

| Directory | Contents | Depends on |
|---|---|---|
| `core/` | Pitch tracking, svaras, steady notes; later tonic detection and raaga matching | C++ standard library only |
| `platform/` | Audio files, the microphone, the pitch plot | Core Audio, AVFoundation, Core Graphics; no AppKit or UIKit |
| `app/macos/` | The macOS app | AppKit |
| `tools/` | Command-line tools for working on recordings | macOS |

An iOS app goes in `app/ios/`: a UIKit front end over the same `core` and
`platform` libraries (the microphone code already sets up the iOS audio
session, and the plot draws into any Core Graphics context).

## Status

- [x] YIN pitch tracking, streaming and per frame
- [x] Pitch to svara, relative to a chosen Sa
- [x] Live pitch display on macOS
- [x] Steady notes, apart from meend and kan
- [ ] Tonic (Sa) detection
- [ ] Andolan, and meends as transitions between svaras
- [ ] Raaga knowledge base and scoring
- [ ] iOS app
