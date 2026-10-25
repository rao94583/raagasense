// Checks the analysis against a folder of labeled recordings.
//
//   bazel run //tools:evaluate -- [--all-sa] [--seconds N] [--pitch-dir DIR]
//                              [--learn-to FILE] [DIRECTORY...]
//
// Each DIRECTORY (by default recordings/) holds audio files and, optionally,
// labels.csv with columns file,raag,sa_hz and, optionally, sa_checked. A Sa
// with sa_checked "no", such as one the app found while recording, doesn't
// count in checking Sa unless --all-sa is given, and sa_hz 0 means Sa is
// unknown. A file missing from
// labels.csv takes its raag from the start of its name (miyan_ki_todi_male.m4a) and its
// Sa, if any, from a note later in it (bilawal_male_C_sharp.m4a). For each
// recording it prints the Sa estimated knowing the raag and without knowing
// it, the share of held notes, and raag guesses/ranks using automatic and
// supplied Sa.
// The guesses use what's learned (raag profiles, and how often each raag's
// phrases turn up by chance) from the other recordings with a known Sa,
// leaving out those by the same artist (artist column or "artist ..." in
// notes). Recordings without artist metadata use rules alone and cannot
// train evaluation profiles. With
// --learn-to, it also writes what's learned from all of them to FILE, for the
// app to load. Also compares automatic and labeled Sa on exactly the same
// labeled subset. --seconds evaluates prefixes; --pitch-dir reads supplied
// tracks named <audio filename>.csv instead of extracting melody from audio.
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/identify.h"
#include "core/melody.h"
#include "core/raag.h"
#include "core/steady.h"
#include "core/svara.h"
#include "core/tonic.h"
#include "platform/audio_file.h"
#include "tools/evaluation.h"

