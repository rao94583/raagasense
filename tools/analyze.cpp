// Tracks the pitch of a recording and summarizes it against a given Sa.
//
//   bazel run //tools:analyze -- [--sa HZ | --svaras "S g m d n"]
//                                 [--steady | --notes | --tonic | --csv] RECORDING
//
// Prints how much of the voiced time falls on each svara; with --steady, how
// much of the time spent holding steady notes does; with --notes, one line per
// steady note; with --tonic, the candidates for Sa; with --csv, one line per
// frame for plotting. Without --sa, uses the best candidate for Sa, which
// --svaras, the raag's svaras in Bhatkhande letters, makes much more reliable.
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/melody.h"
#include "core/pitch.h"
#include "core/steady.h"
#include "core/svara.h"
#include "core/tonic.h"
#include "platform/audio_file.h"

namespace {

constexpr double kMinConfidence = 0.5;

int Usage() {
  std::fprintf(stderr,
               "usage: analyze [--sa HZ | --svaras \"S g m d n\"] [--steady | --notes | --tonic | --csv] RECORDING\n");
  return 2;
}

// Svaras from letters separated by spaces or commas; empty if any is unknown.
std::vector<int> ParseSvaras(const char* text) {
  std::vector<int> svaras;
  std::string letter;
  for (const char* p = text;; ++p) {
    if (*p && *p != ' ' && *p != ',') {
      letter += *p;
      continue;
    }
    if (!letter.empty()) {
      const int svara = raagasense::SvaraFromLetter(letter);
      if (svara < 0) return {};
      svaras.push_back(svara);
      letter.clear();
    }
    if (!*p) return svaras;
  }
}

}  // namespace

int main(int argc, char** argv) {
  using namespace raagasense;
  // Under bazel run, make relative paths relative to where it was run.
  if (const char* directory = std::getenv("BUILD_WORKING_DIRECTORY")) chdir(directory);
  double sa = 0;
  bool csv = false, steady = false, list_notes = false, tonic = false;
  std::vector<int> svaras;
  std::string path;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--sa") && i + 1 < argc) {
      sa = std::atof(argv[++i]);
    } else if (!std::strcmp(argv[i], "--svaras") && i + 1 < argc) {
      svaras = ParseSvaras(argv[++i]);
      if (svaras.empty()) return Usage();
    } else if (!std::strcmp(argv[i], "--csv")) {
      csv = true;
    } else if (!std::strcmp(argv[i], "--steady")) {
      steady = true;
    } else if (!std::strcmp(argv[i], "--notes")) {
      list_notes = true;
    } else if (!std::strcmp(argv[i], "--tonic")) {
      tonic = true;
    } else if (argv[i][0] == '-' || !path.empty()) {
      return Usage();
    } else {
      path = argv[i];
    }
  }
  if (path.empty() || sa < 0 || (sa > 0 && !svaras.empty()) || csv + steady + list_notes + tonic > 1) {
    return Usage();
  }

  std::string error;
  const std::optional<AudioData> audio = ReadAudioFile(path, &error);
  if (!audio) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
    return 1;
  }

  MelodyOptions options;
  options.sample_rate = audio->sample_rate;
  MelodyTracker tracker(options);
  std::vector<PitchFrame> frames;
  tracker.Push(audio->samples, &frames);
  tracker.Finish(&frames);

  SteadyOptions steady_options;
  steady_options.min_confidence = kMinConfidence;
  const std::vector<SteadyNote> notes = FindSteadyNotes(frames, steady_options);
  const std::vector<SteadyNote> held = HeldPitches(frames, steady_options);
  const std::vector<TonicCandidate> candidates =
      svaras.empty() ? EstimateTonic(held) : EstimateTonicForSvaras(held, svaras);
  if (tonic) {
    std::printf("frequency,score\n");
    for (const TonicCandidate& candidate : candidates) {
      std::printf("%.2f,%.3f\n", candidate.frequency, candidate.score);
    }
    return 0;
  }
  const bool estimated = sa == 0;
  if (estimated) {
    if (candidates.empty()) {
      std::fprintf(stderr, "%s: no steady notes to find Sa from; give it with --sa\n", path.c_str());
      return 1;
    }
    sa = candidates[0].frequency;
  }

  if (csv) {
    std::printf("time,frequency,confidence,svara,deviation\n");
    for (const PitchFrame& frame : frames) {
      if (!frame.voiced || frame.confidence < kMinConfidence) {
        std::printf("%.3f,,%.3f,,\n", frame.time, frame.confidence);
        continue;
      }
      const SvaraPosition position = NearestSvara(CentsFromSa(frame.frequency, sa));
      std::printf("%.3f,%.2f,%.3f,%s,%.1f\n", frame.time, frame.frequency, frame.confidence,
                  SvaraLabel(position).c_str(), position.deviation);
    }
    return 0;
  }

  if (list_notes) {
    std::printf("start,end,frequency,svara,deviation\n");
    for (const SteadyNote& note : notes) {
      const SvaraPosition position = NearestSvara(CentsFromSa(note.frequency, sa));
      std::printf("%.3f,%.3f,%.2f,%s,%.1f\n", note.start, note.end, note.frequency, SvaraLabel(position).c_str(),
                  position.deviation);
    }
    return 0;
  }

  // Seconds on each svara.
  std::array<double, 12> seconds{};
  double total = 0;
  if (steady) {
    for (const SteadyNote& note : notes) {
      seconds[NearestSvara(CentsFromSa(note.frequency, sa)).svara] += note.end - note.start;
      total += note.end - note.start;
    }
  } else {
    const double hop = frames.size() > 1 ? frames[1].time - frames[0].time : 0;
    for (const PitchFrame& frame : frames) {
      if (!frame.voiced || frame.confidence < kMinConfidence) continue;
      seconds[NearestSvara(CentsFromSa(frame.frequency, sa)).svara] += hop;
      total += hop;
    }
  }
  const double duration = audio->samples.size() / audio->sample_rate;
  std::printf("%s\n%.1f s, %.0f%% %s, Sa = %.2f Hz%s\n\n", path.c_str(), duration,
              duration > 0 ? 100.0 * total / duration : 0.0, steady ? "in steady notes" : "voiced", sa,
              estimated ? " (estimated)" : "");
  for (int svara = 0; svara < 12; ++svara) {
    const double share = total > 0 ? 100.0 * seconds[svara] / total : 0;
    std::printf("  %-2s %-10s %6.1f s %5.1f%%  %s\n", std::string(SvaraLetter(svara)).c_str(),
                std::string(SvaraName(svara)).c_str(), seconds[svara], share,
                std::string(static_cast<size_t>(share / 2 + 0.5), '#').c_str());
  }
  return 0;
}
