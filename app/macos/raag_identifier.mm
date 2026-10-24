#import "app/macos/raag_identifier.h"

#include <memory>

#include "core/steady.h"

namespace {

// At most this many seconds of the latest frames are used, to bound the work.
constexpr double kMaxSeconds = 600;

}  // namespace

@implementation RSRaagIdentifier {
  raagasense::LivePitch* _livePitch;
  std::shared_ptr<const raagasense::RaagLearning> _learning;
  std::vector<raagasense::PitchFrame> _frames;
  double _lastTime;
  dispatch_queue_t _queue;
  bool _busy;
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

- (void)update:(void (^)(const RSRaagGuesses& guesses))done {
  // A stream that has gone back in time is a new one.
  if (_livePitch->now() < _lastTime) {
    _frames.clear();
    _lastTime = 0;
  }
  for (const raagasense::PitchFrame& frame : _livePitch->Recent(5)) {
    if (frame.time > _lastTime) _frames.push_back(frame);
  }
  if (!_frames.empty()) _lastTime = _frames.back().time;
  while (!_frames.empty() && _frames.front().time < _lastTime - kMaxSeconds) _frames.erase(_frames.begin());
  if (_busy || _frames.empty()) return;

  _busy = true;
  auto frames = std::make_shared<std::vector<raagasense::PitchFrame>>(_frames);
  std::shared_ptr<const raagasense::RaagLearning> learning = _learning;
  __weak RSRaagIdentifier* weakSelf = self;
  dispatch_async(_queue, ^{
    const std::vector<raagasense::SteadyNote> notes = raagasense::HeldPitches(*frames);
    auto guesses = std::make_shared<RSRaagGuesses>();
    guesses->matches = raagasense::IdentifyRaag(notes, raagasense::PassingNotes(*frames),
                                                learning->profiles.empty() ? nullptr : learning.get());
    guesses->heard_seconds = frames->back().time - frames->front().time;
    for (const raagasense::SteadyNote& note : notes) guesses->held_seconds += note.end - note.start;
    dispatch_async(dispatch_get_main_queue(), ^{
      RSRaagIdentifier* identifier = weakSelf;
      if (!identifier) return;
      identifier->_busy = false;
      done(*guesses);
    });
  });
}

@end
