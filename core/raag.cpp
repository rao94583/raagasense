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
          std::string_view samvadi, std::vector<std::string> aliases = {}, std::string_view ascent = "",
          std::string_view descent = "") {
  return {std::move(name),
          Svaras(svaras),
          Svaras(occasional),
          vadi.empty() ? -1 : SvaraFromLetter(vadi),
          samvadi.empty() ? -1 : SvaraFromLetter(samvadi),
          std::move(aliases),
          Svaras(ascent),
          Svaras(descent)};
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
      Make("Abhogi", "S R g m D", "", "m", "S", {"Abhogi Kanada"}),
      Make("Adana", "S R g m P d n", "", "S", "P", {}, "S R m P d n", ""),
      Make("Ahir Bhairav", "S r G m P D n", "", "m", "S"),
      Make("Asavari", "S R g m P d n", "", "d", "g", {}, "S R m P d", ""),
      Make("Asavari (komal Re)", "S r g m P d n", "", "d", "g", {"Komal Rishabh Asavari"}, "S r m P d", ""),
      Make("Bageshree", "S R g m P D n", "P", "m", "S", {"Bageshri"}, "S g m D n", ""),
      Make("Bahaduri Todi", "S r R g m P d n", "R", "", ""),
      Make("Bahar", "S R g m P D n N", "", "m", "S", {}, "S g m P D n N", ""),
      Make("Bairagi", "S r m P n", "", "r", "P", {"Bairagi Bhairav"}),
      Make("Basant", "S r G m M P d N", "m", "S", "P", {}, "S G M d N", ""),
      Make("Basant Mukhari", "S r G m P d n", "", "m", "S", {"Vasant Mukhari"}),
      Make("Bhairav", "S r G m P d N", "", "d", "r"),
      Make("Bhairavi", "S r g m P d n", "", "m", "S"),
      Make("Bhatiyar", "S r G m M P D N", "M", "m", "S"),
      Make("Bhimpalasi", "S R g m P D n", "", "m", "S", {"Bhimpalas"}, "S g m P n", ""),
      Make("Bhoopali", "S R G P D", "", "G", "D", {"Bhupali", "Bhoop", "Bhup"}),
      Make("Bhupal Todi", "S r g P d", "", "", ""),
      // The Marwa-thaat form; Bhairav-thaat Bibhas has komal Dha.
      Make("Bibhas", "S r G P D", "", "", "", {"Vibhas"}),
      Make("Bihag", "S R G m M P D N", "M", "G", "N", {}, "S G m P N", ""),
      Make("Bihagda", "S R G m P D N", "", "", "", {}, "S G m P N", ""),
      Make("Bilaskhani Todi", "S r g m P d n", "", "d", "g", {}, "S r g P d", ""),
      Make("Bilawal", "S R G m P D n N", "n", "D", "G", {"Alhaiya Bilawal", "Alhaiya Bilaval", "Bilaval"}, "S R G P D N", ""),
      Make("Brindavani Sarang", "S R m P n N", "", "R", "P", {"Vrindavani Sarang"}, "S R m P N", "S R m P n"),
      Make("Chandrakauns", "S g m d N", "", "", ""),
      Make("Chhayanat", "S R G m M P D n N", "M n", "R", "P"),
      Make("Darbari Kanada", "S R g m P d n", "", "R", "P", {"Darbari"}),
      Make("Desh", "S R G m P D n N", "", "R", "P", {}, "S R m P N", "S R G m P D n"),
      Make("Deshi", "S R g m P d D n", "d", "P", "R"),
      Make("Deshkar", "S R G P D", "", "D", "G"),
      Make("Devgiri Bilawal", "S R G m P D N", "", "", "", {"Devgiri Bilaval"}),
      Make("Dhanashree", "S R g m P D n", "", "P", "S", {}, "S g m P n", ""),
      // A rarer form in some gharanas, sung by Ulhas Kashalkar among others.
      Make("Dhanashree (Bilawal thaat)", "S R G m P D N", "", "", "", {}, "S G m P N", ""),
      Make("Dhani", "S g m P n", "", "g", "n"),
      Make("Durga", "S R m P D", "", "m", "S"),
      Make("Gaud Malhar", "S R G m P D n N", "n", "m", "S"),
      Make("Gaud Sarang", "S R G m M P D N", "", "G", "D"),
      Make("Gorakh Kalyan", "S R m P D n", "P", "m", "S"),
      Make("Gunakri", "S r m P d", "", "d", "r", {"Gunkali"}),
      Make("Gurjari Todi", "S r g M d N", "", "d", "g", {"Gujari Todi"}),
      Make("Hamir", "S R G m M P D N", "", "D", "G", {"Hameer"}),
      Make("Hansadhwani", "S R G P N", "", "R", "P", {"Hansadhvani", "Hamsadhwani"}),
      Make("Hindol", "S G M D N", "N", "D", "G", {}, "S G M D", ""),
      Make("Jaijaivanti", "S R g G m P D n N", "g n", "R", "P", {"Jaijaiwanti", "Jaijawanti"}),
      Make("Jaitshri", "S r G M P d N", "", "G", "N", {"Jaitashri"}, "S G P N", ""),
      Make("Jaunpuri", "S R g m P d n", "", "d", "g", {}, "S R m P d", ""),
      Make("Jhinjhoti", "S R G m P D n", "", "G", "", {}, "S R m P D", ""),
      Make("Jog", "S g G m P n", "", "S", "P", {}, "S G m P n", ""),
      Make("Jogiya", "S r G m P d N", "G N", "m", "S", {}, "S r m P d", ""),
      Make("Jogkauns", "S g G m P d n N", "n", "m", "S"),
      Make("Kafi", "S R g m P D n", "", "P", "S"),
      Make("Kalashri", "S R G m P D n", "", "", "", {}, "S G m P D n", ""),
      Make("Kalavati", "S G P D n", "", "P", "S"),
      Make("Kamod", "S R G m M P D N", "M", "P", "R"),
      Make("Kedar", "S R G m M P D n N", "G n", "m", "S", {}, "S m M P D N", ""),
      Make("Khamaj", "S R G m P D n N", "", "G", "N", {}, "S G m P D N", "S R G m P D n"),
      Make("Khat", "S r R g G m P d D n", "r", "P", "S"),
      Make("Lalit", "S r G m M d N", "", "m", "S", {"Lalat"}),
      Make("Lalita Gauri", "S r G m M P d N", "m", "", ""),
      Make("Lalita Sohini", "S r G m M D N", "m", "", ""),
      Make("Madhmad Sarang", "S R m P n", "", "R", "P", {"Madhyamad Sarang"}),
      Make("Madhuvanti", "S R g M P D N", "", "P", "S", {}, "S g M P N", ""),
      Make("Malkauns", "S g m d n", "", "m", "S", {"Malkosh"}),
      Make("Maru Bihag", "S R G m M P D N", "m", "G", "N", {}, "S G m M P N", ""),
      Make("Marwa", "S r G M D N", "", "r", "D", {"Marva"}),
      Make("Megh", "S R m P n", "", "S", "P", {"Megh Malhar"}),
      Make("Miyan ki Malhar", "S R g m P D n N", "g", "m", "S", {"Miyan Malhar"}),
      Make("Miyan ki Todi", "S r g M P d N", "P", "d", "g", {"Todi"}, "S r g M d N", ""),
      Make("Multani", "S r g M P d N", "r d", "P", "S", {}, "S g M P N", ""),
      Make("Nand", "S R G m M P D N", "M", "S", "P", {"Anandi", "Nand Kalyan"}),
      Make("Nat Bhairav", "S R G m P d N", "", "m", "S"),
      Make("Nayaki Kanada", "S R g m P n", "", "m", "S"),
      Make("Patdeep", "S R g m P D N", "", "P", "S", {"Patdip"}, "S g m P N", ""),
      Make("Pilu", "S R g G m P d D n N", "d", "g", "n"),
      Make("Puriya", "S r G M D N", "", "G", "N"),
      Make("Puriya Dhanashree", "S r G M P d N", "", "P", "r", {"Puriya Dhanashri"}),
      Make("Purvi", "S r G m M P d N", "m", "G", "N", {}, "S r G m M d N", ""),
      Make("Rageshree", "S R G m D n", "R", "", "", {"Rageshri", "Rageshwari"}, "S G m D n", ""),
      Make("Ramkali", "S r G m M P d n N", "M n", "P", "S"),
      Make("Shahana", "S R g G m P D n N", "G N", "", "", {"Shahana Kanada"}),
      Make("Shankara", "S R G P D N", "R", "G", "N", {}, "S G P D N", ""),
      Make("Shivmat Bhairav", "S r g G m P d n N", "g n", "", "", {"Shiv Bhairav"}),
      Make("Shri", "S r G M P d N", "", "r", "P", {"Shree"}, "S r M P N", ""),
      Make("Shuddh Kalyan", "S R G M P D N", "M N", "G", "D", {}, "S R G P D", ""),
      Make("Shuddh Sarang", "S R m M P D N", "", "R", "P", {}, "S R M P N", ""),
      Make("Shyam Kalyan", "S R G m M P D N", "m", "", "", {}, "S R m M P N", ""),
      Make("Sohini", "S r G M D N", "", "D", "G", {"Sohni"}),
      Make("Sur Malhar", "S R m P D n N", "", "P", "S", {"Surdasi Malhar"}, "S R m P n N", ""),
      Make("Tilak Kamod", "S R G m P D n N", "n", "S", "P", {}, "S R m P N", ""),
      Make("Tilang", "S G m P n N", "", "G", "N", {}, "S G m P N", "S G m P n"),
      Make("Yaman", "S R G M P D N", "", "G", "N", {"Kalyan", "Iman"}),
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
    for (const std::string& alias : raag.aliases) {
      if (Normalize(alias) == wanted) return &raag;
    }
  }
  return nullptr;
}

}  // namespace raagasense
