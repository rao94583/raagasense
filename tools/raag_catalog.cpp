// Canonical names and aliases for dataset importers (tab-separated, no header).
#include <cstdio>

#include "core/raag.h"

int main() {
  for (const auto& raag : raagasense::Raags()) {
    std::printf("%s", raag.name.c_str());
    for (const auto& alias : raag.aliases) std::printf("\t%s", alias.c_str());
    std::putchar('\n');
  }
}
