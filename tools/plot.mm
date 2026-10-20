// Renders a recording's pitch against the svara grid to a PNG, with the same
// drawing code as the apps.
//
//   bazel run //tools:plot -- [--sa HZ] [--width PIXELS] RECORDING OUTPUT.png
//
// Without --sa, estimates Sa from the recording.
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/pitch.h"
#include "core/steady.h"
#include "core/tonic.h"
#include "platform/audio_file.h"
#include "platform/pitch_plot.h"

namespace {

int Usage() {
  std::fprintf(stderr, "usage: plot [--sa HZ] [--width PIXELS] RECORDING OUTPUT.png\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace raagasense;
  double sa = 0;
  int width = 2000;
  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--sa") && i + 1 < argc) {
      sa = std::atof(argv[++i]);
    } else if (!std::strcmp(argv[i], "--width") && i + 1 < argc) {
      width = std::atoi(argv[++i]);
    } else if (argv[i][0] == '-') {
      return Usage();
    } else {
      paths.push_back(argv[i]);
    }
  }
  if (paths.size() != 2 || sa < 0 || width <= 0) return Usage();

  std::string error;
  const std::optional<AudioData> audio = ReadAudioFile(paths[0], &error);
  if (!audio) {
    std::fprintf(stderr, "%s: %s\n", paths[0].c_str(), error.c_str());
    return 1;
  }
  PitchOptions options;
  options.sample_rate = audio->sample_rate;
  PitchTracker tracker(options);
  std::vector<PitchFrame> frames;
  tracker.Push(audio->samples, &frames);
  if (sa == 0) {
    const std::vector<TonicCandidate> candidates = EstimateTonic(FindSteadyNotes(frames));
    if (candidates.empty()) {
      std::fprintf(stderr, "%s: no steady notes to find Sa from; give it with --sa\n", paths[0].c_str());
      return 1;
    }
    sa = candidates[0].frequency;
    std::fprintf(stderr, "Sa = %.2f Hz (estimated)\n", sa);
  }

  PitchPlot plot;
  plot.sa_frequency = sa;
  plot.window = audio->samples.size() / audio->sample_rate;
  plot.now = plot.window;

  const int height = 700;
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context =
      CGBitmapContextCreate(nullptr, width, height, 8, 0, space, kCGImageAlphaPremultipliedLast);
  DrawPitchPlot(context, CGRectMake(0, 0, width, height), plot, frames);
  CGImageRef image = CGBitmapContextCreateImage(context);

  NSURL* url = [NSURL fileURLWithPath:@(paths[1].c_str())];
  CGImageDestinationRef destination = CGImageDestinationCreateWithURL(
      (__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
  bool written = false;
  if (destination) {
    CGImageDestinationAddImage(destination, image, nullptr);
    written = CGImageDestinationFinalize(destination);
    CFRelease(destination);
  }
  CGImageRelease(image);
  CGContextRelease(context);
  CGColorSpaceRelease(space);
  if (!written) {
    std::fprintf(stderr, "%s: can't write the image\n", paths[1].c_str());
    return 1;
  }
  return 0;
}
