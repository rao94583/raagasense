#include "core/identify.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

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
// How a raag's profile learned from recordings counts. Its likelihood for
// each svara is the profile's mean share and the model's, weighted by the
// recordings and kProfilePrior; the held time then scores
// kProfileWeight times how much likelier it is under that than under the
// model alone. Leaving each recording's artist out, on 119 labeled
// recordings, this names the raag first on 45 (35 without) and in the top 3
// on 66 (63). It favors raags with recordings a little: the 34 whose raag no
// other artist has recorded go from 15 first to 14. A weight of 2 does better
// on recorded raags (34 against 20) but worse on the others (13).
constexpr double kProfilePrior = 2;
constexpr double kProfileWeight = 1;
// How much the share of a raag's phrases found beyond chance counts. Leaving
// each recording's artist out, on 119 labeled recordings, weights of 0.5 to 1
// name the raag first on 46 or 47 (45 without) and in the top 3 on 73 or 74
// (66), with no loss on raags no other artist has recorded; 1.5 and up do
// worse. Matching phrases with no svaras between their svaras, or with up
// to three, does worse than with up to two.
constexpr double kPhraseWeight = 0.5;
constexpr int kPhraseGap = 2;
// Passing notes further apart than this, in seconds, aren't in one phrase.
constexpr double kPhraseSeconds = 0.6;
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
  // The passing notes as svaras from the Sa, repeats collapsed, with the
  // number of the phrase each is in.
  std::vector<int> sequence;
  std::vector<int> phrase_of;
};

// The passing notes as svaras from a Sa, repeats within a phrase collapsed,
// and the number of the phrase each is in.
void Sequence(std::span<const SteadyNote> passing, double sa, std::vector<int>* sequence, std::vector<int>* phrase_of) {
  int phrase = 0;
  for (size_t i = 0; i < passing.size(); ++i) {
    if (i > 0 && passing[i].start - passing[i - 1].end > kPhraseSeconds) ++phrase;
    const int svara = NearestSvara(CentsFromSa(passing[i].frequency, sa)).svara;
    if (!sequence->empty() && phrase_of->back() == phrase && sequence->back() == svara) continue;
    sequence->push_back(svara);
    phrase_of->push_back(phrase);
  }
}

// Whether a raag's phrase turns up in order in the sequence, within one of
// its phrases, with up to kPhraseGap other svaras between each of its svaras.
bool Occurs(const std::vector<int>& phrase, const std::vector<int>& sequence, const std::vector<int>& phrase_of) {
  for (size_t start = 0; start < sequence.size(); ++start) {
    if (sequence[start] != phrase[0]) continue;
    size_t at = start, found = 1;
    while (found < phrase.size()) {
      size_t next = at + 1;
      const size_t limit = std::min(sequence.size(), at + 2 + kPhraseGap);
      while (next < limit && phrase_of[next] == phrase_of[at] && sequence[next] != phrase[found]) ++next;
      if (next >= limit || phrase_of[next] != phrase_of[at]) break;
      at = next;
      ++found;
    }
    if (found == phrase.size()) return true;
  }
  return false;
}

// The share of the raag's phrases that turn up in the sequence.
double PhraseShare(const Raag& raag, const std::vector<int>& sequence, const std::vector<int>& phrase_of) {
  int found = 0;
  for (const std::vector<int>& phrase : raag.phrases) found += Occurs(phrase, sequence, phrase_of);
  return static_cast<double>(found) / raag.phrases.size();
}

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

