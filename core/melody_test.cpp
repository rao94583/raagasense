#include "core/melody.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "core/svara.h"
#include "gtest/gtest.h"

namespace raagasense {
namespace {

constexpr double kRate = 48000;

// A tone with the given partial amplitudes (index 0 is the fundamental).
std::vector<float> Tone(double frequency, std::vector<double> partials, double seconds) {
  std::vector<float> out(static_cast<size_t>(seconds * kRate));
  for (size_t i = 0; i < out.size(); ++i) {
    double value = 0;
    for (size_t k = 0; k < partials.size(); ++k) {
      value += partials[k] * std::sin(2 * std::numbers::pi * frequency * (k + 1) * i / kRate);
    }
    out[i] = static_cast<float>(0.2 * value);
  }
  return out;
}

void Mix(std::vector<float>* into, const std::vector<float>& other, double gain) {
  for (size_t i = 0; i < into->size() && i < other.size(); ++i) (*into)[i] += static_cast<float>(gain * other[i]);
}

std::vector<PitchFrame> Track(const std::vector<float>& samples, int percussion_frames = 17) {
  MelodyOptions options;
  options.sample_rate = kRate;
  options.percussion_frames = percussion_frames;
  MelodyTracker tracker(options);
  std::vector<PitchFrame> frames;
  tracker.Push(samples, &frames);
  tracker.Finish(&frames);
  return frames;
}

// The share of frames that are voiced and within 20 cents of frequency.
double ShareOn(const std::vector<PitchFrame>& frames, double frequency) {
  int on = 0;
  for (const PitchFrame& frame : frames) {
    on += frame.voiced && frame.confidence >= 0.5 && std::abs(CentsFromSa(frame.frequency, frequency)) < 20;
  }
  return frames.empty() ? 0 : static_cast<double>(on) / frames.size();
}

const std::vector<double> kVoice = {1, 0.8, 0.6, 0.4, 0.3, 0.2};

TEST(MelodyTracker, HarmonicTones) {
  for (double frequency : {98.0, 146.83, 233.08, 440.0, 880.0}) {
    const std::vector<PitchFrame> frames = Track(Tone(frequency, kVoice, 0.5));
    ASSERT_FALSE(frames.empty());
    for (const PitchFrame& frame : frames) {
      EXPECT_TRUE(frame.voiced) << frequency;
      EXPECT_NEAR(CentsFromSa(frame.frequency, frequency), 0, 5) << frequency;
    }
  }
}

// A low voice's fundamental is often weaker than its harmonics.
TEST(MelodyTracker, WeakFundamental) {
  EXPECT_GT(ShareOn(Track(Tone(98, {0.2, 1, 0.8, 0.5, 0.3}, 0.5)), 98), 0.95);
}

// A harmonium-like instrument on another note, loud enough that YIN finds no
// pitch at all.
TEST(MelodyTracker, VoiceOverAccompaniment) {
  std::vector<float> mix = Tone(233.08, kVoice, 0.5);
  Mix(&mix, Tone(293.66, {1, 0.7, 0.5, 0.4, 0.3}, 0.5), 0.6);
  EXPECT_GT(ShareOn(Track(mix), 233.08), 0.95);
}

// A tanpura-like drone, rich in harmonics, on Sa an octave below the voice,
// which is on Ga.
TEST(MelodyTracker, VoiceOverDrone) {
  std::vector<float> mix = Tone(293.66, kVoice, 0.5);
  Mix(&mix, Tone(116.54, {1, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1}, 0.5), 0.5);
  EXPECT_GT(ShareOn(Track(mix), 293.66), 0.95);
}

// Tabla-like strokes: loud, short bursts of noise four times a second, with
// a ringing low drum under them.
std::vector<float> Strokes(double seconds) {
  std::vector<float> out(static_cast<size_t>(seconds * kRate));
  uint32_t state = 12345;
  for (size_t i = 0; i < out.size(); ++i) {
    const double since = std::fmod(i / kRate, 0.25);
    state = state * 1664525 + 1013904223;
    const double noise = static_cast<double>(state) / 4294967296.0 - 0.5;
    out[i] = static_cast<float>(std::exp(-since / 0.02) * (2.5 * noise + 1.5 * std::sin(2 * std::numbers::pi * 80 * since)));
  }
  return out;
}

TEST(MelodyTracker, VoiceOverTabla) {
  std::vector<float> mix = Tone(233.08, kVoice, 2);
  Mix(&mix, Strokes(2), 1);
  const double with = ShareOn(Track(mix), 233.08), without = ShareOn(Track(mix, 0), 233.08);
  EXPECT_GT(with, 0.9);
  EXPECT_GT(with, without + 0.05);
}

TEST(MelodyTracker, SilenceIsUnvoiced) {
  for (const PitchFrame& frame : Track(std::vector<float>(kRate / 2, 0.0f))) EXPECT_FALSE(frame.voiced);
}

// Voicing is relative to the recent level, so a faint tone after a loud one
// is unvoiced.
TEST(MelodyTracker, FaintAfterLoudIsUnvoiced) {
  std::vector<float> samples = Tone(233.08, kVoice, 1);
  std::vector<float> faint = Tone(233.08, kVoice, 1);
  for (float& x : faint) x *= 0.01f;
  samples.insert(samples.end(), faint.begin(), faint.end());
  const std::vector<PitchFrame> frames = Track(samples);
  for (const PitchFrame& frame : frames) {
    if (frame.time > 1.1) EXPECT_FALSE(frame.voiced) << frame.time;
  }
}

TEST(MelodyTracker, FramesAtEachHopRegardlessOfChunking) {
  const std::vector<float> tone = Tone(220, kVoice, 1);
  MelodyOptions options;
  options.sample_rate = kRate;
  MelodyTracker tracker(options);
  std::vector<PitchFrame> frames;
  for (size_t at = 0; at < tone.size(); at += 1000) {
    tracker.Push(std::span(tone).subspan(at, std::min<size_t>(1000, tone.size() - at)), &frames);
  }
  tracker.Finish(&frames);
  const std::vector<PitchFrame> whole = Track(tone);
  ASSERT_EQ(frames.size(), whole.size());
  const int hop = static_cast<int>(kRate / 100);
  ASSERT_EQ(frames.size(), (tone.size() - options.frame_size) / hop + 1);
  for (size_t i = 0; i < frames.size(); ++i) {
    EXPECT_DOUBLE_EQ(frames[i].time, (i * hop + options.frame_size / 2.0) / kRate);
    EXPECT_DOUBLE_EQ(frames[i].frequency, whole[i].frequency);
  }
}

}  // namespace
}  // namespace raagasense
