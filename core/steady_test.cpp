#include "core/steady.h"

#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

#include "core/svara.h"
#include "gtest/gtest.h"

namespace raagasense {
namespace {

constexpr double kTonic = 138.59;
constexpr double kHop = 512 / 48000.0;

// Appends voiced frames for the given seconds, with the pitch in cents from
// Sa as a function of the seconds since this part began.
void Sing(std::vector<PitchFrame>* frames, double seconds, std::function<double(double)> cents) {
  const double start = frames->empty() ? 0 : frames->back().time + kHop;
  for (double t = 0; t < seconds; t += kHop) {
    frames->push_back({start + t, kTonic * std::exp2(cents(t) / 1200), 0.9, true});
  }
}

void Hold(std::vector<PitchFrame>* frames, double seconds, double cents) {
  Sing(frames, seconds, [cents](double) { return cents; });
}

void Glide(std::vector<PitchFrame>* frames, double seconds, double from, double to) {
  Sing(frames, seconds, [=](double t) { return from + (to - from) * t / seconds; });
}

void Rest(std::vector<PitchFrame>* frames, double seconds) {
  const double start = frames->empty() ? 0 : frames->back().time + kHop;
  for (double t = 0; t < seconds; t += kHop) frames->push_back({start + t, 0, 0.1, false});
}

double Cents(const SteadyNote& note) { return CentsFromSa(note.frequency, kTonic); }

TEST(FindSteadyNotes, HeldNote) {
  std::vector<PitchFrame> frames;
  Hold(&frames, 1.0, 400);
  const std::vector<SteadyNote> notes = FindSteadyNotes(frames);
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_NEAR(Cents(notes[0]), 400, 0.5);
  EXPECT_NEAR(notes[0].start, 0, kHop);
  EXPECT_NEAR(notes[0].end, 1.0, 2 * kHop);
}

TEST(FindSteadyNotes, MeendBetweenNotesIsNotANote) {
  std::vector<PitchFrame> frames;
  Hold(&frames, 0.5, 0);
  Glide(&frames, 0.4, 0, 700);
  Hold(&frames, 0.5, 700);
  const std::vector<SteadyNote> notes = FindSteadyNotes(frames);
  ASSERT_EQ(notes.size(), 2u);
  EXPECT_NEAR(Cents(notes[0]), 0, 0.5);
  EXPECT_NEAR(Cents(notes[1]), 700, 0.5);
  // The notes end and begin within a few frames of the glide.
  EXPECT_NEAR(notes[0].end, 0.5, 0.05);
  EXPECT_NEAR(notes[1].start, 0.9, 0.05);
}

TEST(FindSteadyNotes, SlowGlideIsNotANote) {
  std::vector<PitchFrame> frames;
  Glide(&frames, 1.5, 400, 200);
  EXPECT_TRUE(FindSteadyNotes(frames).empty());
}

TEST(FindSteadyNotes, VibratoIsOneNoteAtItsCenter) {
  std::vector<PitchFrame> frames;
  Sing(&frames, 1.0, [](double t) { return 700 + 20 * std::sin(2 * std::numbers::pi * 5 * t); });
  const std::vector<SteadyNote> notes = FindSteadyNotes(frames);
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_NEAR(Cents(notes[0]), 700, 5);
}

TEST(FindSteadyNotes, KanIsNotANote) {
  std::vector<PitchFrame> frames;
  Hold(&frames, 0.08, 200);
  Hold(&frames, 0.6, 400);
  const std::vector<SteadyNote> notes = FindSteadyNotes(frames);
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_NEAR(Cents(notes[0]), 400, 0.5);
  EXPECT_NEAR(notes[0].start, 0.08, 2 * kHop);
}

TEST(FindSteadyNotes, ShortDropoutDoesNotSplitANote) {
  std::vector<PitchFrame> frames;
  Hold(&frames, 0.4, 1100);
  Rest(&frames, 0.03);
  Hold(&frames, 0.4, 1100);
  EXPECT_EQ(FindSteadyNotes(frames).size(), 1u);
}

TEST(FindSteadyNotes, RestSplitsANote) {
  std::vector<PitchFrame> frames;
  Hold(&frames, 0.4, 1100);
  Rest(&frames, 0.2);
  Hold(&frames, 0.4, 1100);
  EXPECT_EQ(FindSteadyNotes(frames).size(), 2u);
}

TEST(FindSteadyNotes, TooShortIsNotANote) {
  std::vector<PitchFrame> frames;
  Hold(&frames, 0.15, 500);
  EXPECT_TRUE(FindSteadyNotes(frames).empty());
}

TEST(FindSteadyNotes, IgnoresLowConfidenceFrames) {
  std::vector<PitchFrame> frames;
  Hold(&frames, 0.5, 900);
  for (PitchFrame& frame : frames) frame.confidence = 0.3;
  EXPECT_TRUE(FindSteadyNotes(frames).empty());
}

}  // namespace
}  // namespace raagasense
