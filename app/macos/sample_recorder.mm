#import "app/macos/sample_recorder.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/raag.h"
#include "core/sa_finder.h"
#include "core/svara.h"

namespace {

// User defaults keys for the choices to remember.
NSString* const kFolderKey = @"SampleFolder";
NSString* const kRaagKey = @"SampleRaag";
// Raags not in the app's list, as dictionaries of a name and svaras.
NSString* const kOtherRaagsKey = @"SampleOtherRaags";
NSString* const kOtherRaagItem = @"Other Raag…";
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

// Svaras from Bhatkhande letters separated by spaces or commas, in ascending
// order; nullopt if any letter isn't a svara's.
std::optional<std::vector<int>> ParseSvaras(NSString* text) {
  std::vector<int> svaras;
  for (NSString* letter in [text componentsSeparatedByCharactersInSet:
                                     [NSCharacterSet characterSetWithCharactersInString:@" ,"]]) {
    if (letter.length == 0) continue;
    const int svara = raagasense::SvaraFromLetter(letter.UTF8String);
    if (svara < 0) return std::nullopt;
    if (std::find(svaras.begin(), svaras.end(), svara) == svaras.end()) svaras.push_back(svara);
  }
  std::sort(svaras.begin(), svaras.end());
  return svaras;
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

  // The raag chosen before the Other Raag item, to go back to if that's
  // cancelled.
  NSString* _previousRaag;
  // The recording in progress, if any, and its raag. Without the raag's
  // svaras there's no finder, and no Sa.
  raagasense::Raag _raag;
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
  _raagPopUp.target = self;
  _raagPopUp.action = @selector(raagChosen:);
  [self buildRaagMenu:[defaults stringForKey:kRaagKey]];

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

// The app's raags, then any others added, then the item to add another;
// selects the named one if it's there.
- (void)buildRaagMenu:(NSString*)selected {
  [_raagPopUp removeAllItems];
  for (const raagasense::Raag& raag : raagasense::Raags()) [_raagPopUp addItemWithTitle:@(raag.name.c_str())];
  NSArray<NSDictionary*>* others = [NSUserDefaults.standardUserDefaults arrayForKey:kOtherRaagsKey];
  if (others.count > 0) [_raagPopUp.menu addItem:[NSMenuItem separatorItem]];
  for (NSDictionary* other in others) [_raagPopUp addItemWithTitle:other[@"name"]];
  [_raagPopUp.menu addItem:[NSMenuItem separatorItem]];
  [_raagPopUp addItemWithTitle:kOtherRaagItem];
  if (selected && [_raagPopUp itemWithTitle:selected] && ![selected isEqualToString:kOtherRaagItem]) {
    [_raagPopUp selectItemWithTitle:selected];
  } else {
    [_raagPopUp selectItemAtIndex:0];
  }
  _previousRaag = _raagPopUp.titleOfSelectedItem;
}

- (void)raagChosen:(NSPopUpButton*)sender {
  if ([sender.titleOfSelectedItem isEqualToString:kOtherRaagItem]) {
    [self askForOtherRaag:@"" svaras:@""];
  } else {
    _previousRaag = sender.titleOfSelectedItem;
  }
}

// Asks for a raag that isn't in the app's list: its name, and its svaras so
// that Sa can be found.
- (void)askForOtherRaag:(NSString*)name svaras:(NSString*)svaras {
  NSTextField* nameField = [NSTextField textFieldWithString:name];
  nameField.placeholderString = @"Name, e.g. Shuddh Sarang";
  NSTextField* svarasField = [NSTextField textFieldWithString:svaras];
  svarasField.placeholderString = @"Svaras, e.g. S R m M P D N";
  NSStackView* fields = [NSStackView stackViewWithViews:@[ nameField, svarasField ]];
  fields.orientation = NSUserInterfaceLayoutOrientationVertical;
  fields.frame = NSMakeRect(0, 0, 300, 52);
  for (NSTextField* field in @[ nameField, svarasField ]) {
    [field.widthAnchor constraintEqualToConstant:300].active = YES;
  }

  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Other Raag";
  alert.informativeText = @"Give its svaras in Bhatkhande letters: small r g d n for komal, M for tivra Ma. "
                          @"Without them the recording can't find Sa or stop by itself.";
  alert.accessoryView = fields;
  [alert addButtonWithTitle:@"Add"];
  [alert addButtonWithTitle:@"Cancel"];
  alert.window.initialFirstResponder = nameField;
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse response) {
                  if (response != NSAlertFirstButtonReturn) {
                    [self buildRaagMenu:self->_previousRaag];
                    return;
                  }
                  [self addOtherRaag:nameField.stringValue svaras:svarasField.stringValue];
                }];
}

