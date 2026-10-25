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

std::vector<PitchFrame> Track(const std::vector<float>& samples, int percussion_frames = 17, int tracking_frames = 15) {
  MelodyOptions options;
  options.sample_rate = kRate;
  options.percussion_frames = percussion_frames;
  options.tracking_frames = tracking_frames;
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
  // Isolate the percussion mask's contribution using the framewise mode;
  // temporal tracking can reject these strokes even without the mask.
  const double with = ShareOn(Track(mix, 17, 0), 233.08), without = ShareOn(Track(mix, 0, 0), 233.08);
  EXPECT_GT(with, 0.9);
  EXPECT_GT(with, without + 0.05);
  EXPECT_GT(ShareOn(Track(mix), 233.08), 0.95);
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

TEST(MelodyTracker, BriefAccompanimentSurgeDoesNotReplaceTheLead) {
  auto mix = Tone(233.08, kVoice, 2);
  auto accompaniment = Tone(293.66, kVoice, 2);
  for (size_t i = 0; i < accompaniment.size(); ++i) {
    const double time = i / kRate;
    accompaniment[i] *= time > 0.85 && time < 0.91 ? 1.8f : 0.5f;
  }
  Mix(&mix, accompaniment, 1);
  const double temporal = ShareOn(Track(mix), 233.08);
  const double framewise = ShareOn(Track(mix, 17, 0), 233.08);
  EXPECT_GT(temporal, 0.97);
  EXPECT_GT(temporal, framewise + 0.01);
}

TEST(MelodyTracker, SustainedOctaveLeapIsPreserved) {
  auto samples = Tone(220, kVoice, 1);
  const auto high = Tone(440, kVoice, 1);
  samples.insert(samples.end(), high.begin(), high.end());
  const auto frames = Track(samples);
  for (const auto& frame : frames) {
    if (frame.time < 0.9 || frame.time > 1.1) {
      ASSERT_TRUE(frame.voiced);
      EXPECT_NEAR(CentsFromSa(frame.frequency, frame.time < 1 ? 220 : 440), 0, 10) << frame.time;
    }
  }
}

TEST(MelodyTracker, FastNoteChangesArePreserved) {
  // A short taan-like sequence, with notes every 100 ms. Check their centers
  // rather than the FFT windows that overlap the note boundaries.
  const std::vector<double> frequencies = {220, 246.94, 277.18, 329.63, 293.66, 246.94, 220};
  std::vector<float> samples;
  for (double frequency : frequencies) {
    auto note = Tone(frequency, kVoice, 0.1);
    samples.insert(samples.end(), note.begin(), note.end());
  }
  for (const auto& frame : Track(samples)) {
    const size_t note = static_cast<size_t>(frame.time / 0.1);
    const double within = std::fmod(frame.time, 0.1);
    if (note >= frequencies.size() || within < 0.04 || within > 0.06) continue;
    EXPECT_TRUE(frame.voiced);
    EXPECT_NEAR(CentsFromSa(frame.frequency, frequencies[note]), 0, 20) << frame.time;
  }
}

TEST(MelodyTracker, LookaheadIsBoundedAndFinishFlushesExactlyOnce) {
  const auto samples = Tone(220, kVoice, 1);
  MelodyOptions options;
  options.tracking_frames = 15;
  MelodyTracker tracker(options);
  std::vector<PitchFrame> frames;
  tracker.Push(samples, &frames);
  const size_t total = (samples.size() - options.frame_size) / (static_cast<int>(kRate / 100)) + 1;
  const size_t latency = options.percussion_frames / 2 + options.tracking_frames;
  ASSERT_EQ(frames.size(), total - latency);
  const auto prefix = frames;
  tracker.Finish(&frames);
  ASSERT_EQ(frames.size(), total);
  for (size_t i = 0; i < prefix.size(); ++i) {
    EXPECT_DOUBLE_EQ(frames[i].frequency, prefix[i].frequency);
    EXPECT_DOUBLE_EQ(frames[i].time, prefix[i].time);
  }
  tracker.Finish(&frames);
  EXPECT_EQ(frames.size(), total);
}

TEST(MelodyTracker, TracksMeendAndAndolanOverDrone) {
  constexpr double seconds = 3;
  auto pitch = [](double time) {
    // A 500-cent meend followed by a broad, slow oscillation.
    const double cents = time < 1.5 ? 500 * time / 1.5 : 500 + 60 * std::sin(2 * std::numbers::pi * 2 * (time - 1.5));
    return 220 * std::exp2(cents / 1200);
  };
  std::vector<float> samples(static_cast<size_t>(seconds * kRate));
  double phase = 0;
  for (size_t i = 0; i < samples.size(); ++i) {
    phase += 2 * std::numbers::pi * pitch(i / kRate) / kRate;
    for (size_t h = 0; h < kVoice.size(); ++h) samples[i] += 0.2 * kVoice[h] * std::sin((h + 1) * phase);
  }
  Mix(&samples, Tone(110, kVoice, seconds), 0.4);
  const auto frames = Track(samples);
  int correct = 0, count = 0;
  for (const auto& frame : frames) {
    if (frame.time < 0.15 || frame.time > seconds - 0.15) continue;
    ++count;
    correct += frame.voiced && std::abs(CentsFromSa(frame.frequency, pitch(frame.time))) < 25;
  }
  ASSERT_GT(count, 0);
  EXPECT_GT(static_cast<double>(correct) / count, 0.9);
}

TEST(MelodyTracker, SilenceBetweenPhrasesDoesNotBecomeAGlide) {
  auto samples = Tone(220, kVoice, 0.5);
  samples.resize(static_cast<size_t>(kRate), 0);
  const auto high = Tone(440, kVoice, 0.5);
  samples.insert(samples.end(), high.begin(), high.end());
  for (const auto& frame : Track(samples)) {
    if (frame.time > 0.6 && frame.time < 0.9) EXPECT_FALSE(frame.voiced);
    if (frame.time > 1.1) {
      EXPECT_TRUE(frame.voiced);
      EXPECT_NEAR(CentsFromSa(frame.frequency, 440), 0, 10);
    }
  }
}

TEST(MelodyTracker, FramesAtEachHopRegardlessOfChunking) {
  auto tone = Tone(233.08, kVoice, 2);
  auto accompaniment = Tone(293.66, kVoice, 2);
  for (size_t i = 0; i < accompaniment.size(); ++i) {
    const double time = i / kRate;
    accompaniment[i] *= time > 0.85 && time < 0.91 ? 1.8f : 0.5f;
  }
  Mix(&tone, accompaniment, 1);
  MelodyOptions options;
  options.sample_rate = kRate;
  options.tracking_frames = 15;
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
