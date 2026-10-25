#include "core/phrase.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "core/svara.h"

namespace raagasense {

std::vector<int> ParsePhrase(std::string_view notation) {
  std::istringstream in{std::string(notation)};
  std::string token;
  std::vector<int> phrase;
  while (in >> token) {
    size_t at = 0;
    int octave = 0;
    while (at < token.size() && token[at] == '.') { --octave; ++at; }
    if (at == token.size()) return {};
    const int svara = SvaraFromLetter(std::string_view(token).substr(at++, 1));
    if (svara < 0) return {};
    while (at < token.size() && token[at] == '\'') { ++octave; ++at; }
    if (at != token.size()) return {};
    phrase.push_back(svara + 12 * octave);
  }
  return phrase;
}

std::vector<PhraseEvent> PhraseEvents(std::span<const SteadyNote> notes, double sa) {
  std::vector<PhraseEvent> events;
  if (!std::isfinite(sa) || sa <= 0) return events;
  int segment = 0;
  for (const auto& note : notes) {
    if (!std::isfinite(note.frequency) || note.frequency <= 0) continue;
    if (!events.empty() && note.start - events.back().end > 0.6) ++segment;
    const auto position = NearestSvara(CentsFromSa(note.frequency, sa));
    events.push_back({position.svara + 12 * position.octave, note.start, note.end, segment});
  }
  return events;
}

double MatchPhrase(std::span<const int> phrase, std::span<const PhraseEvent> events) {
  if (phrase.empty() || events.size() < phrase.size()) return 0;
  constexpr double unreachable = -std::numeric_limits<double>::infinity();
  double best = 0;
  for (size_t start = 0; start < events.size(); ++start) {
    const int shift = events[start].semitone - phrase[0];
    if (shift % 12 != 0) continue;
    size_t count = std::min(events.size() - start, 1 + 3 * (phrase.size() - 1));
    for (size_t i = 0; i < count; ++i) {
      if (events[start + i].segment != events[start].segment || events[start + i].end - events[start].start > 20) {
        count = i;
        break;
      }
    }
    if (count < phrase.size()) continue;
    std::vector<double> scores(count, unreachable), next(count, unreachable);
    scores[0] = 0;
    // Dynamic programming retains alternative alignments. Taking the first
    // compatible note greedily can miss a later, complete occurrence.
    for (size_t p = 1; p < phrase.size(); ++p) {
      std::fill(next.begin(), next.end(), unreachable);
      for (size_t at = 0; at < count; ++at) {
        if (!std::isfinite(scores[at])) continue;
        for (size_t to = at + 1; to < std::min(count, at + 4); ++to) {
          if (events[start + to].semitone != phrase[p] + shift) continue;
          next[to] = std::max(next[to], scores[at] - 0.5 * (to - at - 1));
        }
      }
      scores.swap(next);
    }
    const double quality = std::exp(*std::max_element(scores.begin(), scores.end()));
    best = std::max(best, quality);
    if (best == 1) return best;
  }
  return best;
}

std::vector<PhraseEvent> ShuffledPhraseEvents(std::span<const PhraseEvent> events, unsigned variant) {
  std::vector<PhraseEvent> shuffled(events.begin(), events.end());
  // Explicit generator and Fisher-Yates indexing keep results independent
  // of the standard library's shuffle implementation.
  uint64_t state = 0x9e3779b97f4a7c15ULL * (static_cast<uint64_t>(variant) + 1);
  size_t first = 0;
  while (first < shuffled.size()) {
    size_t end = first + 1;
    while (end < shuffled.size() && shuffled[end].segment == shuffled[first].segment) ++end;
    for (size_t i = end; i > first + 1; --i) {
      state ^= state >> 12;
      state ^= state << 25;
      state ^= state >> 27;
      const uint64_t random = state * 2685821657736338717ULL;
      const size_t other = first + random % (i - first);
      std::swap(shuffled[i - 1].semitone, shuffled[other].semitone);
    }
    first = end;
  }
  return shuffled;
}

}  // namespace raagasense
