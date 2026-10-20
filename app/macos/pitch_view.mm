#import "app/macos/pitch_view.h"

#include "platform/pitch_plot.h"

@implementation RSPitchView {
  NSTimer* _timer;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _saFrequency = 138.59;
    __weak RSPitchView* weakSelf = self;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60
                                             repeats:YES
                                               block:^(NSTimer*) {
                                                 RSPitchView* view = weakSelf;
                                                 if (view.livePitch && view.livePitch->running()) view.needsDisplay = YES;
                                               }];
  }
  return self;
}

- (void)dealloc {
  [_timer invalidate];
}

- (void)setSaFrequency:(double)saFrequency {
  _saFrequency = saFrequency;
  self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  raagasense::PitchPlot plot;
  plot.sa_frequency = _saFrequency;
  plot.dark = [self.effectiveAppearance bestMatchFromAppearancesWithNames:@[
    NSAppearanceNameAqua, NSAppearanceNameDarkAqua
  ]] == NSAppearanceNameDarkAqua;
  std::vector<raagasense::PitchFrame> frames;
  if (_livePitch) {
    plot.now = _livePitch->now();
    frames = _livePitch->Recent(plot.window);
  }
  raagasense::DrawPitchPlot(NSGraphicsContext.currentContext.CGContext, self.bounds, plot, frames);
}

@end