namespace {

using namespace raagasense;
using evaluation::Label;

// The labels in a directory's labels.csv, by the files' paths; with
// all_sa, Sas not checked by ear count too.
bool LoadLabels(const std::filesystem::path& directory, bool all_sa, std::map<std::string, Label>* labels) {
  std::ifstream in(directory / "labels.csv");
  if (!in) {
    if (!std::filesystem::exists(directory / "labels.csv")) return true;
    std::fprintf(stderr, "%s: can't read labels.csv\n", directory.c_str());
    return false;
  }
  std::vector<Label> parsed;
  std::string error;
  if (!evaluation::ReadLabels(in, all_sa, &parsed, &error)) {
    std::fprintf(stderr, "%s/labels.csv: %s\n", directory.c_str(), error.c_str());
    return false;
  }
  for (const auto& label : parsed) (*labels)[(directory / label.file).string()] = label;
  return true;
}

std::string Lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// The raag whose name or alias the file name starts with, preferring the
// longest.
const Raag* RaagFromName(const std::string& stem) {
  std::string name;
  for (char c : Lower(stem)) {
    if (c != '_' && c != '-' && c != ' ') name += c;
  }
  const Raag* best = nullptr;
  size_t best_length = 0;
  for (const Raag& raag : Raags()) {
    std::vector<std::string> names = raag.aliases;
    names.push_back(raag.name);
    for (const std::string& raag_name : names) {
      std::string key;
      for (char c : Lower(raag_name)) {
        if (c != ' ' && c != '(' && c != ')') key += c;
      }
      if (name.starts_with(key) && key.size() > best_length) {
        best = &raag;
        best_length = key.size();
      }
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

void Usage() {
  std::fprintf(stderr, "usage: evaluate [--all-sa] [--seconds N] [--pitch-dir DIR] [--learn-to FILE] [DIRECTORY...]\n");
}

// A failed prediction counts as a miss, rather than disappearing from the
// denominator. All modes report per-raag recall and their leading confusions.
struct Metrics {
  int total = 0, first = 0, top3 = 0;
  std::map<std::string, std::pair<int, int>> per_raag;
  std::map<std::pair<std::string, std::string>, int> confusions;

  int Add(const Raag& truth, const std::vector<RaagMatch>& matches) {
    const auto at = std::find_if(matches.begin(), matches.end(), [&](const auto& m) { return m.raag == &truth; });
    const int rank = at == matches.end() ? 0 : static_cast<int>(at - matches.begin()) + 1;
    ++total;
    first += rank == 1;
    top3 += rank > 0 && rank <= 3;
    auto& [correct, count] = per_raag[truth.name];
    correct += rank == 1;
    ++count;
    if (rank != 1) ++confusions[{truth.name, matches.empty() ? "(no prediction)" : matches[0].raag->name}];
    return rank;
  }

  void Print(const char* name) const {
    if (total == 0) {
      std::printf("\n%s: no eligible recordings.\n", name);
      return;
    }
    std::printf("\n%s: top-1 %d/%d (%.1f%%); top-3 %d/%d (%.1f%%).\n", name, first, total,
                100.0 * first / total, top3, total, 100.0 * top3 / total);
    double macro = 0;
    std::printf("  Recall by raag:\n");
    for (const auto& [raag, counts] : per_raag) {
      const auto [correct, count] = counts;
      macro += static_cast<double>(correct) / count;
      std::printf("    %-26s %d/%d\n", raag.c_str(), correct, count);
    }
    std::printf("  Mean per-raag recall: %.1f%%\n", 100 * macro / per_raag.size());
    std::vector<std::pair<int, std::pair<std::string, std::string>>> ordered;
    for (const auto& [pair, count] : confusions) ordered.push_back({count, pair});
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    if (!ordered.empty()) std::printf("  Leading confusions (true -> predicted):\n");
    for (size_t i = 0; i < std::min<size_t>(10, ordered.size()); ++i) {
      std::printf("    %s -> %s: %d\n", ordered[i].second.first.c_str(), ordered[i].second.second.c_str(), ordered[i].first);
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  // Under bazel run, make relative paths relative to where it was run.
  if (const char* directory = std::getenv("BUILD_WORKING_DIRECTORY")) chdir(directory);
  bool all_sa = false;
  std::string learn_to;
  std::string pitch_directory;
  double seconds = 0;
  std::vector<std::filesystem::path> directories;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--all-sa")) {
      all_sa = true;
    } else if (!std::strcmp(argv[i], "--learn-to") && i + 1 < argc) {
      learn_to = argv[++i];
    } else if (!std::strcmp(argv[i], "--pitch-dir") && i + 1 < argc) {
      pitch_directory = argv[++i];
    } else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) {
      if (!evaluation::Number(argv[++i], &seconds) || seconds <= 0) {
        Usage();
        return 2;
      }
    } else if (!std::strcmp(argv[i], "--help")) {
      Usage();
      return 0;
    } else if (argv[i][0] == '-') {
      Usage();
      return 2;
    } else {
      directories.push_back(argv[i]);
    }
  }
  if (directories.empty()) directories.push_back("recordings");

  std::map<std::string, Label> labels;
  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path& directory : directories) {
    if (!LoadLabels(directory, all_sa, &labels)) return 1;
    std::vector<std::filesystem::path> here;
    std::error_code error_code;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error_code)) {
      const std::string extension = Lower(entry.path().extension().string());
      if (extension == ".m4a" || extension == ".mp3" || extension == ".wav" || extension == ".aiff" ||
          extension == ".aif" || extension == ".caf") {
        here.push_back(entry.path());
      }
    }
    if (error_code) {
      std::fprintf(stderr, "%s: %s\n", directory.c_str(), error_code.message().c_str());
      return 1;
    }
    std::sort(here.begin(), here.end());
    files.insert(files.end(), here.begin(), here.end());
  }
  if (files.empty()) {
    std::fprintf(stderr, "No audio files found.\n");
    return 1;
  }
  // Sidecars are keyed by filename; reject ambiguous names across directories.
  if (!pitch_directory.empty()) {
    std::map<std::string, int> names;
    for (const auto& file : files) {
      if (++names[file.filename().string()] > 1) {
        std::fprintf(stderr, "--pitch-dir: duplicate audio filename %s\n", file.filename().c_str());
        return 1;
      }
    }
  }
  std::printf("Pitch source: %s; duration: %s.\n", pitch_directory.empty() ? "MelodyTracker" : "supplied CSV tracks",
              seconds > 0 ? (std::to_string(seconds) + " s prefixes").c_str() : "full recordings");
  std::printf("Sa labels: %s. Learning requires explicit artist metadata; other recordings use rules alone.\n",
              all_sa ? "including unchecked (--all-sa)" : "checked where sa_checked is provided");

