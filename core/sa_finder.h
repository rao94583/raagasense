// Finds Sa as a performance of a known raag comes in, and says when it's sure
// enough: when there are enough held notes, the best Sa has held its place for
// a while, and either the raag matches clearly better from it than from the
// next best Sa or it has held its place for much longer.
#pragma once

#include <span>
#include <vector>

#include "core/pitch.h"
#include "core/raag.h"
#include "core/steady.h"

namespace raagasense {

struct SaFinderOptions {
  // Held notes needed, in seconds. Played a second at a time through 33
  // labeled recordings, 30 s made it sure of a wrong Sa twice (a Sa's Ma, and
  // a komal Re); 45 s never did, and still got sure on 22 of them.
  double min_held_seconds = 45;
  // How long the best Sa must have stayed the best, in seconds of the
  // stream, with a lead in MatchRaag's score of at least min_margin...
  double stable_seconds = 20;
  double min_margin = 0.1;
  // ...or with any lead.
  double long_stable_seconds = 90;
};

struct SaStatus {
  // Hz; 0 until there are held notes.
  double sa = 0;
  // How far this Sa's MatchRaag score is ahead of the next best Sa's.
  double margin = 0;
  double held_seconds = 0;
  // How long this Sa has been the best, in seconds of the stream.
  double stable_seconds = 0;
  bool confident = false;
};

class SaFinder {
 public:
  // The raag must outlive the finder.
  explicit SaFinder(const Raag& raag, const SaFinderOptions& options = {});

  // Adds frames later than all those added before, and updates the status.
  void Add(std::span<const PitchFrame> frames);
  const SaStatus& status() const { return status_; }

 private:
  const Raag& raag_;
  SaFinderOptions options_;
  std::vector<PitchFrame> frames_;
  SaStatus status_;
  // The stream time when the current best Sa became the best.
  double best_since_ = 0;
};

}  // namespace raagasense
