// Tracks the pitch of a sound input as it arrives.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/pitch.h"

namespace raagasense {

class LivePitch {
 public:
  LivePitch();
  ~LivePitch();
  LivePitch(const LivePitch&) = delete;
  LivePitch& operator=(const LivePitch&) = delete;

  // Asks for microphone access if needed and starts listening to the input
  // with this id (see AudioInputs), or to the system default when it's
  // empty. Calls done on the
  // main thread with an empty string on success, or the reason it failed.
  void Start(const std::string& input_id, std::function<void(const std::string& error)> done);
  void Stop();
  bool running() const;

  // Frames from the last `seconds` of the stream, oldest first.
  std::vector<PitchFrame> Recent(double seconds) const;
  // The stream time of the latest frame, in seconds.
  double now() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace raagasense
