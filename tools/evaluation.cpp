#include "tools/evaluation.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace raagasense::evaluation {
namespace {

std::string Trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

size_t Column(const std::vector<std::string>& header, std::string_view name) {
  return std::find(header.begin(), header.end(), name) - header.begin();
}

std::string Field(const std::vector<std::string>& fields, size_t column) {
  return column < fields.size() ? Trim(fields[column]) : "";
}

}  // namespace

std::vector<std::string> CsvFields(std::string_view line) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  std::vector<std::string> fields(1);
  bool quoted = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
        fields.back() += '"';
        ++i;
      } else {
        quoted = !quoted;
      }
    } else if (c == ',' && !quoted) {
      fields.emplace_back();
    } else {
      fields.back() += c;
    }
  }
  return quoted ? std::vector<std::string>{} : fields;
}

bool Number(std::string_view text, double* value) {
  const std::string trimmed = Trim(text);
  char* end = nullptr;
  errno = 0;
  *value = std::strtod(trimmed.c_str(), &end);
  return !trimmed.empty() && end == trimmed.c_str() + trimmed.size() && errno != ERANGE && std::isfinite(*value);
}

bool ReadLabels(std::istream& in, bool all_sa, std::vector<Label>* labels, std::string* error) {
  labels->clear();
  std::string line;
  std::getline(in, line);
  const auto header = CsvFields(line);
  const size_t file = Column(header, "file"), raag = Column(header, "raag"), sa = Column(header, "sa_hz");
  const size_t checked = Column(header, "sa_checked"), artist = Column(header, "artist"), notes = Column(header, "notes");
  if (file == header.size() || raag == header.size() || sa == header.size()) {
    *error = "expected file,raag,sa_hz columns";
    return false;
  }
  size_t row = 1;
  while (std::getline(in, line)) {
    ++row;
    if (Trim(line).empty()) continue;
    const auto fields = CsvFields(line);
    Label label{Field(fields, file), Field(fields, raag), 0, Field(fields, artist)};
    if (fields.size() != header.size() || label.file.empty() || label.raag.empty() ||
        !Number(Field(fields, sa), &label.sa) || label.sa < 0) {
      *error = "invalid label at line " + std::to_string(row);
      return false;
    }
    if (!all_sa && checked < header.size() && Field(fields, checked) != "yes") label.sa = 0;
    if (label.artist.empty()) {
      const std::string text = Field(fields, notes);
      const size_t at = text.find("artist ");
      if (at != std::string::npos) label.artist = Trim(text.substr(at + 7, text.find(';', at) - at - 7));
    }
    labels->push_back(std::move(label));
  }
  return true;
}

bool ReadPitchCsv(std::istream& in, std::vector<PitchFrame>* frames, std::string* error) {
  frames->clear();
  std::string line;
  std::getline(in, line);
  const auto header = CsvFields(line);
  const size_t time = Column(header, "time"), frequency = Column(header, "frequency");
  const size_t confidence = Column(header, "confidence");
  if (time == header.size() || frequency == header.size()) {
    *error = "expected time,frequency columns";
    return false;
  }
  size_t row = 1;
  while (std::getline(in, line)) {
    ++row;
    if (Trim(line).empty()) continue;
    const auto fields = CsvFields(line);
    PitchFrame frame;
    frame.confidence = 1;
    const std::string hz = Field(fields, frequency);
    if (fields.size() != header.size() || !Number(Field(fields, time), &frame.time) ||
        frame.time < 0 || (!frames->empty() && frame.time <= frames->back().time) ||
        (!hz.empty() && (!Number(hz, &frame.frequency) || frame.frequency < 0)) ||
        (confidence < header.size() && (!Number(Field(fields, confidence), &frame.confidence) ||
                                      frame.confidence < 0 || frame.confidence > 1))) {
      *error = "invalid pitch frame at line " + std::to_string(row);
      return false;
    }
    frame.voiced = frame.frequency > 0;
    frames->push_back(frame);
  }
  return true;
}

}  // namespace raagasense::evaluation
