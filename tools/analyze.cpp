// Tracks the pitch of a recording and summarizes it against a given Sa.
//
//   bazel run //tools:analyze -- [--sa HZ] [--csv] RECORDING
//
// Prints how much of the voiced time falls on each svara, or with --csv, one
// line per frame for plotting.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/pitch.h"
#include "core/svara.h"
#include "platform/audio_file.h"

namespace {

constexpr double kMinConfidence = 0.5;

int Usage() {
  std::fprintf(stderr, "usage: analyze [--sa HZ] [--csv] RECORDING\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace raagasense;
  double sa = 138.59;
  bool csv = false;
  std::string path;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--sa") && i + 1 < argc) {
      sa = std::atof(argv[++i]);
    } else if (!std::strcmp(argv[i], "--csv")) {
      csv = true;
    } else if (argv[i][0] == '-' || !path.empty()) {
      return Usage();
    } else {
      path = argv[i];
    }
  }
  if (path.empty() || sa <= 0) return Usage();

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

  std::array<int, 12> counts{};
  int voiced = 0;
  for (const PitchFrame& frame : frames) {
    if (!frame.voiced || frame.confidence < kMinConfidence) continue;
    ++counts[NearestSvara(CentsFromSa(frame.frequency, sa)).svara];
    ++voiced;
  }
  const double hop = options.hop_size / audio->sample_rate;
  std::printf("%s\n%.1f s, %.0f%% voiced, Sa = %.2f Hz\n\n", path.c_str(), audio->samples.size() / audio->sample_rate,
              frames.empty() ? 0.0 : 100.0 * voiced / frames.size(), sa);
  for (int svara = 0; svara < 12; ++svara) {
    const double share = voiced ? 100.0 * counts[svara] / voiced : 0;
    std::printf("  %-2s %-10s %6.1f s %5.1f%%  %s\n", std::string(SvaraLetter(svara)).c_str(),
                std::string(SvaraName(svara)).c_str(), counts[svara] * hop, share,
                std::string(static_cast<size_t>(share / 2 + 0.5), '#').c_str());
  }
  return 0;
}
