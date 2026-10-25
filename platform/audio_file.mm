#include "platform/audio_file.h"

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>

namespace raagasense {

namespace {

std::string StatusText(const char* what, OSStatus status) {
  return std::string(what) + " failed (OSStatus " + std::to_string(status) + ")";
}

}  // namespace

std::optional<AudioData> ReadAudioFile(const std::string& path, std::string* error, double max_seconds) {
  if (!std::isfinite(max_seconds) || max_seconds < 0) {
    *error = "Audio duration must be finite and nonnegative";
    return std::nullopt;
  }
  NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
  ExtAudioFileRef file = nullptr;
  OSStatus status = ExtAudioFileOpenURL((__bridge CFURLRef)url, &file);
  if (status != noErr) {
    *error = StatusText("Opening the file", status);
    return std::nullopt;
  }

  AudioStreamBasicDescription source{};
  UInt32 size = sizeof(source);
  status = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &source);
  const UInt32 channels = source.mChannelsPerFrame;
  if (status != noErr || channels == 0) {
    ExtAudioFileDispose(file);
    *error = StatusText("Reading the file's format", status);
    return std::nullopt;
  }

  // Decode to interleaved 32-bit floats at the file's own rate.
  AudioStreamBasicDescription client{};
  client.mSampleRate = source.mSampleRate;
  client.mFormatID = kAudioFormatLinearPCM;
  client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  client.mChannelsPerFrame = channels;
  client.mBitsPerChannel = 32;
  client.mFramesPerPacket = 1;
  client.mBytesPerFrame = 4 * channels;
  client.mBytesPerPacket = client.mBytesPerFrame;
  status = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client);
  if (status != noErr) {
    ExtAudioFileDispose(file);
    *error = StatusText("Setting the decoding format", status);
    return std::nullopt;
  }

  AudioData audio;
  audio.sample_rate = source.mSampleRate;
  std::vector<float> chunk(4096 * channels);
  while (true) {
    UInt32 frames = 4096;
    if (max_seconds > 0) {
      const double remaining = std::floor(max_seconds * audio.sample_rate) - audio.samples.size();
      if (remaining <= 0) break;
      frames = static_cast<UInt32>(std::min(remaining, 4096.0));
    }
    AudioBufferList list;
    list.mNumberBuffers = 1;
    list.mBuffers[0] = {channels, static_cast<UInt32>(chunk.size() * sizeof(float)), chunk.data()};
    status = ExtAudioFileRead(file, &frames, &list);
    if (status != noErr) {
      ExtAudioFileDispose(file);
      *error = StatusText("Decoding", status);
      return std::nullopt;
    }
    if (frames == 0) break;
    for (UInt32 i = 0; i < frames; ++i) {
      float sum = 0;
      for (UInt32 c = 0; c < channels; ++c) sum += chunk[i * channels + c];
      audio.samples.push_back(sum / channels);
    }
  }
  ExtAudioFileDispose(file);
  return audio;
}

}  // namespace raagasense
