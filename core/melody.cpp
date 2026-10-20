#include "core/melody.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace raagasense {

namespace {

// Salience is gathered in bins this many cents wide...
constexpr double kBinCents = 10;
// ...and a peak's vote spreads this far either side of its fundamental.
constexpr double kVoteCents = 50;
// Spectral peaks outside this range don't vote.
constexpr double kMinPeakHz = 55;
constexpr double kMaxPeakHz = 5000;

// An in-place radix-2 FFT; data.size() must be a power of two.
void Fft(std::vector<std::complex<double>>& data) {
  const size_t n = data.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(data[i], data[j]);
  }
  for (size_t length = 2; length <= n; length <<= 1) {
    const std::complex<double> step = std::polar(1.0, -2 * std::numbers::pi / length);
    for (size_t i = 0; i < n; i += length) {
      std::complex<double> w = 1;
      for (size_t k = 0; k < length / 2; ++k) {
        const std::complex<double> a = data[i + k], b = data[i + k + length / 2] * w;
        data[i + k] = a + b;
        data[i + k + length / 2] = a - b;
        w *= step;
      }
    }
  }
}

}  // namespace

MelodyTracker::MelodyTracker(const MelodyOptions& options)
    : options_(options),
      hop_size_(options.hop_size > 0 ? options.hop_size : static_cast<int>(options.sample_rate / 100)),
      window_(options.frame_size) {
  for (int i = 0; i < options.frame_size; ++i) {
    window_[i] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / options.frame_size);
  }
}

std::pair<double, double> MelodyTracker::Analyze(size_t offset) {
  const int n = options_.frame_size;
  const double rate = options_.sample_rate;
  std::vector<std::complex<double>> spectrum(n);
  for (int i = 0; i < n; ++i) spectrum[i] = pending_[offset + i] * window_[i];
  Fft(spectrum);
  std::vector<double> magnitude(n / 2);
  for (int k = 0; k < n / 2; ++k) magnitude[k] = std::abs(spectrum[k]);
  const double loudest = *std::max_element(magnitude.begin(), magnitude.end());
  if (loudest <= 0) return {0, 0};
  const double floor = loudest * std::pow(10, options_.peak_floor_db / 20);

  const int bins = static_cast<int>(1200 * std::log2(options_.max_frequency / options_.min_frequency) / kBinCents) + 1;
  std::vector<double> salience(bins, 0);
  for (int k = 2; k < n / 2 - 1; ++k) {
    const double hz = k * rate / n;
    if (hz < kMinPeakHz || hz > kMaxPeakHz) continue;
    if (magnitude[k] < floor || magnitude[k] <= magnitude[k - 1] || magnitude[k] < magnitude[k + 1]) continue;
    // The peak's frequency, by a parabola through the log magnitudes.
    const double a = std::log(magnitude[k - 1] + 1e-12), b = std::log(magnitude[k]), c = std::log(magnitude[k + 1] + 1e-12);
    const double curvature = a - 2 * b + c;
    const double peak = (k + (curvature < 0 ? 0.5 * (a - c) / curvature : 0)) * rate / n;
    double vote = magnitude[k];
    for (int h = 1; h <= options_.harmonics; ++h, vote *= options_.harmonic_weight) {
      const double f0 = peak / h;
      if (f0 < options_.min_frequency) break;
      if (f0 > options_.max_frequency) continue;
      const double center = 1200 * std::log2(f0 / options_.min_frequency) / kBinCents;
      const int reach = static_cast<int>(kVoteCents / kBinCents);
      for (int bin = static_cast<int>(center) - reach; bin <= static_cast<int>(center) + reach + 1; ++bin) {
        const double distance = std::abs(bin - center) * kBinCents;
        if (bin < 0 || bin >= bins || distance > kVoteCents) continue;
        const double shape = std::cos(std::numbers::pi * distance / (2 * kVoteCents));
        salience[bin] += vote * shape * shape;
      }
    }
  }

  const int best = static_cast<int>(std::max_element(salience.begin(), salience.end()) - salience.begin());
  if (salience[best] <= 0) return {0, 0};
  double refined = best;
  if (best > 0 && best < bins - 1) {
    const double a = salience[best - 1], b = salience[best], c = salience[best + 1];
    const double curvature = a - 2 * b + c;
    if (curvature < 0) refined += 0.5 * (a - c) / curvature;
  }
  return {options_.min_frequency * std::exp2(refined * kBinCents / 1200), salience[best]};
}

void MelodyTracker::Push(std::span<const float> samples, std::vector<PitchFrame>* out) {
  pending_.insert(pending_.end(), samples.begin(), samples.end());
  const size_t frame_size = options_.frame_size;
  const size_t keep = static_cast<size_t>(options_.reference_seconds * options_.sample_rate / hop_size_);
  size_t offset = 0;
  while (pending_.size() - offset >= frame_size) {
    const auto [frequency, salience] = Analyze(offset);
    recent_.push_back(salience);
    if (recent_.size() > keep) recent_.pop_front();
    std::vector<double> sorted(recent_.begin(), recent_.end());
    const auto percentile = sorted.begin() + static_cast<size_t>(0.95 * (sorted.size() - 1));
    std::nth_element(sorted.begin(), percentile, sorted.end());
    const double threshold = options_.voicing * *percentile;

    PitchFrame frame;
    frame.time = (start_ + offset + frame_size / 2.0) / options_.sample_rate;
    frame.confidence = threshold > 0 ? std::min(1.0, 0.5 * salience / threshold) : 0;
    frame.voiced = threshold > 0 && salience >= threshold;
    frame.frequency = frame.voiced ? frequency : 0;
    out->push_back(frame);
    offset += hop_size_;
  }
  pending_.erase(pending_.begin(), pending_.begin() + offset);
  start_ += offset;
}

}  // namespace raagasense
