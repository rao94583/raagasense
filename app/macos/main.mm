// RaagaSense for macOS: one window showing a sound input's pitch against a
// svara grid, for a chosen Sa or the one found, and its best guesses at the
// raag; and another for recording labeled samples.
#import <AppKit/AppKit.h>

#include <cmath>
#include <memory>

#import "app/macos/pitch_view.h"
#import "app/macos/raag_identifier.h"
#import "app/macos/sample_recorder.h"
#include "platform/audio_input.h"
#include "platform/live_pitch.h"

namespace {

// Sa choices from C3 to B3, which covers most male and female voices.
constexpr const char* kNoteNames[] = {"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};
constexpr double kC3 = 130.8128;
constexpr int kDefaultNote = 1;  // C sharp

double NoteFrequency(int note) { return kC3 * std::pow(2.0, note / 12.0); }

// The user defaults key for the chosen sound input's id; absent for the
// system default.
NSString* const kInputIdKey = @"InputID";

// How often the raag guesses update, in seconds, and how many seconds of held
// notes they need before showing.
constexpr double kGuessInterval = 2;
constexpr double kMinHeldToGuess = 10;

// "A♯" for 233 Hz.
NSString* NoteName(double frequency) {
  const long semitones = std::lround(12 * std::log2(frequency / kC3));
  return @(kNoteNames[((semitones % 12) + 12) % 12]);
}

// What's been learned from labeled recordings, from learned.csv in the
// sample recorder's folder (written by evaluate --learn-to); empty if there's
// none.
raagasense::RaagLearning LoadLearning() {
  NSString* folder = [NSUserDefaults.standardUserDefaults stringForKey:@"SampleFolder"]
                         ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Music/RaagaSense Samples"];
  NSString* text = [NSString stringWithContentsOfFile:[folder stringByAppendingPathComponent:@"learned.csv"]
                                             encoding:NSUTF8StringEncoding
                                                error:nil];
  return text ? raagasense::LearningFromText(text.UTF8String) : raagasense::RaagLearning{};
}

}  // namespace

@interface RSAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate, RSSampleRecorderHost>
@end

@implementation RSAppDelegate {
  std::unique_ptr<raagasense::LivePitch> _livePitch;
  NSWindow* _window;
  RSPitchView* _pitchView;
  NSPopUpButton* _inputPopUp;
  NSButton* _listenButton;
  NSPopUpButton* _saPopUp;
  NSTextField* _raagLabel;
  RSRaagIdentifier* _identifier;
  NSTimer* _guessTimer;
  RSSampleRecorder* _sampleRecorder;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  _livePitch = std::make_unique<raagasense::LivePitch>();
  [self buildMenu];

  // Auto follows the Sa of the best raag guess.
  NSPopUpButton* saPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [saPopUp addItemWithTitle:@"Auto"];
  [saPopUp.menu addItem:[NSMenuItem separatorItem]];
  for (int note = 0; note < 12; ++note) {
    NSMenuItem* item = [saPopUp.menu
        addItemWithTitle:[NSString stringWithFormat:@"%@  (%.1f Hz)", @(kNoteNames[note]), NoteFrequency(note)]
                  action:nil
           keyEquivalent:@""];
    item.tag = note;
  }
  [saPopUp selectItemAtIndex:0];
  saPopUp.target = self;
  saPopUp.action = @selector(saChanged:);
  _saPopUp = saPopUp;

  _identifier = [[RSRaagIdentifier alloc] initWithLivePitch:_livePitch.get() learning:LoadLearning()];
  _raagLabel = [NSTextField labelWithString:@""];
  _raagLabel.font = [NSFont systemFontOfSize:15];
  _raagLabel.lineBreakMode = NSLineBreakByTruncatingTail;
  [self showRaagIdle];

  _inputPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  _inputPopUp.menu.delegate = self;
  _inputPopUp.target = self;
  _inputPopUp.action = @selector(inputChanged:);
  [self menuNeedsUpdate:_inputPopUp.menu];

  _listenButton = [NSButton buttonWithTitle:@"Listen" target:self action:@selector(toggleListening:)];
  _listenButton.keyEquivalent = @" ";

  NSButton* recordButton = [NSButton buttonWithTitle:@"Record Sample…" target:self action:@selector(recordSample:)];

  NSStackView* controls = [NSStackView stackViewWithViews:@[
    [NSTextField labelWithString:@"Sa:"], saPopUp, [NSTextField labelWithString:@"Input:"], _inputPopUp,
    [[NSView alloc] init], recordButton, _listenButton
  ]];
  [controls setCustomSpacing:20 afterView:saPopUp];
  controls.edgeInsets = NSEdgeInsetsMake(8, 12, 8, 12);

  _pitchView = [[RSPitchView alloc] initWithFrame:NSZeroRect];
  _pitchView.livePitch = _livePitch.get();
  _pitchView.saFrequency = NoteFrequency(kDefaultNote);

  NSStackView* raagRow = [NSStackView stackViewWithViews:@[ _raagLabel ]];
  raagRow.edgeInsets = NSEdgeInsetsMake(0, 14, 8, 14);

  NSStackView* content = [NSStackView stackViewWithViews:@[ controls, raagRow, _pitchView ]];
  content.orientation = NSUserInterfaceLayoutOrientationVertical;
  content.spacing = 0;
  [controls.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
  [raagRow.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
  [_pitchView.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;

  _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 900, 560)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.title = @"RaagaSense";
  _window.contentView = content;
  _window.contentMinSize = NSMakeSize(480, 320);
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
  _livePitch->Stop();
}

- (void)buildMenu {
  NSMenu* bar = [[NSMenu alloc] init];
  NSMenuItem* appItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
  NSMenu* appMenu = [[NSMenu alloc] init];
  [appMenu addItemWithTitle:@"Record Sample…" action:@selector(recordSample:) keyEquivalent:@"r"];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:@"Quit RaagaSense" action:@selector(terminate:) keyEquivalent:@"q"];
  appItem.submenu = appMenu;
  NSApp.mainMenu = bar;
}

- (void)saChanged:(NSPopUpButton*)sender {
  if (sender.indexOfSelectedItem > 0) _pitchView.saFrequency = NoteFrequency(static_cast<int>(sender.selectedTag));
}

- (void)showRaagIdle {
  _raagLabel.stringValue =
      _identifier.learnedRaags > 0
          ? [NSString stringWithFormat:@"Raag: press Listen to start (learned from recordings of %lu raags)",
                                       static_cast<unsigned long>(_identifier.learnedRaags)]
          : @"Raag: press Listen to start (no learned.csv found, so from svaras and phrases alone)";
}

// Updates the raag guesses, and the Sa if it's on Auto.
- (void)guessRaag {
  __weak RSAppDelegate* weakSelf = self;
  [_identifier update:^(const RSRaagGuesses& guesses) {
    RSAppDelegate* delegate = weakSelf;
    if (!delegate) return;
    const auto& matches = guesses.matches;
    if (matches.empty() || guesses.held_seconds < kMinHeldToGuess) {
      delegate->_raagLabel.stringValue =
          [NSString stringWithFormat:@"Raag: listening… (%.0f s of held notes of the %.0f s needed)",
                                     guesses.held_seconds, kMinHeldToGuess];
      return;
    }
    NSMutableArray<NSString*>* others = [NSMutableArray array];
    for (size_t i = 1; i < matches.size() && i < 3; ++i) {
      [others addObject:[NSString stringWithFormat:@"%s (Sa %@)", matches[i].raag->name.c_str(), NoteName(matches[i].sa)]];
    }
    delegate->_raagLabel.stringValue = [NSString
        stringWithFormat:@"Raag: %s, Sa %@   ·   or %@   ·   %d:%02d heard", matches[0].raag->name.c_str(),
                         NoteName(matches[0].sa), [others componentsJoinedByString:@", "],
                         static_cast<int>(guesses.heard_seconds) / 60, static_cast<int>(guesses.heard_seconds) % 60];
    if (delegate->_saPopUp.indexOfSelectedItem == 0 && !delegate->_sampleRecorder.recording) {
      delegate->_pitchView.saFrequency = matches[0].sa;
    }
  }];
}

// Lists the connected inputs afresh each time the input menu opens, selecting
// the saved choice if it's connected and the system default otherwise.
- (void)menuNeedsUpdate:(NSMenu*)menu {
  NSString* saved = [NSUserDefaults.standardUserDefaults stringForKey:kInputIdKey];
  [menu removeAllItems];
  NSMenuItem* defaultItem = [menu addItemWithTitle:@"System Default" action:nil keyEquivalent:@""];
  NSMenuItem* selected = defaultItem;
  const std::vector<raagasense::AudioInput> inputs = raagasense::AudioInputs();
  if (!inputs.empty()) [menu addItem:[NSMenuItem separatorItem]];
  for (const raagasense::AudioInput& input : inputs) {
    NSMenuItem* item = [menu addItemWithTitle:@(input.name.c_str()) action:nil keyEquivalent:@""];
    item.representedObject = @(input.id.c_str());
    if ([item.representedObject isEqualToString:saved]) selected = item;
  }
  [_inputPopUp selectItem:selected];
}

- (void)inputChanged:(NSPopUpButton*)sender {
  NSString* inputId = sender.selectedItem.representedObject;
  if (inputId) {
    [NSUserDefaults.standardUserDefaults setObject:inputId forKey:kInputIdKey];
  } else {
    [NSUserDefaults.standardUserDefaults removeObjectForKey:kInputIdKey];
  }
  if (_livePitch->running()) {
    _livePitch->Stop();
    [self startListening:nil];
  }
}

- (void)toggleListening:(id)sender {
  if (_livePitch->running()) {
    _livePitch->Stop();
    _listenButton.title = @"Listen";
    return;
  }
  [self startListening:nil];
}

// Keeps the raag guesses updating while listening.
- (void)startGuessing {
  [_guessTimer invalidate];
  __weak RSAppDelegate* weakSelf = self;
  _guessTimer = [NSTimer scheduledTimerWithTimeInterval:kGuessInterval
                                                repeats:YES
                                                  block:^(NSTimer* timer) {
                                                    RSAppDelegate* delegate = weakSelf;
                                                    if (!delegate || !delegate->_livePitch->running()) {
                                                      [timer invalidate];
                                                      return;
                                                    }
                                                    [delegate guessRaag];
                                                  }];
}

// Starts listening, and calls done, if given, with nil or the error, which
// is otherwise shown in an alert.
- (void)startListening:(void (^)(NSString* error))done {
  NSString* inputId = _inputPopUp.selectedItem.representedObject;
  _listenButton.enabled = NO;
  __weak RSAppDelegate* weakSelf = self;
  _livePitch->Start(inputId ? inputId.UTF8String : "", [weakSelf, done](const std::string& error) {
    RSAppDelegate* delegate = weakSelf;
    if (!delegate) return;
    delegate->_listenButton.enabled = YES;
    if (!error.empty()) {
      delegate->_listenButton.title = @"Listen";
      if (done) {
        done(@(error.c_str()));
        return;
      }
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Can't listen to the sound input";
      alert.informativeText = @(error.c_str());
      [alert beginSheetModalForWindow:delegate->_window completionHandler:nil];
      return;
    }
    delegate->_listenButton.title = @"Stop";
    delegate->_raagLabel.stringValue = @"Raag: listening…";
    [delegate startGuessing];
    if (done) done(nil);
  });
}

- (void)recordSample:(id)sender {
  if (!_sampleRecorder) _sampleRecorder = [[RSSampleRecorder alloc] initWithLivePitch:_livePitch.get() host:self];
  [_sampleRecorder showWindow:nil];
}

- (void)ensureListening:(void (^)(NSString* error))done {
  if (_livePitch->running()) {
    done(nil);
    return;
  }
  [self startListening:done];
}

- (void)showSa:(double)frequency {
  _pitchView.saFrequency = frequency;
}

@end

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    RSAppDelegate* delegate = [[RSAppDelegate alloc] init];
    app.delegate = delegate;
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    [app run];
  }
  return 0;
}