// The score of the raag from a Sa with these shares, this score from
// EstimateTonic, and what's been learned, if anything; counts the foreign
// svaras into *foreign_svaras.
double Score(const Raag& raag, const Shares& shares, double sa_and_pa, const RaagLearning* learning,
             int* foreign_svaras) {
  const RaagProfile* profile = nullptr;
  if (learning) {
    const auto found = learning->profiles.find(&raag);
    if (found != learning->profiles.end()) profile = &found->second;
  }
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
    if (weight[svara] == 0 && shares.held[svara] >= kMissingShare) {
      score -= kMissingPenalty;
      ++*foreign_svaras;
    }
    // Where the raag leaves svaras out going up or down, how much likelier the
    // moves are than with no direction.
    score += kDirectionWeight * (shares.up[svara] * std::log(up[svara] / likely[svara]) +
                                 shares.down[svara] * std::log(down[svara] / likely[svara]));
    if (profile && profile->recordings > 0) {
      const double learned = (profile->recordings * profile->held[svara] + kProfilePrior * likely[svara]) /
                             (profile->recordings + kProfilePrior);
      score += kProfileWeight * shares.held[svara] * std::log(learned / likely[svara]);
    }
  }
  if (learning && !raag.phrases.empty()) {
    const auto chance = learning->phrase_chance.find(&raag);
    if (chance != learning->phrase_chance.end() && chance->second.recordings > 0) {
      score += kPhraseWeight * (PhraseShare(raag, shares.sequence, shares.phrase_of) - chance->second.share);
    }
  }
  return score;
}

