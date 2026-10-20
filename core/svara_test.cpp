#include "core/svara.h"

#include "gtest/gtest.h"

namespace raagasense {
namespace {

constexpr double kTonic = 138.59;  // C sharp below middle C

TEST(Svara, CentsFromSa) {
  EXPECT_NEAR(CentsFromSa(kTonic, kTonic), 0, 1e-9);
  EXPECT_NEAR(CentsFromSa(2 * kTonic, kTonic), 1200, 1e-9);
  EXPECT_NEAR(CentsFromSa(kTonic / 2, kTonic), -1200, 1e-9);
}

TEST(Svara, JustPaIsPaSlightlySharp) {
  const SvaraPosition pa = NearestSvara(CentsFromSa(1.5 * kTonic, kTonic));
  EXPECT_EQ(pa.svara, kPa);
  EXPECT_EQ(pa.octave, 0);
  EXPECT_NEAR(pa.deviation, 1.955, 0.001);
}

TEST(Svara, Octaves) {
  EXPECT_EQ(NearestSvara(1200).svara, kSa);
  EXPECT_EQ(NearestSvara(1200).octave, 1);
  EXPECT_EQ(NearestSvara(-100).svara, kNi);
  EXPECT_EQ(NearestSvara(-100).octave, -1);
  // Just under Sa rounds up to Sa, not down to mandra Ni.
  EXPECT_EQ(NearestSvara(-20).svara, kSa);
  EXPECT_EQ(NearestSvara(-20).octave, 0);
  EXPECT_NEAR(NearestSvara(-20).deviation, -20, 1e-9);
}

TEST(Svara, Names) {
  EXPECT_EQ(SvaraLetter(kKomalRe), "r");
  EXPECT_EQ(SvaraLetter(kTivraMa), "M");
  EXPECT_EQ(SvaraName(kKomalDha), "Komal Dha");
}

TEST(Svara, Labels) {
  EXPECT_EQ(SvaraLabel(NearestSvara(400)), "G");
  EXPECT_EQ(SvaraLabel(NearestSvara(-100)), "Ṇ");
  EXPECT_EQ(SvaraLabel(NearestSvara(1200)), "Ṡ");
}

}  // namespace
}  // namespace raagasense
