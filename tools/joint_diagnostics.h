// Diagnostics of the production joint raag/tonic search against reference labels.
#pragma once

#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include "core/identify.h"

namespace raagasense::evaluation {

struct JointDiagnostic {
  std::string outcome;
  bool reference_candidate_present = false;
  std::optional<double> nearest_candidate_error_cents;
  std::optional<size_t> reference_candidate_rank;
  bool winner_raag_correct = false;
  std::optional<bool> winner_sa_correct;
  std::optional<double> winner_sa_error_cents;
  std::optional<double> runner_up_gap;
  std::optional<RaagMatch> reference_pair;
  // Competition rank: 1 + the number of strictly higher scores. Exact/near
  // numerical ties (1e-12) are counted separately, including the pair itself.
  std::optional<size_t> reference_pair_rank;
  size_t reference_pair_ties = 0;
  std::optional<double> reference_pair_gap;
};

// Sa agreement ignores octaves and requires <50 cents, matching evaluate's
// existing tonic metric. A reference pair has the labeled raag and a proposed
// Sa within that tolerance. No reference tonic is injected into the search.
JointDiagnostic DiagnoseJoint(const Raag& truth, double reference_sa,
                              std::span<const RaagMatch> matches, const IdentifyTrace& trace);

// One JSON object per recording, followed by a newline. Includes all proposals,
// all scored pairs, the actual selected winner, diagnostic fields, and the
// separate supplied-Sa result. Missing values are null, not zero or a miss.
void WriteJointDiagnostic(std::ostream& out, std::string_view file, const Raag& truth, double reference_sa,
                           std::span<const RaagMatch> matches, const IdentifyTrace& trace,
                           std::span<const RaagMatch> supplied_sa_matches, const JointDiagnostic& diagnostic);

}  // namespace raagasense::evaluation
