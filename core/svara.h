// Pitches relative to the tonic (Sa), named as the twelve Hindustani svaras.
#pragma once

#include <string>
#include <string_view>

namespace raagasense {

// Semitones above Sa, as indices into the twelve svaras.
enum Svara : int {
  kSa, kKomalRe, kRe, kKomalGa, kGa, kMa, kTivraMa, kPa, kKomalDha, kDha, kKomalNi, kNi,
};

// "S r R g G m M P d D n N": Bhatkhande's letters, lower case for komal and M
// for tivra Ma.
std::string_view SvaraLetter(int svara);
// "Sa", "Komal Re", ... "Tivra Ma", ... "Ni".
std::string_view SvaraName(int svara);
// The svara with this letter, or -1 if none has it.
int SvaraFromLetter(std::string_view letter);

// Cents above Sa (negative below), for frequencies in Hz.
double CentsFromSa(double frequency, double sa_frequency);

struct SvaraPosition {
  int svara = kSa;
  // -1 for mandra, 0 for madhya, 1 for taar, and so on.
  int octave = 0;
  // Cents from the svara's equal-tempered position, in [-50, 50).
  double deviation = 0;
};

// The nearest svara to a pitch in cents from Sa.
SvaraPosition NearestSvara(double cents);

// The svara's letter, with a dot below for each octave down and a dot above
// for each octave up, as in Bhatkhande notation: "Ṇ", "S", "Ṡ".
std::string SvaraLabel(const SvaraPosition& position);

}  // namespace raagasense
