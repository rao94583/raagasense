// What the app knows about each raag. For now, its svaras; emphasis and
// characteristic phrases come next.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace raagasense {

struct Raag {
  std::string name;
  // Svara values, in ascending order: every svara the raag uses, including
  // ones it only touches.
  std::vector<int> svaras;
};

// Every raag the app knows, in alphabetical order.
std::span<const Raag> Raags();

// The raag with this name, ignoring case, spaces, underscores and hyphens
// ("Miyan ki Todi", "miyan_ki_todi"); nullptr if there's none.
const Raag* FindRaag(std::string_view name);

}  // namespace raagasense
