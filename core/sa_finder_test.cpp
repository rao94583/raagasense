#include "core/sa_finder.h"

#include <cmath>
#include <vector>

#include "core/svara.h"
#include "gtest/gtest.h"

namespace raagasense {
namespace {

constexpr double kSa = 233.08;
constexpr double kHop = 0.01;

// Appends a held note of the given seconds, in cents from kSa, then a rest.
void Hold(std::vector<PitchFrame>* frames, double cents, double seconds) {
  const double start = frames->empty() ? 0 : frames->back().time + kHop;
  for (double t = 0; t < seconds; t += kHop) {
    frames->push_back({start + t, kSa * std::exp2(cents / 1200), 0.9, true});
  }
  for (double t = 0; t < 0.3; t += kHop) frames->push_back({start + seconds + t, 0, 0, false});
}

// Yaman phrases, about 6 s of held notes each; without Dha, 5.5 s.
std::vector<PitchFrame> Yaman(int phrases, bool dha = true) {
  std::vector<PitchFrame> frames;
  for (int i = 0; i < phrases; ++i) {
    for (auto [cents, seconds] : {std::pair{-100, 0.6}, {200, 0.5}, {400, 1.0}, {600, 0.4}, {700, 1.2}, {900, 0.5},
                                  {1100, 0.6}, {0, 1.2}}) {
      if (cents != 900 || dha) Hold(&frames, cents, seconds);
    }
  }
  return frames;
}


const Raag& kYaman = *FindRaag("Yaman");

// The stream time when the finder is done, adding frames a second at a time;
// 0 if never.
double DoneAt(SaFinder& finder, const std::vector<PitchFrame>& frames) {
  for (size_t at = 0; at < frames.size(); at += 100) {
    finder.Add(std::span(frames).subspan(at, std::min<size_t>(100, frames.size() - at)));
    if (finder.status().done) return frames[std::min(at + 99, frames.size() - 1)].time;
  }
  return 0;
}

TEST(SaFinder, NothingYet) {
  SaFinder finder(kYaman);
  EXPECT_EQ(finder.status().sa, 0);
  EXPECT_FALSE(finder.status().confident);
}

TEST(SaFinder, ConfidentOnceThereAreEnoughHeldNotes) {
  const std::vector<PitchFrame> frames = Yaman(12);
  // Without the quick route, and with it.
  SaFinderOptions slow;
  slow.quick_held_seconds = 1e9;
  SaFinder slow_finder(kYaman, slow), finder(kYaman);
  const double slow_at = DoneAt(slow_finder, frames), quick_at = DoneAt(finder, frames);
  const SaStatus& status = finder.status();
  EXPECT_TRUE(status.confident);
  EXPECT_NEAR(std::remainder(CentsFromSa(status.sa, kSa), 1200), 0, 5);
  EXPECT_GT(status.margin, 1);
  // Not before 45 s of held notes, which takes about 60 s with the rests...
  EXPECT_GT(slow_at, 55);
  // ...or, with this clear a lead, 25 s of them and 30 s of the same Sa.
  EXPECT_GT(quick_at, 30);
  EXPECT_LT(quick_at, slow_at);
}

TEST(SaFinder, NotConfidentWithTooFewHeldNotes) {
  const std::vector<PitchFrame> frames = Yaman(3);
  SaFinder finder(kYaman);
  finder.Add(frames);
  EXPECT_NEAR(std::remainder(CentsFromSa(finder.status().sa, kSa), 1200), 0, 5);
  EXPECT_FALSE(finder.status().confident);
}

TEST(SaFinder, FollowsTheSvarasHeard) {
  SaFinder finder(kYaman);
  finder.Add(Yaman(10, /*dha=*/false));
  const SaStatus& status = finder.status();
  EXPECT_EQ(status.missing, std::vector<int>{kDha});
  EXPECT_EQ(status.shares[kDha], 0);
  EXPECT_NEAR(status.shares[kPa], 1.2 * 10 / status.held_seconds, 0.01);
}

TEST(SaFinder, DoneWhenSureUnlessWaitingForSvaras) {
  SaFinderOptions waiting;
  waiting.wait_for_svaras = true;
  // With all the svaras, waiting changes nothing.
  SaFinder all(kYaman), all_waiting(kYaman, waiting);
  const double all_done = DoneAt(all, Yaman(14));
  EXPECT_GT(all_done, 0);
  EXPECT_EQ(DoneAt(all_waiting, Yaman(14)), all_done);
  // Without Dha, waiting holds off for max_wait_seconds once sure.
  SaFinder no_dha(kYaman), no_dha_waiting(kYaman, waiting);
  const double sure = DoneAt(no_dha, Yaman(40, false));
  EXPECT_GT(sure, 0);
  EXPECT_NEAR(DoneAt(no_dha_waiting, Yaman(40, false)), sure + waiting.max_wait_seconds, 1.5);
  EXPECT_EQ(no_dha_waiting.status().missing, std::vector<int>{kDha});
}

}  // namespace
}  // namespace raagasense
