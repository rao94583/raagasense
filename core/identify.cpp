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
      const double in = raag.svaras.size(), out = 12 - in;
      double score = kSaPaWeight * candidate.score;
      for (int svara = 0; svara < 12; ++svara) {
        const bool used = std::find(raag.svaras.begin(), raag.svaras.end(), svara) != raag.svaras.end();
        score += share[svara] * std::log(used ? (1 - kOffRaag) / in : kOffRaag / out);
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
