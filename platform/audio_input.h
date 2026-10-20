// The sound input devices, and pointing an input audio unit at one of them.
#pragma once

#include <AudioToolbox/AudioToolbox.h>

#include <string>
#include <vector>

namespace raagasense {

// A sound input device the system offers.
struct AudioInput {
  // Stable across launches and reconnections.
  std::string id;
  std::string name;
  int channels = 0;
};

// The devices that can record, in the system's order. Empty on iOS, where the
// audio session picks the input.
std::vector<AudioInput> AudioInputs();

// Makes an input audio unit, such as an AVAudioEngine's input node's, record
// from the input with this id. Returns an empty string on success, or the
// reason it failed. Call it before reading the unit's format.
std::string UseAudioInput(AudioUnit unit, const std::string& input_id);

}  // namespace raagasense
