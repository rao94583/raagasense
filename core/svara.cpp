#include "core/svara.h"

#include <cmath>

namespace raagasense {

namespace {

constexpr std::string_view kLetters[] = {"S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N"};
constexpr std::string_view kNames[] = {
    "Sa", "Komal Re", "Re", "Komal Ga", "Ga", "Ma", "Tivra Ma", "Pa", "Komal Dha", "Dha", "Komal Ni", "Ni",
};

// UTF-8 combining dot below (U+0323) and dot above (U+0307).
constexpr std::string_view kDotBelow = "\xcc\xa3";
constexpr std::string_view kDotAbove = "\xcc\x87";

}  // namespace

std::string_view SvaraLetter(int svara) { return kLetters[svara]; }

std::string_view SvaraName(int svara) { return kNames[svara]; }

double CentsFromSa(double frequency, double sa_frequency) { return 1200 * std::log2(frequency / sa_frequency); }

SvaraPosition NearestSvara(double cents) {
  const int semitones = static_cast<int>(std::floor(cents / 100 + 0.5));
  SvaraPosition position;
  position.octave = static_cast<int>(std::floor(semitones / 12.0));
  position.svara = semitones - 12 * position.octave;
  position.deviation = cents - 100 * semitones;
  return position;
}

std::string SvaraLabel(const SvaraPosition& position) {
  std::string label(kLetters[position.svara]);
  for (int i = 0; i < std::abs(position.octave); ++i) label += position.octave < 0 ? kDotBelow : kDotAbove;
  return label;
}

}  // namespace raagasense
