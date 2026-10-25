#include "core/identify.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <cmath>
#include <limits>
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

TEST(IdentifyRaag, TracePreservesPredictionsAndAlternativeTonicHypotheses) {
  std::vector<SteadyNote> notes;
  for (int cents : {0, 200, 400, 600, 700, 900, 1100}) Hold(&notes, cents, 1);
  std::vector<SteadyNote> passing;
  for (int cents : {-100, 200, 400}) Hold(&passing, cents, 0.05);
  RaagLearning learning;
  Learn(*FindRaag("Yaman"), HeldShares(notes, kSa), &learning);
  IdentifyTrace trace;
  const auto traced = IdentifyRaag(notes, passing, &learning, {}, &trace);
  const auto normal = IdentifyRaag(notes, passing, &learning);
  ASSERT_EQ(traced.size(), normal.size());
  ASSERT_GT(trace.candidates.size(), 1u);
  EXPECT_EQ(trace.hypotheses.size(), trace.candidates.size() * Raags().size());
  for (size_t i = 0; i < normal.size(); ++i) {
    EXPECT_EQ(traced[i].raag, normal[i].raag);
    EXPECT_DOUBLE_EQ(traced[i].sa, normal[i].sa);
    EXPECT_DOUBLE_EQ(traced[i].score, normal[i].score);
    EXPECT_EQ(traced[i].foreign_svaras, normal[i].foreign_svaras);
    const auto best = std::find_if(trace.hypotheses.begin(), trace.hypotheses.end(),
                                   [&](const auto& pair) { return pair.raag == traced[i].raag; });
    ASSERT_NE(best, trace.hypotheses.end());
    EXPECT_DOUBLE_EQ(best->score, traced[i].score);
    EXPECT_DOUBLE_EQ(best->sa, traced[i].sa);
  }
  EXPECT_TRUE(IdentifyRaag({}, {}, nullptr, {}, &trace).empty());
  EXPECT_TRUE(trace.candidates.empty());
  EXPECT_TRUE(trace.hypotheses.empty());
}

TEST(IdentifyRaagAtSa, UsesTheSuppliedTonicEvenWhenSaIsNotHeld) {
  std::vector<SteadyNote> notes;
  for (int cents : {100, 400, 600, 900, 1100}) Hold(&notes, cents, 1);
  const auto matches = IdentifyRaagAtSa(notes, kSa);
  ASSERT_EQ(matches.size(), Raags().size());
  for (size_t i = 0; i < matches.size(); ++i) {
    EXPECT_DOUBLE_EQ(matches[i].sa, kSa);
    EXPECT_TRUE(std::isfinite(matches[i].score));
    if (i > 0) EXPECT_GE(matches[i - 1].score, matches[i].score);
  }
  // Auto Sa can only propose pitches that were held in this passage.
  for (const auto& match : IdentifyRaag(notes)) {
    EXPECT_GT(std::abs(std::remainder(CentsFromSa(match.sa, kSa), 1200)), 50);
  }
  const auto octave = IdentifyRaagAtSa(notes, kSa * 2);
  for (size_t i = 0; i < matches.size(); ++i) {
    EXPECT_EQ(matches[i].raag, octave[i].raag);
    EXPECT_NEAR(matches[i].score, octave[i].score, 1e-12);
  }
}

TEST(IdentifyRaagAtSa, RejectsInvalidTonicAndEmptyNotes) {
  std::vector<SteadyNote> notes;
  Hold(&notes, 0, 1);
  EXPECT_TRUE(IdentifyRaagAtSa({}, kSa).empty());
  for (double sa : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_TRUE(IdentifyRaagAtSa(notes, sa).empty());
  }
}

TEST(IdentifyRaagAtSa, TonicChangesTheInterpretationAndTranspositionPreservesIt) {
  std::vector<SteadyNote> notes;
  for (int cents : {0, 200, 400, 600, 700, 900, 1100}) Hold(&notes, cents, 1);
  const auto matches = IdentifyRaagAtSa(notes, kSa);
  const auto wrong = IdentifyRaagAtSa(notes, kSa * std::exp2(100.0 / 1200));
  const Raag* yaman = FindRaag("Yaman");
  auto score = [yaman](const auto& ranked) {
    return std::find_if(ranked.begin(), ranked.end(), [yaman](const auto& m) { return m.raag == yaman; })->score;
  };
  EXPECT_GT(score(matches), score(wrong));
  const double factor = std::exp2(350.0 / 1200);
  for (auto& note : notes) note.frequency *= factor;
  const auto transposed = IdentifyRaagAtSa(notes, kSa * factor);
  ASSERT_EQ(matches.size(), transposed.size());
  for (size_t i = 0; i < matches.size(); ++i) {
    EXPECT_EQ(matches[i].raag, transposed[i].raag);
    EXPECT_NEAR(matches[i].score, transposed[i].score, 1e-12);
  }
}

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
  EXPECT_EQ(IdentifyRaagAtSa(notes, kSa, {}, &learning)[0].raag, other);
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
  EXPECT_NEAR(forward.at(&yaman), std::exp(-0.5) * one, 1e-12);
  EXPECT_LT(backward.at(&yaman), forward.at(&yaman));
}

