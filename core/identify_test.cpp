#include "core/identify.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <cmath>
#include <string>
#include <vector>

#include "core/svara.h"
#include "gtest/gtest.h"

namespace raagasense {
namespace {

constexpr double kSa = 146.83;

void Hold(std::vector<SteadyNote>* notes, double cents, double seconds) {
  const double start = notes->empty() ? 0 : notes->back().end + 0.3;
  notes->push_back({start, start + seconds, kSa * std::exp2(cents / 1200)});
}

TEST(IdentifyRaag, NoNotes) { EXPECT_TRUE(IdentifyRaag({}).empty()); }

TEST(IdentifyRaag, Yaman) {
  std::vector<SteadyNote> notes;
  for (int i = 0; i < 3; ++i) {
    for (auto [cents, seconds] : {std::pair{0, 1.0}, {200, 0.4}, {400, 0.8}, {600, 0.3}, {700, 1.0}, {900, 0.4},
                                  {1100, 0.5}}) {
      Hold(&notes, cents, seconds);
    }
  }
  const std::vector<RaagMatch> matches = IdentifyRaag(notes);
  ASSERT_FALSE(matches.empty());
  EXPECT_EQ(matches[0].raag->name, "Yaman");
  EXPECT_NEAR(std::remainder(CentsFromSa(matches[0].sa, kSa), 1200), 0, 5);
  // Every raag appears once.
  EXPECT_EQ(matches.size(), Raags().size());
}

// Five held svaras of a seven-svara raag fit a pentatonic raag better; the
// two others, held as well, tip it back.
TEST(IdentifyRaag, FewerSvarasFitBetterUnlessTheOthersAreHeld) {
  std::vector<SteadyNote> notes;
  for (int i = 0; i < 3; ++i) {
    for (auto [cents, seconds] : {std::pair{0, 1.0}, {200, 0.4}, {400, 0.8}, {700, 1.0}, {900, 0.4}}) {
      Hold(&notes, cents, seconds);
    }
  }
  const std::string pentatonic = IdentifyRaag(notes)[0].raag->name;
  EXPECT_TRUE(pentatonic == "Bhoopali" || pentatonic == "Deshkar") << pentatonic;
  for (int i = 0; i < 3; ++i) {
    Hold(&notes, 500, 0.5);
    Hold(&notes, 1100, 0.5);
  }
  const std::vector<int>& svaras = IdentifyRaag(notes)[0].raag->svaras;
  for (int svara : {Svara::kSa, kRe, kGa, kMa, kPa, kDha, kNi}) {
    EXPECT_NE(std::find(svaras.begin(), svaras.end(), svara), svaras.end()) << SvaraName(svara);
  }
}

// Marwa, Puriya and Sohini share their svaras; a profile learned from
// recordings of one decides between them.
TEST(IdentifyRaag, ProfilesDecideBetweenRaagsWithTheSameSvaras) {
  std::vector<SteadyNote> notes;
  for (int i = 0; i < 3; ++i) {
    for (auto [cents, seconds] : {std::pair{0, 0.6}, {100, 0.6}, {400, 0.6}, {600, 0.6}, {900, 0.6}, {1100, 0.6}}) {
      Hold(&notes, cents, seconds);
    }
  }
  std::vector<RaagMatch> matches = IdentifyRaag(notes);
  ASSERT_FALSE(matches.empty());
  // Whichever of the three the model alone doesn't pick.
  const Raag* other = FindRaag(matches[0].raag->name == "Marwa" ? "Puriya" : "Marwa");
  ASSERT_EQ(other->svaras, matches[0].raag->svaras);

  RaagLearning learning;
  for (int i = 0; i < 3; ++i) AddToProfile(HeldShares(notes, kSa), &learning.profiles[other]);
  EXPECT_EQ(learning.profiles[other].recordings, 3);
  EXPECT_NEAR(learning.profiles[other].held[kKomalRe], 1.0 / 6, 1e-9);
  matches = IdentifyRaag(notes, {}, &learning);
  EXPECT_EQ(matches[0].raag, other);
}

// Short passing notes, one after another, in cents from kSa.
std::vector<SteadyNote> Passing(std::initializer_list<int> cents) {
  std::vector<SteadyNote> notes;
  for (int c : cents) Hold(&notes, c, 0.05);
  for (size_t i = 1; i < notes.size(); ++i) {
    notes[i].start = notes[i - 1].end + 0.05;
    notes[i].end = notes[i].start + 0.05;
  }
  return notes;
}

TEST(PhraseShares, FindsPhrasesInOrderWithGraceNotes) {
  const Raag& yaman = *FindRaag("Yaman");
  // Yaman's first phrase, N R G, with a grace note (S) between N and R, then
  // the same svaras backwards.
  const std::map<const Raag*, double> forward = PhraseShares(Passing({-100, 0, 200, 400}), kSa);
  const std::map<const Raag*, double> backward = PhraseShares(Passing({400, 200, -100}), kSa);
  const double one = 1.0 / yaman.phrases.size();
  EXPECT_GE(forward.at(&yaman), one);
  EXPECT_LT(backward.at(&yaman), forward.at(&yaman));
}

TEST(Learn, ProfilesAndChance) {
  const Raag& yaman = *FindRaag("Yaman");
  const Raag& bhoopali = *FindRaag("Bhoopali");
  RaagLearning learning;
  const std::map<const Raag*, double> shares = {{&yaman, 0.5}, {&bhoopali, 0.2}};
  Learn(yaman, {}, shares, &learning);
  Learn(yaman, {}, {{&yaman, 0.5}, {&bhoopali, 0.4}}, &learning);
  EXPECT_EQ(learning.profiles[&yaman].recordings, 2);
  // Chance is learned only from other raags' recordings.
  EXPECT_EQ(learning.phrase_chance.count(&yaman), 0u);
  EXPECT_EQ(learning.phrase_chance[&bhoopali].recordings, 2);
  EXPECT_NEAR(learning.phrase_chance[&bhoopali].share, 0.3, 1e-9);
}

}  // namespace
}  // namespace raagasense
