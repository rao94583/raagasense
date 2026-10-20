#include "core/steady.h"

#include <algorithm>
#include <cmath>

namespace raagasense {

namespace {

// Pitches are worked on in cents from this arbitrary reference.
constexpr double kReference = 440;

struct Point {
  double time;
  double cents;
};

double Median(std::span<const Point> points) {
  std::vector<double> cents;
  for (const Point& point : points) cents.push_back(point.cents);
  const auto middle = cents.begin() + cents.size() / 2;
  std::nth_element(cents.begin(), middle, cents.end());
  return *middle;
}

// The slope of the least-squares line through the points, in cents a second.
double Slope(std::span<const Point> points) {
  double mean_time = 0, mean_cents = 0;
  for (const Point& point : points) {
    mean_time += point.time;
    mean_cents += point.cents;
  }
  mean_time /= points.size();
  mean_cents /= points.size();
  double covariance = 0, variance = 0;
  for (const Point& point : points) {
    covariance += (point.time - mean_time) * (point.cents - mean_cents);
    variance += (point.time - mean_time) * (point.time - mean_time);
  }
  return variance > 0 ? covariance / variance : 0;
}

// Finds the notes in a run of voiced points with no long gaps.
void FindInRun(std::span<const Point> run, const SteadyOptions& options, std::vector<SteadyNote>* notes) {
  size_t i = 0;
  while (i < run.size()) {
    // The longest stretch from i that stays within the band.
    double low = run[i].cents, high = run[i].cents;
    size_t j = i + 1;
    while (j < run.size() && std::max(high, run[j].cents) - std::min(low, run[j].cents) <= options.max_range_cents) {
      low = std::min(low, run[j].cents);
      high = std::max(high, run[j].cents);
      ++j;
    }
    // The stretch can begin or end partway into a meend; trim the ends back
    // to pitches near the median before judging it.
    std::span<const Point> note = run.subspan(i, j - i);
    const double median = Median(note);
    const double trim = options.max_range_cents / 4;
    while (!note.empty() && std::abs(note.front().cents - median) > trim) note = note.subspan(1);
    while (!note.empty() && std::abs(note.back().cents - median) > trim) note = note.first(note.size() - 1);

    if (!note.empty() && note.back().time - note.front().time >= options.min_duration &&
        std::abs(Slope(note)) <= options.max_slope) {
      notes->push_back({note.front().time, note.back().time, kReference * std::exp2(Median(note) / 1200)});
      i = j;
    } else {
      ++i;
    }
  }
}

}  // namespace

std::vector<SteadyNote> FindSteadyNotes(std::span<const PitchFrame> frames, const SteadyOptions& options) {
  std::vector<SteadyNote> notes;
  std::vector<Point> run;
  for (const PitchFrame& frame : frames) {
    if (!frame.voiced || frame.confidence < options.min_confidence || frame.frequency <= 0) continue;
    if (!run.empty() && frame.time - run.back().time > options.max_gap) {
      FindInRun(run, options, &notes);
      run.clear();
    }
    run.push_back({frame.time, 1200 * std::log2(frame.frequency / kReference)});
  }
  FindInRun(run, options, &notes);
  return notes;
}

}  // namespace raagasense
