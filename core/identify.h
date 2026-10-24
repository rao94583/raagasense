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

// Each known raag's best match, best first; empty without notes. notes are
// the held pitches (HeldPitches), passing the notes to follow the melody's
// movement by (PassingNotes), and learning what's been learned from labeled
// recordings, if any.
std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing = {},
                                    const RaagLearning* learning = nullptr);

// The matches for a known raag from each candidate Sa, best first; empty
// without notes.
std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag,
                                 std::span<const SteadyNote> passing = {}, const RaagLearning* learning = nullptr);

}  // namespace raagasense
