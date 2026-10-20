#include "platform/pitch_plot.h"

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "core/svara.h"

namespace raagasense {

namespace {

// The plot spans mandra Pa to taar Pa.
constexpr double kLowCents = -500;
constexpr double kHighCents = 1900;
constexpr CGFloat kGutter = 36;
// Frames further apart than this aren't joined by a line.
constexpr double kMaxGapSeconds = 0.06;
constexpr double kMaxJumpCents = 150;

struct Colors {
  CGColorRef background, sa, shuddha, vikrit, label, trace, current;
};

Colors MakeColors(bool dark) {
  if (dark) {
    return {CGColorCreateSRGB(0.11, 0.11, 0.12, 1), CGColorCreateSRGB(0.95, 0.65, 0.25, 0.7),
            CGColorCreateSRGB(1, 1, 1, 0.22),        CGColorCreateSRGB(1, 1, 1, 0.08),
            CGColorCreateSRGB(1, 1, 1, 0.7),         CGColorCreateSRGB(0.4, 0.75, 1, 1),
            CGColorCreateSRGB(1, 1, 1, 0.95)};
  }
  return {CGColorCreateSRGB(1, 1, 1, 1),          CGColorCreateSRGB(0.85, 0.45, 0.05, 0.7),
          CGColorCreateSRGB(0, 0, 0, 0.2),         CGColorCreateSRGB(0, 0, 0, 0.07),
          CGColorCreateSRGB(0, 0, 0, 0.65),        CGColorCreateSRGB(0.05, 0.4, 0.8, 1),
          CGColorCreateSRGB(0, 0, 0, 0.9)};
}

void ReleaseColors(const Colors& colors) {
  for (CGColorRef color : {colors.background, colors.sa, colors.shuddha, colors.vikrit, colors.label,
                           colors.trace, colors.current}) {
    CGColorRelease(color);
  }
}

// Draws text with its baseline's left end at (x, y).
void DrawText(CGContextRef context, const std::string& text, CGFloat x, CGFloat y, CGFloat size,
              CGColorRef color) {
  CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
  NSDictionary* attributes = @{
    (__bridge id)kCTFontAttributeName : (__bridge id)font,
    (__bridge id)kCTForegroundColorAttributeName : (__bridge id)color,
  };
  NSAttributedString* string = [[NSAttributedString alloc] initWithString:@(text.c_str())
                                                               attributes:attributes];
  CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)string);
  CGContextSetTextMatrix(context, CGAffineTransformIdentity);
  CGContextSetTextPosition(context, x, y);
  CTLineDraw(line, context);
  CFRelease(line);
  CFRelease(font);
}

}  // namespace

void DrawPitchPlot(CGContextRef context, CGRect bounds, const PitchPlot& plot,
                   const std::vector<PitchFrame>& frames) {
  const Colors colors = MakeColors(plot.dark);
  CGContextSaveGState(context);
  CGContextSetFillColorWithColor(context, colors.background);
  CGContextFillRect(context, bounds);

  const CGFloat left = CGRectGetMinX(bounds) + kGutter;
  const CGFloat right = CGRectGetMaxX(bounds);
  const CGFloat bottom = CGRectGetMinY(bounds) + 8;
  const CGFloat top = CGRectGetMaxY(bounds) - 8;
  auto y_for = [&](double cents) { return bottom + (cents - kLowCents) / (kHighCents - kLowCents) * (top - bottom); };
  auto x_for = [&](double time) { return right - (plot.now - time) / plot.window * (right - left); };

  // One line per svara: Sa strongest, then the shuddha svaras, then komal
  // and tivra.
  for (int cents = static_cast<int>(kLowCents); cents <= kHighCents; cents += 100) {
    const SvaraPosition position = NearestSvara(cents);
    const bool shuddha = position.svara == kSa || position.svara == kRe || position.svara == kGa ||
                         position.svara == kMa || position.svara == kPa || position.svara == kDha ||
                         position.svara == kNi;
    CGColorRef color = position.svara == kSa ? colors.sa : shuddha ? colors.shuddha : colors.vikrit;
    const CGFloat y = y_for(cents);
    CGContextSetStrokeColorWithColor(context, color);
    CGContextSetLineWidth(context, position.svara == kSa ? 1.5 : 1);
    CGContextMoveToPoint(context, left, y);
    CGContextAddLineToPoint(context, right, y);
    CGContextStrokePath(context);
    if (shuddha) DrawText(context, SvaraLabel(position), CGRectGetMinX(bounds) + 10, y - 4, 11, colors.label);
  }

  // The pitch trace, broken across unvoiced frames and large jumps.
  CGContextSetStrokeColorWithColor(context, colors.trace);
  CGContextSetLineWidth(context, 2);
  CGContextSetLineJoin(context, kCGLineJoinRound);
  CGContextSetLineCap(context, kCGLineCapRound);
  const PitchFrame* previous = nullptr;
  double previous_cents = 0;
  for (const PitchFrame& frame : frames) {
    if (!frame.voiced || frame.confidence < plot.min_confidence) {
      previous = nullptr;
      continue;
    }
    const double cents = CentsFromSa(frame.frequency, plot.sa_frequency);
    const CGPoint point = CGPointMake(x_for(frame.time), y_for(cents));
    if (previous && frame.time - previous->time < kMaxGapSeconds &&
        std::abs(cents - previous_cents) < kMaxJumpCents) {
      CGContextAddLineToPoint(context, point.x, point.y);
    } else {
      CGContextStrokePath(context);
      CGContextMoveToPoint(context, point.x, point.y);
      CGContextAddLineToPoint(context, point.x, point.y);
    }
    previous = &frame;
    previous_cents = cents;
  }
  CGContextStrokePath(context);

  // The svara being sung now, with its deviation from equal temperament.
  if (previous && plot.now - previous->time < 0.2) {
    const SvaraPosition position = NearestSvara(previous_cents);
    char deviation[16];
    std::snprintf(deviation, sizeof deviation, "%+.0f¢", position.deviation);
    DrawText(context, SvaraLabel(position), right - 120, top - 44, 40, colors.current);
    DrawText(context, deviation, right - 64, top - 40, 16, colors.label);
  }

  CGContextRestoreGState(context);
  ReleaseColors(colors);
}

}  // namespace raagasense
