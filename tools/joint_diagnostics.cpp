#include "tools/joint_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <type_traits>

#include "core/svara.h"

namespace raagasense::evaluation {
namespace {
constexpr double kTieTolerance = 1e-12;

bool ValidSa(double sa) { return std::isfinite(sa) && sa > 0; }
double Error(double sa, double reference) { return std::remainder(CentsFromSa(sa, reference), 1200); }
bool Agrees(double sa, double reference) { return ValidSa(sa) && std::abs(Error(sa, reference)) < 50; }

void String(std::ostream& out, std::string_view value) {
  out << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
    else out << c;
  }
  out << '"';
}
void Number(std::ostream& out, double value) {
  if (std::isfinite(value)) out << std::setprecision(17) << value;
  else out << "null";
}
template <typename T>
void Optional(std::ostream& out, const std::optional<T>& value) {
  if (!value) out << "null";
  else if constexpr (std::is_same_v<T, bool>) out << (*value ? "true" : "false");
  else Number(out, *value);
}
void Match(std::ostream& out, const RaagMatch& match) {
  out << "{\"raag\":"; String(out, match.raag->name);
  out << ",\"sa_hz\":"; Number(out, match.sa);
  out << ",\"score\":"; Number(out, match.score);
  out << ",\"foreign_svaras\":" << match.foreign_svaras << '}';
}
}  // namespace

JointDiagnostic DiagnoseJoint(const Raag& truth, double reference_sa,
                              std::span<const RaagMatch> matches, const IdentifyTrace& trace) {
  JointDiagnostic result;
  if (!matches.empty()) result.winner_raag_correct = matches.front().raag == &truth;
  if (trace.hypotheses.size() > 1 && !matches.empty()) {
    result.runner_up_gap = matches.front().score - trace.hypotheses[1].score;
  }
  if (!ValidSa(reference_sa)) {
    result.outcome = "no_reference_sa";
    return result;
  }
  for (size_t i = 0; i < trace.candidates.size(); ++i) {
    const double error = Error(trace.candidates[i].frequency, reference_sa);
    if (!result.nearest_candidate_error_cents || std::abs(error) < std::abs(*result.nearest_candidate_error_cents)) {
      result.nearest_candidate_error_cents = error;
      result.reference_candidate_rank = std::abs(error) < 50 ? std::optional<size_t>(i + 1) : std::nullopt;
    }
    result.reference_candidate_present |= std::abs(error) < 50;
  }
  for (const auto& pair : trace.hypotheses) {
    if (pair.raag == &truth && Agrees(pair.sa, reference_sa) &&
        (!result.reference_pair || pair.score > result.reference_pair->score)) result.reference_pair = pair;
  }
  if (result.reference_pair) {
    size_t higher = 0;
    for (const auto& pair : trace.hypotheses) {
      higher += pair.score > result.reference_pair->score + kTieTolerance;
      result.reference_pair_ties += std::abs(pair.score - result.reference_pair->score) <= kTieTolerance;
    }
    result.reference_pair_rank = higher + 1;
    if (!matches.empty()) result.reference_pair_gap = matches.front().score - result.reference_pair->score;
  }
  if (matches.empty()) {
    result.outcome = "no_prediction";
    return result;
  }
  result.winner_sa_error_cents = Error(matches.front().sa, reference_sa);
  result.winner_sa_correct = Agrees(matches.front().sa, reference_sa);
  if (!result.reference_candidate_present) result.outcome = "reference_sa_not_proposed";
  else if (result.winner_raag_correct) {
    result.outcome = *result.winner_sa_correct ? "correct_raag_and_sa" : "correct_raag_wrong_sa";
  } else {
    result.outcome = *result.winner_sa_correct ? "wrong_raag_reference_sa" : "wrong_raag_wrong_sa";
  }
  return result;
}

void WriteJointDiagnostic(std::ostream& out, std::string_view file, const Raag& truth, double reference_sa,
                           std::span<const RaagMatch> matches, const IdentifyTrace& trace,
                           std::span<const RaagMatch> supplied_sa_matches, const JointDiagnostic& d) {
  out << "{\"type\":\"recording\",\"file\":"; String(out, file);
  out << ",\"truth_raag\":"; String(out, truth.name);
  out << ",\"reference_sa_hz\":"; Number(out, ValidSa(reference_sa) ? reference_sa : std::numeric_limits<double>::quiet_NaN());
  out << ",\"outcome\":"; String(out, d.outcome);
  out << ",\"candidate_count\":" << trace.candidates.size();
  out << ",\"reference_candidate_present\":";
  if (ValidSa(reference_sa)) out << (d.reference_candidate_present ? "true" : "false"); else out << "null";
  out << ",\"nearest_candidate_pitch_class_error_cents\":"; Optional(out, d.nearest_candidate_error_cents);
  out << ",\"reference_candidate_rank\":"; Optional(out, d.reference_candidate_rank);
  out << ",\"winner\":";
  if (matches.empty()) out << "null"; else Match(out, matches.front());
  out << ",\"winner_raag_correct\":" << (d.winner_raag_correct ? "true" : "false");
  out << ",\"winner_sa_correct\":"; Optional(out, d.winner_sa_correct);
  out << ",\"winner_sa_pitch_class_error_cents\":"; Optional(out, d.winner_sa_error_cents);
  out << ",\"runner_up_gap\":"; Optional(out, d.runner_up_gap);
  out << ",\"reference_pair\":";
  if (d.reference_pair) Match(out, *d.reference_pair); else out << "null";
  out << ",\"reference_pair_rank\":"; Optional(out, d.reference_pair_rank);
  out << ",\"reference_pair_ties\":" << d.reference_pair_ties;
  out << ",\"reference_pair_gap\":"; Optional(out, d.reference_pair_gap);
  out << ",\"supplied_sa_winner\":";
  if (supplied_sa_matches.empty()) out << "null"; else Match(out, supplied_sa_matches.front());
  out << ",\"supplied_sa_truth_rank\":";
  const auto truth_at = std::find_if(supplied_sa_matches.begin(), supplied_sa_matches.end(),
                                    [&](const auto& pair) { return pair.raag == &truth; });
  if (truth_at == supplied_sa_matches.end()) out << "null"; else out << (truth_at - supplied_sa_matches.begin() + 1);
  out << ",\"candidates\":[";
  for (size_t i = 0; i < trace.candidates.size(); ++i) {
    if (i) out << ',';
    const auto& candidate = trace.candidates[i];
    out << "{\"proposal_rank\":" << i + 1 << ",\"sa_hz\":"; Number(out, candidate.frequency);
    out << ",\"sa_pa_score\":"; Number(out, candidate.score);
    out << '}';
  }
  out << "],\"hypotheses\":[";
  for (size_t i = 0; i < trace.hypotheses.size(); ++i) {
    if (i) out << ',';
    Match(out, trace.hypotheses[i]);
  }
  out << "]}\n";
}

}  // namespace raagasense::evaluation
