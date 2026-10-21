#import "app/macos/sample_recorder.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/raag.h"
#include "core/sa_finder.h"

namespace {

// User defaults keys for the choices to remember.
NSString* const kFolderKey = @"SampleFolder";
NSString* const kRaagKey = @"SampleRaag";
NSString* const kPerformerKey = @"SamplePerformer";

// How often the status updates, in seconds.
constexpr double kTick = 0.5;
// Held notes needed before showing a Sa at all, in seconds.
constexpr double kShowSaAfter = 5;

// Note names for file names ("A_sharp") and for showing ("A♯").
constexpr const char* kFileNotes[] = {"C",       "C_sharp", "D",       "D_sharp", "E",       "F",
                                      "F_sharp", "G",       "G_sharp", "A",       "A_sharp", "B"};
constexpr const char* kShownNotes[] = {"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};

// The note nearest a frequency, as semitones above C, and the cents off it.
std::pair<int, double> Note(double frequency) {
  const double cents = 1200 * std::log2(frequency / 130.8128);
  const long semitones = std::lround(cents / 100);
  return {static_cast<int>(((semitones % 12) + 12) % 12), cents - 100 * semitones};
}

NSString* ShownNote(double frequency) {
  const auto [note, cents] = Note(frequency);
  return [NSString stringWithFormat:@"%@ (%+.0f¢)", @(kShownNotes[note]), cents];
}

// Lower case letters and digits, with anything else as single underscores.
NSString* Slug(NSString* text) {
  NSMutableString* slug = [NSMutableString string];
  for (NSUInteger i = 0; i < text.length; ++i) {
    const unichar c = [text.lowercaseString characterAtIndex:i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      [slug appendFormat:@"%C", c];
    } else if (slug.length > 0 && ![slug hasSuffix:@"_"]) {
      [slug appendString:@"_"];
    }
  }
  while ([slug hasSuffix:@"_"]) [slug deleteCharactersInRange:NSMakeRange(slug.length - 1, 1)];
  return slug;
}

NSString* CsvField(NSString* text) {
  if ([text rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@",\"\n"]].location ==
      NSNotFound) {
    return text;
  }
  return [NSString stringWithFormat:@"\"%@\"", [text stringByReplacingOccurrencesOfString:@"\"" withString:@"\"\""]];
}

NSString* MinutesAndSeconds(double seconds) {
  return [NSString stringWithFormat:@"%d:%02d", static_cast<int>(seconds) / 60, static_cast<int>(seconds) % 60];
}

}  // namespace

@interface RSSampleRecorder () <NSWindowDelegate>
@end

@implementation RSSampleRecorder {
  raagasense::LivePitch* _livePitch;
  __weak id<RSSampleRecorderHost> _host;

  NSPopUpButton* _raagPopUp;
  NSComboBox* _performerBox;
  NSTextField* _artistField;
  NSTextField* _folderLabel;
  NSTextField* _statusLabel;
  NSProgressIndicator* _progress;
  NSButton* _startButton;
  NSButton* _stopButton;
  NSButton* _discardButton;

  // The recording in progress, if any.
  const raagasense::Raag* _raag;
  std::unique_ptr<raagasense::SaFinder> _finder;
  NSTimer* _timer;
  NSString* _partialPath;
  // Stream times: when recording began, and the latest frame given to the
  // finder.
  double _startTime;
  double _lastTime;
}

- (instancetype)initWithLivePitch:(raagasense::LivePitch*)livePitch host:(id<RSSampleRecorderHost>)host {
  NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 520, 300)
                                                 styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  window.title = @"Record a Sample";
  if ((self = [super initWithWindow:window])) {
    _livePitch = livePitch;
    _host = host;
    window.delegate = self;
    [self buildContent];
    [window center];
  }
  return self;
}

