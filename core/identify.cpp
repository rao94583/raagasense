#include "core/identify.h"

#include <algorithm>
#include <cmath>

#include "core/svara.h"
#include "core/tonic.h"

namespace raagasense {

namespace {

// The share of held time the model expects off the raag's svaras: slips,
// grace notes and pitch tracking errors.
constexpr double kOffRaag = 0.03;
// How likely, relative to its other svaras, the model takes a raag's
// occasional svaras and its vadi to be held.
constexpr double kOccasionalWeight = 0.25;
constexpr double kVadiWeight = 1.5;
// What a svara the raag can't do without costs when it gets less than this
// share of the held time.
constexpr double kMissingPenalty = 0.4;
constexpr double kMissingShare = 0.01;
// How much EstimateTonic's score for the Sa (the share of held time on it and
// its Pa) adds to a match's score.
constexpr double kSaPaWeight = 0.5;

// The score of the raag from a Sa with this share of the held time on each
// svara, and this score from EstimateTonic.
double Score(const Raag& raag, const double (&share)[12], double sa_and_pa) {
  double weight[12] = {}, total_weight = 0;
  for (int svara : raag.svaras) {
    const bool occasional = std::find(raag.occasional.begin(), raag.occasional.end(), svara) != raag.occasional.end();
    weight[svara] = (occasional ? kOccasionalWeight : 1) * (svara == raag.vadi ? kVadiWeight : 1);
    total_weight += weight[svara];
  }
  const double out = 12.0 - raag.svaras.size();
  double score = kSaPaWeight * sa_and_pa;
  for (int svara = 0; svara < 12; ++svara) {
    score += share[svara] * std::log(weight[svara] > 0 ? (1 - kOffRaag) * weight[svara] / total_weight : kOffRaag / out);
    const bool essential =
        weight[svara] > 0 && std::find(raag.occasional.begin(), raag.occasional.end(), svara) == raag.occasional.end();
    if (essential && share[svara] < kMissingShare) score -= kMissingPenalty;
  }
  return score;
}

// Calls score(candidate, share) for each candidate Sa, with the share of the
// held time on each svara from it.
template <typename Function>
void ForEachSa(std::span<const SteadyNote> notes, Function score) {
  double total = 0;
  for (const SteadyNote& note : notes) total += note.end - note.start;
  if (total <= 0) return;
  for (const TonicCandidate& candidate : EstimateTonic(notes)) {
    double share[12] = {};
    for (const SteadyNote& note : notes) {
      share[NearestSvara(CentsFromSa(note.frequency, candidate.frequency)).svara] += (note.end - note.start) / total;
    }
    score(candidate, share);
  }
}

bool Better(const RaagMatch& a, const RaagMatch& b) { return a.score > b.score; }

}  // namespace

std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes) {
  std::vector<RaagMatch> best;
  ForEachSa(notes, [&](const TonicCandidate& candidate, const double (&share)[12]) {
    for (const Raag& raag : Raags()) {
      const RaagMatch match{&raag, candidate.frequency, Score(raag, share, candidate.score)};
      auto known = std::find_if(best.begin(), best.end(), [&](const RaagMatch& m) { return m.raag == &raag; });
      if (known == best.end()) {
        best.push_back(match);
      } else if (Better(match, *known)) {
        *known = match;
      }
    }
  });
  std::sort(best.begin(), best.end(), Better);
  return best;
}

std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag) {
  std::vector<RaagMatch> matches;
  ForEachSa(notes, [&](const TonicCandidate& candidate, const double (&share)[12]) {
    matches.push_back({&raag, candidate.frequency, Score(raag, share, candidate.score)});
  });
  std::sort(matches.begin(), matches.end(), Better);
  return matches;
}

}  // namespace raagasense
