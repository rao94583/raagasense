#include "core/pitch.h"

#include <cmath>
#include <numbers>
#include <vector>

#include "gtest/gtest.h"

namespace raagasense {
namespace {

constexpr double kRate = 48000;

// A tone with the given partial amplitudes (index 0 is the fundamental).
std::vector<float> Tone(double frequency, std::vector<double> partials, int samples) {
  std::vector<float> out(samples);
  for (int i = 0; i < samples; ++i) {
    double value = 0;
    for (size_t k = 0; k < partials.size(); ++k) {
      value += partials[k] * std::sin(2 * std::numbers::pi * frequency * (k + 1) * i / kRate);
    }
    out[i] = static_cast<float>(0.3 * value);
  }
  return out;
}

PitchOptions Options() {
  PitchOptions options;
  options.sample_rate = kRate;
  return options;
}

TEST(EstimatePitch, SineTones) {
  for (double frequency : {80.0, 138.59, 220.0, 440.0, 1000.0}) {
    const PitchFrame frame = EstimatePitch(Tone(frequency, {1}, 2048), Options());
    EXPECT_TRUE(frame.voiced) << frequency;
    EXPECT_NEAR(frame.frequency, frequency, frequency * 0.002) << frequency;
    EXPECT_GT(frame.confidence, 0.95) << frequency;
  }
}

// A voice's fundamental is often weaker than its harmonics; YIN should still
// report the fundamental, not an octave above it.
TEST(EstimatePitch, WeakFundamental) {
  const PitchFrame frame = EstimatePitch(Tone(146.83, {0.3, 1, 0.8, 0.5, 0.3}, 2048), Options());
  EXPECT_TRUE(frame.voiced);
  EXPECT_NEAR(frame.frequency, 146.83, 0.5);
}

// A voice with a quieter instrument playing another note; at the paper's
// threshold of 0.15 this frame is unvoiced.
TEST(EstimatePitch, VoiceOverAccompaniment) {
  std::vector<float> mix = Tone(233.08, {1, 0.8, 0.5, 0.3}, 2048);
  const std::vector<float> harmonium = Tone(293.66, {1, 0.7, 0.5, 0.4, 0.3}, 2048);
  for (size_t i = 0; i < mix.size(); ++i) mix[i] += 0.4f * harmonium[i];
  const PitchFrame frame = EstimatePitch(mix, Options());
  EXPECT_TRUE(frame.voiced);
  EXPECT_NEAR(frame.frequency, 233.08, 1);
}

TEST(EstimatePitch, SilenceIsUnvoiced) {
  const PitchFrame frame = EstimatePitch(std::vector<float>(2048, 0.0f), Options());
  EXPECT_FALSE(frame.voiced);
  EXPECT_EQ(frame.frequency, 0);
}

TEST(EstimatePitch, NoiseIsUnvoiced) {
  std::vector<float> noise(2048);
  uint32_t state = 12345;
  for (float& x : noise) {
    state = state * 1664525 + 1013904223;
    x = static_cast<float>(state) / 4294967296.0f - 0.5f;
  }
  EXPECT_FALSE(EstimatePitch(noise, Options()).voiced);
}

TEST(PitchTracker, FramesAtEachHopRegardlessOfChunking) {
  const PitchOptions options = Options();
  const std::vector<float> tone = Tone(220, {1, 0.5}, 48000);
  PitchTracker tracker(options);
  std::vector<PitchFrame> frames;
  for (size_t at = 0; at < tone.size(); at += 1000) {
    tracker.Push(std::span(tone).subspan(at, std::min<size_t>(1000, tone.size() - at)), &frames);
  }
  const size_t expected = (tone.size() - options.frame_size) / options.hop_size + 1;
  ASSERT_EQ(frames.size(), expected);
  for (size_t i = 0; i < frames.size(); ++i) {
    EXPECT_DOUBLE_EQ(frames[i].time, (i * options.hop_size + options.frame_size / 2.0) / kRate);
    EXPECT_NEAR(frames[i].frequency, 220, 0.5);
  }
}

}  // namespace
}  // namespace raagasense
