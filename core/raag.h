// What the app knows about each raag: its svaras, which of them it uses only
// sparingly, its vadi and samvadi, which it leaves out going up or down, and
// whether it's seldom performed. The list covers the raags of the NCPA's
// AUTRIM project (autrimncpa.wordpress.com) and common ones from tanarang.com,
// whose notes on each raag were checked for the svaras, and a few more;
// characteristic phrases come later.
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
  // The svaras it passes through going up, and going down, where it leaves
  // some out (Bhimpalasi skips Re and Dha going up); empty where it uses all
  // its svaras, or where that isn't recorded.
  std::vector<int> ascent;
  std::vector<int> descent;
  // Seldom performed, so less likely to be what's playing.
  bool rare = false;
  // Characteristic phrases (RaagPhrases) as svaras, repeats collapsed and
  // octaves left out; only those of three svaras or more.
  std::vector<std::vector<int>> phrases;
};

// Every raag the app knows, in alphabetical order.
std::span<const Raag> Raags();

// The raag with this name or alias, ignoring case, spaces, underscores and
// hyphens ("Miyan ki Todi", "miyan_ki_todi", "Todi"); nullptr if there's none.
const Raag* FindRaag(std::string_view name);

}  // namespace raagasense
