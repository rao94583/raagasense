// Names the raag from the steady notes of a performance, finding Sa along the
// way: every raag the app knows is tried from every candidate Sa, and scored
// by how likely the held time on each svara is under a simple model of the
// raag: its svaras likely, its vadi more so and its occasional svaras less,
// every other svara rare, and a penalty for each svara it can't do without
// that's missing and for each svara it doesn't use that's there. Given the
// passing notes too, it also weighs the svaras passed through going up and
// going down against those the raag leaves out each way. Raags with the same
// svaras are otherwise told apart only by their vadi.
#pragma once

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

// Each known raag's best match, best first; empty without notes. notes are
// the held pitches (HeldPitches), and passing the notes to follow the
// melody's movement by (PassingNotes), if any.
std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing = {});

// The matches for a known raag from each candidate Sa, best first; empty
// without notes.
std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag,
                                 std::span<const SteadyNote> passing = {});

}  // namespace raagasense
