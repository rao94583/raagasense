#include "tools/evaluation.h"

#include <sstream>

#include "gtest/gtest.h"

namespace raagasense::evaluation {
namespace {

TEST(Labels, WindowsLineEndingsDoNotAdmitUncheckedSa) {
  const std::string csv = "file,raag,sa_hz,notes,sa_checked\r\n"
                          "a.wav,Yaman,220,\"A, B; artist Artist One; other notes\",no\r\n"
                          "b.wav,Bhairav,146.7,\"artist Artist Two\",yes\r\n";
  std::vector<Label> labels;
  std::string error;
  std::istringstream in(csv);
  ASSERT_TRUE(ReadLabels(in, false, &labels, &error)) << error;
  ASSERT_EQ(labels.size(), 2u);
  EXPECT_DOUBLE_EQ(labels[0].sa, 0);
  EXPECT_EQ(labels[0].artist, "Artist One");
  EXPECT_DOUBLE_EQ(labels[1].sa, 146.7);
  EXPECT_EQ(labels[1].artist, "Artist Two");
  std::istringstream all(csv);
  ASSERT_TRUE(ReadLabels(all, true, &labels, &error)) << error;
  EXPECT_DOUBLE_EQ(labels[0].sa, 220);
}

TEST(Labels, ReadsColumnsByNameAndPreservesEscapedQuotes) {
  std::istringstream in("artist,sa_hz,file,raag,sa_checked,notes\n"
                        "\"Artist \"\"A\"\"\",220,a.wav,Yaman,yes,note\n");
  std::vector<Label> labels;
  std::string error;
  ASSERT_TRUE(ReadLabels(in, false, &labels, &error)) << error;
  ASSERT_EQ(labels.size(), 1u);
  EXPECT_EQ(labels[0].artist, "Artist \"A\"");
  EXPECT_EQ(labels[0].file, "a.wav");
  EXPECT_EQ(labels[0].raag, "Yaman");
  EXPECT_DOUBLE_EQ(labels[0].sa, 220);
}

TEST(Labels, ExplicitUnknownArtistDoesNotFallBackToOldNotes) {
  std::istringstream in("file,raag,sa_hz,artist,notes\na.wav,Yaman,220,,artist Old Name\n");
  std::vector<Label> labels;
  std::string error;
  ASSERT_TRUE(ReadLabels(in, false, &labels, &error)) << error;
  ASSERT_EQ(labels.size(), 1u);
  EXPECT_TRUE(labels[0].artist.empty());
}

TEST(PitchCsv, ReadsAnalyzeOutputAndUnvoicedFrames) {
  std::istringstream in("time,frequency,confidence,svara,deviation\r\n"
                        "0.043,220,0.9,S,0\r\n0.053,,0.1,,\r\n");
  std::vector<PitchFrame> frames;
  std::string error;
  ASSERT_TRUE(ReadPitchCsv(in, &frames, &error)) << error;
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_TRUE(frames[0].voiced);
  EXPECT_DOUBLE_EQ(frames[0].frequency, 220);
  EXPECT_FALSE(frames[1].voiced);
}

TEST(PitchCsv, AllowsMinimalCorrectedTrack) {
  std::istringstream in("time,frequency\n0,220\n0.01,0\n");
  std::vector<PitchFrame> frames;
  std::string error;
  ASSERT_TRUE(ReadPitchCsv(in, &frames, &error)) << error;
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_DOUBLE_EQ(frames[0].confidence, 1);
  EXPECT_FALSE(frames[1].voiced);
}

TEST(PitchCsv, RejectsNonfiniteOutOfOrderAndMalformedFrames) {
  for (const char* body : {"nan,220,1\n", "0,inf,1\n", "0,220,1.1\n", "0,220,1\n0,220,1\n",
                            "0.01,220,1\n0,220,1\n", "0,-220,1\n", "0,220oops,1\n", "0\n",
                            "0,\"220,1\n", "0,220,1,extra\n"}) {
    std::istringstream in(std::string("time,frequency,confidence\n") + body);
    std::vector<PitchFrame> frames;
    std::string error;
    EXPECT_FALSE(ReadPitchCsv(in, &frames, &error)) << body;
    EXPECT_FALSE(error.empty());
  }
}

TEST(SaragaPitch, PreservesTimingVoicingAndFrequencyAndCanReadAPrefix) {
  const std::string data = "0.000000e+00\t0.000000e+00\r\n4.444444e-03\t220.5\r\n0.008888889 221\n";
  std::istringstream in(data);
  std::vector<PitchFrame> frames;
  std::string error;
  ASSERT_TRUE(ReadSaragaPitch(in, &frames, &error)) << error;
  ASSERT_EQ(frames.size(), 3u);
  EXPECT_FALSE(frames[0].voiced);
  EXPECT_TRUE(frames[1].voiced);
  EXPECT_DOUBLE_EQ(frames[1].frequency, 220.5);
  EXPECT_DOUBLE_EQ(frames[1].time, 0.004444444);
  std::istringstream prefix(data);
  ASSERT_TRUE(ReadSaragaPitch(prefix, &frames, &error, 0.008888889)) << error;
  ASSERT_EQ(frames.size(), 2u);
}

TEST(SaragaPitch, RejectsInvalidTracksRatherThanSilentlyDroppingFrames) {
  for (const char* data : {"", "0 nan\n", "0 -220\n", "-1 220\n", "0 220\n0 221\n",
                          "0.01 220\n0 221\n", "0 220 extra\n", "0\n", "nan 220\n"}) {
    std::istringstream in(data);
    std::vector<PitchFrame> frames;
    std::string error;
    EXPECT_FALSE(ReadSaragaPitch(in, &frames, &error)) << data;
    EXPECT_FALSE(error.empty());
  }
}

}  // namespace
}  // namespace raagasense::evaluation