  // Each recording analyzed, for learning raag profiles from the others and
  // then naming its raag.
  struct Analysis {
    std::string name, artist;
    const Raag* raag;
    double labeled_sa, held_share;
    std::vector<SteadyNote> notes, passing;
    std::vector<TonicCandidate> with_raag, alone;
    // From the labeled Sa, if known, for learning from.
    std::array<double, 12> held;
    std::map<const Raag*, double> phrase_shares;
  };
  std::vector<Analysis> analyses;
  for (const std::filesystem::path& file : files) {
    const std::string name = file.filename().string();
    const auto label = labels.find(file.string());
    const Raag* raag = label != labels.end() ? FindRaag(label->second.raag) : RaagFromName(file.stem().string());
    const double labeled_sa = label != labels.end() ? label->second.sa : SaFromName(file.stem().string());
    if (!raag) {
      std::printf("%-40s no known raag in the name or labels.csv\n", name.c_str());
      continue;
    }

    std::string error;
    std::optional<AudioData> audio = ReadAudioFile(file.string(), &error);
    if (!audio) {
      std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str());
      return 1;
    }
    if (seconds > 0 && seconds < audio->samples.size() / audio->sample_rate) {
      audio->samples.resize(static_cast<size_t>(seconds * audio->sample_rate));
    }
    const double duration = audio->samples.size() / audio->sample_rate;
    std::vector<PitchFrame> frames;
    if (pitch_directory.empty()) {
      MelodyOptions options;
      options.sample_rate = audio->sample_rate;
      MelodyTracker tracker(options);
      tracker.Push(audio->samples, &frames);
      tracker.Finish(&frames);
    } else {
      const auto path = std::filesystem::path(pitch_directory) / (name + ".csv");
      std::ifstream in(path);
      if (!in || !evaluation::ReadPitchCsv(in, &frames, &error)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), in ? error.c_str() : "can't read pitch CSV");
        return 1;
      }
      frames.erase(std::lower_bound(frames.begin(), frames.end(), duration,
                                   [](const PitchFrame& frame, double end) { return frame.time < end; }), frames.end());
    }
    Analysis analysis{name, label != labels.end() ? label->second.artist : "",
                      raag, labeled_sa, 0, HeldPitches(frames), PassingNotes(frames), {}, {}, {}, {}};
    double held = 0;
    for (const SteadyNote& note : analysis.notes) held += note.end - note.start;
    analysis.held_share = duration > 0 ? held / duration : 0;
    analysis.with_raag = EstimateTonicForSvaras(analysis.notes, raag->svaras);
    analysis.alone = EstimateTonic(analysis.notes);
    if (labeled_sa > 0) {
      analysis.held = HeldShares(analysis.notes, labeled_sa);
      analysis.phrase_shares = PhraseShares(analysis.passing, labeled_sa);
    }
    analyses.push_back(std::move(analysis));
  }
  if (analyses.empty()) {
    std::fprintf(stderr, "No recordings with recognized raag labels.\n");
    return 1;
  }

  if (!learn_to.empty()) {
    RaagLearning learning;
    int trained = 0;
    for (const Analysis& analysis : analyses) {
      if (analysis.labeled_sa > 0 && !analysis.notes.empty()) {
        Learn(*analysis.raag, analysis.held, analysis.phrase_shares, &learning);
        ++trained;
      }
    }
    std::ofstream out(learn_to);
    out << LearningText(learning);
    if (!out) {
      std::fprintf(stderr, "%s: can't write\n", learn_to.c_str());
      return 1;
    }
    std::fprintf(stderr, "Learned from %d recordings of %zu raags; wrote %s\n", trained,
                 learning.profiles.size(), learn_to.c_str());
  }

  std::printf("%-40s %-26s %-10s %-10s %-10s %5s  %-26s %4s  %-26s %4s\n", "file", "raag", "label Sa",
              "Sa | raag", "Sa alone", "held", "auto-Sa guess", "rank", "label-Sa guess", "rank");
  int labeled = 0, right_with_raag = 0, right_alone = 0;
  int missing_artists = 0;
  Metrics automatic, paired_automatic, fixed;
  for (const Analysis& analysis : analyses) {
    const Raag* raag = analysis.raag;
    const std::vector<TonicCandidate>& with_raag = analysis.with_raag;
    const std::vector<TonicCandidate>& alone = analysis.alone;
    const double labeled_sa = analysis.labeled_sa;
    std::string mark_with = " ", mark_alone = " ";
    if (labeled_sa > 0) {
      ++labeled;
      const bool with_ok = !with_raag.empty() && SamePitchClass(with_raag[0].frequency, labeled_sa);
      const bool alone_ok = !alone.empty() && SamePitchClass(alone[0].frequency, labeled_sa);
      right_with_raag += with_ok;
      right_alone += alone_ok;
      mark_with = with_ok ? " " : "x";
      mark_alone = alone_ok ? " " : "x";
    }

    // Learned from the other artists' recordings with a known Sa, so that no
    // recording helps name itself or its artist's others.
    RaagLearning learning;
    if (analysis.artist.empty()) ++missing_artists;
    if (!analysis.artist.empty()) {
      for (const Analysis& other : analyses) {
        if (other.artist.empty() || other.artist == analysis.artist || other.labeled_sa <= 0 || other.notes.empty()) continue;
        Learn(*other.raag, other.held, other.phrase_shares, &learning);
      }
    }
    const std::vector<RaagMatch> matches = IdentifyRaag(analysis.notes, analysis.passing, &learning);
    const int rank = automatic.Add(*raag, matches);
    std::vector<RaagMatch> fixed_matches;
    int fixed_rank = 0;
    if (labeled_sa > 0) {
      paired_automatic.Add(*raag, matches);
      fixed_matches = IdentifyRaagAtSa(analysis.notes, labeled_sa, analysis.passing, &learning);
      fixed_rank = fixed.Add(*raag, fixed_matches);
    }
    std::printf("%-40s %-26s %-10s %s%-9s %s%-9s %4.0f%%  %-26s %4d  %-26s %4s\n",
                analysis.name.c_str(), raag->name.c_str(), labeled_sa > 0 ? NoteName(labeled_sa).c_str() : "?",
                mark_with.c_str(), with_raag.empty() ? "?" : NoteName(with_raag[0].frequency).c_str(),
                mark_alone.c_str(), alone.empty() ? "?" : NoteName(alone[0].frequency).c_str(),
                100 * analysis.held_share, matches.empty() ? "(no prediction)" : matches[0].raag->name.c_str(), rank,
                labeled_sa <= 0 ? "-" : fixed_matches.empty() ? "(no prediction)" : fixed_matches[0].raag->name.c_str(),
                labeled_sa > 0 ? std::to_string(fixed_rank).c_str() : "-");
  }
  if (labeled > 0) {
    std::printf("\nSa right on %d of %d labeled recordings knowing the raag, %d without (x marks a miss).\n",
                right_with_raag, labeled, right_alone);
  }
  std::printf("%d/%zu recordings lack artist metadata and were evaluated with rules alone. Rank 0 means no prediction.\n",
              missing_artists, analyses.size());
  automatic.Print("Automatic Sa (all recordings)");
  paired_automatic.Print("Automatic Sa (labeled subset)");
  fixed.Print("Labeled Sa (same subset)");
  return 0;
}