// Calls score(candidate, shares) for each candidate Sa.
template <typename Function>
void ForEachSa(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing,
               std::span<const TonicCandidate> candidates, Function score) {
  double total = 0, passing_total = 0;
  for (const SteadyNote& note : notes) total += note.end - note.start;
  for (const SteadyNote& note : passing) passing_total += note.end - note.start;
  if (total <= 0) return;
  for (const TonicCandidate& candidate : candidates) {
    Shares shares;
    for (const SteadyNote& note : notes) {
      shares.held[NearestSvara(CentsFromSa(note.frequency, candidate.frequency)).svara] += (note.end - note.start) / total;
    }
    for (size_t i = 1; i + 1 < passing.size(); ++i) {
      if (passing[i].start - passing[i - 1].end > kPhraseSeconds || passing[i + 1].start - passing[i].end > kPhraseSeconds) {
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
    Sequence(passing, candidate.frequency, &shares.sequence, &shares.phrase_of);
    score(candidate, shares);
  }
}

bool Better(const RaagMatch& a, const RaagMatch& b) { return a.score > b.score; }

std::vector<RaagMatch> RankRaags(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing,
                                const RaagLearning* learning, std::span<const TonicCandidate> candidates) {
  std::vector<RaagMatch> best;
  ForEachSa(notes, passing, candidates, [&](const TonicCandidate& candidate, const Shares& shares) {
    for (const Raag& raag : Raags()) {
      RaagMatch match{&raag, candidate.frequency, 0, 0};
      match.score = Score(raag, shares, candidate.score, learning, &match.foreign_svaras);
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

}  // namespace

std::array<double, 12> HeldShares(std::span<const SteadyNote> notes, double sa) {
  std::array<double, 12> held = {};
  double total = 0;
  for (const SteadyNote& note : notes) total += note.end - note.start;
  if (total <= 0) return held;
  for (const SteadyNote& note : notes) {
    held[NearestSvara(CentsFromSa(note.frequency, sa)).svara] += (note.end - note.start) / total;
  }
  return held;
}

void AddToProfile(const std::array<double, 12>& held, RaagProfile* profile) {
  for (int svara = 0; svara < 12; ++svara) {
    profile->held[svara] = (profile->held[svara] * profile->recordings + held[svara]) / (profile->recordings + 1);
  }
  ++profile->recordings;
}

std::map<const Raag*, double> PhraseShares(std::span<const SteadyNote> passing, double sa) {
  std::vector<int> sequence, phrase_of;
  Sequence(passing, sa, &sequence, &phrase_of);
  std::map<const Raag*, double> shares;
  for (const Raag& raag : Raags()) {
    if (!raag.phrases.empty()) shares[&raag] = PhraseShare(raag, sequence, phrase_of);
  }
  return shares;
}

void Learn(const Raag& raag, const std::array<double, 12>& held, const std::map<const Raag*, double>& phrase_shares,
           RaagLearning* learning) {
  AddToProfile(held, &learning->profiles[&raag]);
  for (const auto& [other, share] : phrase_shares) {
    if (other == &raag) continue;
    PhraseChance& chance = learning->phrase_chance[other];
    chance.share = (chance.share * chance.recordings + share) / (chance.recordings + 1);
    ++chance.recordings;
  }
}

std::vector<RaagMatch> IdentifyRaag(std::span<const SteadyNote> notes, std::span<const SteadyNote> passing,
                                    const RaagLearning* learning) {
  return RankRaags(notes, passing, learning, EstimateTonic(notes));
}

std::vector<RaagMatch> IdentifyRaagAtSa(std::span<const SteadyNote> notes, double sa,
                                      std::span<const SteadyNote> passing, const RaagLearning* learning) {
  if (!std::isfinite(sa) || sa <= 0) return {};
  const TonicCandidate candidate{sa, 0};
  return RankRaags(notes, passing, learning, std::span(&candidate, 1));
}

std::vector<RaagMatch> MatchRaag(std::span<const SteadyNote> notes, const Raag& raag,
                                 std::span<const SteadyNote> passing, const RaagLearning* learning) {
  std::vector<RaagMatch> matches;
  ForEachSa(notes, passing, EstimateTonic(notes), [&](const TonicCandidate& candidate, const Shares& shares) {
    RaagMatch match{&raag, candidate.frequency, 0, 0};
    match.score = Score(raag, shares, candidate.score, learning, &match.foreign_svaras);
    matches.push_back(match);
  });
  std::sort(matches.begin(), matches.end(), Better);
  return matches;
}

std::string LearningText(const RaagLearning& learning) {
  // "profile,<raag>,<recordings>,<12 shares>" and "chance,<raag>,<recordings>,<share>".
  std::ostringstream text;
  text.precision(6);
  for (const auto& [raag, profile] : learning.profiles) {
    text << "profile," << raag->name << ',' << profile.recordings;
    for (double share : profile.held) text << ',' << share;
    text << '\n';
  }
  for (const auto& [raag, chance] : learning.phrase_chance) {
    text << "chance," << raag->name << ',' << chance.recordings << ',' << chance.share << '\n';
  }
  return text.str();
}

RaagLearning LearningFromText(std::string_view text) {
  RaagLearning learning;
  std::istringstream lines{std::string(text)};
  std::string line;
  while (std::getline(lines, line)) {
    std::vector<std::string> fields(1);
    for (char c : line) {
      if (c == ',') {
        fields.emplace_back();
      } else {
        fields.back() += c;
      }
    }
    if (fields.size() < 4) continue;
    const Raag* raag = FindRaag(fields[1]);
    if (!raag) continue;
    const int recordings = std::atoi(fields[2].c_str());
    if (fields[0] == "profile" && fields.size() == 15) {
      RaagProfile& profile = learning.profiles[raag];
      profile.recordings = recordings;
      for (int svara = 0; svara < 12; ++svara) profile.held[svara] = std::atof(fields[3 + svara].c_str());
    } else if (fields[0] == "chance") {
      learning.phrase_chance[raag] = {recordings, std::atof(fields[3].c_str())};
    }
  }
  return learning;
}

std::vector<RaagMatch> RaagTracker::Update(std::vector<RaagMatch> matches, double now) {
  if (matches.empty()) return matches;
  auto leader = std::find_if(matches.begin(), matches.end(), [&](const RaagMatch& m) { return m.raag == leader_; });
  bool change = leader == matches.end();
  if (!change && matches[0].raag != leader_) {
    // A foreign svara turning up for the leader is reason enough; otherwise
    // the best must have led it clearly for a while.
    if (leader->foreign_svaras > leader_foreign_) {
      change = true;
    } else if (matches[0].score - leader->score >= options_.margin) {
      if (challenger_ != matches[0].raag) {
        challenger_ = matches[0].raag;
        challenger_since_ = now;
      }
      change = now - challenger_since_ >= options_.seconds;
    } else {
      challenger_ = nullptr;
    }
  }
  if (change) {
    leader_ = matches[0].raag;
    leader_foreign_ = matches[0].foreign_svaras;
    challenger_ = nullptr;
    return matches;
  }
  leader_foreign_ = leader->foreign_svaras;
  std::rotate(matches.begin(), leader, leader + 1);
  return matches;
}

}  // namespace raagasense
