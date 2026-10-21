#include "core/raag.h"

#include <algorithm>
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
    auto uses = [&](int svara) {
      return std::find(raag.svaras.begin(), raag.svaras.end(), svara) != raag.svaras.end();
    };
    for (int svara : raag.occasional) EXPECT_TRUE(uses(svara)) << raag.name << ": occasional svara it doesn't use";
    if (raag.vadi >= 0) EXPECT_TRUE(uses(raag.vadi)) << raag.name << ": vadi";
    if (raag.samvadi >= 0) EXPECT_TRUE(uses(raag.samvadi)) << raag.name << ": samvadi";
  }
}

TEST(Raags, NamesAndAliasesAreUnique) {
  for (const Raag& raag : Raags()) {
    EXPECT_EQ(FindRaag(raag.name), &raag) << raag.name;
    for (const std::string& alias : raag.aliases) EXPECT_EQ(FindRaag(alias), &raag) << alias;
  }
}

TEST(Raags, Find) {
  ASSERT_NE(FindRaag("Miyan ki Todi"), nullptr);
  EXPECT_EQ(FindRaag("miyan_ki_todi"), FindRaag("Miyan ki Todi"));
  EXPECT_EQ(FindRaag("YAMAN")->name, "Yaman");
  EXPECT_EQ(FindRaag("Raag Imaginary"), nullptr);
  ASSERT_NE(FindRaag("Dhanashree (Bilawal thaat)"), nullptr);
  EXPECT_NE(FindRaag("Dhanashree (Bilawal thaat)"), FindRaag("Dhanashree"));
  EXPECT_EQ(FindRaag("Bhupali"), FindRaag("Bhoopali"));
  EXPECT_EQ(FindRaag("marva"), FindRaag("Marwa"));
}

TEST(Raags, Svaras) {
  EXPECT_EQ(FindRaag("Malkauns")->svaras, (std::vector<int>{kSa, kKomalGa, kMa, kKomalDha, kKomalNi}));
  EXPECT_EQ(FindRaag("Yaman")->svaras, (std::vector<int>{kSa, kRe, kGa, kTivraMa, kPa, kDha, kNi}));
}

}  // namespace
}  // namespace raagasense
