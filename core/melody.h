// Tracks the predominant pitch, the melody, in music with accompaniment: a
// harmonic-summation pitch salience in the manner of Salamon and Gómez (2012),
// simplified. Each spectral peak votes for every fundamental it could be a
// harmonic of, and the most-voted fundamental wins. Unlike YIN it doesn't need
// the frame to be periodic, so a tanpura or harmonium under the voice doesn't
// stop it finding the voice.
#pragma once

#include <cstdint>
#include <deque>
#include <span>
#include <vector>

#include "core/pitch.h"

namespace raagasense {

struct MelodyOptions {
  double sample_rate = 48000;
  // Samples per analysis frame, a power of two. 4096 at 48 kHz resolves the
  // harmonics of a low male voice.
  int frame_size = 4096;
  // Samples between frames; 0 for 10 ms.
  int hop_size = 0;
  double min_frequency = 70;
  double max_frequency = 1100;
  // How many harmonics each peak can be, and how much less each higher
  // harmonic's vote counts than the one below it.
  int harmonics = 20;
  double harmonic_weight = 0.8;
  // Spectral peaks this far below the frame's loudest don't vote.
  double peak_floor_db = -40;
  // A frame is voiced when its salience is at least this fraction of the
  // reference: the 95th percentile of the salience over the last
  // reference_seconds.
  double voicing = 0.2;
  double reference_seconds = 10;
};

// Estimates the melody's pitch over a stream of samples pushed in arbitrary
// chunks. A frame's confidence is 0.5 at the voicing threshold and reaches 1
// at twice it, so that frames below 0.5 can be treated as unvoiced, as with
// YIN's.
class MelodyTracker {
 public:
  explicit MelodyTracker(const MelodyOptions& options);

  // Appends samples, and appends a frame to *out for every hop completed.
  void Push(std::span<const float> samples, std::vector<PitchFrame>* out);

  const MelodyOptions& options() const { return options_; }

 private:
  // The best fundamental's frequency and salience for the frame at pending_[offset].
  std::pair<double, double> Analyze(size_t offset);

  MelodyOptions options_;
  int hop_size_;
  std::vector<double> window_;
  std::vector<float> pending_;
  // Stream index of pending_[0].
  int64_t start_ = 0;
  // Recent frames' salience, for the voicing reference.
  std::deque<double> recent_;
};

}  // namespace raagasense
