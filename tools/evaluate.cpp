// Checks the analysis against a folder of labeled recordings.
//
//   bazel run //tools:evaluate -- [DIRECTORY]
//
// DIRECTORY (by default recordings/) holds audio files and, optionally,
// labels.csv with columns file,raag,sa_hz. A file missing from labels.csv
// takes its raag from the start of its name (miyan_ki_todi_male.m4a) and its
// Sa, if any, from a note later in it (bilawal_male_C_sharp.m4a). For each
// recording it prints the Sa estimated knowing the raag, with how much of the
// held time fits the raag from it, and the same for the next best Sa; the Sa
// estimated without knowing the raag; and how much of the recording is held
// notes. A small gap between the two fits means the Sa is uncertain.
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/melody.h"
#include "core/raag.h"
#include "core/steady.h"
#include "core/svara.h"
#include "core/tonic.h"
#include "platform/audio_file.h"

namespace {

using namespace raagasense;

struct Label {
  std::string raag;
  double sa = 0;  // Hz; 0 if unknown
};

// The fields of one CSV line, with double-quoted fields unquoted.
std::vector<std::string> CsvFields(const std::string& line) {
  std::vector<std::string> fields(1);
  bool quoted = false;
  for (char c : line) {
    if (c == '"') {
      quoted = !quoted;
    } else if (c == ',' && !quoted) {
      fields.emplace_back();
    } else {
      fields.back() += c;
    }
  }
  return fields;
}

std::map<std::string, Label> ReadLabels(const std::filesystem::path& path) {
  std::map<std::string, Label> labels;
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);  // The header.
  while (std::getline(in, line)) {
    const std::vector<std::string> fields = CsvFields(line);
    if (fields.size() < 3) continue;
    labels[fields[0]] = {fields[1], std::atof(fields[2].c_str())};
  }
  return labels;
}

std::string Lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// The raag whose name the file name starts with, preferring the longest.
const Raag* RaagFromName(const std::string& stem) {
  std::string name;
  for (char c : Lower(stem)) {
    if (c != '_' && c != '-' && c != ' ') name += c;
  }
  const Raag* best = nullptr;
  size_t best_length = 0;
  for (const Raag& raag : Raags()) {
    std::string key;
    for (char c : Lower(raag.name)) {
      if (c != ' ') key += c;
    }
    if (name.starts_with(key) && key.size() > best_length) {
      best = &raag;
      best_length = key.size();
    }
  }
  return best;
}

// The pitch class named in the file name ("_C_sharp", "_D", "_Eb"), as Hz in
// the octave from C3; 0 if none.
double SaFromName(const std::string& stem) {
  constexpr const char* kNotes = "C D EF G A B";
  std::vector<std::string> tokens(1);
  for (char c : stem) {
    if (c == '_' || c == '-' || c == ' ') {
      tokens.emplace_back();
    } else {
      tokens.back() += c;
    }
  }
  for (size_t i = 1; i < tokens.size(); ++i) {
    const std::string& token = tokens[i];
    if (token.empty() || token[0] < 'A' || token[0] > 'G') continue;
    const std::string rest = Lower(token.substr(1));
    const std::string next = i + 1 < tokens.size() ? Lower(tokens[i + 1]) : "";
    int semitone = static_cast<int>(std::string(kNotes).find(token[0]));
    if (rest == "#" || rest == "sharp" || next == "sharp") {
      ++semitone;
    } else if (rest == "b" || rest == "flat" || next == "flat") {
      --semitone;
    } else if (!rest.empty()) {
      continue;
    }
    return 130.8128 * std::exp2(((semitone + 12) % 12) / 12.0);
  }
  return 0;
}

// "A#3 +5" for 233.6 Hz.
std::string NoteName(double frequency) {
  static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  const double cents = 1200 * std::log2(frequency / 130.8128);
  const int semitones = static_cast<int>(std::lround(cents / 100));
  char text[32];
  std::snprintf(text, sizeof text, "%s%d %+.0f", kNames[((semitones % 12) + 12) % 12],
                3 + static_cast<int>(std::floor(semitones / 12.0)), cents - 100 * semitones);
  return text;
}

bool SamePitchClass(double a, double b) { return std::abs(std::remainder(CentsFromSa(a, b), 1200)) < 50; }

}  // namespace

