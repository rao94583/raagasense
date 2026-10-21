// What the app knows about each raag: its svaras, which of them it uses only
// sparingly, and its vadi and samvadi. The list covers the raags of the
// NCPA's AUTRIM project (autrimncpa.wordpress.com), whose notes on each raag
// were checked for the svaras, and a few more; characteristic phrases come
// later.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace raagasense {

struct Raag {
  // The common name, followed for a less common form of a raag by what tells
  // it apart: "Dhanashree (Bilawal thaat)".
  std::string name;
  // Svara values, in ascending order: every svara the raag uses, including
  // ones it only touches.
  std::vector<int> svaras;
  // The svaras it uses only sparingly, which a performance can leave out.
  std::vector<int> occasional;
  // The most and second most important svaras, after Bhatkhande; -1 where
  // not recorded.
  int vadi = -1;
  int samvadi = -1;
  // Other names and spellings it goes by.
  std::vector<std::string> aliases;
};

// Every raag the app knows, in alphabetical order.
std::span<const Raag> Raags();

// The raag with this name or alias, ignoring case, spaces, underscores and
// hyphens ("Miyan ki Todi", "miyan_ki_todi", "Todi"); nullptr if there's none.
const Raag* FindRaag(std::string_view name);

}  // namespace raagasense
