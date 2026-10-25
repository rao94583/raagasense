// Input formats shared by the evaluation tool and its tests.
#pragma once

#include <istream>
#include <string>
#include <string_view>
#include <vector>

#include "core/pitch.h"

namespace raagasense::evaluation {

struct Label {
  std::string file;
  std::string raag;
  double sa = 0;
  std::string artist;
};

// CSV with quoted commas, escaped quotes, and Unix or Windows line endings;
// empty for an unterminated quoted field. Fields cannot span lines.
std::vector<std::string> CsvFields(std::string_view line);

// Required columns: file,raag,sa_hz. Optional: sa_checked,artist,notes.
// If sa_checked exists, only "yes" is trusted unless all_sa is set.
// Without an artist column, reads "artist NAME" from notes.
bool ReadLabels(std::istream& in, bool all_sa, std::vector<Label>* labels, std::string* error);

// Required columns: time,frequency (Hz; blank or 0 for unvoiced).
// Optional confidence defaults to 1; extra columns from analyze --csv are
// ignored. Times must be finite, nonnegative, and strictly increasing.
bool ReadPitchCsv(std::istream& in, std::vector<PitchFrame>* frames, std::string* error);

// Saraga's headerless, whitespace-separated time/Hz Melodia output. Zero Hz
// means unvoiced. No confidence is supplied; 1 is a processing weight only.
// A positive end_time reads a prefix (seconds, exclusive); 0 reads all rows.
bool ReadSaragaPitch(std::istream& in, std::vector<PitchFrame>* frames,
                     std::string* error, double end_time = 0);

// Parses one finite number, allowing surrounding whitespace.
bool Number(std::string_view text, double* value);

}  // namespace raagasense::evaluation
