// Octave-aware matching of characteristic melodic phrases.
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "core/steady.h"

namespace raagasense {

// Signed semitones from madhya Sa: .N = -1, S = 0, S' = 12.
// Preserves repeated tokens; returns empty for invalid notation.
std::vector<int> ParsePhrase(std::string_view notation);

struct PhraseEvent {
  int semitone;
  double start;
  double end;
  int segment;
};

// Each input note is an event; note extraction is responsible for merging
// fragments of the same held note. Gaps over 0.6 s start a new segment.
std::vector<PhraseEvent> PhraseEvents(std::span<const SteadyNote> notes, double sa);

// Best complete occurrence, in [0,1]. Allows an overall octave shift, but
// preserves the contour and repeats within the phrase. Up to two extra
// events between required notes cost exp(-0.5) each. Matches cannot cross
// segments or span more than 20 s. No match is 0; a contiguous match is 1.
double MatchPhrase(std::span<const int> phrase, std::span<const PhraseEvent> events);

// Deterministically permutes pitches within each segment, preserving times
// and the segment's pitch histogram. Used for an excerpt-specific null
// model; variant selects an independent reproducible permutation.
std::vector<PhraseEvent> ShuffledPhraseEvents(std::span<const PhraseEvent> events, unsigned variant);

}  // namespace raagasense
