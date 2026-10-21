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

std::string Normalize(std::string_view name) {
  std::string normalized;
  for (char c : name) {
    if (c != ' ' && c != '_' && c != '-') normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return normalized;
}

const std::vector<Raag>& AllRaags() {
  static const std::vector<Raag> raags = {
      {"Adana", Svaras("S R g m P d n")},
      {"Ahir Bhairav", Svaras("S r G m P D n")},
      {"Bageshree", Svaras("S R g m P D n")},
      {"Bhairav", Svaras("S r G m P d N")},
      {"Bhimpalasi", Svaras("S R g m P D n")},
      {"Bhoopali", Svaras("S R G P D")},
      {"Bihag", Svaras("S R G m M P D N")},
      {"Bilawal", Svaras("S R G m P D n N")},
      {"Chandrakauns", Svaras("S g m d N")},
      {"Darbari Kanada", Svaras("S R g m P d n")},
      {"Desh", Svaras("S R G m P D n N")},
      {"Deshkar", Svaras("S R G P D")},
      {"Dhanashree", Svaras("S R g m P D n")},
      {"Dhani", Svaras("S g m P n")},
      {"Durga", Svaras("S R m P D")},
      {"Gurjari Todi", Svaras("S r g M d N")},
      {"Kafi", Svaras("S R g m P D n")},
      {"Madhmad Sarang", Svaras("S R m P n")},
      {"Malkauns", Svaras("S g m d n")},
      {"Marwa", Svaras("S r G M D N")},
      {"Miyan ki Todi", Svaras("S r g M P d N")},
      {"Multani", Svaras("S r g M P d N")},
      {"Puriya", Svaras("S r G M D N")},
      {"Puriya Dhanashree", Svaras("S r G M P d N")},
      {"Ramkali", Svaras("S r G m M P d n N")},
      {"Sohini", Svaras("S r G M D N")},
      {"Tilak Kamod", Svaras("S R G m P D n N")},
      {"Yaman", Svaras("S R G M P D N")},
      {"Yaman Kalyan", Svaras("S R G m M P D N")},
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
