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
constexpr size_t kMaxCandidates = 8;
constexpr double kCandidateFloor = 0.15;
// Log-salience cost per semitone. This favors continuity without forbidding
// real leaps; sustained evidence can overcome any single transition cost.
constexpr double kContinuityCost = 0.4;
constexpr double kVoicingSwitchCost = 0.3;

double TransitionCost(double from, double to) {
  if (from <= 0 || to <= 0) return from == to ? 0 : kVoicingSwitchCost;
  return kContinuityCost * std::abs(12 * std::log2(to / from));
}

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

std::vector<double> MelodyTracker::Magnitude(size_t offset) {
  const int n = options_.frame_size;
  std::vector<std::complex<double>> spectrum(n);
  for (int i = 0; i < n; ++i) spectrum[i] = pending_[offset + i] * window_[i];
  Fft(spectrum);
  const int bins = std::min(n / 2, static_cast<int>(kMaxPeakHz * n / options_.sample_rate) + 2);
  std::vector<double> magnitude(bins);
  for (int k = 0; k < bins; ++k) magnitude[k] = std::abs(spectrum[k]);
  return magnitude;
}

std::vector<MelodyTracker::Candidate> MelodyTracker::Candidates(const std::vector<double>& magnitude) const {
  const int n = options_.frame_size;
  const double rate = options_.sample_rate;
  const double loudest = *std::max_element(magnitude.begin(), magnitude.end());
  if (loudest <= 0) return {};
  const double floor = loudest * std::pow(10, options_.peak_floor_db / 20);

  const int bins = static_cast<int>(1200 * std::log2(options_.max_frequency / options_.min_frequency) / kBinCents) + 1;
  std::vector<double> salience(bins, 0);
  for (int k = 2; k < static_cast<int>(magnitude.size()) - 1; ++k) {
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

  const double highest = *std::max_element(salience.begin(), salience.end());
  if (highest <= 0) return {};
  std::vector<Candidate> candidates;
  for (int bin = 0; bin < bins; ++bin) {
    if (salience[bin] < kCandidateFloor * highest || (bin > 0 && salience[bin] <= salience[bin - 1]) ||
        (bin + 1 < bins && salience[bin] < salience[bin + 1])) continue;
    double refined = bin;
    if (bin > 0 && bin + 1 < bins) {
      const double a = salience[bin - 1], b = salience[bin], c = salience[bin + 1];
      const double curvature = a - 2 * b + c;
      if (curvature < 0) refined += 0.5 * (a - c) / curvature;
    }
    candidates.push_back({options_.min_frequency * std::exp2(refined * kBinCents / 1200), salience[bin]});
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.salience != b.salience ? a.salience > b.salience : a.frequency < b.frequency;
  });
  if (candidates.size() > kMaxCandidates) candidates.resize(kMaxCandidates);
  return candidates;
}

void MelodyTracker::Select(bool all, std::vector<PitchFrame>* out) {
  const size_t lookahead = static_cast<size_t>(std::max(0, options_.tracking_frames));
  while (!observations_.empty() && (all || observations_.size() > lookahead)) {
    const auto& oldest = observations_.front();
    size_t chosen = 0;
    if (lookahead == 0) {
      chosen = oldest.candidates.empty() ? 0 : 1;
    } else {
      // Viterbi over a small fixed-lag window, anchored to the last committed
      // state. State 0 is unvoiced; the other states are salience peaks.
      std::vector<std::vector<size_t>> back(observations_.size());
      std::vector<double> scores;
      for (size_t t = 0; t < observations_.size(); ++t) {
        const auto& here = observations_[t];
        const auto frequency = [&](size_t state) { return state == 0 ? 0.0 : here.candidates[state - 1].frequency; };
        std::vector<double> next(here.candidates.size() + 1);
        back[t].resize(next.size());
        for (size_t state = 0; state < next.size(); ++state) {
          double best = -TransitionCost(previous_frequency_, frequency(state));
          if (t > 0) {
            best = -INFINITY;
            const auto& prior = observations_[t - 1];
            for (size_t from = 0; from < scores.size(); ++from) {
              const double hz = from == 0 ? 0.0 : prior.candidates[from - 1].frequency;
              const double value = scores[from] - TransitionCost(hz, frequency(state));
              if (value > best) {
                best = value;
                back[t][state] = from;
              }
            }
          }
          const double emission = state == 0 || here.threshold <= 0
                                      ? 0 : std::log(here.candidates[state - 1].salience / here.threshold);
          next[state] = best + emission;
        }
        scores = std::move(next);
      }
      chosen = static_cast<size_t>(std::max_element(scores.begin(), scores.end()) - scores.begin());
      for (size_t t = observations_.size() - 1; t > 0; --t) chosen = back[t][chosen];
    }
    PitchFrame frame;
    frame.time = oldest.time;
    if (chosen > 0 && oldest.threshold > 0) {
      const auto& candidate = oldest.candidates[chosen - 1];
      frame.confidence = std::min(1.0, 0.5 * candidate.salience / oldest.threshold);
      frame.voiced = candidate.salience >= oldest.threshold;
      frame.frequency = frame.voiced ? candidate.frequency : 0;
    }
    previous_frequency_ = frame.frequency;
    out->push_back(frame);
    observations_.pop_front();
  }
}

