#include "platform/audio_input.h"

#import <Foundation/Foundation.h>
#if !TARGET_OS_IPHONE
#import <CoreAudio/CoreAudio.h>
#endif

namespace raagasense {

#if !TARGET_OS_IPHONE

namespace {

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

int InputChannels(AudioDeviceID device) {
  const AudioObjectPropertyAddress address = {kAudioDevicePropertyStreamConfiguration,
                                              kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size == 0) return 0;
  std::vector<std::byte> storage(size);
  auto* buffers = reinterpret_cast<AudioBufferList*>(storage.data());
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, buffers) != noErr) return 0;
  int channels = 0;
  for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i) channels += buffers->mBuffers[i].mNumberChannels;
  return channels;
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

}  // namespace

#endif

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
    const int channels = InputChannels(device);
    if (channels == 0) continue;
    AudioInput input{StringProperty(device, kAudioDevicePropertyDeviceUID),
                     StringProperty(device, kAudioObjectPropertyName), channels};
    if (!input.id.empty()) inputs.push_back(input);
  }
#endif
  return inputs;
}

std::string UseAudioInput(AudioUnit unit, const std::string& input_id) {
#if !TARGET_OS_IPHONE
  AudioDeviceID device = DeviceForInputId(input_id);
  if (device == kAudioObjectUnknown) return "The chosen sound input isn't connected.";
  if (AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device,
                           sizeof device) != noErr) {
    return "Can't switch to the chosen sound input.";
  }
#endif
  return "";
}

}  // namespace raagasense
