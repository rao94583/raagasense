#include "tools/joint_diagnostics.h"

#include <cmath>
#include <sstream>

#include "gtest/gtest.h"

namespace raagasense::evaluation {
namespace {
constexpr double kSa = 220;
const Raag& Truth() { return *FindRaag("Yaman"); }
const Raag& Other() { return *FindRaag("Bhairav"); }

TEST(JointDiagnostic, SeparatesCandidateAbsenceFromScoringFailures) {
  const double wrong_sa = kSa * std::exp2(700.0 / 1200);
  IdentifyTrace trace{{{wrong_sa, 1}}, {{&Other(), wrong_sa, 2, 0}, {&Truth(), wrong_sa, 1, 0}}};
  const std::vector<RaagMatch> matches = trace.hypotheses;
  auto result = DiagnoseJoint(Truth(), kSa, matches, trace);
  EXPECT_EQ(result.outcome, "reference_sa_not_proposed");
  EXPECT_FALSE(result.reference_candidate_present);
  EXPECT_FALSE(result.reference_pair);
  EXPECT_FALSE(result.reference_candidate_rank);
  EXPECT_NEAR(*result.nearest_candidate_error_cents, -500, 1e-9);
  // Adding a candidate does not itself repair a bad ranking.
  trace.candidates.push_back({kSa, 0.1});
  trace.hypotheses.push_back({&Truth(), kSa, 0, 0});
  result = DiagnoseJoint(Truth(), kSa, matches, trace);
  EXPECT_EQ(result.outcome, "wrong_raag_wrong_sa");
  EXPECT_EQ(result.reference_candidate_rank, 2u);
  EXPECT_EQ(result.reference_pair_rank, 3u);
  EXPECT_EQ(result.reference_pair_ties, 1u);
  EXPECT_DOUBLE_EQ(*result.reference_pair_gap, 2);
}

TEST(JointDiagnostic, DistinguishesRightRaagFromRightJointPair) {
  const double wrong_sa = kSa * std::exp2(200.0 / 1200);
  IdentifyTrace trace{{{wrong_sa, 1}, {kSa, 0.5}},
                       {{&Truth(), wrong_sa, 3, 0}, {&Other(), kSa, 2, 0}, {&Truth(), kSa, 1, 0}}};
  std::vector<RaagMatch> matches = {{&Truth(), wrong_sa, 3, 0}, {&Other(), kSa, 2, 0}};
  EXPECT_EQ(DiagnoseJoint(Truth(), kSa, matches, trace).outcome, "correct_raag_wrong_sa");
  trace.hypotheses = {{&Other(), kSa, 3, 0}, {&Truth(), wrong_sa, 2, 0}, {&Truth(), kSa, 1, 0}};
  matches = {{&Other(), kSa, 3, 0}, {&Truth(), wrong_sa, 2, 0}};
  EXPECT_EQ(DiagnoseJoint(Truth(), kSa, matches, trace).outcome, "wrong_raag_reference_sa");
  trace.hypotheses = {{&Truth(), kSa, 3, 0}, {&Other(), kSa, 2, 0}, {&Truth(), wrong_sa, 1, 0}};
  matches = {{&Truth(), kSa, 3, 0}, {&Other(), kSa, 2, 0}};
  EXPECT_EQ(DiagnoseJoint(Truth(), kSa, matches, trace).outcome, "correct_raag_and_sa");
}

TEST(JointDiagnostic, IgnoresOctaveButNotSemitoneAndUsesStrictTolerance) {
  for (double offset : {0.0, 49.0, 51.0, 100.0}) {
    const double candidate = 2 * kSa * std::exp2(offset / 1200);
    IdentifyTrace trace{{{candidate, 1}}, {{&Truth(), candidate, 1, 0}}};
    const auto result = DiagnoseJoint(Truth(), kSa, trace.hypotheses, trace);
    EXPECT_EQ(result.reference_candidate_present, offset < 50);
    EXPECT_EQ(result.winner_sa_correct, offset < 50);
    EXPECT_NEAR(*result.nearest_candidate_error_cents, offset, 1e-9);
  }
}

TEST(JointDiagnostic, ExposesTiedReferencePairRatherThanCallingItUnique) {
  IdentifyTrace trace{{{kSa, 1}}, {{&Other(), kSa, 3, 0}, {&Truth(), kSa, 3, 0}}};
  const auto result = DiagnoseJoint(Truth(), kSa, trace.hypotheses, trace);
  EXPECT_EQ(result.outcome, "wrong_raag_reference_sa");
  EXPECT_EQ(result.reference_pair_rank, 1u);
  EXPECT_EQ(result.reference_pair_ties, 2u);
  EXPECT_DOUBLE_EQ(*result.runner_up_gap, 0);
  EXPECT_DOUBLE_EQ(*result.reference_pair_gap, 0);
}

TEST(JointDiagnostic, HandlesNoPredictionAndUnknownReferenceWithoutInventedErrors) {
  const auto missing = DiagnoseJoint(Truth(), kSa, {}, {});
  EXPECT_EQ(missing.outcome, "no_prediction");
  EXPECT_FALSE(missing.nearest_candidate_error_cents);
  EXPECT_FALSE(missing.winner_sa_correct);
  EXPECT_FALSE(missing.reference_pair_rank);
  IdentifyTrace trace{{{kSa, 1}}, {{&Truth(), kSa, 1, 0}}};
  const auto unknown = DiagnoseJoint(Truth(), 0, trace.hypotheses, trace);
  EXPECT_EQ(unknown.outcome, "no_reference_sa");
  EXPECT_TRUE(unknown.winner_raag_correct);
  EXPECT_FALSE(unknown.winner_sa_correct);
}

TEST(JointDiagnostic, JsonEscapesFilenamesAndPreservesNullsAndAllPairs) {
  IdentifyTrace trace{{{kSa, 1}}, {{&Truth(), kSa, 1, 0}, {&Other(), kSa, 0, 1}}};
  auto result = DiagnoseJoint(Truth(), 0, trace.hypotheses, trace);
  std::ostringstream out;
  WriteJointDiagnostic(out, "a\"b\\c\n.wav", Truth(), 0, trace.hypotheses, trace, {}, result);
  EXPECT_NE(out.str().find("a\\\"b\\\\c\\u000a.wav"), std::string::npos);
  EXPECT_NE(out.str().find("\"reference_sa_hz\":null"), std::string::npos);
  EXPECT_NE(out.str().find("\"reference_candidate_present\":null"), std::string::npos);
  EXPECT_NE(out.str().find("\"supplied_sa_truth_rank\":null"), std::string::npos);
  EXPECT_NE(out.str().find("\"hypotheses\":[{\"raag\":\"Yaman\""), std::string::npos);
  EXPECT_NE(out.str().find("{\"raag\":\"Bhairav\""), std::string::npos);
}

}  // namespace
}  // namespace raagasense::evaluation
