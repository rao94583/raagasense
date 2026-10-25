#include "platform/audio_file.h"

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <cstdlib>
#include <vector>

#include "gtest/gtest.h"

namespace raagasense {
namespace {

// Writes interleaved 16-bit stereo samples to a WAV file.
void WriteStereoWav(const std::string& path, double rate, const std::vector<int16_t>& interleaved) {
  AudioStreamBasicDescription format{};
  format.mSampleRate = rate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 16;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = 4;
  format.mBytesPerPacket = 4;
  NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
  ExtAudioFileRef file = nullptr;
  ASSERT_EQ(ExtAudioFileCreateWithURL((__bridge CFURLRef)url, kAudioFileWAVEType, &format, nullptr,
                                      kAudioFileFlags_EraseFile, &file),
            noErr);
  AudioBufferList list;
  list.mNumberBuffers = 1;
  list.mBuffers[0] = {2, static_cast<UInt32>(interleaved.size() * 2), const_cast<int16_t*>(interleaved.data())};
  ASSERT_EQ(ExtAudioFileWrite(file, static_cast<UInt32>(interleaved.size() / 2), &list), noErr);
  ExtAudioFileDispose(file);
}

TEST(ReadAudioFile, MixesStereoToMono) {
  const std::string path = std::string(std::getenv("TEST_TMPDIR")) + "/stereo.wav";
  // Left at half scale, right silent: mono should be a quarter of full scale.
  std::vector<int16_t> interleaved;
  for (int i = 0; i < 10000; ++i) {
    interleaved.push_back(16384);
    interleaved.push_back(0);
  }
  WriteStereoWav(path, 44100, interleaved);

  std::string error;
  const std::optional<AudioData> audio = ReadAudioFile(path, &error);
  ASSERT_TRUE(audio) << error;
  EXPECT_EQ(audio->sample_rate, 44100);
  ASSERT_EQ(audio->samples.size(), 10000u);
  EXPECT_NEAR(audio->samples[5000], 0.25, 1e-4);
}

TEST(ReadAudioFile, MissingFile) {
  std::string error;
  EXPECT_FALSE(ReadAudioFile("/nonexistent/recording.wav", &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace raagasense
