#include "core/sa_finder.h"

#include <algorithm>
#include <cmath>

#include "core/identify.h"
#include "core/svara.h"

namespace raagasense {

SaFinder::SaFinder(const Raag& raag, const SaFinderOptions& options) : raag_(raag), options_(options) {}

void SaFinder::Add(std::span<const PitchFrame> frames) {
  if (frames.empty()) return;
  frames_.insert(frames_.end(), frames.begin(), frames.end());
  const double now = frames_.back().time;

  const std::vector<SteadyNote> notes = HeldPitches(frames_);
  const std::vector<RaagMatch> matches = MatchRaag(notes, raag_, PassingNotes(frames_));
  SaStatus status;
  for (const SteadyNote& note : notes) status.held_seconds += note.end - note.start;
  if (!matches.empty()) {
    status.sa = matches[0].sa;
    status.margin = matches.size() > 1 ? matches[0].score - matches[1].score : 1;
  }
  // The same Sa, octaves aside, keeps its place.
  const bool same = status_.sa > 0 && status.sa > 0 && std::abs(std::remainder(CentsFromSa(status.sa, status_.sa), 1200)) < 50;
  if (!same) best_since_ = now;
  status.stable_seconds = status.sa > 0 ? now - best_since_ : 0;
  status.confident = status.sa > 0 && status.held_seconds >= options_.min_held_seconds &&
                     ((status.margin >= options_.min_margin && status.stable_seconds >= options_.stable_seconds) ||
                      (status.margin > 0 && status.stable_seconds >= options_.long_stable_seconds));
  if (status.confident && !status_.confident) confident_since_ = now;

  if (status.sa > 0) {
    for (const SteadyNote& note : notes) {
      status.shares[NearestSvara(CentsFromSa(note.frequency, status.sa)).svara] +=
          (note.end - note.start) / status.held_seconds;
    }
    for (int svara : raag_.svaras) {
      const bool occasional = std::find(raag_.occasional.begin(), raag_.occasional.end(), svara) != raag_.occasional.end();
      if (!occasional && status.shares[svara] < options_.heard_share) status.missing.push_back(svara);
    }
  }
  status.done = status.confident && (!options_.wait_for_svaras || status.missing.empty() ||
                                     now - confident_since_ >= options_.max_wait_seconds);
  status_ = status;
}

}  // namespace raagasense
