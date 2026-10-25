#include "core/annotation.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <numbers>
#include <set>
#include <sstream>

namespace raagasense::annotation {
namespace {

std::string Trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

std::string CsvLine(std::span<const std::string> fields) {
  std::string line;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) line += ',';
    if (fields[i].find_first_of(",\"\r\n") == std::string::npos) {
      line += fields[i];
    } else {
      line += '"';
      for (char c : fields[i]) { if (c == '"') line += '"'; line += c; }
      line += '"';
    }
  }
  return line + '\n';
}

}  // namespace

std::vector<std::string> CsvFields(std::string_view line) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  std::vector<std::string> fields(1);
  bool quoted = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (quoted && i + 1 < line.size() && line[i + 1] == '"') { fields.back() += '"'; ++i; }
      else quoted = !quoted;
    } else if (c == ',' && !quoted) fields.emplace_back();
    else fields.back() += c;
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

std::string ArtistFromNotes(std::string_view notes) {
  const size_t at = notes.find("artist ");
  return at == std::string_view::npos ? "" : Trim(notes.substr(at + 7, notes.find(';', at) - at - 7));
}

size_t Labels::Column(std::string_view name) const {
  return std::find(header_.begin(), header_.end(), name) - header_.begin();
}

std::string Labels::Field(const std::vector<std::string>& row, std::string_view name) const {
  const size_t at = Column(name);
  return at < row.size() ? row[at] : "";
}

std::optional<Labels> Labels::Parse(std::string_view text, std::string* error) {
  Labels labels;
  if (text.empty()) {
    labels.header_ = {"file", "raag", "sa_hz", "sa_checked", "artist", "notes"};
    return labels;
  }
  std::istringstream in{std::string(text)};
  std::string line;
  std::getline(in, line);
  labels.header_ = CsvFields(line);
  if (labels.Column("file") == labels.header_.size() || labels.Column("raag") == labels.header_.size() ||
      labels.Column("sa_hz") == labels.header_.size()) {
    *error = "labels.csv must contain file, raag, and sa_hz columns.";
    return std::nullopt;
  }
  std::set<std::string> files;
  while (std::getline(in, line)) {
    if (Trim(line).empty()) continue;
    auto row = CsvFields(line);
    if (row.size() != labels.header_.size() || !files.insert(labels.Field(row, "file")).second) {
      *error = "Malformed or duplicate row in labels.csv; it has not been changed.";
      return std::nullopt;
    }
    labels.rows_.push_back(std::move(row));
  }
  return labels;
}

Label Labels::Get(std::string_view file) const {
  Label label;
  label.file = file;
  for (const auto& row : rows_) {
    if (Field(row, "file") != file) continue;
    label.raag = Field(row, "raag");
    label.notes = Field(row, "notes");
    label.performer = Field(row, "singer");
    label.artist = Column("artist") < header_.size() ? Field(row, "artist") : ArtistFromNotes(label.notes);
    if (!Number(Field(row, "sa_hz"), &label.sa) || label.sa < 0) label.sa = 0;
    label.sa_checked = label.sa > 0 && Field(row, "sa_checked") == "yes";
    break;
  }
  return label;
}

bool Labels::Set(const Label& label, std::string* error) {
  for (const auto& field : {label.file, label.raag, label.artist, label.performer, label.notes}) {
    if (field.find_first_of("\r\n") != std::string::npos) {
      *error = "Please keep each label field on one line.";
      return false;
    }
  }
  if (label.file.empty() || label.raag.empty() || !std::isfinite(label.sa) || label.sa < 0) {
    *error = "A filename, raag (or Unknown), and a valid Sa are required.";
    return false;
  }
  for (const auto& name : {"sa_checked", "artist", "notes", "singer"}) {
    if (Column(name) < header_.size()) continue;
    for (auto& row : rows_) {
      // Adding an explicit artist column must retain all legacy names.
      row.push_back(std::string_view(name) == "artist" ? ArtistFromNotes(Field(row, "notes")) : "");
    }
    header_.push_back(name);
  }
  auto found = std::find_if(rows_.begin(), rows_.end(), [&](const auto& row) { return Field(row, "file") == label.file; });
  if (found == rows_.end()) {
    rows_.emplace_back(header_.size());
    found = rows_.end() - 1;
  }
  std::ostringstream sa;
  sa << std::setprecision(12) << label.sa;
  for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
           {"file", label.file}, {"raag", label.raag}, {"artist", label.artist}, {"singer", label.performer}, {"notes", label.notes},
           {"sa_hz", sa.str()}, {"sa_checked", label.sa_checked && label.sa > 0 ? "yes" : "no"}}) {
    (*found)[Column(key)] = value;
  }
  return true;
}

