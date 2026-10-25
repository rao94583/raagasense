// Names the raag of what's being listened to, from everything heard since
// listening began or the last start over, using what's been learned from
// labeled recordings, and keeping its first guess unless there's good reason
// to change (RaagTracker).
#import <Foundation/Foundation.h>

#include <string>
#include <vector>

#include "core/identify.h"
#include "platform/live_pitch.h"

// The best guesses so far, best first.
struct RSRaagGuesses {
  std::vector<raagasense::RaagMatch> matches;
  // Seconds of the stream heard, and of it held notes.
  double heard_seconds = 0;
  double held_seconds = 0;
};

@interface RSRaagIdentifier : NSObject

// The live pitch is not owned and must outlive the identifier. learning may
// be empty, for no learning.
- (instancetype)initWithLivePitch:(raagasense::LivePitch*)livePitch learning:(raagasense::RaagLearning)learning;

// Takes in the frames heard since the last call and, unless the last guess
// is still being worked out, works out a new one in the background, then
// calls done with it on the main thread. Starts over when listening does.
- (void)update:(void (^)(const RSRaagGuesses& guesses))done;

// Forgets what's been heard, for a new piece.
- (void)startOver;

// How many raags the learning has profiles for.
@property(nonatomic, readonly) NSUInteger learnedRaags;

@end
