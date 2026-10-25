#include "core/annotation.h"

#include <cmath>
#include <vector>

#include "gtest/gtest.h"

namespace raagasense::annotation {
namespace {

TEST(AnnotationLabels, PreservesOtherRowsColumnsAndLegacyArtistNames) {
  const std::string text = "file,raag,sa_hz,singer,notes,sa_checked\r\n"
      "a.wav,Yaman,220,female,\"old note; artist Artist A\",yes\r\n"
      "b.wav,Bhairav,146.7,male,\"source, live; artist Artist B\",no\r\n";
  std::string error;
  auto labels = Labels::Parse(text, &error);
  ASSERT_TRUE(labels) << error;
  auto first = labels->Get("a.wav");
  EXPECT_EQ(first.artist, "Artist A");
  EXPECT_TRUE(first.sa_checked);
  first.artist = "Artist \"New\", Name";
  ASSERT_TRUE(labels->Set(first, &error)) << error;
  auto reread = Labels::Parse(labels->Text(), &error);
  ASSERT_TRUE(reread) << error;
  EXPECT_EQ(reread->Get("a.wav").artist, first.artist);
  EXPECT_EQ(reread->Get("b.wav").artist, "Artist B");
  EXPECT_EQ(reread->Get("b.wav").notes, "source, live; artist Artist B");
  EXPECT_NE(labels->Text().find(",female,"), std::string::npos);
  EXPECT_NE(labels->Text().find(",male,"), std::string::npos);
  first.artist.clear();
  ASSERT_TRUE(labels->Set(first, &error));
  EXPECT_TRUE(labels->Get("a.wav").artist.empty());
}

TEST(AnnotationLabels, RejectsMalformedTablesAndMultilineEdits) {
  std::string error;
  EXPECT_FALSE(Labels::Parse("file,raag,sa_hz\na,Yaman,220\na,Yaman,220\n", &error));
  EXPECT_FALSE(Labels::Parse("file,raag,sa_hz\na,\"broken\n", &error));
  auto labels = Labels::Parse("", &error);
  ASSERT_TRUE(labels);
  Label label;
  label.file = "a.wav";
  label.notes = "two\nlines";
  EXPECT_FALSE(labels->Set(label, &error));
}

TEST(PitchEditor, ChangesWholeIntervalsAndUndoPreservesTimesAndGaps) {
  const std::vector<PitchFrame> original = {{0.1, 220, 0.8, true}, {0.2, 0, 0.1, false}, {0.3, 230, 0.9, true}};
  PitchEditor editor(original);
  ASSERT_TRUE(editor.Apply(Edit::kTranspose, 0, 0.25, 12));
  EXPECT_DOUBLE_EQ(editor.frames()[0].frequency, 440);
  EXPECT_FALSE(editor.frames()[1].voiced);
  EXPECT_DOUBLE_EQ(editor.frames()[2].frequency, 230);
  ASSERT_TRUE(editor.Apply(Edit::kNoLead, 0.25, 0.4));
  EXPECT_FALSE(editor.frames()[2].voiced);
  ASSERT_TRUE(editor.Undo());
  EXPECT_DOUBLE_EQ(editor.frames()[2].frequency, 230);
  ASSERT_TRUE(editor.Undo());
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_DOUBLE_EQ(editor.frames()[i].time, original[i].time);
    EXPECT_DOUBLE_EQ(editor.frames()[i].frequency, original[i].frequency);
    EXPECT_DOUBLE_EQ(editor.frames()[i].confidence, original[i].confidence);
  }
  EXPECT_FALSE(editor.Undo());
  EXPECT_FALSE(editor.Apply(Edit::kSetFrequency, 0, 1, -1));
  ASSERT_TRUE(editor.Apply(Edit::kSetFrequency, 0, 0.25, 293.66));
  EXPECT_TRUE(editor.frames()[1].voiced);
  ASSERT_TRUE(editor.Apply(Edit::kRestore, 0, 1));
  EXPECT_FALSE(editor.frames()[1].voiced);
}

TEST(PitchEditor, ExportsAllReviewedFramesWithExplicitSilence) {
  PitchEditor editor({{0.1, 220, 0.8, true}, {0.2, 0, 0.1, false}});
  const auto csv = editor.ReviewedCsv();
  EXPECT_NE(csv.find("220,1\n"), std::string::npos);
  EXPECT_NE(csv.find(",0,1\n"), std::string::npos);
}

TEST(PitchGuide, LeavesNoLeadPassagesSilentAndWritesPlayableWaveFormat) {
  const std::vector<PitchFrame> frames = {{0, 220, 1, true}, {0.5, 0, 1, false}};
  const auto samples = PitchGuide(frames, 1);
  ASSERT_EQ(samples.size(), 16000u);
  double early = 0, late = 0;
  for (size_t i = 1600; i < 3200; ++i) early += samples[i] * samples[i];
  for (size_t i = 12000; i < 14000; ++i) late += samples[i] * samples[i];
  EXPECT_GT(early, 1);
  EXPECT_LT(late, 1e-8);
  const auto wav = WavBytes(samples, 16000);
  EXPECT_EQ(wav.size(), 44 + samples.size() * 2);
  EXPECT_EQ(wav.substr(0, 4), "RIFF");
  EXPECT_EQ(wav.substr(8, 8), "WAVEfmt ");
  EXPECT_EQ(wav.substr(36, 4), "data");
}

}  // namespace
}  // namespace raagasense::annotation
