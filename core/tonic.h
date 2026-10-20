// Estimates the tonic (Sa) from the steady notes of a performance. Sa and Pa
// are the svaras performers hold most, and they're a fifth apart, so the best
// candidate is the pitch that, taken as Sa, puts the most held time on Sa and
// Pa. Raagas that omit Pa (Malkauns, Marwa) will need more than this.
#pragma once

#include <span>
#include <vector>

#include "core/steady.h"

namespace raagasense {

struct TonicCandidate {
  // Hz, in the octave that puts the median held pitch between Sa and taar
  // Sa: the octave the performer treats as madhya.
  double frequency = 0;
  // The fraction of the held time on this Sa or its Pa, in any octave.
  double score = 0;
};

// Candidates for Sa, best first; empty without notes.
std::vector<TonicCandidate> EstimateTonic(std::span<const SteadyNote> notes);

// Candidates for Sa when the raag's svaras (Svara values) are known, best
// first: the same candidates as EstimateTonic's, but each one's score is the
// fraction of the held time on the raag's svaras taken from that Sa. Ties
// keep EstimateTonic's order.
std::vector<TonicCandidate> EstimateTonicForSvaras(std::span<const SteadyNote> notes, std::span<const int> svaras);

}  // namespace raagasense