- (void)buildContent {
  NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;

  _raagPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  for (const raagasense::Raag& raag : raagasense::Raags()) [_raagPopUp addItemWithTitle:@(raag.name.c_str())];
  if (NSString* raag = [defaults stringForKey:kRaagKey]) [_raagPopUp selectItemWithTitle:raag];

  _performerBox = [[NSComboBox alloc] initWithFrame:NSZeroRect];
  [_performerBox addItemsWithObjectValues:@[
    @"male", @"female", @"sitar", @"sarod", @"bansuri", @"santoor", @"violin", @"shehnai", @"harmonium"
  ]];
  _performerBox.stringValue = [defaults stringForKey:kPerformerKey] ?: @"female";
  _performerBox.toolTip = @"Male or female voice, or the instrument";

  _artistField = [NSTextField textFieldWithString:@""];
  _artistField.placeholderString = @"Optional";

  _folderLabel = [NSTextField labelWithString:@""];
  _folderLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
  [_folderLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                         forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSButton* chooseButton = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(chooseFolder:)];
  NSStackView* folderRow = [NSStackView stackViewWithViews:@[ _folderLabel, chooseButton ]];
  [self showFolder];

  NSGridView* form = [NSGridView gridViewWithViews:@[
    @[ [NSTextField labelWithString:@"Raag:"], _raagPopUp ],
    @[ [NSTextField labelWithString:@"Performer:"], _performerBox ],
    @[ [NSTextField labelWithString:@"Artist:"], _artistField ],
    @[ [NSTextField labelWithString:@"Save in:"], folderRow ],
  ]];
  [form columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  form.rowAlignment = NSGridRowAlignmentNone;
  form.rowSpacing = 10;
  for (NSInteger row = 0; row < form.numberOfRows; ++row) [form rowAtIndex:row].yPlacement = NSGridCellPlacementCenter;
  [_artistField.widthAnchor constraintGreaterThanOrEqualToConstant:260].active = YES;

  _statusLabel = [NSTextField wrappingLabelWithString:
                                  @"Choose the raag, start the music, then press Start. Recording stops by itself "
                                  @"once Sa is clear."];
  _progress = [[NSProgressIndicator alloc] init];
  _progress.indeterminate = NO;
  _progress.minValue = 0;
  _progress.maxValue = 1;

  _startButton = [NSButton buttonWithTitle:@"Start" target:self action:@selector(start:)];
  _startButton.keyEquivalent = @"\r";
  _stopButton = [NSButton buttonWithTitle:@"Stop and Save" target:self action:@selector(stopAndSave:)];
  _discardButton = [NSButton buttonWithTitle:@"Discard" target:self action:@selector(discard:)];
  NSStackView* buttons = [NSStackView stackViewWithViews:@[ [[NSView alloc] init], _discardButton, _stopButton,
                                                             _startButton ]];
  [self setRecording:NO];

  NSStackView* content = [NSStackView stackViewWithViews:@[ form, _statusLabel, _progress, buttons ]];
  content.orientation = NSUserInterfaceLayoutOrientationVertical;
  content.alignment = NSLayoutAttributeLeading;
  content.spacing = 14;
  content.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
  for (NSView* view in @[ form, _statusLabel, _progress, buttons ]) {
    [view.widthAnchor constraintEqualToAnchor:content.widthAnchor constant:-40].active = YES;
  }
  self.window.contentView = content;
}

- (NSString*)folder {
  return [NSUserDefaults.standardUserDefaults stringForKey:kFolderKey]
             ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Music/RaagaSense Samples"];
}

- (void)showFolder {
  _folderLabel.stringValue = [self.folder stringByAbbreviatingWithTildeInPath];
  _folderLabel.toolTip = self.folder;
}

- (void)chooseFolder:(id)sender {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseDirectories = YES;
  panel.canChooseFiles = NO;
  panel.canCreateDirectories = YES;
  panel.prompt = @"Save Here";
  panel.directoryURL = [NSURL fileURLWithPath:self.folder];
  [panel beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse response) {
                  if (response != NSModalResponseOK) return;
                  [NSUserDefaults.standardUserDefaults setObject:panel.URL.path forKey:kFolderKey];
                  [self showFolder];
                }];
}

- (void)setRecording:(BOOL)recording {
  for (NSControl* control in @[ _raagPopUp, _performerBox, _artistField, _startButton ]) control.enabled = !recording;
  _stopButton.enabled = recording;
  _discardButton.enabled = recording;
}

