// A window for recording labeled samples: choose the raag and performer,
// start, and it records until it has found Sa with confidence, then saves the
// file, named for what it is, and adds it to labels.csv in the same folder.
#import <AppKit/AppKit.h>

#include "platform/live_pitch.h"

// What the sample recorder needs from the app.
@protocol RSSampleRecorderHost <NSObject>
// Starts listening if it isn't already; calls done with nil, or the reason
// it can't.
- (void)ensureListening:(void (^)(NSString* error))done;
// Shows the Sa found so far.
- (void)showSa:(double)frequency;
@end

@interface RSSampleRecorder : NSWindowController

// The live pitch is not owned and must outlive the recorder.
- (instancetype)initWithLivePitch:(raagasense::LivePitch*)livePitch host:(id<RSSampleRecorderHost>)host;

@end
