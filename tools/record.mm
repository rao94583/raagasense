// Records a sound input to a file, for analyzing later.
//
//   bazel run //tools:record -- --list
//   bazel run //tools:record -- [--input NAME] [--channels 3,4] [--seconds N] OUTPUT
//
// NAME is any part of an input's name from --list, ignoring case; without it,
// the system default input. Channels count from 1, and default to all of the
// input's. OUTPUT ends in .m4a (AAC), .wav, .aiff or .caf. Records until N
// seconds have passed or Ctrl-C, printing the level every second so a silent
// input shows up straight away.
#import <AVFoundation/AVFoundation.h>

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "platform/audio_input.h"

namespace {

int Usage() {
  std::fprintf(stderr,
               "usage: record --list\n"
               "       record [--input NAME] [--channels 3,4] [--seconds N] OUTPUT.m4a|.wav|.aiff|.caf\n");
  return 2;
}

int List() {
  for (const raagasense::AudioInput& input : raagasense::AudioInputs()) {
    std::printf("%-40s %d channel%s\n", input.name.c_str(), input.channels, input.channels == 1 ? "" : "s");
  }
  return 0;
}

// Channel numbers from "3,4", counting from 1; empty if malformed.
std::vector<int> ParseChannels(const char* text) {
  std::vector<int> channels;
  for (const char* p = text; *p;) {
    char* end = nullptr;
    const long channel = std::strtol(p, &end, 10);
    if (end == p || channel < 1) return {};
    channels.push_back(static_cast<int>(channel));
    p = *end == ',' ? end + 1 : end;
    if (*end && *end != ',') return {};
  }
  return channels;
}

// Settings for AVAudioFile from the output's extension; nil if unsupported.
NSDictionary* FileSettings(NSString* path, double sample_rate, int channels) {
  NSString* extension = path.pathExtension.lowercaseString;
  if ([extension isEqualToString:@"m4a"]) {
    return @{
      AVFormatIDKey : @(kAudioFormatMPEG4AAC),
      AVSampleRateKey : @(sample_rate),
      AVNumberOfChannelsKey : @(channels),
      AVEncoderBitRateKey : @(128000 * channels),
    };
  }
  if ([@[ @"wav", @"aiff", @"aif", @"caf" ] containsObject:extension]) {
    return @{
      AVFormatIDKey : @(kAudioFormatLinearPCM),
      AVSampleRateKey : @(sample_rate),
      AVNumberOfChannelsKey : @(channels),
      AVLinearPCMBitDepthKey : @24,
      AVLinearPCMIsFloatKey : @NO,
      AVLinearPCMIsBigEndianKey : @([extension hasPrefix:@"aif"]),
    };
  }
  return nil;
}

bool MicrophoneAllowed() {
  const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status != AVAuthorizationStatusNotDetermined) return status == AVAuthorizationStatusAuthorized;
  dispatch_semaphore_t answered = dispatch_semaphore_create(0);
  __block BOOL allowed = NO;
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                           completionHandler:^(BOOL granted) {
                             allowed = granted;
                             dispatch_semaphore_signal(answered);
                           }];
  dispatch_semaphore_wait(answered, DISPATCH_TIME_FOREVER);
  return allowed;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    using namespace raagasense;
    // Under bazel run, make relative paths relative to where it was run.
    if (const char* directory = std::getenv("BUILD_WORKING_DIRECTORY")) chdir(directory);

    std::string input_name, path;
    std::vector<int> channels;
    double seconds = 0;
    for (int i = 1; i < argc; ++i) {
      if (!std::strcmp(argv[i], "--list")) {
        return List();
      } else if (!std::strcmp(argv[i], "--input") && i + 1 < argc) {
        input_name = argv[++i];
      } else if (!std::strcmp(argv[i], "--channels") && i + 1 < argc) {
        channels = ParseChannels(argv[++i]);
        if (channels.empty()) return Usage();
      } else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) {
        seconds = std::atof(argv[++i]);
        if (seconds <= 0) return Usage();
      } else if (argv[i][0] == '-' || !path.empty()) {
        return Usage();
      } else {
        path = argv[i];
      }
    }
    if (path.empty()) return Usage();

    if (!MicrophoneAllowed()) {
      std::fprintf(stderr, "Microphone access is turned off for this terminal. Allow it in System Settings → "
                           "Privacy & Security → Microphone.\n");
      return 1;
    }

    AVAudioEngine* engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = engine.inputNode;
    std::string device_name = "the system default input";
    if (!input_name.empty()) {
      std::vector<AudioInput> matches;
      for (const AudioInput& candidate : AudioInputs()) {
        if ([@(candidate.name.c_str()) localizedCaseInsensitiveContainsString:@(input_name.c_str())]) {
          matches.push_back(candidate);
        }
      }
      if (matches.size() != 1) {
        std::fprintf(stderr, "%s inputs match \"%s\"; see --list\n", matches.empty() ? "No" : "Several",
                     input_name.c_str());
        return 1;
      }
      const std::string error = UseAudioInput(input.audioUnit, matches[0].id);
      if (!error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
      }
      device_name = matches[0].name;
    }

    // The device's own format: after switching devices, the output bus can
    // still report the previous one's channels and rate.
    AVAudioFormat* format = [input inputFormatForBus:0];
    const int available = static_cast<int>(format.channelCount);
    if (format.sampleRate == 0 || available == 0) {
      std::fprintf(stderr, "No audio input device is available.\n");
      return 1;
    }
    if (channels.empty()) {
      for (int channel = 1; channel <= available; ++channel) channels.push_back(channel);
    }
    for (int channel : channels) {
      if (channel > available) {
        std::fprintf(stderr, "%s has %d channel%s; see --list\n", device_name.c_str(), available,
                     available == 1 ? "" : "s");
        return 1;
      }
    }

    const int count = static_cast<int>(channels.size());
    NSString* output = @(path.c_str());
    NSDictionary* settings = FileSettings(output, format.sampleRate, count);
    if (!settings) return Usage();
    NSError* error = nil;
    __block AVAudioFile* file = [[AVAudioFile alloc] initForWriting:[NSURL fileURLWithPath:output]
                                                           settings:settings
                                                       commonFormat:AVAudioPCMFormatFloat32
                                                        interleaved:NO
                                                              error:&error];
    if (!file) {
      std::fprintf(stderr, "%s: %s\n", path.c_str(), error.localizedDescription.UTF8String);
      return 1;
    }

    // Written on the tap's thread, read on the main thread.
    static std::atomic<float> peak{0};
    static std::atomic<int64_t> written{0};
    const int64_t limit = seconds > 0 ? static_cast<int64_t>(seconds * format.sampleRate) : INT64_MAX;
    AVAudioFormat* file_format = file.processingFormat;
    [input installTapOnBus:0
                bufferSize:4096
                    format:format
                     block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
                       const int64_t room = limit - written.load();
                       if (room <= 0) return;
                       const AVAudioFrameCount frames =
                           static_cast<AVAudioFrameCount>(std::min<int64_t>(buffer.frameLength, room));
                       AVAudioPCMBuffer* selected = [[AVAudioPCMBuffer alloc] initWithPCMFormat:file_format
                                                                                  frameCapacity:frames];
                       selected.frameLength = frames;
                       float loudest = peak.load();
                       for (int c = 0; c < count; ++c) {
                         const float* from = buffer.floatChannelData[channels[c] - 1];
                         float* to = selected.floatChannelData[c];
                         for (AVAudioFrameCount i = 0; i < frames; ++i) {
                           to[i] = from[i];
                           loudest = std::max(loudest, std::abs(from[i]));
                         }
                       }
                       peak.store(loudest);
                       [file writeFromBuffer:selected error:nil];
                       written += frames;
                     }];
    if (![engine startAndReturnError:&error]) {
      std::fprintf(stderr, "Can't start recording: %s\n", error.localizedDescription.UTF8String);
      return 1;
    }
    std::string channel_list;
    for (int channel : channels) channel_list += (channel_list.empty() ? "" : ",") + std::to_string(channel);
    std::fprintf(stderr, "Recording %s, channel%s %s, to %s. Ctrl-C stops.\n", device_name.c_str(),
                 count == 1 ? "" : "s", channel_list.c_str(), path.c_str());

    const double rate = format.sampleRate;
    void (^finish)(void) = ^{
      [input removeTapOnBus:0];
      [engine stop];
      file = nil;  // Closes the file.
      std::fprintf(stderr, "\nWrote %.1f s to %s\n", written.load() / rate, path.c_str());
      std::exit(0);
    };

    signal(SIGINT, SIG_IGN);
    dispatch_source_t interrupt = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(interrupt, finish);
    dispatch_resume(interrupt);

    // The level each second, as the loudest sample in dBFS.
    dispatch_source_t meter = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(meter, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC, NSEC_PER_SEC / 10);
    dispatch_source_set_event_handler(meter, ^{
      const float loudest = peak.exchange(0);
      const double db = loudest > 0 ? 20 * std::log10(loudest) : -INFINITY;
      const int bar = std::isfinite(db) ? std::clamp(static_cast<int>((db + 60) / 2), 0, 30) : 0;
      std::fprintf(stderr, "\r%6.0f s  %6.1f dB  %-30s%s", written.load() / rate, db, std::string(bar, '#').c_str(),
                   loudest == 0 ? "  silent!" : "        ");
      if (written.load() >= limit) finish();
    });
    dispatch_resume(meter);
    dispatch_main();
  }
}
