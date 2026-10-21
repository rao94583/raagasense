#include "core/raag.h"

#include <set>
#include <string>

#include "core/svara.h"
#include "gtest/gtest.h"

namespace raagasense {
namespace {

TEST(Raags, WellFormed) {
  std::string previous;
  for (const Raag& raag : Raags()) {
    EXPECT_LT(previous, raag.name) << "not in alphabetical order";
    previous = raag.name;
    ASSERT_FALSE(raag.svaras.empty()) << raag.name;
    EXPECT_EQ(raag.svaras.front(), kSa) << raag.name;
    for (size_t i = 0; i < raag.svaras.size(); ++i) {
      EXPECT_GE(raag.svaras[i], 0) << raag.name << ": unknown letter";
      if (i > 0) EXPECT_LT(raag.svaras[i - 1], raag.svaras[i]) << raag.name << ": not ascending";
    }
  }
}

TEST(Raags, Find) {
  ASSERT_NE(FindRaag("Miyan ki Todi"), nullptr);
  EXPECT_EQ(FindRaag("miyan_ki_todi"), FindRaag("Miyan ki Todi"));
  EXPECT_EQ(FindRaag("YAMAN")->name, "Yaman");
  EXPECT_EQ(FindRaag("Raag Imaginary"), nullptr);
}

TEST(Raags, Svaras) {
  EXPECT_EQ(FindRaag("Malkauns")->svaras, (std::vector<int>{kSa, kKomalGa, kMa, kKomalDha, kKomalNi}));
  EXPECT_EQ(FindRaag("Yaman")->svaras, (std::vector<int>{kSa, kRe, kGa, kTivraMa, kPa, kDha, kNi}));
}

}  // namespace
}  // namespace raagasense
