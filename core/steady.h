// Finds the steady notes in a pitch track: the stretches where a performer
// holds a pitch, as opposed to moving between pitches in a meend or touching
// one in passing as a kan. Doesn't need the tonic, so tonic detection can use
// it. Frames an octave or two away from their neighbors, which accompaniment
// often causes, are moved back into line first.
#pragma once

#include <span>
#include <vector>

#include "core/pitch.h"

namespace raagasense {

struct SteadyOptions {
  // Frames below this confidence count as unvoiced.
  double min_confidence = 0.5;
  // A note's pitch stays within a band this many cents wide...
  double max_range_cents = 60;
  // ...lasts at least this many seconds...
  double min_duration = 0.08;
  // ...and its pitch's trend (the slope of a straight-line fit) is at most
  // this many cents a second, which rules out slow glides that stay within
  // the band for a while.
  double max_slope = 60;
  // Voiced frames further apart than this many seconds are never in the same
  // note, so brief dropouts in the pitch track don't end one.
  double max_gap = 0.06;
  // Notes this close in time and pitch are one note, broken by a wobble or
  // a dropout.
  double merge_gap = 0.1;
  double merge_cents = 25;
};

struct SteadyNote {
  // Seconds; the times of the note's first and last frames.
  double start = 0;
  double end = 0;
  // The median pitch, in Hz.
  double frequency = 0;
};

// The steady notes in frames, which must be in time order, earliest first.
std::vector<SteadyNote> FindSteadyNotes(std::span<const PitchFrame> frames, const SteadyOptions& options = {});

// Every voiced frame as a short note, lasting its share of the time times how
// steady the pitch is around it: nearly all of it on a held pitch, little in
// a glide. Unlike FindSteadyNotes it has no minimum length, so it still finds
// the notes in fast music, such as taans or film songs.
std::vector<SteadyNote> SteadinessWeighted(std::span<const PitchFrame> frames, const SteadyOptions& options = {});

// The pitches to judge Sa and the raag by: the steady notes, or, when they
// cover less than a fifth of the voiced time, SteadinessWeighted's.
std::vector<SteadyNote> HeldPitches(std::span<const PitchFrame> frames, const SteadyOptions& options = {});

}  // namespace raagasense
