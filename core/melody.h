// Tracks the predominant pitch, the melody, in music with accompaniment: a
// harmonic-summation pitch salience in the manner of Salamon and Gómez (2012),
// simplified. Each spectral peak votes for every fundamental it could be a
// harmonic of, and the most-voted fundamental wins. Unlike YIN it doesn't need
// the frame to be periodic, so a tanpura or harmonium under the voice doesn't
// stop it finding the voice. Before voting, percussion such as tabla is
// suppressed by harmonic/percussive median filtering (Fitzgerald 2010): a
// spectral bin counts in proportion to how much steadier it is over time than
// across frequency.
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
  // The length, in frames and in bins, of the medians over time and over
  // frequency that separate sustained sounds from percussive ones; 0 or 1
  // turns percussion suppression off. Frames come out half this many hops
  // late.
  int percussion_frames = 17;
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

  // Appends samples, and appends a frame to *out for every hop that's ready:
  // all but the last percussion_frames / 2 completed.
  void Push(std::span<const float> samples, std::vector<PitchFrame>* out);
  // Appends the frames still held back, at the end of the stream.
  void Finish(std::vector<PitchFrame>* out);

  const MelodyOptions& options() const { return options_; }

 private:
  struct Spectrum {
    double time;
    std::vector<double> magnitude;
  };

  // The magnitude spectrum of the frame at pending_[offset], up to the
  // highest bin that can vote.
  std::vector<double> Magnitude(size_t offset);
  // The best fundamental's frequency and salience in a magnitude spectrum.
  std::pair<double, double> Salience(const std::vector<double>& magnitude) const;
  // Emits the frames whose later neighbors have arrived, or all of them.
  void Emit(bool all, std::vector<PitchFrame>* out);

  MelodyOptions options_;
  int hop_size_;
  std::vector<double> window_;
  std::vector<float> pending_;
  // Stream index of pending_[0].
  int64_t start_ = 0;
  // Spectra awaiting emission, and the ones before them that their medians
  // still need; next_ indexes the next to emit.
  std::deque<Spectrum> spectra_;
  size_t next_ = 0;
  // Recent frames' salience, for the voicing reference.
  std::deque<double> recent_;
};

}  // namespace raagasense