TEST(Learn, Profiles) {
  const Raag& yaman = *FindRaag("Yaman");
  RaagLearning learning;
  Learn(yaman, {}, &learning);
  Learn(yaman, {}, &learning);
  EXPECT_EQ(learning.profiles[&yaman].recordings, 2);
}

TEST(Learning, RoundTripsAsText) {
  const Raag& yaman = *FindRaag("Yaman");
  RaagLearning learning;
  std::array<double, 12> held = {};
  held[kGa] = 0.25;
  held[kNi] = 0.75;
  Learn(yaman, held, &learning);
  const RaagLearning read = LearningFromText(LearningText(learning) +
                                            "profile,Raag Imaginary,1\nchance,Bhoopali,2,0.125\n");
  ASSERT_EQ(read.profiles.size(), 1u);
  EXPECT_EQ(read.profiles.at(&yaman).recordings, 1);
  EXPECT_DOUBLE_EQ(read.profiles.at(&yaman).held[kNi], 0.75);
  EXPECT_EQ(LearningText(read), LearningText(learning));
}

TEST(IdentifyRaagAtSa, PhraseEvidenceWorksWithoutLearnedProfiles) {
  std::vector<SteadyNote> notes;
  for (int cents : {0, 200, 400, 600, 700, 900, 1100}) Hold(&notes, cents, 1);
  const auto passing = Passing({-100, 200, 400});
  const auto with = IdentifyRaagAtSa(notes, kSa, passing);
  const auto without = IdentifyRaagAtSa(notes, kSa, passing, nullptr, IdentifyOptions{false});
  const auto score = [](const auto& matches) {
    const auto yaman = FindRaag("Yaman");
    return std::find_if(matches.begin(), matches.end(), [yaman](const auto& m) { return m.raag == yaman; })->score;
  };
  EXPECT_GT(score(with), score(without));
  const auto wrong_contour = IdentifyRaagAtSa(notes, kSa, Passing({1100, 200, 400}));
  EXPECT_GT(score(with), score(wrong_contour));
  const auto repeat = IdentifyRaagAtSa(notes, kSa, passing);
  for (size_t i = 0; i < with.size(); ++i) {
    EXPECT_EQ(with[i].raag, repeat[i].raag);
    EXPECT_DOUBLE_EQ(with[i].score, repeat[i].score);
  }
}

TEST(IdentifyRaagAtSa, PhraseEvidenceIsNeutralWithoutPassingNotes) {
  std::vector<SteadyNote> notes;
  for (int cents : {0, 200, 400, 600, 700, 900, 1100}) Hold(&notes, cents, 1);
  const auto with = IdentifyRaagAtSa(notes, kSa);
  const auto without = IdentifyRaagAtSa(notes, kSa, {}, nullptr, IdentifyOptions{false});
  ASSERT_EQ(with.size(), without.size());
  for (size_t i = 0; i < with.size(); ++i) {
    EXPECT_EQ(with[i].raag, without[i].raag);
    EXPECT_DOUBLE_EQ(with[i].score, without[i].score);
  }
}

TEST(IdentifyRaagAtSa, PhraseContextDoesNotAccumulateForever) {
  std::vector<SteadyNote> notes;
  for (int cents : {0, 200, 400, 600, 700, 900, 1100}) Hold(&notes, cents, 1);
  auto passing = Passing({-100, 200, 400});
  passing.push_back({200, 201, kSa});
  const auto with = IdentifyRaagAtSa(notes, kSa, passing);
  const auto without = IdentifyRaagAtSa(notes, kSa, passing, nullptr, IdentifyOptions{false});
  ASSERT_EQ(with.size(), without.size());
  for (size_t i = 0; i < with.size(); ++i) {
    EXPECT_EQ(with[i].raag, without[i].raag);
    EXPECT_DOUBLE_EQ(with[i].score, without[i].score);
  }
}

TEST(RaagTracker, KeepsTheLeaderUnlessClearlyBeatenForAWhile) {
  const Raag* yaman = FindRaag("Yaman");
  const Raag* kalyan = FindRaag("Yaman Kalyan");
  const Raag* bilawal = FindRaag("Bilawal");
  RaagTracker tracker(TrackerOptions{0.1, 6});
  auto update = [&](const Raag* first, double first_score, const Raag* second, double second_score, double now,
                    int second_foreign = 0) {
    return tracker.Update({{first, 233, first_score, 0}, {second, 233, second_score, second_foreign}}, now)[0].raag;
  };
  EXPECT_EQ(update(yaman, -1.0, kalyan, -1.2, 0), yaman);
  // Narrowly beaten: kept.
  EXPECT_EQ(update(kalyan, -1.0, yaman, -1.05, 2), yaman);
  // Clearly beaten, but not yet for long enough: kept.
  EXPECT_EQ(update(kalyan, -1.0, yaman, -1.2, 4), yaman);
  EXPECT_EQ(update(kalyan, -1.0, yaman, -1.2, 8), yaman);
  // Clearly beaten for 6 s: replaced.
  EXPECT_EQ(update(kalyan, -1.0, yaman, -1.2, 10), kalyan);
  // A foreign svara turning up for the leader replaces it at once.
  EXPECT_EQ(update(bilawal, -1.0, kalyan, -1.02, 12, 1), bilawal);
}

}  // namespace
}  // namespace raagasense
