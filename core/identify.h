// Names the raag from the steady notes of a performance, finding Sa along the
// way: every raag the app knows is tried from every candidate Sa, and scored
// by how likely the held time on each svara is under a simple model of the
// raag: its svaras likely, its vadi more so and its occasional svaras less,
// every other svara rare, and a penalty for each svara it can't do without
// that's missing and for each svara it doesn't use that's there. It knows nothing yet of phrasing, so raags with the same
// svaras are told apart only by their vadi.
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

// Each known raag's best match, best first; empty without notes.
std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes);

// The matches for a known raag from each candidate Sa, best first; empty
// without notes.
std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag);

}  // namespace raagasense
