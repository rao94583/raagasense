// Data editing and interchange for short, manually reviewed excerpts.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/pitch.h"

namespace raagasense::annotation {

// Single-line CSV, including escaped quotes and CRLF line endings.
std::vector<std::string> CsvFields(std::string_view line);
bool Number(std::string_view text, double* value);
std::string ArtistFromNotes(std::string_view notes);

struct Label {
  std::string file;
  std::string raag = "Unknown";
  std::string artist;
  std::string performer;
  double sa = 0;
  bool sa_checked = false;
  std::string notes;
};

// Retains unknown columns and other recordings' rows when updating a label.
class Labels {
 public:
  static std::optional<Labels> Parse(std::string_view text, std::string* error);
  Label Get(std::string_view file) const;
  bool Set(const Label& label, std::string* error);
  std::string Text() const;

 private:
  size_t Column(std::string_view name) const;
  std::string Field(const std::vector<std::string>& row, std::string_view name) const;
  std::vector<std::string> header_;
  std::vector<std::vector<std::string>> rows_;
};

enum class Edit { kTranspose, kNoLead, kSetFrequency, kRestore };

class PitchEditor {
 public:
  explicit PitchEditor(std::vector<PitchFrame> frames = {});
  // Times are relative to the excerpt. Transpose value is in semitones;
  // SetFrequency value is Hz. Edits never change frame timestamps.
  bool Apply(Edit edit, double start, double end, double value = 0);
  bool Undo();
  const std::vector<PitchFrame>& frames() const { return frames_; }
  const std::vector<PitchFrame>& original() const { return original_; }
  bool edited() const { return edited_; }
  // Call only after human review. Unvoiced rows are explicit frequency 0;
  // every exported frame has confidence 1, meaning a reviewed annotation.
  std::string ReviewedCsv() const;

 private:
  std::vector<PitchFrame> original_, frames_;
  std::vector<std::vector<PitchFrame>> undo_;
  bool edited_ = false;
};

// A quiet sine-wave guide that follows the current contour, including gaps.
std::vector<float> PitchGuide(std::span<const PitchFrame> frames, double duration, int rate = 16000);
// Mono PCM16 WAV bytes; audio is copied, never written back to its source.
std::string WavBytes(std::span<const float> samples, int rate);

}  // namespace raagasense::annotation
