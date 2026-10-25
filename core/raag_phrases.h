// Characteristic phrases of the raags, by raag name.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace raagasense {

// Each raag's phrases as written, svaras in Bhatkhande letters separated by
// spaces: ".N R G M P", "M D N S'".
const std::map<std::string, std::vector<std::string>>& RaagPhrases();

}  // namespace raagasense
