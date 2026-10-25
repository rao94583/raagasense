#import "app/macos/raag_identifier.h"

#include <memory>

#include "core/steady.h"

@implementation RSRaagIdentifier {
  raagasense::LivePitch* _livePitch;
  std::shared_ptr<const raagasense::RaagLearning> _learning;
  std::vector<raagasense::PitchFrame> _frames;
  double _lastTime;
  raagasense::RaagTracker _tracker;
  dispatch_queue_t _queue;
  bool _busy;
  double _saFrequency;
  NSUInteger _generation;
}

- (instancetype)initWithLivePitch:(raagasense::LivePitch*)livePitch learning:(raagasense::RaagLearning)learning {
  if ((self = [super init])) {
    _livePitch = livePitch;
    _learning = std::make_shared<const raagasense::RaagLearning>(std::move(learning));
    _lastTime = 0;
    _queue = dispatch_queue_create("raagasense.identify", DISPATCH_QUEUE_SERIAL);
  }
  return self;
}

- (NSUInteger)learnedRaags {
  return _learning->profiles.size();
}

- (void)startOver {
  _frames.clear();
  _tracker = raagasense::RaagTracker();
  ++_generation;
}

- (double)saFrequency {
  return _saFrequency;
}

- (void)setSaFrequency:(double)frequency {
  if (_saFrequency == frequency) return;
  _saFrequency = frequency;
  _tracker = raagasense::RaagTracker();
  ++_generation;
}

- (void)update:(void (^)(const RSRaagGuesses& guesses))done {
  // A stream that has gone back in time is a new one.
  if (_livePitch->now() < _lastTime) {
    [self startOver];
    _lastTime = 0;
  }
  for (const raagasense::PitchFrame& frame : _livePitch->Recent(5)) {
    if (frame.time > _lastTime) _frames.push_back(frame);
  }
  if (!_frames.empty()) _lastTime = _frames.back().time;
  if (_busy || _frames.empty()) return;

  _busy = true;
  auto frames = std::make_shared<std::vector<raagasense::PitchFrame>>(_frames);
  std::shared_ptr<const raagasense::RaagLearning> learning = _learning;
  const double sa = _saFrequency;
  const NSUInteger generation = _generation;
  __weak RSRaagIdentifier* weakSelf = self;
  dispatch_async(_queue, ^{
    const std::vector<raagasense::SteadyNote> notes = raagasense::HeldPitches(*frames);
    auto guesses = std::make_shared<RSRaagGuesses>();
    const auto passing = raagasense::PassingNotes(*frames);
    guesses->matches = sa > 0 ? raagasense::IdentifyRaagAtSa(notes, sa, passing, learning.get())
                             : raagasense::IdentifyRaag(notes, passing, learning.get());
    guesses->heard_seconds = frames->back().time - frames->front().time;
    for (const raagasense::SteadyNote& note : notes) guesses->held_seconds += note.end - note.start;
    dispatch_async(dispatch_get_main_queue(), ^{
      RSRaagIdentifier* identifier = weakSelf;
      if (!identifier) return;
      identifier->_busy = false;
      // Starting over or changing Sa invalidates in-flight work, even when
      // a restarted stream happens to have the same frame timestamps.
      if (generation != identifier->_generation) return;
      guesses->matches = identifier->_tracker.Update(std::move(guesses->matches), frames->back().time);
      done(*guesses);
    });
  });
}

@end