- (void)addOtherRaag:(NSString*)name svaras:(NSString*)svaras {
  name = [name stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  const std::optional<std::vector<int>> parsed = ParseSvaras(svaras);
  NSString* problem = nil;
  if (name.length == 0) {
    problem = @"Give the raag's name.";
  } else if (!parsed) {
    problem = @"The svaras must be Bhatkhande letters: S r R g G m M P d D n N.";
  } else if (!parsed->empty() && parsed->front() != raagasense::kSa) {
    problem = @"The svaras must include Sa.";
  }
  if (problem) {
    // Ask again once this alert has gone.
    dispatch_async(dispatch_get_main_queue(), ^{
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = problem;
      [alert beginSheetModalForWindow:self.window
                    completionHandler:^(NSModalResponse) {
                      [self askForOtherRaag:name svaras:svaras];
                    }];
    });
    return;
  }
  if (raagasense::FindRaag(name.UTF8String)) {
    // It's one the app knows after all.
    [self buildRaagMenu:@(raagasense::FindRaag(name.UTF8String)->name.c_str())];
    return;
  }
  NSMutableArray<NSDictionary*>* others =
      [([NSUserDefaults.standardUserDefaults arrayForKey:kOtherRaagsKey] ?: @[]) mutableCopy];
  [others filterUsingPredicate:[NSPredicate predicateWithFormat:@"name !=[c] %@", name]];
  [others addObject:@{@"name" : name, @"svaras" : svaras}];
  [NSUserDefaults.standardUserDefaults setObject:others forKey:kOtherRaagsKey];
  [self buildRaagMenu:name];
}

// The chosen raag: one of the app's, or another with whatever svaras were
// given for it.
- (raagasense::Raag)chosenRaag {
  NSString* name = _raagPopUp.titleOfSelectedItem;
  if (const raagasense::Raag* known = raagasense::FindRaag(name.UTF8String)) return *known;
  raagasense::Raag raag{name.UTF8String, {}, {}, -1, -1};
  for (NSDictionary* other in [NSUserDefaults.standardUserDefaults arrayForKey:kOtherRaagsKey]) {
    if ([other[@"name"] isEqualToString:name]) raag.svaras = ParseSvaras(other[@"svaras"]).value_or(std::vector<int>{});
  }
  return raag;
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
  _raag = [self chosenRaag];
  _finder = _raag.svaras.empty() ? nullptr : std::make_unique<raagasense::SaFinder>(_raag);
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
    if (_finder) _finder->Add(fresh);
  }
  [self update];
  if (_finder && _finder->status().confident) [self finish:YES];
}

- (void)update {
  if (!_finder) {
    _statusLabel.stringValue = [NSString
        stringWithFormat:@"Recording %@ of %@. Without its svaras there's no Sa to find, so stop when you have enough.",
                         MinutesAndSeconds(_lastTime - _startTime), @(_raag.name.c_str())];
    return;
  }
  const raagasense::SaStatus& status = _finder->status();
  const double needed = raagasense::SaFinderOptions().min_held_seconds;
  _progress.doubleValue = std::min(1.0, status.held_seconds / needed);
  NSString* sa = status.sa > 0 && status.held_seconds >= kShowSaAfter
                     ? [NSString stringWithFormat:@"Sa looks like %@, for the last %.0f s.", ShownNote(status.sa),
                                                  status.stable_seconds]
                     : @"No Sa yet.";
  _statusLabel.stringValue =
      [NSString stringWithFormat:@"Recording %@ of %@. Held notes so far: %.0f s of the %.0f s needed.\n%@",
                                 MinutesAndSeconds(_lastTime - _startTime), @(_raag.name.c_str()), status.held_seconds,
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

  const raagasense::SaStatus status = _finder ? _finder->status() : raagasense::SaStatus{};
  NSString* performer = Slug(_performerBox.stringValue);
  NSString* artist = Slug(_artistField.stringValue);
  NSMutableString* stem = [NSMutableString stringWithFormat:@"%@_%@", Slug(@(_raag.name.c_str())), performer];
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
  NSMutableString* notes =
      _finder ? [NSMutableString
                    stringWithFormat:@"Recorded with RaagaSense; Sa found live from the raag (%@, held notes %.0f s, lead %.2f)",
                                     sure, status.held_seconds, status.margin]
              : [NSMutableString stringWithString:@"Recorded with RaagaSense; no Sa, as the raag's svaras weren't given"];
  if (!raagasense::FindRaag(_raag.name)) {
    std::string letters;
    for (int svara : _raag.svaras) letters += (letters.empty() ? "" : " ") + std::string(raagasense::SvaraLetter(svara));
    [notes appendFormat:@"; not in the app's raags%@",
                        letters.empty() ? @"" : [NSString stringWithFormat:@", svaras %s", letters.c_str()]];
  }
  if (artist.length > 0) [notes appendFormat:@"; artist %@", _artistField.stringValue];
  NSString* labels = [self.folder stringByAppendingPathComponent:@"labels.csv"];
  NSMutableString* row = [NSMutableString string];
  if (![files fileExistsAtPath:labels]) [row appendString:@"file,raag,sa_hz,singer,notes,sa_checked\n"];
  const double sa = status.held_seconds >= kShowSaAfter ? status.sa : 0;
  [row appendFormat:@"%@,%@,%.1f,%@,%@,no\n", CsvField(name), CsvField(@(_raag.name.c_str())), sa,
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
                           : _finder ? @"No Sa: too few held notes." : @"No Sa: the raag's svaras weren't given."];
}

@end
