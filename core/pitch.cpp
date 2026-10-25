#include "core/pitch.h"

#include <algorithm>
#include <cmath>

namespace raagasense {

PitchFrame EstimatePitch(std::span<const float> frame, const PitchOptions& options) {
  PitchFrame result;
  const int n = static_cast<int>(frame.size());
  const int max_lag = std::min(static_cast<int>(std::ceil(options.sample_rate / options.min_frequency)), n / 2);
  const int min_lag = std::max(2, static_cast<int>(options.sample_rate / options.max_frequency));
  if (max_lag <= min_lag + 1) return result;

  double energy = 0;
  for (float x : frame) energy += double(x) * x;
  if (std::sqrt(energy / n) < options.silence_rms) return result;

  // The difference function over a window that every lag can reach, and its
  // cumulative mean normalized form (YIN steps 2 and 3).
  const int window = n - max_lag;
  std::vector<double> normalized(max_lag + 1, 1.0);
  double running = 0;
  for (int lag = 1; lag <= max_lag; ++lag) {
    double sum = 0;
    for (int j = 0; j < window; ++j) {
      const double delta = double(frame[j]) - frame[j + lag];
      sum += delta * delta;
    }
    running += sum;
    normalized[lag] = running > 0 ? sum * lag / running : 1.0;
  }

  // The first dip below the threshold, followed to its minimum (step 4); with
  // none, the global minimum, reported as unvoiced.
  int lag = -1;
  for (int t = min_lag; t < max_lag; ++t) {
    if (normalized[t] < options.threshold) {
      while (t + 1 < max_lag && normalized[t + 1] < normalized[t]) ++t;
      lag = t;
      break;
    }
  }
  const bool voiced = lag >= 0;
  if (!voiced) {
    lag = static_cast<int>(std::min_element(normalized.begin() + min_lag, normalized.begin() + max_lag) -
                           normalized.begin());
  }

  // Parabolic interpolation around the chosen lag (step 5).
  double refined = lag;
  const double a = normalized[lag - 1], b = normalized[lag], c = normalized[lag + 1];
  const double curvature = a - 2 * b + c;
  if (curvature > 0) refined += 0.5 * (a - c) / curvature;

  result.confidence = std::clamp(1.0 - b, 0.0, 1.0);
  result.voiced = voiced;
  result.frequency = voiced ? options.sample_rate / refined : 0;
  return result;
}

PitchTracker::PitchTracker(const PitchOptions& options) : options_(options) {}

void PitchTracker::Push(std::span<const float> samples, std::vector<PitchFrame>* out) {
  pending_.insert(pending_.end(), samples.begin(), samples.end());
  const size_t frame_size = options_.frame_size;
  size_t offset = 0;
  while (pending_.size() - offset >= frame_size) {
    PitchFrame frame = EstimatePitch(std::span(pending_).subspan(offset, frame_size), options_);
    frame.time = (start_ + offset + frame_size / 2.0) / options_.sample_rate;
    out->push_back(frame);
    offset += options_.hop_size;
  }
  pending_.erase(pending_.begin(), pending_.begin() + offset);
  start_ += offset;
}

}  // namespace raagasense
