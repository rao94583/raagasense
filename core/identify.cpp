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

}  // namespace

std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes) {
  double total = 0;
  for (const SteadyNote& note : notes) total += note.end - note.start;
  std::vector<RaagMatch> best;
  if (total <= 0) return best;

  for (const TonicCandidate& candidate : EstimateTonic(notes)) {
    // The share of held time on each svara from this Sa.
    double share[12] = {};
    for (const SteadyNote& note : notes) {
      share[NearestSvara(CentsFromSa(note.frequency, candidate.frequency)).svara] += (note.end - note.start) / total;
    }
    for (const Raag& raag : Raags()) {
      double weight[12] = {}, total_weight = 0;
      for (int svara : raag.svaras) {
        const bool occasional = std::find(raag.occasional.begin(), raag.occasional.end(), svara) != raag.occasional.end();
        weight[svara] = (occasional ? kOccasionalWeight : 1) * (svara == raag.vadi ? kVadiWeight : 1);
        total_weight += weight[svara];
      }
      const double out = 12.0 - raag.svaras.size();
      double score = kSaPaWeight * candidate.score;
      for (int svara = 0; svara < 12; ++svara) {
        score += share[svara] * std::log(weight[svara] > 0 ? (1 - kOffRaag) * weight[svara] / total_weight : kOffRaag / out);
        const bool essential = weight[svara] > 0 &&
            std::find(raag.occasional.begin(), raag.occasional.end(), svara) == raag.occasional.end();
        if (essential && share[svara] < kMissingShare) score -= kMissingPenalty;
      }
      auto match = std::find_if(best.begin(), best.end(), [&](const RaagMatch& m) { return m.raag == &raag; });
      if (match == best.end()) {
        best.push_back({&raag, candidate.frequency, score});
      } else if (score > match->score) {
        *match = {&raag, candidate.frequency, score};
      }
    }
  }
  std::sort(best.begin(), best.end(), [](const RaagMatch& a, const RaagMatch& b) { return a.score > b.score; });
  return best;
}

}  // namespace raagasense
