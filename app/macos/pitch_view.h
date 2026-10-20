// Shows the live pitch against a grid of svaras.
#import <AppKit/AppKit.h>

#include "platform/live_pitch.h"

@interface RSPitchView : NSView

// Not owned; must outlive the view.
@property(nonatomic) raagasense::LivePitch* livePitch;
@property(nonatomic) double saFrequency;

@end
