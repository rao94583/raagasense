// Tracks the pitch of a recording and summarizes it against a given Sa.
//
//   bazel run //tools:analyze -- [--sa HZ] [--steady | --notes | --csv] RECORDING
//
// Prints how much of the voiced time falls on each svara; with --steady, how
// much of the time spent holding steady notes does; with --notes, one line per
// steady note; with --csv, one line per frame for plotting.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/pitch.h"
#include "core/steady.h"
#include "core/svara.h"
#include "platform/audio_file.h"

namespace {

constexpr double kMinConfidence = 0.5;

int Usage() {
  std::fprintf(stderr, "usage: analyze [--sa HZ] [--steady | --notes | --csv] RECORDING\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace raagasense;
  double sa = 138.59;
  bool csv = false, steady = false, list_notes = false;
  std::string path;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--sa") && i + 1 < argc) {
      sa = std::atof(argv[++i]);
    } else if (!std::strcmp(argv[i], "--csv")) {
      csv = true;
    } else if (!std::strcmp(argv[i], "--steady")) {
      steady = true;
    } else if (!std::strcmp(argv[i], "--notes")) {
      list_notes = true;
    } else if (argv[i][0] == '-' || !path.empty()) {
      return Usage();
    } else {
      path = argv[i];
    }
  }
  if (path.empty() || sa <= 0 || csv + steady + list_notes > 1) return Usage();

  std::string error;
  const std::optional<AudioData> audio = ReadAudioFile(path, &error);
  if (!audio) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
    return 1;
  }

  PitchOptions options;
  options.sample_rate = audio->sample_rate;
  PitchTracker tracker(options);
  std::vector<PitchFrame> frames;
  tracker.Push(audio->samples, &frames);

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

  SteadyOptions steady_options;
  steady_options.min_confidence = kMinConfidence;
  const std::vector<SteadyNote> notes = FindSteadyNotes(frames, steady_options);
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
    const double hop = options.hop_size / audio->sample_rate;
    for (const PitchFrame& frame : frames) {
      if (!frame.voiced || frame.confidence < kMinConfidence) continue;
      seconds[NearestSvara(CentsFromSa(frame.frequency, sa)).svara] += hop;
      total += hop;
    }
  }
  const double duration = audio->samples.size() / audio->sample_rate;
  std::printf("%s\n%.1f s, %.0f%% %s, Sa = %.2f Hz\n\n", path.c_str(), duration,
              duration > 0 ? 100.0 * total / duration : 0.0, steady ? "in steady notes" : "voiced", sa);
  for (int svara = 0; svara < 12; ++svara) {
    const double share = total > 0 ? 100.0 * seconds[svara] / total : 0;
    std::printf("  %-2s %-10s %6.1f s %5.1f%%  %s\n", std::string(SvaraLetter(svara)).c_str(),
                std::string(SvaraName(svara)).c_str(), seconds[svara], share,
                std::string(static_cast<size_t>(share / 2 + 0.5), '#').c_str());
  }
  return 0;
}