void MelodyTracker::Emit(bool all, std::vector<PitchFrame>* out) {
  const size_t half = options_.percussion_frames > 1 ? options_.percussion_frames / 2 : 0;
  const size_t keep = static_cast<size_t>(options_.reference_seconds * options_.sample_rate / hop_size_);
  std::vector<double> values;
  while (next_ < spectra_.size() && (all || spectra_.size() - 1 - next_ >= half)) {
    std::vector<double> magnitude = spectra_[next_].magnitude;
    if (half > 0) {
      // Weight each bin by a soft mask: steady over time (harmonic) against
      // steady across frequency (percussive), both as squared medians.
      const size_t first = next_ >= half ? next_ - half : 0, last = std::min(spectra_.size() - 1, next_ + half);
      const std::vector<double>& here = spectra_[next_].magnitude;
      const int bins = static_cast<int>(here.size());
      for (int k = 0; k < bins; ++k) {
        values.clear();
        for (size_t t = first; t <= last; ++t) values.push_back(spectra_[t].magnitude[k]);
        std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
        const double harmonic = values[values.size() / 2];
        values.clear();
        for (int j = std::max(0, k - static_cast<int>(half)); j <= std::min(bins - 1, k + static_cast<int>(half)); ++j) {
          values.push_back(here[j]);
        }
        std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
        const double percussive = values[values.size() / 2];
        const double h = harmonic * harmonic, p = percussive * percussive;
        magnitude[k] = h + p > 0 ? here[k] * h / (h + p) : 0;
      }
    }
    auto candidates = Candidates(magnitude);
    const double salience = candidates.empty() ? 0 : candidates[0].salience;

    recent_.push_back(salience);
    if (recent_.size() > keep) recent_.pop_front();
    std::vector<double> sorted(recent_.begin(), recent_.end());
    const auto percentile = sorted.begin() + static_cast<size_t>(0.95 * (sorted.size() - 1));
    std::nth_element(sorted.begin(), percentile, sorted.end());
    const double threshold = options_.voicing * *percentile;

    observations_.push_back({spectra_[next_].time, threshold, std::move(candidates)});
    Select(false, out);
    ++next_;
    while (next_ > half) {
      spectra_.pop_front();
      --next_;
    }
  }
  if (all) Select(true, out);
}

void MelodyTracker::Push(std::span<const float> samples, std::vector<PitchFrame>* out) {
  pending_.insert(pending_.end(), samples.begin(), samples.end());
  const size_t frame_size = options_.frame_size;
  size_t offset = 0;
  while (pending_.size() - offset >= frame_size) {
    spectra_.push_back({(start_ + offset + frame_size / 2.0) / options_.sample_rate, Magnitude(offset)});
    Emit(false, out);
    offset += hop_size_;
  }
  pending_.erase(pending_.begin(), pending_.begin() + offset);
  start_ += offset;
}

void MelodyTracker::Finish(std::vector<PitchFrame>* out) { Emit(true, out); }

}  // namespace raagasense
