#include "platform/live_pitch.h"

#import <AVFoundation/AVFoundation.h>
#if !TARGET_OS_IPHONE
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#endif

#include <deque>
#include <mutex>

namespace raagasense {

namespace {

// How much history Recent() can return.
constexpr double kKeepSeconds = 60;

#if !TARGET_OS_IPHONE

AudioObjectPropertyAddress GlobalProperty(AudioObjectPropertySelector selector) {
  return {selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

std::string StringProperty(AudioObjectID object, AudioObjectPropertySelector selector) {
  const AudioObjectPropertyAddress address = GlobalProperty(selector);
  CFStringRef value = nullptr;
  UInt32 size = sizeof value;
  if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) != noErr || !value) return "";
  NSString* string = (__bridge_transfer NSString*)value;
  return string.UTF8String ?: "";
}

bool HasInputStreams(AudioDeviceID device) {
  const AudioObjectPropertyAddress address = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
                                              kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  return AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) == noErr && size > 0;
}

// kAudioObjectUnknown when no connected device has this id.
AudioDeviceID DeviceForInputId(const std::string& input_id) {
  const AudioObjectPropertyAddress address = GlobalProperty(kAudioHardwarePropertyTranslateUIDToDevice);
  CFStringRef uid = (__bridge CFStringRef) @(input_id.c_str());
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 size = sizeof device;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof uid, &uid, &size, &device) != noErr) {
    return kAudioObjectUnknown;
  }
  return device;
}

#endif

}  // namespace

std::vector<AudioInput> AudioInputs() {
  std::vector<AudioInput> inputs;
#if !TARGET_OS_IPHONE
  const AudioObjectPropertyAddress address = GlobalProperty(kAudioHardwarePropertyDevices);
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) return inputs;
  std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr) {
    return inputs;
  }
  devices.resize(size / sizeof(AudioDeviceID));
  for (AudioDeviceID device : devices) {
    if (!HasInputStreams(device)) continue;
    AudioInput input{StringProperty(device, kAudioDevicePropertyDeviceUID),
                     StringProperty(device, kAudioObjectPropertyName)};
    if (!input.id.empty()) inputs.push_back(input);
  }
#endif
  return inputs;
}

struct LivePitch::Impl {
  AVAudioEngine* engine = nil;
  mutable std::mutex mutex;
  std::unique_ptr<PitchTracker> tracker;
  std::deque<PitchFrame> frames;
  double now = 0;

  // On the tap's thread: mixes the buffer to mono and tracks its pitch.
  void Process(AVAudioPCMBuffer* buffer) {
    const AVAudioFrameCount count = buffer.frameLength;
    const AVAudioChannelCount channels = buffer.format.channelCount;
    std::vector<float> mono(count, 0.0f);
    for (AVAudioChannelCount c = 0; c < channels; ++c) {
      const float* data = buffer.floatChannelData[c];
      for (AVAudioFrameCount i = 0; i < count; ++i) mono[i] += data[i] / channels;
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
#if !TARGET_OS_IPHONE
    // The device has to be set before the input's format is read.
    if (!input_id.empty()) {
      AudioDeviceID device = DeviceForInputId(input_id);
      if (device == kAudioObjectUnknown) {
        engine = nil;
        return "The chosen sound input isn't connected.";
      }
      if (AudioUnitSetProperty(input.audioUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                               &device, sizeof device) != noErr) {
        engine = nil;
        return "Can't switch to the chosen sound input.";
      }
    }
#endif
    AVAudioFormat* format = [input outputFormatForBus:0];
    if (format.sampleRate == 0 || format.channelCount == 0) {
      engine = nil;
      return "No audio input device is available.";
    }
    {
      std::lock_guard lock(mutex);
      PitchOptions options;
      options.sample_rate = format.sampleRate;
      tracker = std::make_unique<PitchTracker>(options);
      frames.clear();
      now = 0;
    }
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
  [impl_->engine.inputNode removeTapOnBus:0];
  [impl_->engine stop];
  impl_->engine = nil;
}

bool LivePitch::running() const { return impl_->engine != nil; }

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
