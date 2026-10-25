// Names the raag from the steady notes of a performance, finding Sa along the
// way: every raag the app knows is tried from every candidate Sa, and scored
// by how likely the held time on each svara is under a simple model of the
// raag: its svaras likely, its vadi more so and its occasional svaras less,
// every other svara rare, and a penalty for each svara it can't do without
// that's missing and for each svara it doesn't use that's there. Given the
// passing notes too, it also weighs the svaras passed through going up and
// going down against those the raag leaves out each way. Given what's been
// learned from labeled recordings, it also weighs how the held time on a
// raag's svaras compares with its recordings', and how many of its
// characteristic phrases turn up beyond what turns up by chance in other
// raags' recordings.
#pragma once

#include <array>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/raag.h"
#include "core/steady.h"

namespace raagasense {

struct RaagMatch {
  const Raag* raag = nullptr;
  // Hz, as from EstimateTonic.
  double sa = 0;
  // Higher is better; only comparable between matches for the same notes.
  double score = 0;
  // How many svaras the raag doesn't use have at least a small share of the
  // held time from this Sa: evidence against it.
  int foreign_svaras = 0;
};

// What labeled recordings of a raag show: the mean share of their held time
// on each svara, and how many recordings that's from.
struct RaagProfile {
  int recordings = 0;
  std::array<double, 12> held = {};
};
using RaagProfiles = std::map<const Raag*, RaagProfile>;

// How much of a raag's phrases turn up in recordings of other raags, from
// their Sa: what turns up by chance.
struct PhraseChance {
  int recordings = 0;
  double share = 0;
};

// What's been learned from labeled recordings.
struct RaagLearning {
  RaagProfiles profiles;
  std::map<const Raag*, PhraseChance> phrase_chance;
};

// The share of the held time in notes on each svara from this Sa.
std::array<double, 12> HeldShares(std::span<const SteadyNote> notes, double sa);

// Adds a recording's held shares (HeldShares from its Sa) to the raag's profile.
void AddToProfile(const std::array<double, 12>& held, RaagProfile* profile);

// The share of each raag's phrases that turn up in order among the passing
// notes from this Sa, within a phrase of them, with up to two other svaras
// between each of a phrase's svaras; raags without phrases are left out.
std::map<const Raag*, double> PhraseShares(std::span<const SteadyNote> passing, double sa);

// Learns from a recording of this raag with this Sa: its held shares for
// the raag's profile, and its phrase shares (PhraseShares) for every other
// raag's chance.
void Learn(const Raag& raag, const std::array<double, 12>& held, const std::map<const Raag*, double>& phrase_shares,
           RaagLearning* learning);

// The learning as text, one line per raag's profile or phrase chance, to
// save; and the learning from such text, skipping raags it doesn't know.
std::string LearningText(const RaagLearning& learning);
RaagLearning LearningFromText(std::string_view text);

// Each known raag's best match, best first; empty without notes. notes are
// the held pitches (HeldPitches), passing the notes to follow the melody's
// movement by (PassingNotes), and learning what's been learned from labeled
// recordings, if any.
std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing = {},
                                    const RaagLearning* learning = nullptr);

// Ranks raags using a supplied tonic, even if Sa is absent from the notes.
// Every result retains exactly this Sa. Empty for no notes or an invalid Sa
// (nonpositive or nonfinite). The Sa/Pa prior is omitted since it would be
// the same for every raag; scores need not equal IdentifyRaag's scores.
std::vector<RaagMatch> IdentifyRaagAtSa(std::span<const SteadyNote> notes, double sa,
                                     std::span<const SteadyNote> passing = {}, const RaagLearning* learning = nullptr);

// The matches for a known raag from each candidate Sa, best first; empty
// without notes.
std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag,
                                 std::span<const SteadyNote> passing = {}, const RaagLearning* learning = nullptr);

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
