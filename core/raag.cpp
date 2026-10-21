#include "core/raag.h"

#include <cctype>

#include "core/svara.h"

namespace raagasense {

namespace {

// Svaras from Bhatkhande letters separated by spaces.
std::vector<int> Svaras(std::string_view letters) {
  std::vector<int> svaras;
  for (char letter : letters) {
    if (letter != ' ') svaras.push_back(SvaraFromLetter(std::string_view(&letter, 1)));
  }
  return svaras;
}

// A raag from its svaras, the occasional ones among them, and its vadi and
// samvadi, all as Bhatkhande letters; empty vadi and samvadi when not known.
Raag Make(std::string name, std::string_view svaras, std::string_view occasional, std::string_view vadi,
          std::string_view samvadi) {
  return {std::move(name), Svaras(svaras), Svaras(occasional), vadi.empty() ? -1 : SvaraFromLetter(vadi),
          samvadi.empty() ? -1 : SvaraFromLetter(samvadi)};
}

std::string Normalize(std::string_view name) {
  std::string normalized;
  for (char c : name) {
    if (c != ' ' && c != '_' && c != '-') normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return normalized;
}

const std::vector<Raag>& AllRaags() {
  static const std::vector<Raag> raags = {
      Make("Adana", "S R g m P d n", "", "S", "P"),
      Make("Ahir Bhairav", "S r G m P D n", "", "m", "S"),
      Make("Bageshree", "S R g m P D n", "P", "m", "S"),
      Make("Bhairav", "S r G m P d N", "", "d", "r"),
      Make("Bhimpalasi", "S R g m P D n", "", "m", "S"),
      Make("Bhoopali", "S R G P D", "", "G", "D"),
      Make("Bihag", "S R G m M P D N", "M", "G", "N"),
      Make("Bilawal", "S R G m P D n N", "n", "D", "G"),
      Make("Chandrakauns", "S g m d N", "", "", ""),
      Make("Darbari Kanada", "S R g m P d n", "", "R", "P"),
      Make("Desh", "S R G m P D n N", "", "R", "P"),
      Make("Deshkar", "S R G P D", "", "D", "G"),
      Make("Dhanashree", "S R g m P D n", "", "P", "S"),
      // A rarer form in some gharanas, sung by Ulhas Kashalkar among others.
      Make("Dhanashree (Bilawal thaat)", "S R G m P D N", "", "", ""),
      Make("Dhani", "S g m P n", "", "g", "n"),
      Make("Durga", "S R m P D", "", "m", "S"),
      Make("Gurjari Todi", "S r g M d N", "", "d", "g"),
      Make("Kafi", "S R g m P D n", "", "P", "S"),
      Make("Madhmad Sarang", "S R m P n", "", "R", "P"),
      Make("Malkauns", "S g m d n", "", "m", "S"),
      Make("Marwa", "S r G M D N", "", "r", "D"),
      Make("Miyan ki Todi", "S r g M P d N", "P", "d", "g"),
      Make("Multani", "S r g M P d N", "r d", "P", "S"),
      Make("Puriya", "S r G M D N", "", "G", "N"),
      Make("Puriya Dhanashree", "S r G M P d N", "", "P", "r"),
      Make("Ramkali", "S r G m M P d n N", "M n", "P", "S"),
      Make("Sohini", "S r G M D N", "", "D", "G"),
      Make("Tilak Kamod", "S R G m P D n N", "n", "S", "P"),
      Make("Yaman", "S R G M P D N", "", "G", "N"),
      Make("Yaman Kalyan", "S R G m M P D N", "m", "G", "N"),
  };
  return raags;
}

}  // namespace

std::span<const Raag> Raags() { return AllRaags(); }

const Raag* FindRaag(std::string_view name) {
  const std::string wanted = Normalize(name);
  for (const Raag& raag : AllRaags()) {
    if (Normalize(raag.name) == wanted) return &raag;
  }
  return nullptr;
}

}  // namespace raagasense