std::string Labels::Text() const {
  std::string text = CsvLine(header_);
  for (const auto& row : rows_) text += CsvLine(row);
  return text;
}

PitchEditor::PitchEditor(std::vector<PitchFrame> frames) : original_(frames), frames_(std::move(frames)) {}

bool PitchEditor::Apply(Edit edit, double start, double end, double value) {
  if (!std::isfinite(start) || !std::isfinite(end) || end <= start || !std::isfinite(value) ||
      (edit == Edit::kSetFrequency && (value < 20 || value > 5000)) ||
      (edit == Edit::kTranspose && std::abs(value) > 24)) return false;
  auto changed = frames_;
  bool any = false;
  for (size_t i = 0; i < changed.size(); ++i) {
    auto& frame = changed[i];
    if (frame.time < start || frame.time >= end) continue;
    if (edit == Edit::kRestore) frame = original_[i];
    else if (edit == Edit::kNoLead) { frame.frequency = 0; frame.voiced = false; frame.confidence = 1; }
    else if (edit == Edit::kSetFrequency) { frame.frequency = value; frame.voiced = true; frame.confidence = 1; }
    else if (frame.voiced) { frame.frequency *= std::exp2(value / 12); frame.confidence = 1; }
    if (frame.voiced && (!std::isfinite(frame.frequency) || frame.frequency < 20 || frame.frequency > 5000)) return false;
    if (frame.frequency != frames_[i].frequency || frame.voiced != frames_[i].voiced ||
        frame.confidence != frames_[i].confidence) any = true;
  }
  if (!any) return false;
  if (undo_.size() >= 20) undo_.erase(undo_.begin());
  undo_.push_back(frames_);
  frames_ = std::move(changed);
  edited_ = true;
  return true;
}

bool PitchEditor::Undo() {
  if (undo_.empty()) return false;
  frames_ = std::move(undo_.back());
  undo_.pop_back();
  edited_ = true;
  return true;
}

std::string PitchEditor::ReviewedCsv() const {
  std::ostringstream out;
  out << "time,frequency,confidence\n" << std::setprecision(17);
  for (const auto& frame : frames_) out << frame.time << ',' << (frame.voiced ? frame.frequency : 0) << ",1\n";
  return out.str();
}

std::vector<float> PitchGuide(std::span<const PitchFrame> frames, double duration, int rate) {
  if (frames.empty() || !std::isfinite(duration) || duration <= 0 || duration > 60 || rate < 8000 || rate > 96000) return {};
  std::vector<float> samples(static_cast<size_t>(duration * rate));
  size_t at = 0;
  double phase = 0, amplitude = 0;
  const double smoothing = 1 - std::exp(-1.0 / (0.004 * rate));
  for (size_t i = 0; i < samples.size(); ++i) {
    const double time = static_cast<double>(i) / rate;
    while (at + 1 < frames.size() && frames[at + 1].time <= time) ++at;
    double frequency = frames[at].voiced ? frames[at].frequency : 0;
    if (frequency > 0 && at + 1 < frames.size() && frames[at + 1].voiced && time > frames[at].time) {
      const double next = frames[at + 1].frequency;
      if (next > 0 && std::abs(12 * std::log2(next / frequency)) < 3) {
        const double fraction = (time - frames[at].time) / (frames[at + 1].time - frames[at].time);
        frequency *= std::exp2(fraction * std::log2(next / frequency));
      }
    }
    amplitude += smoothing * ((frequency > 0 ? 0.12 : 0) - amplitude);
    if (frequency > 0) phase = std::fmod(phase + 2 * std::numbers::pi * frequency / rate, 2 * std::numbers::pi);
    samples[i] = static_cast<float>(amplitude * std::sin(phase));
  }
  return samples;
}

std::string WavBytes(std::span<const float> samples, int rate) {
  if (rate <= 0 || samples.size() > (std::numeric_limits<uint32_t>::max() - 36) / 2) return {};
  std::string out;
  auto u16 = [&](uint16_t value) { out += static_cast<char>(value & 255); out += static_cast<char>(value >> 8); };
  auto u32 = [&](uint32_t value) { u16(value & 65535); u16(value >> 16); };
  out += "RIFF"; u32(36 + samples.size() * 2); out += "WAVEfmt "; u32(16);
  u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
  out += "data"; u32(samples.size() * 2);
  for (float sample : samples) {
    const double value = std::isfinite(sample) ? std::clamp(static_cast<double>(sample), -1.0, 1.0) : 0;
    u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(value * 32767))));
  }
  return out;
}

}  // namespace raagasense::annotation
