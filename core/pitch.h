// Fundamental frequency estimation with the YIN algorithm (de Cheveigné and
// Kawahara, 2002), for one frame or a stream of samples.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace raagasense {

struct PitchOptions {
  double sample_rate = 48000;
  // Samples per analysis frame; the lowest detectable pitch needs at least
  // two periods in a frame.
  int frame_size = 2048;
  // Samples between the starts of consecutive frames.
  int hop_size = 512;
  // The search range, in Hz. Hindustani vocals stay well within it.
  double min_frequency = 70;
  double max_frequency = 1100;
  // YIN's absolute threshold on the cumulative mean normalized difference.
  // Higher than the paper's 0.1 to 0.15, so that a voice over accompaniment
  // still counts as voiced.
  double threshold = 0.25;
  // Frames quieter than this RMS level are unvoiced.
  double silence_rms = 1e-3;
};

struct PitchFrame {
  // Seconds from the start of the stream to the frame's center.
  double time = 0;
  // Hz; 0 when the frame is unvoiced.
  double frequency = 0;
  // 1 minus YIN's normalized difference at the chosen lag: near 1 for a
  // clean periodic signal.
  double confidence = 0;
  bool voiced = false;
};

// Estimates the pitch of one frame of options.frame_size samples. The
// returned frame's time is 0.
PitchFrame EstimatePitch(std::span<const float> frame, const PitchOptions& options);

// Estimates pitch over a stream of samples pushed in arbitrary chunks.
class PitchTracker {
 public:
  explicit PitchTracker(const PitchOptions& options);

  // Appends samples, and appends a frame to *out for every hop completed.
  void Push(std::span<const float> samples, std::vector<PitchFrame>* out);

  const PitchOptions& options() const { return options_; }

 private:
  PitchOptions options_;
  std::vector<float> pending_;
  // Stream index of pending_[0].
  int64_t start_ = 0;
};

}  // namespace raagasense
