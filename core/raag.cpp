#include "core/raag.h"

#include <algorithm>
#include <cctype>

#include "core/raag_phrases.h"
#include "core/phrase.h"
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

// Raags seldom performed: still known, so a recording can be labeled with
// them, but IdentifyRaag needs more evidence to name them.
bool Rare(std::string_view name) {
  static const std::vector<std::string_view> rare = {
      "Bahaduri Todi",
      "Bairagi Todi",
      "Bihagda",
      "Deshi",
      "Dev Gandhar",
      "Devgiri Bilawal",
      "Dhanashree (Bilawal thaat)",
      "Gunakri",
      "Hemant",
      "Jaldhar Kedar",
      "Janasammohini",
      "Jogeshwari",
      "Kalashri",
      "Kaushik Dhwani",
      "Khambavati",
      "Khat",
      "Lalita Gauri",
      "Lalita Sohini",
      "Lanka Dahan Sarang",
      "Narayani",
      "Nayaki Kanada",
      "Parmeshwari",
      "Ramdasi Malhar",
      "Shivmat Bhairav",
      "Sindhura",
      "Sughrai",
      "Suha",
      "Yamani Bilawal",
  };
  return std::find(rare.begin(), rare.end(), name) != rare.end();
}

// A raag from its svaras, the occasional ones among them, and its vadi and
// samvadi, all as Bhatkhande letters; empty vadi and samvadi when not known.
Raag Make(std::string name, std::string_view svaras, std::string_view occasional, std::string_view vadi,
          std::string_view samvadi, std::vector<std::string> aliases = {}, std::string_view ascent = "",
          std::string_view descent = "") {
  const bool rare = Rare(name);
  return {std::move(name),
          Svaras(svaras),
          Svaras(occasional),
          vadi.empty() ? -1 : SvaraFromLetter(vadi),
          samvadi.empty() ? -1 : SvaraFromLetter(samvadi),
          std::move(aliases),
          Svaras(ascent),
          Svaras(descent),
          rare};
}

