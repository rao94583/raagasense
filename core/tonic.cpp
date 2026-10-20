#include "core/tonic.h"

#include <algorithm>
#include <cmath>

namespace raagasense {

namespace {

// Pitches are worked on in cents from this arbitrary reference.
constexpr double kReference = 440;
constexpr int kBinCents = 10;
constexpr int kBins = 1200 / kBinCents;
// The width of the smoothing applied to the held-time histogram.
constexpr double kSmoothingCents = 15;
// Peaks smaller than this fraction of the largest aren't candidates.
constexpr double kMinPeak = 0.05;
// Held notes this close to a svara count as that svara.
constexpr double kSvaraCents = 50;
constexpr double kPaCents = 700;

double Cents(double frequency) { return 1200 * std::log2(frequency / kReference); }

double Fold(double cents) { return cents - 1200 * std::floor(cents / 1200); }

// The distance between two pitches with their octaves ignored, in [0, 600].
double FoldedDistance(double a, double b) {
  const double d = Fold(a - b);
  return std::min(d, 1200 - d);
}

double Duration(const SteadyNote& note) { return note.end - note.start; }

}  // namespace

std::vector<TonicCandidate> EstimateTonic(std::span<const SteadyNote> notes) {
  std::vector<TonicCandidate> candidates;
  double total = 0;
  for (const SteadyNote& note : notes) total += Duration(note);
  if (total <= 0) return candidates;

  // Held time by pitch with octaves folded together, smoothed.
  std::vector<double> histogram(kBins, 0);
  for (const SteadyNote& note : notes) {
    const double cents = Cents(note.frequency);
    for (int bin = 0; bin < kBins; ++bin) {
      const double d = FoldedDistance(cents, bin * kBinCents) / kSmoothingCents;
      histogram[bin] += Duration(note) * std::exp(-0.5 * d * d);
    }
  }
  const double highest = *std::max_element(histogram.begin(), histogram.end());

  // The median held pitch, which fixes each candidate's octave.
  std::vector<SteadyNote> by_pitch(notes.begin(), notes.end());
  std::sort(by_pitch.begin(), by_pitch.end(),
            [](const SteadyNote& a, const SteadyNote& b) { return a.frequency < b.frequency; });
  double median = 0, seen = 0;
  for (const SteadyNote& note : by_pitch) {
    seen += Duration(note);
    if (seen >= total / 2) {
      median = Cents(note.frequency);
      break;
    }
  }

  for (int bin = 0; bin < kBins; ++bin) {
    const double here = histogram[bin];
    if (here < kMinPeak * highest || here < histogram[(bin + kBins - 1) % kBins] ||
        here <= histogram[(bin + 1) % kBins]) {
      continue;
    }
    // The peak's pitch: the held pitches near it, averaged by duration.
    const double center = bin * kBinCents;
    double weight = 0, offset = 0;
    for (const SteadyNote& note : notes) {
      const double d = Fold(Cents(note.frequency) - center + 600) - 600;
      if (std::abs(d) > kSvaraCents) continue;
      weight += Duration(note);
      offset += Duration(note) * d;
    }
    const double sa = weight > 0 ? center + offset / weight : center;

    double on_sa_or_pa = 0;
    for (const SteadyNote& note : notes) {
      const double cents = Cents(note.frequency);
      if (FoldedDistance(cents, sa) <= kSvaraCents || FoldedDistance(cents, sa + kPaCents) <= kSvaraCents) {
        on_sa_or_pa += Duration(note);
      }
    }
    const double octave_sa = median - Fold(median - sa);
    candidates.push_back({kReference * std::exp2(octave_sa / 1200), on_sa_or_pa / total});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const TonicCandidate& a, const TonicCandidate& b) { return a.score > b.score; });
  return candidates;
}

}  // namespace raagasense
