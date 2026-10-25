#include "core/phrase.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gtest/gtest.h"

namespace raagasense {
namespace {

std::vector<PhraseEvent> Events(std::initializer_list<int> pitches) {
  std::vector<PhraseEvent> events;
  for (int pitch : pitches) {
    const double start = events.size() * 0.25;
    events.push_back({pitch, start, start + 0.2, 0});
  }
  return events;
}

TEST(ParsePhrase, KeepsOctavesAndRepetitions) {
  EXPECT_EQ(ParsePhrase(".N R G S' r' r'"), (std::vector<int>{-1, 2, 4, 12, 13, 13}));
  EXPECT_TRUE(ParsePhrase("S ? G").empty());
  EXPECT_TRUE(ParsePhrase("S Rgarbage").empty());
}

TEST(MatchPhrase, PreservesOctaveContourButAllowsAnOverallOctaveShift) {
  const auto phrase = ParsePhrase(".N R G");
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, Events({-1, 2, 4})), 1);
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, Events({11, 14, 16})), 1);
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, Events({11, 2, 4})), 0);
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, Events({4, 2, -1})), 0);
}

TEST(MatchPhrase, ARepeatRequiresSeparateEvents) {
  const auto phrase = ParsePhrase("r r S");
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, Events({1, 1, 0})), 1);
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, Events({1, 0})), 0);
}

TEST(MatchPhrase, DiscountsExtraNotesAndSearchesAlternativeAlignments) {
  const auto phrase = ParsePhrase("S R G");
  EXPECT_NEAR(MatchPhrase(phrase, Events({0, 1, 2, 4})), std::exp(-0.5), 1e-12);
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, Events({0, 1, 3, 5, 2, 4})), 0);
  // The first R is too far from G, but taking the second R completes it.
  EXPECT_GT(MatchPhrase(phrase, Events({0, 2, 2, 5, 5, 4})), 0);
}

TEST(MatchPhrase, DoesNotCrossPausesOrJoinDistantNotes) {
  const auto phrase = ParsePhrase("S R G");
  auto events = Events({0, 2, 4});
  events[2].segment = 1;
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, events), 0);
  events = Events({0, 2, 4});
  events[2].start = 21;
  events[2].end = 22;
  EXPECT_DOUBLE_EQ(MatchPhrase(phrase, events), 0);
}

TEST(PhraseEvents, KeepsSeparateNotesAndSplitsAtLongGaps) {
  const std::vector<SteadyNote> notes = {{0, 0.2, 220}, {0.3, 0.5, 220}, {1.5, 1.7, 440}};
  const auto events = PhraseEvents(notes, 220);
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].semitone, 0);
  EXPECT_EQ(events[1].semitone, 0);
  EXPECT_EQ(events[2].semitone, 12);
  EXPECT_EQ(events[0].segment, events[1].segment);
  EXPECT_NE(events[1].segment, events[2].segment);
}

TEST(ShuffledPhraseEvents, PreservesTimingAndEachSegmentsHistogram) {
  auto events = Events({-1, 0, 2, 4, 7, 11, 12, 14});
  for (size_t i = 4; i < events.size(); ++i) events[i].segment = 1;
  const auto shuffled = ShuffledPhraseEvents(events, 0);
  const auto repeat = ShuffledPhraseEvents(events, 0);
  for (size_t i = 0; i < events.size(); ++i) {
    EXPECT_EQ(shuffled[i].segment, events[i].segment);
    EXPECT_DOUBLE_EQ(shuffled[i].start, events[i].start);
    EXPECT_DOUBLE_EQ(shuffled[i].end, events[i].end);
    EXPECT_EQ(shuffled[i].semitone, repeat[i].semitone);
  }
  for (size_t first : {0u, 4u}) {
    std::vector<int> before, after;
    for (size_t i = first; i < first + 4; ++i) {
      before.push_back(events[i].semitone);
      after.push_back(shuffled[i].semitone);
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    EXPECT_EQ(before, after);
  }
}

}  // namespace
}  // namespace raagasense
