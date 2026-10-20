// RaagaSense for macOS: one window showing a sound input's pitch against a
// svara grid for a chosen Sa.
#import <AppKit/AppKit.h>

#include <cmath>
#include <memory>

#import "app/macos/pitch_view.h"
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

}  // namespace

@interface RSAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate>
@end

@implementation RSAppDelegate {
  std::unique_ptr<raagasense::LivePitch> _livePitch;
  NSWindow* _window;
  RSPitchView* _pitchView;
  NSPopUpButton* _inputPopUp;
  NSButton* _listenButton;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  _livePitch = std::make_unique<raagasense::LivePitch>();
  [self buildMenu];

  NSPopUpButton* saPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  for (int note = 0; note < 12; ++note) {
    [saPopUp addItemWithTitle:[NSString stringWithFormat:@"%@  (%.1f Hz)", @(kNoteNames[note]), NoteFrequency(note)]];
  }
  [saPopUp selectItemAtIndex:kDefaultNote];
  saPopUp.target = self;
  saPopUp.action = @selector(saChanged:);

  _inputPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  _inputPopUp.menu.delegate = self;
  _inputPopUp.target = self;
  _inputPopUp.action = @selector(inputChanged:);
  [self menuNeedsUpdate:_inputPopUp.menu];

  _listenButton = [NSButton buttonWithTitle:@"Listen" target:self action:@selector(toggleListening:)];
  _listenButton.keyEquivalent = @" ";

  NSStackView* controls = [NSStackView stackViewWithViews:@[
    [NSTextField labelWithString:@"Sa:"], saPopUp, [NSTextField labelWithString:@"Input:"], _inputPopUp,
    [[NSView alloc] init], _listenButton
  ]];
  [controls setCustomSpacing:20 afterView:saPopUp];
  controls.edgeInsets = NSEdgeInsetsMake(8, 12, 8, 12);

  _pitchView = [[RSPitchView alloc] initWithFrame:NSZeroRect];
  _pitchView.livePitch = _livePitch.get();
  _pitchView.saFrequency = NoteFrequency(kDefaultNote);

  NSStackView* content = [NSStackView stackViewWithViews:@[ controls, _pitchView ]];
  content.orientation = NSUserInterfaceLayoutOrientationVertical;
  content.spacing = 0;
  [controls.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
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
  [appMenu addItemWithTitle:@"Quit RaagaSense" action:@selector(terminate:) keyEquivalent:@"q"];
  appItem.submenu = appMenu;
  NSApp.mainMenu = bar;
}

- (void)saChanged:(NSPopUpButton*)sender {
  _pitchView.saFrequency = NoteFrequency(static_cast<int>(sender.indexOfSelectedItem));
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
    [self startListening];
  }
}

- (void)toggleListening:(id)sender {
  if (_livePitch->running()) {
    _livePitch->Stop();
    _listenButton.title = @"Listen";
    return;
  }
  [self startListening];
}

- (void)startListening {
  NSString* inputId = _inputPopUp.selectedItem.representedObject;
  _listenButton.enabled = NO;
  __weak RSAppDelegate* weakSelf = self;
  _livePitch->Start(inputId ? inputId.UTF8String : "", [weakSelf](const std::string& error) {
    RSAppDelegate* delegate = weakSelf;
    if (!delegate) return;
    delegate->_listenButton.enabled = YES;
    if (!error.empty()) {
      delegate->_listenButton.title = @"Listen";
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Can't listen to the sound input";
      alert.informativeText = @(error.c_str());
      [alert beginSheetModalForWindow:delegate->_window completionHandler:nil];
      return;
    }
    delegate->_listenButton.title = @"Stop";
  });
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
