// Decodes audio files (WAV, AIFF, MP3, AAC, ...) through Core Audio.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace raagasense {

struct AudioData {
  // Mono samples, the average of the file's channels.
  std::vector<float> samples;
  double sample_rate = 0;
};

// The file's audio, or nullopt with *error describing why it can't be read.
// A positive max_seconds decodes only that prefix; 0 reads the whole file.
std::optional<AudioData> ReadAudioFile(const std::string& path, std::string* error, double max_seconds = 0);

}  // namespace raagasense
