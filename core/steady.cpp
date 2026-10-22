#include "core/steady.h"

#include <algorithm>
#include <cmath>

namespace raagasense {

namespace {

// Pitches are worked on in cents from this arbitrary reference.
constexpr double kReference = 440;
// A frame's pitch is checked against the median of the frames this many
// seconds either side of it.
constexpr double kOctaveCheckSeconds = 0.05;
// SteadinessWeighted measures a frame's slope over this many voiced frames
// either side of it, and weights it by exp(-(slope / kSteadySlope)^2), in
// cents a second. On the labeled recordings, 100 to 200 do about as well as
// steady notes; 400 is much worse, and no weighting at all fails.
constexpr int kSlopeReach = 3;
constexpr double kSteadySlope = 150;
// The shortest passing note, in seconds: about three frames.
constexpr double kPassingNoteSeconds = 0.03;
// HeldPitches turns to SteadinessWeighted when the steady notes cover less
// than this share of the voiced time. None of the labeled recordings comes
// under it, so it's untried; it's for fast music.
constexpr double kMinSteadyShare = 0.2;

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

// Moves each point by whole octaves to the octave nearest its neighbors'
// median, when it's more than half an octave away from it.
void FixOctaveErrors(std::vector<Point>* points) {
  std::vector<Point> fixed = *points;
  size_t first = 0, last = 0;
  for (size_t i = 0; i < points->size(); ++i) {
    const double time = (*points)[i].time;
    while ((*points)[first].time < time - kOctaveCheckSeconds) ++first;
    while (last < points->size() && (*points)[last].time <= time + kOctaveCheckSeconds) ++last;
    const double median = Median(std::span(*points).subspan(first, last - first));
    const double offset = fixed[i].cents - median;
    if (std::abs(offset) > 600) fixed[i].cents -= 1200 * std::round(offset / 1200);
  }
  *points = std::move(fixed);
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

// The voiced frames as points, with octave errors fixed.
std::vector<Point> VoicedPoints(std::span<const PitchFrame> frames, const SteadyOptions& options) {
  std::vector<Point> points;
  for (const PitchFrame& frame : frames) {
    if (!frame.voiced || frame.confidence < options.min_confidence || frame.frequency <= 0) continue;
    points.push_back({frame.time, 1200 * std::log2(frame.frequency / kReference)});
  }
  FixOctaveErrors(&points);
  return points;
}

}  // namespace

std::vector<SteadyNote> SteadinessWeighted(std::span<const PitchFrame> frames, const SteadyOptions& options) {
  const std::vector<Point> points = VoicedPoints(frames, options);
  std::vector<SteadyNote> notes;
  for (size_t i = 0; i < points.size(); ++i) {
    // The neighbors to measure the slope by, within this run of voiced frames.
    size_t first = i, last = i;
    while (first > 0 && i - first < kSlopeReach && points[first].time - points[first - 1].time <= options.max_gap) {
      --first;
    }
    while (last + 1 < points.size() && last - i < kSlopeReach &&
           points[last + 1].time - points[last].time <= options.max_gap) {
      ++last;
    }
    if (first == last) continue;
    const double slope = (points[last].cents - points[first].cents) / (points[last].time - points[first].time);
    // The frame's share of the time: the mean spacing of its neighbors.
    const double spacing = (points[last].time - points[first].time) / (last - first);
    const double steadiness = std::exp(-(slope / kSteadySlope) * (slope / kSteadySlope));
    notes.push_back({points[i].time, points[i].time + spacing * steadiness,
                     kReference * std::exp2(points[i].cents / 1200)});
  }
  return notes;
}

std::vector<SteadyNote> PassingNotes(std::span<const PitchFrame> frames, const SteadyOptions& options) {
  SteadyOptions passing = options;
  passing.min_duration = kPassingNoteSeconds;
  return FindSteadyNotes(frames, passing);
}

std::vector<SteadyNote> HeldPitches(std::span<const PitchFrame> frames, const SteadyOptions& options) {
  std::vector<SteadyNote> notes = FindSteadyNotes(frames, options);
  double held = 0, voiced = 0;
  for (const SteadyNote& note : notes) held += note.end - note.start;
  const std::vector<Point> points = VoicedPoints(frames, options);
  for (size_t i = 1; i < points.size(); ++i) {
    if (points[i].time - points[i - 1].time <= options.max_gap) voiced += points[i].time - points[i - 1].time;
  }
  if (held >= kMinSteadyShare * voiced) return notes;
  return SteadinessWeighted(frames, options);
}

std::vector<SteadyNote> FindSteadyNotes(std::span<const PitchFrame> frames, const SteadyOptions& options) {
  const std::vector<Point> points = VoicedPoints(frames, options);

  std::vector<SteadyNote> found;
  size_t start = 0;
  for (size_t i = 1; i <= points.size(); ++i) {
    if (i == points.size() || points[i].time - points[i - 1].time > options.max_gap) {
      FindInRun(std::span(points).subspan(start, i - start), options, &found);
      start = i;
    }
  }

  std::vector<SteadyNote> notes;
  for (const SteadyNote& note : found) {
    if (!notes.empty() && note.start - notes.back().end <= options.merge_gap &&
        std::abs(1200 * std::log2(note.frequency / notes.back().frequency)) <= options.merge_cents) {
      // The merged pitch is the two pitches' mean in cents, weighted by duration.
      SteadyNote& previous = notes.back();
      const double a = previous.end - previous.start, b = note.end - note.start;
      previous.frequency = std::exp2((a * std::log2(previous.frequency) + b * std::log2(note.frequency)) / (a + b));
      previous.end = note.end;
    } else {
      notes.push_back(note);
    }
  }
  return notes;
}

}  // namespace raagasense
