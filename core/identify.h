// Names the raag from the steady notes of a performance, finding Sa along the
// way: every raag the app knows is tried from every candidate Sa, and scored
// by how likely the held time on each svara is under a simple model of the
// raag, its svaras equally likely and every other svara rare. A first
// version: it can't yet tell apart raags with the same svaras, which need
// emphasis and phrasing.
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

}  // namespace raagasense
