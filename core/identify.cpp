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
// share of the held time, and what a svara the raag doesn't use costs when it
// gets at least that much. Without the second, raags with few svaras win too
// often: they make each of their own likelier, and the model expects some
// held time off any raag's svaras anyway.
constexpr double kMissingPenalty = 0.4;
constexpr double kMissingShare = 0.01;
// How much EstimateTonic's score for the Sa (the share of held time on it and
// its Pa) adds to a match's score.
constexpr double kSaPaWeight = 0.5;
// How much the svaras passed through going up and down count, against the
// model with no direction. On the 59 labeled recordings, weights from 4 to 16
// put the true raag in the top 3 on 33 to 37, against 30 without; 8 is best.
constexpr double kDirectionWeight = 8;
// What naming a rarely performed raag costs, so that it takes more evidence
// than a common one with the same svaras. With 122 raags, 28 of them rare,
// penalties from 0.1 to 0.4 name 20 of the 59 labeled recordings first,
// against 18 with none.
constexpr double kRarePenalty = 0.2;
// Passing notes further apart than this, in seconds, aren't in one phrase.
constexpr double kPhraseGap = 0.6;
// A move between passing notes is up or down when it's at least this many
// cents.
constexpr double kMoveCents = 50;

// The share of the time on each svara from a Sa, of notes, and of passing
// notes passed through going up (between a lower and a higher neighbor) and
// going down.
struct Shares {
  double held[12] = {};
  double up[12] = {};
  double down[12] = {};
};

// The model's likelihood for each svara: the raag's svaras, or only those in
// `allowed` if given, by weight, and the rest sharing kOffRaag.
void Likelihoods(const Raag& raag, const double (&weight)[12], const std::vector<int>& allowed, double (&out)[12]) {
  double kept[12] = {}, total = 0;
  int left_out = 0;
  for (int svara = 0; svara < 12; ++svara) {
    const bool in = allowed.empty() || std::find(allowed.begin(), allowed.end(), svara) != allowed.end();
    kept[svara] = in ? weight[svara] : 0;
    total += kept[svara];
    left_out += kept[svara] == 0;
  }
  for (int svara = 0; svara < 12; ++svara) {
    out[svara] = kept[svara] > 0 ? (1 - kOffRaag) * kept[svara] / total : kOffRaag / left_out;
  }
}

// The score of the raag from a Sa with these shares, and this score from
// EstimateTonic.
double Score(const Raag& raag, const Shares& shares, double sa_and_pa) {
  double weight[12] = {};
  for (int svara : raag.svaras) {
    const bool occasional = std::find(raag.occasional.begin(), raag.occasional.end(), svara) != raag.occasional.end();
    weight[svara] = (occasional ? kOccasionalWeight : 1) * (svara == raag.vadi ? kVadiWeight : 1);
  }
  double likely[12], up[12], down[12];
  Likelihoods(raag, weight, {}, likely);
  Likelihoods(raag, weight, raag.ascent, up);
  Likelihoods(raag, weight, raag.descent, down);
  double score = kSaPaWeight * sa_and_pa - (raag.rare ? kRarePenalty : 0);
  for (int svara = 0; svara < 12; ++svara) {
    score += shares.held[svara] * std::log(likely[svara]);
    const bool essential =
        weight[svara] > 0 && std::find(raag.occasional.begin(), raag.occasional.end(), svara) == raag.occasional.end();
    if (essential && shares.held[svara] < kMissingShare) score -= kMissingPenalty;
    if (weight[svara] == 0 && shares.held[svara] >= kMissingShare) score -= kMissingPenalty;
    // Where the raag leaves svaras out going up or down, how much likelier the
    // moves are than with no direction.
    score += kDirectionWeight * (shares.up[svara] * std::log(up[svara] / likely[svara]) +
                                 shares.down[svara] * std::log(down[svara] / likely[svara]));
  }
  return score;
}

// Calls score(candidate, shares) for each candidate Sa.
template <typename Function>
void ForEachSa(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing, Function score) {
  double total = 0, passing_total = 0;
  for (const SteadyNote& note : notes) total += note.end - note.start;
  for (const SteadyNote& note : passing) passing_total += note.end - note.start;
  if (total <= 0) return;
  for (const TonicCandidate& candidate : EstimateTonic(notes)) {
    Shares shares;
    for (const SteadyNote& note : notes) {
      shares.held[NearestSvara(CentsFromSa(note.frequency, candidate.frequency)).svara] += (note.end - note.start) / total;
    }
    for (size_t i = 1; i + 1 < passing.size(); ++i) {
      if (passing[i].start - passing[i - 1].end > kPhraseGap || passing[i + 1].start - passing[i].end > kPhraseGap) {
        continue;
      }
      const double cents = CentsFromSa(passing[i].frequency, candidate.frequency);
      const double from = cents - CentsFromSa(passing[i - 1].frequency, candidate.frequency);
      const double to = CentsFromSa(passing[i + 1].frequency, candidate.frequency) - cents;
      const double share = (passing[i].end - passing[i].start) / passing_total;
      const int svara = NearestSvara(cents).svara;
      if (from >= kMoveCents && to >= kMoveCents) shares.up[svara] += share;
      if (from <= -kMoveCents && to <= -kMoveCents) shares.down[svara] += share;
    }
    score(candidate, shares);
  }
}

bool Better(const RaagMatch& a, const RaagMatch& b) { return a.score > b.score; }

}  // namespace

std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing) {
  std::vector<RaagMatch> best;
  ForEachSa(notes, passing, [&](const TonicCandidate& candidate, const Shares& shares) {
    for (const Raag& raag : Raags()) {
      const RaagMatch match{&raag, candidate.frequency, Score(raag, shares, candidate.score)};
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

std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag,
                                 std::span<const SteadyNote> passing) {
  std::vector<RaagMatch> matches;
  ForEachSa(notes, passing, [&](const TonicCandidate& candidate, const Shares& shares) {
    matches.push_back({&raag, candidate.frequency, Score(raag, shares, candidate.score)});
  });
  std::sort(matches.begin(), matches.end(), Better);
  return matches;
}

}  // namespace raagasense