- (void)showError:(NSString*)message {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Can't record";
  alert.informativeText = message;
  [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)start:(id)sender {
  if (Slug(_performerBox.stringValue).length == 0) {
    [self showError:@"Enter the performer: male, female, or the instrument."];
    return;
  }
  NSError* error = nil;
  if (![NSFileManager.defaultManager createDirectoryAtPath:self.folder
                               withIntermediateDirectories:YES
                                                attributes:nil
                                                     error:&error]) {
    [self showError:error.localizedDescription];
    return;
  }
  NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
  [defaults setObject:_raagPopUp.titleOfSelectedItem forKey:kRaagKey];
  [defaults setObject:_performerBox.stringValue forKey:kPerformerKey];

  [self setRecording:YES];
  __weak RSSampleRecorder* weakSelf = self;
  [_host ensureListening:^(NSString* listenError) {
    RSSampleRecorder* recorder = weakSelf;
    if (!recorder) return;
    if (listenError) {
      [recorder setRecording:NO];
      [recorder showError:listenError];
      return;
    }
    [recorder beginRecording];
  }];
}

- (void)beginRecording {
  _raag = &raagasense::Raags()[_raagPopUp.indexOfSelectedItem];
  _finder = std::make_unique<raagasense::SaFinder>(*_raag);
  _partialPath = [self.folder stringByAppendingPathComponent:@".recording.m4a"];
  const std::string error = _livePitch->StartRecording(_partialPath.UTF8String);
  if (!error.empty()) {
    [self setRecording:NO];
    [self showError:@(error.c_str())];
    return;
  }
  _startTime = _lastTime = _livePitch->now();
  _progress.doubleValue = 0;
  [self update];
  __weak RSSampleRecorder* weakSelf = self;
  _timer = [NSTimer scheduledTimerWithTimeInterval:kTick
                                           repeats:YES
                                             block:^(NSTimer*) {
                                               [weakSelf tick];
                                             }];
}

- (void)tick {
  if (!_livePitch->running()) {
    // Listening was stopped in the main window.
    [self finish:YES];
    return;
  }
  std::vector<raagasense::PitchFrame> fresh;
  for (const raagasense::PitchFrame& frame : _livePitch->Recent(5)) {
    if (frame.time > _lastTime) fresh.push_back(frame);
  }
  if (!fresh.empty()) {
    _lastTime = fresh.back().time;
    _finder->Add(fresh);
  }
  [self update];
  if (_finder->status().confident) [self finish:YES];
}

- (void)update {
  const raagasense::SaStatus& status = _finder->status();
  const double needed = raagasense::SaFinderOptions().min_held_seconds;
  _progress.doubleValue = std::min(1.0, status.held_seconds / needed);
  NSString* sa = status.sa > 0 && status.held_seconds >= kShowSaAfter
                     ? [NSString stringWithFormat:@"Sa looks like %@, for the last %.0f s.", ShownNote(status.sa),
                                                  status.stable_seconds]
                     : @"No Sa yet.";
  _statusLabel.stringValue =
      [NSString stringWithFormat:@"Recording %@ of %@. Held notes so far: %.0f s of the %.0f s needed.\n%@",
                                 MinutesAndSeconds(_lastTime - _startTime), @(_raag->name.c_str()), status.held_seconds,
                                 needed, sa];
  if (status.sa > 0 && status.held_seconds >= kShowSaAfter) [_host showSa:status.sa];
}

- (void)stopAndSave:(id)sender {
  [self finish:YES];
}

- (void)discard:(id)sender {
  [self finish:NO];
}

- (void)windowWillClose:(NSNotification*)notification {
  if (_timer) [self finish:YES];
}

// Stops recording, and either names the file and adds it to labels.csv or
// deletes it.
- (void)finish:(BOOL)save {
  [_timer invalidate];
  _timer = nil;
  const double seconds = _livePitch->StopRecording();
  [self setRecording:NO];
  NSFileManager* files = NSFileManager.defaultManager;
  if (!save) {
    [files removeItemAtPath:_partialPath error:nil];
    _statusLabel.stringValue = @"Discarded.";
    return;
  }

  const raagasense::SaStatus status = _finder->status();
  NSString* performer = Slug(_performerBox.stringValue);
  NSString* artist = Slug(_artistField.stringValue);
  NSMutableString* stem = [NSMutableString stringWithFormat:@"%@_%@", Slug(@(_raag->name.c_str())), performer];
  if (artist.length > 0) [stem appendFormat:@"_%@", artist];
  // Only a Sa it's sure of goes in the name.
  if (status.confident) [stem appendFormat:@"_%s", kFileNotes[Note(status.sa).first]];
  NSString* name = [stem stringByAppendingPathExtension:@"m4a"];
  for (int n = 2; [files fileExistsAtPath:[self.folder stringByAppendingPathComponent:name]]; ++n) {
    name = [[NSString stringWithFormat:@"%@_%d", stem, n] stringByAppendingPathExtension:@"m4a"];
  }
  NSError* error = nil;
  if (![files moveItemAtPath:_partialPath toPath:[self.folder stringByAppendingPathComponent:name] error:&error]) {
    [self showError:error.localizedDescription];
    return;
  }

  NSString* sure = status.confident ? @"sure" : @"stopped before it was sure";
  NSMutableString* notes = [NSMutableString
      stringWithFormat:@"Recorded with RaagaSense; Sa found live from the raag (%@, held notes %.0f s, lead %.2f)",
                       sure, status.held_seconds, status.margin];
  if (artist.length > 0) [notes appendFormat:@"; artist %@", _artistField.stringValue];
  NSString* labels = [self.folder stringByAppendingPathComponent:@"labels.csv"];
  NSMutableString* row = [NSMutableString string];
  if (![files fileExistsAtPath:labels]) [row appendString:@"file,raag,sa_hz,singer,notes,sa_checked\n"];
  const double sa = status.held_seconds >= kShowSaAfter ? status.sa : 0;
  [row appendFormat:@"%@,%@,%.1f,%@,%@,no\n", CsvField(name), CsvField(@(_raag->name.c_str())), sa,
                    CsvField(performer), CsvField(notes)];
  NSFileHandle* handle = [NSFileHandle fileHandleForWritingAtPath:labels];
  if (handle) {
    [handle seekToEndOfFile];
    [handle writeData:[row dataUsingEncoding:NSUTF8StringEncoding]];
    [handle closeFile];
  } else {
    [row writeToFile:labels atomically:YES encoding:NSUTF8StringEncoding error:nil];
  }

  _statusLabel.stringValue = [NSString
      stringWithFormat:@"Saved %@ (%@).\n%@", name, MinutesAndSeconds(seconds),
                       status.sa > 0 && status.held_seconds >= kShowSaAfter
                           ? [NSString stringWithFormat:@"Sa %@, %@. Please check it by ear.", ShownNote(status.sa), sure]
                           : @"No Sa: too few held notes."];
}

@end
