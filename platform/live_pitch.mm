#include "platform/live_pitch.h"

#import <AVFoundation/AVFoundation.h>

#include <deque>
#include <mutex>

#include "core/melody.h"
#include "platform/audio_input.h"

namespace raagasense {

namespace {

// How much history Recent() can return.
constexpr double kKeepSeconds = 60;

}  // namespace

struct LivePitch::Impl {
  AVAudioEngine* engine = nil;
  double sample_rate = 0;
  mutable std::mutex mutex;
  std::unique_ptr<MelodyTracker> tracker;
  std::deque<PitchFrame> frames;
  double now = 0;
  // The recording in progress, if any, written on the tap's thread.
  std::mutex file_mutex;
  AVAudioFile* file = nil;
  int64_t recorded = 0;

  // On the tap's thread: mixes the buffer to mono and tracks its pitch.
  void Process(AVAudioPCMBuffer* buffer) {
    const AVAudioFrameCount count = buffer.frameLength;
    const AVAudioChannelCount channels = buffer.format.channelCount;
    std::vector<float> mono(count, 0.0f);
    for (AVAudioChannelCount c = 0; c < channels; ++c) {
      const float* data = buffer.floatChannelData[c];
      for (AVAudioFrameCount i = 0; i < count; ++i) mono[i] += data[i] / channels;
    }
    {
      std::lock_guard lock(file_mutex);
      if (file) {
        AVAudioPCMBuffer* copy = [[AVAudioPCMBuffer alloc] initWithPCMFormat:file.processingFormat frameCapacity:count];
        copy.frameLength = count;
        std::copy(mono.begin(), mono.end(), copy.floatChannelData[0]);
        [file writeFromBuffer:copy error:nil];
        recorded += count;
      }
    }
    // Only this thread touches the tracker once the tap is installed.
    std::vector<PitchFrame> added;
    tracker->Push(mono, &added);
    std::lock_guard lock(mutex);
    for (const PitchFrame& frame : added) frames.push_back(frame);
    if (!added.empty()) now = added.back().time;
    while (!frames.empty() && frames.front().time < now - kKeepSeconds) frames.pop_front();
  }

  std::string Listen(const std::string& input_id) {
#if TARGET_OS_IPHONE
    NSError* sessionError = nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    if (![session setCategory:AVAudioSessionCategoryRecord mode:AVAudioSessionModeMeasurement options:0
                        error:&sessionError] ||
        ![session setActive:YES error:&sessionError]) {
      return sessionError.localizedDescription.UTF8String;
    }
#endif
    engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = engine.inputNode;
    if (!input_id.empty()) {
      const std::string error = UseAudioInput(input.audioUnit, input_id);
      if (!error.empty()) {
        engine = nil;
        return error;
      }
    }
    // The device's own format: after switching devices, the output bus can
    // still report the previous one's channels and rate.
    AVAudioFormat* format = [input inputFormatForBus:0];
    if (format.sampleRate == 0 || format.channelCount == 0) {
      engine = nil;
      return "No audio input device is available.";
    }
    {
      std::lock_guard lock(mutex);
      MelodyOptions options;
      options.sample_rate = format.sampleRate;
      tracker = std::make_unique<MelodyTracker>(options);
      frames.clear();
      now = 0;
    }
    sample_rate = format.sampleRate;
    Impl* self = this;
    [input installTapOnBus:0 bufferSize:1024 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
      self->Process(buffer);
    }];
    NSError* error = nil;
    if (![engine startAndReturnError:&error]) {
      [input removeTapOnBus:0];
      engine = nil;
      return error.localizedDescription.UTF8String;
    }
    return "";
  }
};

LivePitch::LivePitch() : impl_(std::make_unique<Impl>()) {}

LivePitch::~LivePitch() { Stop(); }

void LivePitch::Start(const std::string& input_id, std::function<void(const std::string& error)> done) {
  if (running()) {
    done("");
    return;
  }
  Impl* impl = impl_.get();
  auto listen = [impl, input_id, done](BOOL granted) {
    if (!granted) {
      done("Microphone access is turned off. Allow it in System Settings → Privacy & Security → Microphone.");
      return;
    }
    done(impl->Listen(input_id));
  };
  const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status == AVAuthorizationStatusNotDetermined) {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                               dispatch_async(dispatch_get_main_queue(), ^{
                                 listen(granted);
                               });
                             }];
  } else {
    listen(status == AVAuthorizationStatusAuthorized);
  }
}

void LivePitch::Stop() {
  if (!impl_->engine) return;
  StopRecording();
  [impl_->engine.inputNode removeTapOnBus:0];
  [impl_->engine stop];
  impl_->engine = nil;
}

bool LivePitch::running() const { return impl_->engine != nil; }

std::string LivePitch::StartRecording(const std::string& path) {
  if (!running()) return "Not listening.";
  NSString* output = @(path.c_str());
  NSString* extension = output.pathExtension.lowercaseString;
  NSDictionary* settings;
  if ([extension isEqualToString:@"m4a"]) {
    settings = @{
      AVFormatIDKey : @(kAudioFormatMPEG4AAC),
      AVSampleRateKey : @(impl_->sample_rate),
      AVNumberOfChannelsKey : @1,
      AVEncoderBitRateKey : @128000,
    };
  } else if ([@[ @"wav", @"aiff", @"aif", @"caf" ] containsObject:extension]) {
    settings = @{
      AVFormatIDKey : @(kAudioFormatLinearPCM),
      AVSampleRateKey : @(impl_->sample_rate),
      AVNumberOfChannelsKey : @1,
      AVLinearPCMBitDepthKey : @24,
      AVLinearPCMIsFloatKey : @NO,
      AVLinearPCMIsBigEndianKey : @([extension hasPrefix:@"aif"]),
    };
  } else {
    return "Recordings must end in .m4a, .wav, .aiff or .caf.";
  }
  NSError* error = nil;
  AVAudioFile* file = [[AVAudioFile alloc] initForWriting:[NSURL fileURLWithPath:output]
                                                 settings:settings
                                             commonFormat:AVAudioPCMFormatFloat32
                                              interleaved:NO
                                                    error:&error];
  if (!file) return error.localizedDescription.UTF8String;
  std::lock_guard lock(impl_->file_mutex);
  impl_->file = file;
  impl_->recorded = 0;
  return "";
}

double LivePitch::StopRecording() {
  std::lock_guard lock(impl_->file_mutex);
  if (!impl_->file) return 0;
  impl_->file = nil;  // Closes it.
  return impl_->sample_rate > 0 ? impl_->recorded / impl_->sample_rate : 0;
}

std::vector<PitchFrame> LivePitch::Recent(double seconds) const {
  std::lock_guard lock(impl_->mutex);
  std::vector<PitchFrame> recent;
  for (const PitchFrame& frame : impl_->frames) {
    if (frame.time >= impl_->now - seconds) recent.push_back(frame);
  }
  return recent;
}

double LivePitch::now() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->now;
}

}  // namespace raagasense