std::string Normalize(std::string_view name) {
  std::string normalized;
  for (char c : name) {
    if (c != ' ' && c != '_' && c != '-') normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return normalized;
}

// The raags with their phrases from RaagPhrases, found by name or alias.
std::vector<Raag> WithPhrases(std::vector<Raag> raags) {
  for (const auto& [name, phrases] : RaagPhrases()) {
    for (Raag& raag : raags) {
      bool named = Normalize(raag.name) == Normalize(name);
      for (const std::string& alias : raag.aliases) named = named || Normalize(alias) == Normalize(name);
      if (!named) continue;
      for (const std::string& phrase : phrases) {
        std::vector<int> svaras = ParsePhrase(phrase);
        // Imported notation can include unrelated examples or a different
        // raag variant. Do not use contradictory templates as evidence;
        // retain the source strings for a separate catalog audit.
        const bool compatible = std::all_of(svaras.begin(), svaras.end(), [&](int pitch) {
          const int svara = (pitch % 12 + 12) % 12;
          return std::find(raag.svaras.begin(), raag.svaras.end(), svara) != raag.svaras.end();
        });
        if (svaras.size() >= 3 && compatible &&
            std::find(raag.phrases.begin(), raag.phrases.end(), svaras) == raag.phrases.end()) {
          raag.phrases.push_back(std::move(svaras));
        }
      }
    }
  }
  return raags;
}

const std::vector<Raag>& AllRaags() {
  static const std::vector<Raag> raags = WithPhrases({
      Make("Abhogi", "S R g m D", "", "m", "S", {"Abhogi Kanada"}),
      Make("Adana", "S R g m P d n", "", "S", "P", {}, "S R m P d n", ""),
      Make("Ahir Bhairav", "S r G m P D n", "", "m", "S", {"Aheer Bhairav"}),
      Make("Asavari", "S R g m P d n", "", "d", "g", {}, "S R m P d", ""),
      Make("Asavari (komal Re)", "S r g m P d n", "", "d", "g", {"Komal Rishabh Asavari"}, "S r m P d", ""),
      Make("Bageshree", "S R g m P D n", "P", "m", "S", {"Bageshri"}, "S g m D n", ""),
      Make("Bahaduri Todi", "S r R g m P d n", "R", "", ""),
      Make("Bahar", "S R g m P D n N", "", "m", "S", {}, "S g m P D n N", ""),
      Make("Bairagi", "S r m P n", "", "r", "P", {"Bairagi Bhairav"}),
      Make("Bairagi Todi", "S r g P n", "", "P", "S"),
      Make("Basant", "S r G m M P d N", "", "S", "P", {}, "S G M d N", ""),
      Make("Basant Mukhari", "S r G m P d n", "", "m", "S", {"Vasant Mukhari"}),
      Make("Bhairav", "S r G m P d N", "", "d", "r"),
      Make("Bhairavi", "S r g m P d n", "", "m", "S"),
      Make("Bhatiyar", "S r G m M P D N", "M", "m", "S"),
      Make("Bhimpalasi", "S R g m P D n", "", "m", "S", {"Bhimpalas", "Bheempalasi"}, "S g m P n", ""),
      Make("Bhoopali", "S R G P D", "", "G", "D", {"Bhupali", "Bhoop", "Bhup"}),
      Make("Bhupal Todi", "S r g P d", "", "", ""),
      // The Marwa-thaat form; Bhairav-thaat Bibhas has komal Dha.
      Make("Bibhas", "S r G P D", "", "", "", {"Vibhas"}),
      Make("Bihag", "S R G m M P D N", "M", "G", "N", {}, "S G m P N", ""),
      Make("Bihagda", "S R G m P D N", "", "", "", {}, "S G m P N", ""),
      Make("Bilaskhani Todi", "S r g m P d n", "", "d", "g", {}, "S r g P d", ""),
      Make("Bilawal", "S R G m P D n N", "n", "D", "G", {"Alhaiya Bilawal", "Alhaiya Bilaval", "Bilaval"},
           "S R G P D N", ""),
      Make("Brindavani Sarang", "S R m P n N", "", "R", "P", {"Vrindavani Sarang"}, "S R m P N", "S R m P n"),
      Make("Chandrakauns", "S g m d N", "", "", ""),
      Make("Charukesi", "S R G m P d n", "", "m", "S", {"Charukeshi"}),
      Make("Chhayanat", "S R G m M P D n N", "M n", "R", "P", {"Chhayanut"}),
      Make("Darbari Kanada", "S R g m P d n", "", "R", "P", {"Darbari"}),
      Make("Desh", "S R G m P D n N", "", "R", "P", {"Des"}, "S R m P N", "S R G m P D n"),
      Make("Deshi", "S R g m P d D n", "d", "P", "R"),
      Make("Deshkar", "S R G P D", "", "D", "G"),
      Make("Dev Gandhar", "S R g G m P d n", "", "d", "g", {"Devgandhar"}),
      Make("Devgiri Bilawal", "S R G m P D N", "", "", "", {"Devgiri Bilaval"}),
      Make("Dhanashree", "S R g m P D n", "", "P", "S", {}, "S g m P n", ""),
      // A rarer form in some gharanas, sung by Ulhas Kashalkar among others.
      Make("Dhanashree (Bilawal thaat)", "S R G m P D N", "", "", "", {}, "S G m P N", ""),
      Make("Dhani", "S g m P n", "", "g", "n"),
      Make("Durga", "S R m P D", "", "m", "S"),
      Make("Gara", "S R g G m P D n N", "g", "", ""),
      Make("Gaud Malhar", "S R G m P D n N", "n", "m", "S"),
      Make("Gaud Sarang", "S R G m M P D N", "", "G", "D"),
      Make("Gauri", "S r G m P d N", "", "r", "P"),
      Make("Gorakh Kalyan", "S R m P D n", "P", "m", "S"),
      Make("Gunakri", "S r m P d", "", "d", "r", {"Gunkali"}),
      Make("Gurjari Todi", "S r g M d N", "", "d", "g", {"Gujari Todi"}),
      Make("Hamir", "S R G m M P D N", "", "D", "G", {"Hameer"}),
      Make("Hansadhwani", "S R G P N", "", "R", "P", {"Hansadhvani", "Hamsadhwani", "Hansdhwani"}),
      Make("Hemant", "S R G m P D N", "", "m", "S", {}, "S G m D N", ""),
      Make("Hindol", "S G M D N", "N", "D", "G", {}, "S G M D", ""),
      Make("Jaijaivanti", "S R g G m P D n N", "g n", "R", "P", {"Jaijaiwanti", "Jaijawanti"}),
      Make("Jaitshri", "S r G M P d N", "", "G", "N", {"Jaitashri"}, "S G P N", ""),
      Make("Jaldhar Kedar", "S R m P D", "", "m", "S"),
      Make("Janasammohini", "S R G P D n", "", "", "", {"Jansammohini"}),
      Make("Jaunpuri", "S R g m P d n", "", "d", "g", {}, "S R m P d", ""),
      Make("Jhinjhoti", "S R G m P D n", "", "G", "", {}, "S R m P D", ""),
      Make("Jog", "S g G m P n", "", "S", "P", {}, "S G m P n", ""),
      Make("Jogeshwari", "S g G m D n", "", "m", "S"),
      Make("Jogiya", "S r G m P d N", "G N", "m", "S", {}, "S r m P d", ""),
      Make("Jogkauns", "S g G m P d n N", "n", "m", "S"),
      Make("Kafi", "S R g m P D n", "", "P", "S"),
      Make("Kalashri", "S R G m P D n", "", "", "", {}, "S G m P D n", ""),
      Make("Kalavati", "S G P D n", "", "P", "S", {"Kalawati"}),
      Make("Kalingda", "S r G m P d N", "", "P", "S", {"Kalingada"}),
      Make("Kamod", "S R G m M P D N", "M", "P", "R"),
      Make("Kaunsi Kanada", "S R g m P d n", "", "m", "S", {"Kausi Kanada", "Kaushi Kanada"}, "S g m d n", ""),
      Make("Kaushik Dhwani", "S G m D N", "", "m", "S", {"Bhinna Shadja", "Bhinn Shadj"}),
      Make("Kedar", "S R G m M P D n N", "G n", "m", "S", {}, "S m M P D N", ""),
      Make("Khamaj", "S R G m P D n N", "", "G", "N", {}, "S G m P D N", "S R G m P D n"),
      Make("Khambavati", "S R G m P D n", "", "G", "D", {}, "S R m P D", ""),
      Make("Khat", "S r R g G m P d D n", "r", "P", "S"),
      Make("Kirwani", "S R g m P d N", "", "P", "S", {"Keervani", "Kiravani"}),
      Make("Lalit", "S r G m M d N", "", "m", "S", {"Lalat"}),
      Make("Lalita Gauri", "S r G m M P d N", "", "", ""),
      Make("Lalita Sohini", "S r G m M D N", "", "", ""),
      Make("Lanka Dahan Sarang", "S R g m P D n N", "", "S", "m", {}, "S R m P N", ""),
      Make("Madhmad Sarang", "S R m P n", "", "R", "P", {"Madhyamad Sarang", "Madhumad Sarang"}),
      Make("Madhukauns", "S g M P n", "", "P", "S"),
      Make("Madhuvanti", "S R g M P D N", "", "P", "S", {}, "S g M P N", ""),
      Make("Malgunji", "S R g G m P D n N", "", "m", "S", {}, "S R G m D n", ""),
      Make("Malkauns", "S g m d n", "", "m", "S", {"Malkosh"}),
      Make("Mand", "S R G m P D N", "", "S", "P", {"Maand"}),
      Make("Maru Bihag", "S R G m M P D N", "m", "G", "N", {}, "S G m M P N", ""),
      Make("Marwa", "S r G M D N", "", "r", "D", {"Marva"}),
      Make("Megh", "S R m P n", "", "S", "P", {"Megh Malhar"}),
      Make("Miyan ki Malhar", "S R g m P D n N", "g", "m", "S", {"Miyan Malhar"}),
      Make("Miyan ki Todi", "S r g M P d N", "P", "d", "g", {"Todi"}, "S r g M d N", ""),
      Make("Multani", "S r g M P d N", "r d", "P", "S", {}, "S g M P N", ""),
      Make("Nand", "S R G m M P D N", "M", "S", "P", {"Anandi", "Nand Kalyan"}),
      Make("Narayani", "S R m P D n", "", "S", "P"),
      Make("Nat Bhairav", "S R G m P d N", "", "m", "S", {"Nut Bhairav"}),
      Make("Nayaki Kanada", "S R g m P n", "", "m", "S"),
      Make("Pahadi", "S R G m P D n N", "m n N", "", "", {"Pahari"}),
      Make("Paraj", "S r G m M P d N", "", "S", "P", {}, "S G M d N", ""),
      Make("Parmeshwari", "S r g m D n", "", "m", "S", {"Parameshwari"}),
      Make("Patdeep", "S R g m P D N", "", "P", "S", {"Patdip"}, "S g m P N", ""),
      Make("Pilu", "S R g G m P d D n N", "d", "g", "n"),
      Make("Puriya", "S r G M D N", "", "G", "N"),
      Make("Puriya Dhanashree", "S r G M P d N", "", "P", "r", {"Puriya Dhanashri"}),
      Make("Puriya Kalyan", "S r G M P D N", "", "S", "P"),
      Make("Purvi", "S r G m M P d N", "", "G", "N", {"Poorvi"}, "S r G m M d N", ""),
      Make("Rageshree", "S R G m D n", "R", "", "", {"Rageshri", "Rageshwari"}, "S G m D n", ""),
      Make("Ramdasi Malhar", "S R g G m P D n N", "", "m", "S"),
      Make("Ramkali", "S r G m M P d n N", "M n", "P", "S"),
      Make("Saraswati", "S R M P D n", "", "P", "R", {}, "S R M P D", ""),
      Make("Shahana", "S R g G m P D n N", "G N", "", "", {"Shahana Kanada"}),
      Make("Shankara", "S R G P D N", "R", "G", "N", {}, "S G P D N", ""),
      Make("Shivmat Bhairav", "S r g G m P d n N", "g n", "", "", {"Shiv Bhairav"}),
      Make("Shivranjani", "S R g P D", "", "", "", {"Shivaranjani"}),
      Make("Shri", "S r G M P d N", "", "r", "P", {"Shree"}, "S r M P N", ""),
      Make("Shuddh Kalyan", "S R G M P D N", "M N", "G", "D", {"Shuddha Kalyan"}, "S R G P D", ""),
      Make("Shuddh Sarang", "S R m M P D N", "", "R", "P", {"Shuddha Sarang"}, "S R M P N", ""),
      Make("Shyam Kalyan", "S R G m M P D N", "m", "", "", {}, "S R m M P N", ""),
      Make("Sindhura", "S R g m P D n", "", "S", "P", {"Sindhoora"}, "S R m P D", ""),
      Make("Sohini", "S r G M D N", "", "D", "G", {"Sohni", "Sohani"}),
      Make("Sughrai", "S R g m P n", "", "", ""),
      Make("Suha", "S R g m P n", "", "", ""),
      Make("Sur Malhar", "S R m P D n N", "", "P", "S", {"Surdasi Malhar"}, "S R m P n N", ""),
      Make("Tilak Kamod", "S R G m P D n N", "n", "S", "P", {}, "S R m P N", ""),
      Make("Tilang", "S G m P n N", "", "G", "N", {}, "S G m P N", "S G m P n"),
      Make("Vachaspati", "S R G M P D n", "", "S", "P", {}, "S G M P n", ""),
      Make("Yaman", "S R G M P D N", "", "G", "N", {"Kalyan", "Iman"}),
      Make("Yaman Kalyan", "S R G m M P D N", "m", "G", "N"),
      Make("Yamani Bilawal", "S R G m M P D N", "", "S", "P"),
  });
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
