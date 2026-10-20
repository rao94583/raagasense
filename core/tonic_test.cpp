#include "core/tonic.h"

#include <cmath>
#include <vector>

#include "core/svara.h"
#include "gtest/gtest.h"

namespace raagasense {
namespace {

constexpr double kSa = 233.8;

// Appends a note held for the given seconds, in cents from kSa.
void Hold(std::vector<SteadyNote>* notes, double cents, double seconds) {
  const double start = notes->empty() ? 0 : notes->back().end + 0.3;
  notes->push_back({start, start + seconds, kSa * std::exp2(cents / 1200)});
}

TEST(EstimateTonic, NoNotes) { EXPECT_TRUE(EstimateTonic({}).empty()); }

// Held time like a Yaman alap's: Pa held most, then Ga, then Sa. Taken as
// Sa, Pa (with Re as its Pa) scores lower than the real Sa.
TEST(EstimateTonic, YamanAlap) {
  std::vector<SteadyNote> notes;
  for (int i = 0; i < 3; ++i) {
    Hold(&notes, 700, 1.0);
    Hold(&notes, 386, 0.7);
    Hold(&notes, 204, 0.3);
    Hold(&notes, 5, 0.6);
    Hold(&notes, 590, 0.2);
    Hold(&notes, 700, 0.8);
    Hold(&notes, -110, 0.2);
  }
  const std::vector<TonicCandidate> candidates = EstimateTonic(notes);
  ASSERT_GE(candidates.size(), 2u);
  EXPECT_NEAR(CentsFromSa(candidates[0].frequency, kSa), 2, 5);
  EXPECT_GT(candidates[0].score, candidates[1].score);
}

TEST(EstimateTonic, OctaveFollowsTheMedianHeldPitch) {
  std::vector<SteadyNote> notes;
  Hold(&notes, -500, 0.5);  // mandra Pa
  Hold(&notes, 0, 1.0);
  Hold(&notes, 700, 1.5);
  Hold(&notes, 1200, 0.5);
  const std::vector<TonicCandidate> candidates = EstimateTonic(notes);
  ASSERT_FALSE(candidates.empty());
  EXPECT_NEAR(CentsFromSa(candidates[0].frequency, kSa), 0, 5);
}

TEST(EstimateTonic, ScoreIsTheShareOnSaAndPa) {
  std::vector<SteadyNote> notes;
  Hold(&notes, 0, 1.0);
  Hold(&notes, 700, 1.0);
  Hold(&notes, 400, 1.5);
  const std::vector<TonicCandidate> candidates = EstimateTonic(notes);
  ASSERT_FALSE(candidates.empty());
  // Ga is held longest, but Sa and Pa together are held longer.
  EXPECT_NEAR(CentsFromSa(candidates[0].frequency, kSa), 0, 5);
  EXPECT_NEAR(candidates[0].score, 2 / 3.5, 1e-9);
}

// Malkauns on D holds the same pitches as Bhoopali on A#, and A# wins on Sa
// and Pa; knowing the svaras are Malkauns' picks D.
TEST(EstimateTonicForSvaras, MalkaunsNotBhoopali) {
  std::vector<SteadyNote> notes;
  for (int i = 0; i < 3; ++i) {
    Hold(&notes, 0, 0.8);     // S
    Hold(&notes, 300, 0.3);   // g
    Hold(&notes, 500, 0.6);   // m
    Hold(&notes, 800, 1.2);   // d
    Hold(&notes, 1000, 0.3);  // n
  }
  // Komal Dha, which as Sa makes these Bhoopali.
  const double guess = CentsFromSa(EstimateTonic(notes)[0].frequency, kSa);
  ASSERT_NEAR(guess - 1200 * std::floor(guess / 1200), 800, 5);
  const int malkauns[] = {Svara::kSa, kKomalGa, kMa, kKomalDha, kKomalNi};
  const std::vector<TonicCandidate> candidates = EstimateTonicForSvaras(notes, malkauns);
  ASSERT_FALSE(candidates.empty());
  EXPECT_NEAR(CentsFromSa(candidates[0].frequency, kSa), 0, 5);
  EXPECT_NEAR(candidates[0].score, 1, 1e-9);
}

}  // namespace
}  // namespace raagasense
