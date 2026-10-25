// Names the raag from the steady notes of a performance, finding Sa along the
// way: every raag the app knows is tried from every candidate Sa, and scored
// by how likely the held time on each svara is under a simple model of the
// raag: its svaras likely, its vadi more so and its occasional svaras less,
// every other svara rare, and a penalty for each svara it can't do without
// that's missing and for each svara it doesn't use that's there. Given the
// passing notes too, it also weighs the svaras passed through going up and
// going down against those the raag leaves out each way. Learned profiles,
// if available, compare held time with labeled recordings. Characteristic
// phrases are scored against a shuffled-note null model, even without
// learned profiles.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/raag.h"
#include "core/steady.h"
#include "core/tonic.h"

namespace raagasense {

// Weighted contributions to the existing score. Populated only when tracing.
// Their sum agrees with score to floating-point rounding precision.
struct RaagScoreComponents {
  double tonic_prior = 0;
  double rare_prior = 0;
  double held_fit = 0;
  double missing_notes = 0;
  double foreign_notes = 0;
  double direction = 0;
  double learned_profile = 0;
  double phrases = 0;
};

struct RaagMatch {
  const Raag* raag = nullptr;
  // Hz, as from EstimateTonic.
  double sa = 0;
  // Higher is better; only comparable between matches for the same notes.
  double score = 0;
  // How many svaras the raag doesn't use have at least a small share of the
  // held time from this Sa: evidence against it.
  int foreign_svaras = 0;
  std::optional<RaagScoreComponents> components;
};

// What labeled recordings of a raag show: the mean share of their held time
// on each svara, and how many recordings that's from.
struct RaagProfile {
  int recordings = 0;
  std::array<double, 12> held = {};
};
using RaagProfiles = std::map<const Raag*, RaagProfile>;

// What's been learned from labeled recordings.
struct RaagLearning {
  RaagProfiles profiles;
};

// The share of the held time in notes on each svara from this Sa.
std::array<double, 12> HeldShares(std::span<const SteadyNote> notes, double sa);

// Adds a recording's held shares (HeldShares from its Sa) to the raag's profile.
void AddToProfile(const std::array<double, 12>& held, RaagProfile* profile);

// Mean complete-phrase match quality for each raag, preserving octave
// contour and repeats. Extra notes are discounted; raags without phrases
// are left out. This is raw coverage, before chance correction.
std::map<const Raag*, double> PhraseShares(std::span<const SteadyNote> passing, double sa);

// Adds the recording's held shares to its raag's learned profile.
void Learn(const Raag& raag, const std::array<double, 12>& held, RaagLearning* learning);

// One CSV line per profile; unknown raags and legacy "chance" rows are
// ignored when loading. Old learned pitch profiles remain compatible.
std::string LearningText(const RaagLearning& learning);
RaagLearning LearningFromText(std::string_view text);

struct IdentifyOptions {
  // Disable to measure the contribution of characteristic phrase evidence.
  bool use_phrases = true;
  // Experimental: a stable passing note can satisfy presence of an expected
  // svara even when it accounts for little held time. Other score terms stay
  // unchanged. Disabled by default because current comparisons show tradeoffs.
  bool use_passing_note_presence = false;
};

// Optional diagnostic output. Captures the actual production search without
// supplying the reference Sa or changing scoring. Hypotheses retain every
// raag/Sa pair, including alternative Sas for a single raag, best score first.
// Scores and gaps are heuristic evidence, not calibrated probabilities.
struct IdentifyTrace {
  std::vector<TonicCandidate> candidates;
  std::vector<RaagMatch> hypotheses;
};

// Each known raag's best match, best first; empty without notes. notes are
// the held pitches (HeldPitches), passing the notes to follow the melody's
// movement by (PassingNotes), and learning what's been learned from labeled
// recordings, if any.
std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing = {},
                                    const RaagLearning* learning = nullptr, const IdentifyOptions& options = {},
                                    IdentifyTrace* trace = nullptr);

// Ranks raags using a supplied tonic, even if Sa is absent from the notes.
// Every result retains exactly this Sa. Empty for no notes or an invalid Sa
// (nonpositive or nonfinite). The Sa/Pa prior is omitted since it would be
// the same for every raag; scores need not equal IdentifyRaag's scores.
std::vector<RaagMatch> IdentifyRaagAtSa(std::span<const SteadyNote> notes, double sa,
                                     std::span<const SteadyNote> passing = {}, const RaagLearning* learning = nullptr,
                                     const IdentifyOptions& options = {}, IdentifyTrace* trace = nullptr);

// The matches for a known raag from each candidate Sa, best first; empty
// without notes.
std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag,
                                 std::span<const SteadyNote> passing = {}, const RaagLearning* learning = nullptr,
                                 const IdentifyOptions& options = {});

// Keeps a guess at the raag in first place while the evidence keeps coming
// in, so that it changes only for good reason: when another raag has led it
// clearly for a while, or when a svara the leader doesn't use turns up.
struct TrackerOptions {
  // How far ahead another raag's score must be, and for how many seconds of
  // the stream, to take first place. Replaying 133 labeled recordings with a
  // guess every 2 s, the first guess changed 491 times after the first 30 s
  // without it, 223 times with these, while right 41% of that time without
  // and 39% with. Holding on harder (0.15 for 20 s, or 0.3 for 30 s) changes
  // it less but keeps more early mistakes: right 37% and 35% of the time.
  double margin = 0.1;
  double seconds = 6;
};

class RaagTracker {
 public:
  explicit RaagTracker(const TrackerOptions& options = {}) : options_(options) {}

  // Takes the latest matches (IdentifyRaag's, best first) at this stream
  // time, and returns them with the raag kept in first place, if it's kept.
  std::vector<RaagMatch> Update(std::vector<RaagMatch> matches, double now);

 private:
  TrackerOptions options_;
  const Raag* leader_ = nullptr;
  int leader_foreign_ = 0;
  // The raag that has been ahead of the leader by the margin, and since when.
  const Raag* challenger_ = nullptr;
  double challenger_since_ = 0;
};

}  // namespace raagasense