int main(int argc, char** argv) {
  // Under bazel run, make relative paths relative to where it was run.
  if (const char* directory = std::getenv("BUILD_WORKING_DIRECTORY")) chdir(directory);
  if (argc > 2) {
    std::fprintf(stderr, "usage: evaluate [DIRECTORY]\n");
    return 2;
  }
  const std::filesystem::path directory = argc > 1 ? argv[1] : "recordings";
  const std::map<std::string, Label> labels = ReadLabels(directory / "labels.csv");

  std::vector<std::filesystem::path> files;
  std::error_code error_code;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error_code)) {
    const std::string extension = Lower(entry.path().extension().string());
    if (extension == ".m4a" || extension == ".mp3" || extension == ".wav" || extension == ".aiff" ||
        extension == ".aif" || extension == ".caf") {
      files.push_back(entry.path());
    }
  }
  if (error_code) {
    std::fprintf(stderr, "%s: %s\n", directory.c_str(), error_code.message().c_str());
    return 1;
  }
  std::sort(files.begin(), files.end());

  std::printf("%-40s %-18s %-10s  %-10s %4s  %-10s %4s  %-10s %5s\n", "file", "raag", "label Sa", "Sa | raag",
              "fit", "next Sa", "fit", "Sa alone", "held");
  int labeled = 0, right_with_raag = 0, right_alone = 0;
  for (const std::filesystem::path& file : files) {
    const std::string name = file.filename().string();
    const auto label = labels.find(name);
    const Raag* raag = label != labels.end() ? FindRaag(label->second.raag) : RaagFromName(file.stem().string());
    const double labeled_sa = label != labels.end() ? label->second.sa : SaFromName(file.stem().string());
    if (!raag) {
      std::printf("%-40s no known raag in the name or labels.csv\n", name.c_str());
      continue;
    }

    std::string error;
    const std::optional<AudioData> audio = ReadAudioFile(file.string(), &error);
    if (!audio) {
      std::printf("%-40s %s\n", name.c_str(), error.c_str());
      continue;
    }
    MelodyOptions options;
    options.sample_rate = audio->sample_rate;
    MelodyTracker tracker(options);
    std::vector<PitchFrame> frames;
    tracker.Push(audio->samples, &frames);
    const std::vector<SteadyNote> notes = FindSteadyNotes(frames);
    double held = 0;
    for (const SteadyNote& note : notes) held += note.end - note.start;
    const std::vector<TonicCandidate> with_raag = EstimateTonicForSvaras(notes, raag->svaras);
    const std::vector<TonicCandidate> alone = EstimateTonic(notes);
    if (with_raag.empty()) {
      std::printf("%-40s %-18s no held notes\n", name.c_str(), raag->name.c_str());
      continue;
    }

    std::string mark_with = " ", mark_alone = " ";
    if (labeled_sa > 0) {
      ++labeled;
      const bool with_ok = SamePitchClass(with_raag[0].frequency, labeled_sa);
      const bool alone_ok = SamePitchClass(alone[0].frequency, labeled_sa);
      right_with_raag += with_ok;
      right_alone += alone_ok;
      mark_with = with_ok ? " " : "x";
      mark_alone = alone_ok ? " " : "x";
    }
    const bool has_next = with_raag.size() > 1;
    std::printf("%-40s %-18s %-10s %s%-10s %3.0f%%  %-10s %3.0f%% %s%-10s %4.0f%%\n", name.c_str(),
                raag->name.c_str(), labeled_sa > 0 ? NoteName(labeled_sa).c_str() : "?", mark_with.c_str(),
                NoteName(with_raag[0].frequency).c_str(), 100 * with_raag[0].score,
                has_next ? NoteName(with_raag[1].frequency).c_str() : "-", has_next ? 100 * with_raag[1].score : 0.0,
                mark_alone.c_str(), NoteName(alone[0].frequency).c_str(),
                100 * held / (audio->samples.size() / audio->sample_rate));
  }
  if (labeled > 0) {
    std::printf("\nSa right on %d of %d labeled recordings knowing the raag, %d without (x marks a miss).\n",
                right_with_raag, labeled, right_alone);
  }
  return 0;
}
