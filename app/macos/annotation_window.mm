#import "app/macos/annotation_window.h"

#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/annotation.h"
#include "core/melody.h"
#include "core/raag.h"
#include "core/steady.h"
#include "core/svara.h"
#include "core/tonic.h"
#include "platform/audio_file.h"

namespace {
using raagasense::annotation::Label;
using raagasense::annotation::Labels;
using raagasense::annotation::PitchEditor;

NSString* Text(const std::string& text) { return @(text.c_str()); }
NSData* Bytes(const std::string& data) { return [NSData dataWithBytes:data.data() length:data.size()]; }
NSString* Clock(double seconds) {
  return [NSString stringWithFormat:@"%d:%04.1f", static_cast<int>(seconds) / 60, std::fmod(seconds, 60)];
}
NSStackView* Row(NSArray<NSView*>* views) {
  NSStackView* row = [NSStackView stackViewWithViews:views];
  row.spacing = 8;
  return row;
}
NSTextField* Hint(NSString* text) {
  NSTextField* field = [NSTextField wrappingLabelWithString:text];
  field.font = [NSFont systemFontOfSize:12];
  field.textColor = NSColor.secondaryLabelColor;
  return field;
}
std::optional<Labels> ReadLabels(NSString* folder, NSString** failure) {
  NSString* path = [folder stringByAppendingPathComponent:@"labels.csv"];
  NSError* error = nil;
  NSString* text = @"";
  if ([NSFileManager.defaultManager fileExistsAtPath:path]) {
    text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
    if (!text) { *failure = error.localizedDescription; return std::nullopt; }
  }
  std::string message;
  auto labels = Labels::Parse(text.UTF8String, &message);
  if (!labels) *failure = Text(message);
  return labels;
}
}  // namespace

@interface RSAnnotationPlot : NSView
@property(nonatomic) double sa;
@property(nonatomic) double duration;
@property(nonatomic) double selectionStart;
@property(nonatomic) double selectionEnd;
@property(nonatomic) double playhead;
@property(nonatomic, copy) void (^selectionChanged)(double start, double end);
- (void)setEditor:(std::shared_ptr<PitchEditor>)editor;
@end

@implementation RSAnnotationPlot {
  std::shared_ptr<PitchEditor> _editor;
  double _anchor;
}
- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _sa = 220; _duration = 10; _selectionEnd = 10; _playhead = -1;
    self.accessibilityLabel = @"Pitch guide. Drag horizontally to select a section to correct.";
  }
  return self;
}
- (void)setEditor:(std::shared_ptr<PitchEditor>)editor { _editor = std::move(editor); self.needsDisplay = YES; }
- (NSRect)plotRect { return NSMakeRect(50, 24, std::max(1.0, self.bounds.size.width - 64), std::max(1.0, self.bounds.size.height - 36)); }
- (double)timeAt:(NSPoint)point {
  const NSRect rect = [self plotRect];
  return std::clamp((point.x - rect.origin.x) / rect.size.width * _duration, 0.0, _duration);
}
- (void)mouseDown:(NSEvent*)event {
  _anchor = [self timeAt:[self convertPoint:event.locationInWindow fromView:nil]];
  _selectionStart = _selectionEnd = _anchor;
  self.needsDisplay = YES;
}
- (void)mouseDragged:(NSEvent*)event {
  const double time = [self timeAt:[self convertPoint:event.locationInWindow fromView:nil]];
  _selectionStart = std::min(time, _anchor); _selectionEnd = std::max(time, _anchor);
  if (_selectionChanged) _selectionChanged(_selectionStart, _selectionEnd);
  self.needsDisplay = YES;
}
- (void)mouseUp:(NSEvent*)event {
  [self mouseDragged:event];
  if (_selectionEnd - _selectionStart < 0.04) {
    _selectionStart = std::max(0.0, _anchor - 0.25);
    _selectionEnd = std::min(_duration, _anchor + 0.25);
  }
  if (_selectionChanged) _selectionChanged(_selectionStart, _selectionEnd);
  self.needsDisplay = YES;
}
- (void)drawRect:(NSRect)dirty {
  [NSColor.controlBackgroundColor setFill]; NSRectFill(self.bounds);
  if (!_editor || _editor->frames().empty()) {
    [@"Pitch review is optional. Choose a short passage and click Analyze excerpt." drawAtPoint:NSMakePoint(24, 100)
          withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:14], NSForegroundColorAttributeName:NSColor.secondaryLabelColor}];
    return;
  }
  const NSRect rect = [self plotRect];
  const double sa = std::isfinite(_sa) && _sa > 0 ? _sa : 220;
  double low = -5, high = 12;
  for (const auto* track : {&_editor->original(), &_editor->frames()}) {
    for (const auto& frame : *track) if (frame.voiced && frame.frequency > 0) {
      const double pitch = 12 * std::log2(frame.frequency / sa);
      low = std::min(low, std::floor(pitch) - 1); high = std::max(high, std::ceil(pitch) + 1);
    }
  }
  auto x = [&](double time) { return rect.origin.x + time / _duration * rect.size.width; };
  auto y = [&](double pitch) { return rect.origin.y + (pitch - low) / (high - low) * rect.size.height; };
  const auto attributes = @{NSFontAttributeName:[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular],
                            NSForegroundColorAttributeName:NSColor.secondaryLabelColor};
  for (int pitch = static_cast<int>(low); pitch <= high; ++pitch) {
    [(pitch % 12 == 0 ? NSColor.separatorColor : [NSColor.separatorColor colorWithAlphaComponent:0.35]) setStroke];
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint:NSMakePoint(rect.origin.x, y(pitch))];
    [line lineToPoint:NSMakePoint(NSMaxX(rect), y(pitch))]; [line stroke];
    [Text(raagasense::SvaraLabel(raagasense::NearestSvara(pitch * 100))).description
        drawAtPoint:NSMakePoint(8, y(pitch) - 5) withAttributes:attributes];
  }
  [[NSColor.systemBlueColor colorWithAlphaComponent:0.12] setFill];
  NSRectFillUsingOperation(NSMakeRect(x(_selectionStart), rect.origin.y, x(_selectionEnd) - x(_selectionStart), rect.size.height), NSCompositingOperationSourceOver);
  for (int layer = 0; layer < 2; ++layer) {
    const auto& frames = layer == 0 ? _editor->original() : _editor->frames();
    [(layer == 0 ? NSColor.tertiaryLabelColor : NSColor.systemTealColor) setStroke];
    NSBezierPath* path = [NSBezierPath bezierPath]; path.lineWidth = layer == 0 ? 3 : 1.6;
    bool connected = false; double previous = 0, previousTime = 0;
    for (const auto& frame : frames) {
      if (!frame.voiced || frame.frequency <= 0) { connected = false; continue; }
      const double pitch = 12 * std::log2(frame.frequency / sa);
      const NSPoint point = NSMakePoint(x(frame.time), y(pitch));
      if (connected && frame.time - previousTime < 0.06 && std::abs(pitch - previous) < 6) [path lineToPoint:point];
      else [path moveToPoint:point];
      connected = true; previous = pitch; previousTime = frame.time;
    }
    [path stroke];
  }
  for (int tick = 0; tick <= 5; ++tick) {
    const double time = _duration * tick / 5;
    [[NSString stringWithFormat:@"%.1f s", time] drawAtPoint:NSMakePoint(x(time) - 12, 4) withAttributes:attributes];
  }
  if (_playhead >= 0 && _playhead <= _duration) {
    [NSColor.systemOrangeColor setStroke]; NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint:NSMakePoint(x(_playhead), rect.origin.y)];
    [line lineToPoint:NSMakePoint(x(_playhead), NSMaxY(rect))]; [line stroke];
  }
}
@end

@interface RSAnnotationWindow () <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate, NSWindowDelegate>
@end

@implementation RSAnnotationWindow {
  NSString* _folder;
  NSArray<NSString*>* _files;
  std::optional<Labels> _labels;
  NSTableView* _table;
  NSTextField *_folderLabel, *_fileLabel, *_artist, *_sa, *_saNote, *_notes, *_start, *_selectionLabel, *_status;
  NSPopUpButton *_raag, *_length, *_note, *_speed;
  NSButton *_saChecked, *_reviewed, *_loop;
  NSButton *_analyze, *_export, *_save;
  RSAnnotationPlot* _plot;
  std::shared_ptr<PitchEditor> _editor;
  std::shared_ptr<raagasense::AudioData> _audio;
  AVAudioPlayer *_original, *_guide, *_reference;
  NSTimer* _timer;
  double _excerptStart, _excerptEnd, _playEnd, _playReadyAt;
  NSInteger _playMode;
  NSUInteger _generation;
  BOOL _metadataDirty, _pitchPending, _backedUp, _loading;
  NSString* _exportFolder;
}

- (instancetype)init {
  NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1120, 760)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
  window.title = @"Label & Review Recordings";
  if ((self = [super initWithWindow:window])) {
    window.delegate = self; window.contentMinSize = NSMakeSize(1000, 700);
    _files = @[];
    [self buildContent]; [window center];
  }
  return self;
}
- (NSButton*)button:(NSString*)title action:(SEL)action {
  return [NSButton buttonWithTitle:title target:self action:action];
}
- (NSTextField*)field:(NSString*)placeholder width:(double)width {
  NSTextField* field = [NSTextField textFieldWithString:@""];
  field.placeholderString = placeholder; field.delegate = self;
  [field.widthAnchor constraintGreaterThanOrEqualToConstant:width].active = YES;
  field.cell.usesSingleLineMode = YES;
  return field;
}
- (void)buildContent {
  _folderLabel = Hint(@"Choose a folder of recordings. Audio stays on this Mac.");
  _folderLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
  NSStackView* top = Row(@[[self button:@"Choose folder…" action:@selector(chooseFolder:)], _folderLabel]);
  _table = [[NSTableView alloc] initWithFrame:NSZeroRect];
  NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"file"];
  column.title = @"Recordings"; column.width = 215; [_table addTableColumn:column];
  _table.dataSource = self; _table.delegate = self;
  _table.rowHeight = 42;
  NSScrollView* browser = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  browser.documentView = _table; browser.hasVerticalScroller = YES;
  [browser.widthAnchor constraintEqualToConstant:240].active = YES;
  _fileLabel = [NSTextField labelWithString:@"Start with performer names — pitch review can wait."];
  _fileLabel.font = [NSFont boldSystemFontOfSize:16];
  _fileLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
  _artist = [self field:@"e.g. Kaushiki Chakraborty; leave blank if unknown" width:360];
  _raag = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [_raag addItemWithTitle:@"Unknown / not sure"];
  for (const auto& raag : raagasense::Raags()) [_raag addItemWithTitle:Text(raag.name)];
  _raag.target = self; _raag.action = @selector(metadataChanged:);
  _sa = [self field:@"Hz, or 0 for unknown" width:100];
  _saNote = [NSTextField labelWithString:@"—"];
  _saNote.toolTip = @"Concert pitch, A4 = 440 Hz (middle C = C4). Cents above (+) or below (−) the nearest equal-tempered note.";
  [_saNote setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
  _saChecked = [NSButton checkboxWithTitle:@"Sa checked by ear" target:self action:@selector(metadataChanged:)];
  _notes = [self field:@"Optional source or notes" width:360];
  NSGridView* metadata = [NSGridView gridViewWithViews:@[
      @[[NSTextField labelWithString:@"Lead performer"], _artist],
      @[[NSTextField labelWithString:@"Raag"], _raag],
      @[[NSTextField labelWithString:@"Sa (Hz)"], Row(@[_sa, _saNote, [self button:@"Hear Sa" action:@selector(hearSa:)], _saChecked])],
      @[[NSTextField labelWithString:@"Notes"], _notes]]];
  metadata.rowSpacing = 6; [metadata columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  _save = [self button:@"Save labels" action:@selector(saveLabels:)]; _save.enabled = NO;
  NSStackView* labelActions = Row(@[_save, [self button:@"Save & next recording" action:@selector(saveAndNext:)],
      Hint(@"Use the same performer's name across their recordings.")]);
  _start = [self field:@"Start in seconds" width:80]; _start.stringValue = @"0";
  _length = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  for (int seconds : {5, 10, 15}) { [_length addItemWithTitle:[NSString stringWithFormat:@"%d seconds", seconds]]; _length.lastItem.tag = seconds; }
  [_length selectItemWithTag:10];
  _analyze = [self button:@"Analyze excerpt" action:@selector(analyze:)]; _analyze.enabled = NO;
  NSStackView* excerpt = Row(@[[NSTextField labelWithString:@"Optional pitch review · start"], _start,
      [NSTextField labelWithString:@"seconds"], _length, _analyze,
      [self button:@"Skip / next excerpt" action:@selector(nextExcerpt:)]]);
  _plot = [[RSAnnotationPlot alloc] initWithFrame:NSZeroRect];
  [_plot.heightAnchor constraintGreaterThanOrEqualToConstant:200].active = YES;
  _plot.toolTip = @"Drag across time to select a section. Grey is the original estimate; teal is your working pitch guide.";
  __weak RSAnnotationWindow* weakSelf = self;
  _plot.selectionChanged = ^(double start, double end) { [weakSelf showSelection]; };
  _selectionLabel = Hint(@"Drag across the graph to select a section; times below are relative to the excerpt.");
  _loop = [NSButton checkboxWithTitle:@"Loop" target:nil action:nil];
  _speed = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  for (NSString* title in @[@"1×", @"0.75×", @"0.5×"]) [_speed addItemWithTitle:title];
  NSStackView* playback = Row(@[[self button:@"Listen" action:@selector(playOriginal:)],
      [self button:@"Pitch guide" action:@selector(playGuide:)], [self button:@"Both" action:@selector(playBoth:)],
      [self button:@"Stop" action:@selector(stop:)], _loop, _speed,
      [self button:@"Select whole excerpt" action:@selector(selectAll:)]]);
  NSMutableArray<NSView*>* edits = [NSMutableArray array];
  for (NSString* title in @[@"Up an octave", @"Down an octave", @"No lead", @"Restore estimate", @"Undo"]) {
    NSButton* button = [self button:title action:@selector(edit:)]; button.tag = edits.count; [edits addObject:button];
  }
  _note = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  for (int semitone = -12; semitone <= 24; ++semitone) {
    [_note addItemWithTitle:Text(raagasense::SvaraLabel(raagasense::NearestSvara(semitone * 100)))];
    _note.lastItem.tag = semitone;
  }
  [_note selectItemWithTag:0];
  NSButton* setNote = [self button:@"Set held note" action:@selector(edit:)]; setNote.tag = 5;
  _reviewed = [NSButton checkboxWithTitle:@"I listened through this whole excerpt and checked the pitch guide" target:self action:@selector(reviewChanged:)];
  _export = [self button:@"Export checked excerpt…" action:@selector(exportExcerpt:)]; _export.enabled = NO;
  _status = Hint(@"Label recordings first. Review only a few clear excerpts; skip anything uncertain.");
  NSStackView* detail = [NSStackView stackViewWithViews:@[_fileLabel, metadata, labelActions,
      Hint(@"Pitch review: grey = original estimate · teal = working guide. Fix sections, not individual frames."),
      excerpt, _plot, _selectionLabel, playback, Row(edits), Row(@[_note, setNote,
      Hint(@"For a held note only. Octave shifts preserve meend and andolan.")]), _reviewed,
      Row(@[_export, [self button:@"Export folder…" action:@selector(chooseExportFolder:)]]), _status]];
  detail.orientation = NSUserInterfaceLayoutOrientationVertical; detail.alignment = NSLayoutAttributeLeading; detail.spacing = 9;
  [_plot.widthAnchor constraintEqualToAnchor:detail.widthAnchor].active = YES;
  [_status.widthAnchor constraintEqualToAnchor:detail.widthAnchor].active = YES;
  NSStackView* body = Row(@[browser, detail]); body.alignment = NSLayoutAttributeTop;
  [browser.heightAnchor constraintEqualToAnchor:body.heightAnchor].active = YES;
  NSStackView* content = [NSStackView stackViewWithViews:@[top, body]];
  content.orientation = NSUserInterfaceLayoutOrientationVertical; content.alignment = NSLayoutAttributeLeading;
  content.spacing = 12; content.edgeInsets = NSEdgeInsetsMake(14, 14, 14, 14);
  [top.widthAnchor constraintEqualToAnchor:content.widthAnchor constant:-28].active = YES;
  [body.widthAnchor constraintEqualToAnchor:content.widthAnchor constant:-28].active = YES;
  self.window.contentView = content;
}
- (void)showError:(NSString*)message {
  NSAlert* alert = [[NSAlert alloc] init]; alert.messageText = @"Could not complete that action";
  alert.informativeText = message ?: @"Unknown error"; [alert runModal];
}
- (BOOL)canLeave {
  [self.window makeFirstResponder:nil];
  if (_metadataDirty) {
    NSAlert* alert = [[NSAlert alloc] init]; alert.messageText = @"Save the label changes?";
    [alert addButtonWithTitle:@"Save labels"]; [alert addButtonWithTitle:@"Cancel"]; [alert addButtonWithTitle:@"Discard labels"];
    const auto response = [alert runModal];
    if (response == NSAlertSecondButtonReturn) return NO;
    if (response == NSAlertFirstButtonReturn && ![self saveMetadata]) return NO;
  }
  if (_pitchPending) {
    NSAlert* alert = [[NSAlert alloc] init]; alert.messageText = @"Discard the unexported pitch edits?";
    alert.informativeText = @"Return to the excerpt to check and export it, or discard these edits.";
    [alert addButtonWithTitle:@"Keep editing"]; [alert addButtonWithTitle:@"Discard pitch edits"];
    if ([alert runModal] == NSAlertFirstButtonReturn) return NO;
  }
  return YES;
}
- (void)chooseFolder:(id)sender {
  NSOpenPanel* panel = [NSOpenPanel openPanel]; panel.canChooseDirectories = YES; panel.canChooseFiles = NO;
  panel.message = @"Choose the folder containing the recordings to label.";
  if ([panel runModal] == NSModalResponseOK) [self openFolder:panel.URL.path];
}
- (void)openFolder:(NSString*)folder {
  if (![self canLeave]) return;
  NSString* failure = nil;
  auto labels = ReadLabels(folder, &failure);
  if (!labels) { [self showError:failure]; return; }
  NSError* error = nil;
  NSArray* contents = [NSFileManager.defaultManager contentsOfDirectoryAtPath:folder error:&error];
  if (!contents) { [self showError:error.localizedDescription]; return; }
  NSMutableArray* files = [NSMutableArray array];
  NSSet* extensions = [NSSet setWithArray:@[@"wav", @"m4a", @"mp3", @"aiff", @"aif", @"caf"]];
  for (NSString* name in contents) if ([extensions containsObject:name.pathExtension.lowercaseString]) [files addObject:name];
  [self stop:nil]; ++_generation; _loading = NO; _editor.reset(); _audio.reset();
  _original = nil; _guide = nil; [_plot setEditor:{}];
  _reviewed.state = NSControlStateValueOff; _export.enabled = NO;
  _folder = folder; _labels = std::move(labels); _backedUp = NO;
  _metadataDirty = _pitchPending = NO;
  [_table deselectAll:nil];
  _files = [files sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
  _folderLabel.stringValue = folder; [_table reloadData];
  [NSUserDefaults.standardUserDefaults setObject:folder forKey:@"AnnotationFolder"];
  if (_files.count) [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
  else {
    _fileLabel.stringValue = @"No supported audio files in this folder."; _analyze.enabled = NO; _save.enabled = NO;
    _sa.stringValue = @""; [self updateSaNote];
  }
}
- (NSInteger)numberOfRowsInTableView:(NSTableView*)table { return _files.count; }
- (NSView*)tableView:(NSTableView*)table viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
  const auto label = _labels->Get([_files[row] UTF8String]);
  NSString* caption = [NSString stringWithFormat:@"%@\n%@", _files[row], label.artist.empty() ? @"Performer unknown" : Text(label.artist)];
  NSTextField* field = [NSTextField wrappingLabelWithString:caption]; field.font = [NSFont systemFontOfSize:11];
  field.lineBreakMode = NSLineBreakByTruncatingMiddle; return field;
}
- (BOOL)tableView:(NSTableView*)table shouldSelectRow:(NSInteger)row {
  return row == table.selectedRow || [self canLeave];
}
- (void)tableViewSelectionDidChange:(NSNotification*)notification {
  const NSInteger row = _table.selectedRow;
  if (row < 0 || row >= static_cast<NSInteger>(_files.count)) return;
  [self stop:nil]; ++_generation; _loading = NO; _editor.reset(); _audio.reset(); _guide = nil; [_plot setEditor:{}];
  _reviewed.state = NSControlStateValueOff; _export.enabled = NO; _pitchPending = NO;
  const auto label = _labels->Get([_files[row] UTF8String]);
  _fileLabel.stringValue = _files[row]; _artist.stringValue = Text(label.artist); _notes.stringValue = Text(label.notes);
  [_raag selectItemAtIndex:0];
  const auto* known = raagasense::FindRaag(label.raag);
  if (known) [_raag selectItemWithTitle:Text(known->name)];
  else if (!label.raag.empty() && label.raag != "Unknown") {
    if (![_raag itemWithTitle:Text(label.raag)]) [_raag addItemWithTitle:Text(label.raag)];
    [_raag selectItemWithTitle:Text(label.raag)];
  }
  _sa.stringValue = label.sa > 0 ? [NSString stringWithFormat:@"%.3f", label.sa] : @"0";
  [self updateSaNote];
  _saChecked.state = label.sa_checked ? NSControlStateValueOn : NSControlStateValueOff;
  _start.stringValue = @"0"; _metadataDirty = NO; _save.enabled = YES; _analyze.enabled = YES;
  _start.enabled = YES; _length.enabled = YES;
  NSError* error = nil;
  _original = [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:[_folder stringByAppendingPathComponent:_files[row]]] error:&error];
  _original.enableRate = YES;
  _status.stringValue = _original ? [NSString stringWithFormat:@"%@ long. Save a performer name, or optionally review a short excerpt.", Clock(_original.duration)] : error.localizedDescription;
}
- (Label)currentLabel {
  Label label = _labels->Get([_files[_table.selectedRow] UTF8String]);
  label.artist = _artist.stringValue.UTF8String; label.notes = _notes.stringValue.UTF8String;
  label.raag = _raag.indexOfSelectedItem == 0 ? "Unknown" : _raag.titleOfSelectedItem.UTF8String;
  if (!raagasense::annotation::Number(_sa.stringValue.UTF8String, &label.sa) || label.sa < 0) label.sa = -1;
  label.sa_checked = _saChecked.state == NSControlStateValueOn;
  return label;
}
- (BOOL)saveMetadata {
  if (_table.selectedRow < 0) return NO;
  [self.window makeFirstResponder:nil];
  NSString* failure = nil;
  auto latest = ReadLabels(_folder, &failure);
  if (!latest) { [self showError:failure]; return NO; }
  std::string errorText;
  if (!latest->Set([self currentLabel], &errorText)) { [self showError:Text(errorText)]; return NO; }
  NSString* path = [_folder stringByAppendingPathComponent:@"labels.csv"];
  NSError* error = nil;
  if (!_backedUp && [NSFileManager.defaultManager fileExistsAtPath:path]) {
    NSString* backup = [path stringByAppendingFormat:@".backup-%@", NSUUID.UUID.UUIDString];
    if (![NSFileManager.defaultManager copyItemAtPath:path toPath:backup error:&error]) { [self showError:error.localizedDescription]; return NO; }
  }
  if (![Text(latest->Text()) writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
    [self showError:error.localizedDescription]; return NO;
  }
  _backedUp = YES; _labels = std::move(latest); _metadataDirty = NO;
  [_table reloadDataForRowIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, _files.count)]
                    columnIndexes:[NSIndexSet indexSetWithIndex:0]];
  _status.stringValue = @"Labels saved. Unknown names may stay blank; pitch review is optional.";
  return YES;
}
- (void)saveLabels:(id)sender { [self saveMetadata]; }
- (void)saveAndNext:(id)sender {
  if (![self saveMetadata]) return;
  if (![self canLeave]) return;
  const NSInteger next = _table.selectedRow + 1;
  if (next < static_cast<NSInteger>(_files.count)) [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:next] byExtendingSelection:NO];
  else _status.stringValue = @"Reached the last recording.";
}
- (void)metadataChanged:(id)sender { _metadataDirty = YES; }
- (void)updateSaNote {
  double frequency = 0;
  if (!raagasense::annotation::Number(_sa.stringValue.UTF8String, &frequency) || frequency <= 0) {
    _saNote.stringValue = @"—";
    return;
  }
  // Separate logarithms also handle very small positive input without underflow.
  const double midi = 69 + 12 * (std::log2(frequency) - std::log2(440.0));
  const long nearest = std::lround(midi);
  const long cents = std::lround(100 * (midi - nearest));
  NSArray<NSString*>* names = @[@"C", @"C♯", @"D", @"D♯", @"E", @"F", @"F♯", @"G", @"G♯", @"A", @"A♯", @"B"];
  const long index = (nearest % 12 + 12) % 12;
  const long octave = (nearest - index) / 12 - 1;
  _saNote.stringValue = [NSString stringWithFormat:@"%@%ld (%+ld¢)", names[index], octave, cents];
}
- (void)controlTextDidChange:(NSNotification*)notification {
  if (notification.object == _start) return;
  _metadataDirty = YES;
  if (notification.object == _sa) {
    [self updateSaNote];
    _saChecked.state = NSControlStateValueOff; _plot.sa = _sa.doubleValue; _plot.needsDisplay = YES;
  }
}
- (void)analyze:(id)sender {
  if (_table.selectedRow < 0 || _loading) return;
  if (_pitchPending) {
    NSAlert* alert = [[NSAlert alloc] init]; alert.messageText = @"Replace the unexported pitch edits?";
    [alert addButtonWithTitle:@"Keep editing"]; [alert addButtonWithTitle:@"Replace excerpt"];
    if ([alert runModal] == NSAlertFirstButtonReturn) return;
  }
  double start = 0;
  if (!raagasense::annotation::Number(_start.stringValue.UTF8String, &start) || start < 0 || !_original || start >= _original.duration) {
    [self showError:@"Enter a start time in seconds within the recording."]; return;
  }
  [self stop:nil];
  const double end = std::min(_original.duration, start + _length.selectedTag);
  NSString* path = [_folder stringByAppendingPathComponent:_files[_table.selectedRow]];
  const NSUInteger generation = ++_generation;
  _loading = YES; _analyze.enabled = NO; _export.enabled = NO; _reviewed.state = NSControlStateValueOff;
  _start.enabled = NO; _length.enabled = NO;
  _editor.reset(); _guide = nil; [_plot setEditor:{}];
  _status.stringValue = @"Preparing this short excerpt… You can still edit performer labels.";
  auto cached = _audio;
  __weak RSAnnotationWindow* weakSelf = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    std::string failure;
    auto audio = cached;
    if (!audio) {
      auto decoded = raagasense::ReadAudioFile(path.UTF8String, &failure);
      if (decoded) audio = std::make_shared<raagasense::AudioData>(std::move(*decoded));
    }
    auto frames = std::make_shared<std::vector<raagasense::PitchFrame>>();
    double actualStart = start, actualEnd = end;
    if (audio) {
      const size_t first = std::min(audio->samples.size(), static_cast<size_t>(start * audio->sample_rate));
      const size_t last = std::min(audio->samples.size(), static_cast<size_t>(end * audio->sample_rate));
      actualStart = first / audio->sample_rate; actualEnd = last / audio->sample_rate;
      raagasense::MelodyOptions options; options.sample_rate = audio->sample_rate;
      raagasense::MelodyTracker tracker(options);
      tracker.Push(std::span(audio->samples).subspan(first, last - first), frames.get()); tracker.Finish(frames.get());
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      RSAnnotationWindow* self = weakSelf;
      if (!self || generation != self->_generation) return;
      self->_loading = NO; self->_analyze.enabled = YES;
      self->_start.enabled = YES; self->_length.enabled = YES;
      if (!audio || frames->empty()) { [self showError:audio ? @"That excerpt is too short to analyze." : Text(failure)]; return; }
      self->_audio = audio; self->_excerptStart = actualStart; self->_excerptEnd = actualEnd;
      self->_editor = std::make_shared<PitchEditor>(std::move(*frames)); self->_pitchPending = NO;
      self->_plot.duration = actualEnd - actualStart; self->_plot.selectionStart = 0;
      self->_plot.selectionEnd = self->_plot.duration; self->_plot.sa = self->_sa.doubleValue;
      if (self->_sa.doubleValue <= 0) {
        const auto notes = raagasense::HeldPitches(self->_editor->frames());
        const auto* raag = raagasense::FindRaag(self->_raag.titleOfSelectedItem.UTF8String);
        const auto candidates = raag ? raagasense::EstimateTonicForSvaras(notes, raag->svaras) : raagasense::EstimateTonic(notes);
        if (!candidates.empty()) {
          self->_sa.stringValue = [NSString stringWithFormat:@"%.3f", candidates[0].frequency];
          self->_plot.sa = candidates[0].frequency; self->_saChecked.state = NSControlStateValueOff; self->_metadataDirty = YES;
        }
      }
      [self updateSaNote];
      [self->_plot setEditor:self->_editor]; [self showSelection];
      self->_status.stringValue = @"Listen to the recording and pitch guide. Correct clear errors; skip anything uncertain. Check Sa only when you're sure.";
    });
  });
}
- (void)nextExcerpt:(id)sender {
  if (_loading) return;
  // This button explicitly discards the current draft, without requiring
  // pitch review to proceed through the recording.
  [self stop:nil]; _pitchPending = NO;
  const double start = std::max(0.0, _start.doubleValue) + _length.selectedTag;
  if (_original && start < _original.duration) { _start.stringValue = [NSString stringWithFormat:@"%.3f", start]; [self analyze:nil]; }
  else _status.stringValue = @"Reached the end. Choose another recording.";
}
- (void)showSelection {
  _selectionLabel.stringValue = [NSString stringWithFormat:@"Excerpt %@–%@ · selected %.2f–%.2f s within it · grey: estimate · teal: working guide",
      Clock(_excerptStart), Clock(_excerptEnd), _plot.selectionStart, _plot.selectionEnd];
}
- (void)selectAll:(id)sender {
  _plot.selectionStart = 0; _plot.selectionEnd = _plot.duration; _plot.needsDisplay = YES; [self showSelection];
}
- (void)edit:(NSButton*)sender {
  if (!_editor) return;
  [self stop:nil];
  bool changed = false;
  using raagasense::annotation::Edit;
  if (sender.tag == 4) changed = _editor->Undo();
  else {
    Edit edit = sender.tag == 0 || sender.tag == 1 ? Edit::kTranspose : sender.tag == 2 ? Edit::kNoLead : sender.tag == 3 ? Edit::kRestore : Edit::kSetFrequency;
    double value = sender.tag == 0 ? 12 : sender.tag == 1 ? -12 : 0;
    if (sender.tag == 5) {
      double sa = 0;
      if (!raagasense::annotation::Number(_sa.stringValue.UTF8String, &sa) || sa <= 0) { [self showError:@"Set Sa before assigning a held note."]; return; }
      value = sa * std::exp2(_note.selectedTag / 12.0);
    }
    changed = _editor->Apply(edit, _plot.selectionStart, _plot.selectionEnd, value);
  }
  if (changed) {
    _pitchPending = YES; _reviewed.state = NSControlStateValueOff; _export.enabled = NO;
    _plot.needsDisplay = YES; _guide = nil;
    _status.stringValue = @"Correction applied. Listen again before marking this excerpt checked.";
  }
}
- (void)reviewChanged:(id)sender {
  _export.enabled = _editor && _reviewed.state == NSControlStateValueOn;
  if (_export.enabled) _pitchPending = YES;
}
- (void)playOriginal:(id)sender { [self play:0]; }
- (void)hearSa:(id)sender {
  [self stop:nil];
  double sa = 0;
  if (!raagasense::annotation::Number(_sa.stringValue.UTF8String, &sa) || sa < 20 || sa > 2000) {
    [self showError:@"Enter Sa in Hz first, or analyze an excerpt for an unverified suggestion."]; return;
  }
  const std::vector<raagasense::PitchFrame> frames = {{0, sa, 1, true}};
  const auto samples = raagasense::annotation::PitchGuide(frames, 2);
  NSError* error = nil;
  _reference = [[AVAudioPlayer alloc] initWithData:Bytes(raagasense::annotation::WavBytes(samples, 16000)) error:&error];
  if (!_reference) { [self showError:error.localizedDescription]; return; }
  [_reference play];
}
- (void)playGuide:(id)sender { [self play:1]; }
- (void)playBoth:(id)sender { [self play:2]; }
- (void)play:(NSInteger)mode {
  [self stop:nil];
  if (!_original) return;
  if (mode > 0 && !_editor) { [self showError:@"Analyze a short excerpt first to hear its pitch guide."]; return; }
  const double duration = _editor ? _plot.duration : std::min(15.0, _original.duration);
  const double start = _editor ? _plot.selectionStart : std::clamp(_start.doubleValue, 0.0, std::max(0.0, _original.duration - duration));
  const double end = _editor ? _plot.selectionEnd : start + duration;
  if (end <= start) return;
  const double origin = _editor ? _excerptStart : 0;
  const float rate = _speed.indexOfSelectedItem == 0 ? 1 : _speed.indexOfSelectedItem == 1 ? 0.75 : 0.5;
  if (mode > 0) {
    if (!_guide) {
      const auto samples = raagasense::annotation::PitchGuide(_editor->frames(), _plot.duration);
      NSError* error = nil;
      _guide = [[AVAudioPlayer alloc] initWithData:Bytes(raagasense::annotation::WavBytes(samples, 16000)) error:&error];
      if (!_guide) { [self showError:error.localizedDescription]; return; }
      _guide.enableRate = YES;
    }
    _guide.rate = rate; _guide.currentTime = start; _guide.volume = mode == 2 ? 0.65 : 1; [_guide prepareToPlay];
  }
  _original.rate = rate; _original.currentTime = origin + start; _original.volume = mode == 2 ? 0.65 : 1; [_original prepareToPlay];
  const double when = _original.deviceCurrentTime + 0.05;
  if (mode != 1) [_original playAtTime:when];
  if (mode > 0) [_guide playAtTime:when];
  _playMode = mode; _playEnd = origin + end;
  _playReadyAt = NSDate.timeIntervalSinceReferenceDate + 0.1;
  __weak RSAnnotationWindow* weakSelf = self;
  _timer = [NSTimer scheduledTimerWithTimeInterval:0.03 repeats:YES block:^(NSTimer*) { [weakSelf playbackTick]; }];
}
- (void)playbackTick {
  if (NSDate.timeIntervalSinceReferenceDate < _playReadyAt) return;
  const double time = _playMode == 1 ? _excerptStart + _guide.currentTime : _original.currentTime;
  _plot.playhead = time - _excerptStart; _plot.needsDisplay = YES;
  if (time >= _playEnd - 0.02 || (_playMode == 1 ? !_guide.playing : !_original.playing)) {
    const NSInteger mode = _playMode;
    if (_loop.state == NSControlStateValueOn) [self play:mode]; else [self stop:nil];
  }
}
- (void)stop:(id)sender {
  [_timer invalidate]; _timer = nil; [_original stop]; [_guide stop]; [_reference stop];
  _plot.playhead = -1; _plot.needsDisplay = YES;
}
- (void)exportExcerpt:(id)sender {
  if (!_editor || !_audio || _reviewed.state != NSControlStateValueOn) return;
  if (raagasense::FindRaag(_raag.titleOfSelectedItem.UTF8String) == nullptr) {
    [self showError:@"Choose the excerpt's raag before exporting. You can leave an uncertain passage unexported."]; return;
  }
  Label label = [self currentLabel];
  if (label.sa < 0) { [self showError:@"Enter a valid Sa in Hz, or 0 if unknown."]; return; }
  if (!_exportFolder) {
    [self chooseExportFolder:nil];
    if (!_exportFolder) return;
  }
  [self stop:nil];
  NSFileManager* files = NSFileManager.defaultManager;
  NSString* recordings = [_exportFolder stringByAppendingPathComponent:@"recordings"];
  NSString* pitches = [_exportFolder stringByAppendingPathComponent:@"corrected-pitch"];
  NSError* error = nil;
  if (![files createDirectoryAtPath:recordings withIntermediateDirectories:YES attributes:nil error:&error] ||
      ![files createDirectoryAtPath:pitches withIntermediateDirectories:YES attributes:nil error:&error]) { [self showError:error.localizedDescription]; return; }
  NSString* originalName = _files[_table.selectedRow];
  NSString* stem = [NSString stringWithFormat:@"%@__%.0f-%.0f", originalName.stringByDeletingPathExtension, _excerptStart * 1000, _excerptEnd * 1000];
  NSString* name = [stem stringByAppendingPathExtension:@"wav"];
  for (int n = 2; [files fileExistsAtPath:[recordings stringByAppendingPathComponent:name]] ||
                  [files fileExistsAtPath:[pitches stringByAppendingPathComponent:[name stringByAppendingString:@".csv"]]]; ++n) {
    name = [[stem stringByAppendingFormat:@"_%d", n] stringByAppendingPathExtension:@"wav"];
  }
  NSString* failure = nil;
  auto labels = ReadLabels(recordings, &failure);
  if (!labels) { [self showError:failure]; return; }
  label.file = name.UTF8String;
  label.notes += "; pitch_reviewed=yes; source " + std::string(originalName.UTF8String) +
                 "; source_start_seconds=" + std::to_string(_excerptStart);
  std::string message;
  if (!labels->Set(label, &message)) { [self showError:Text(message)]; return; }
  const size_t first = static_cast<size_t>(std::llround(_excerptStart * _audio->sample_rate));
  const size_t last = std::min(_audio->samples.size(), static_cast<size_t>(std::llround(_excerptEnd * _audio->sample_rate)));
  const auto wav = raagasense::annotation::WavBytes(std::span(_audio->samples).subspan(first, last - first), static_cast<int>(_audio->sample_rate));
  NSString* wavePath = [recordings stringByAppendingPathComponent:name];
  NSString* pitchPath = [pitches stringByAppendingPathComponent:[name stringByAppendingString:@".csv"]];
  if (![Bytes(wav) writeToFile:wavePath options:NSDataWritingAtomic error:&error]) { [self showError:error.localizedDescription]; return; }
  if (![Text(_editor->ReviewedCsv()) writeToFile:pitchPath atomically:YES encoding:NSUTF8StringEncoding error:&error] ||
      ![Text(labels->Text()) writeToFile:[recordings stringByAppendingPathComponent:@"labels.csv"] atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
    [files removeItemAtPath:wavePath error:nil]; [files removeItemAtPath:pitchPath error:nil];
    [self showError:error.localizedDescription]; return;
  }
  _pitchPending = NO;
  _status.stringValue = [NSString stringWithFormat:@"Checked excerpt exported to %@. Original recording unchanged.", _exportFolder];
}
- (void)chooseExportFolder:(id)sender {
  NSOpenPanel* panel = [NSOpenPanel openPanel]; panel.canChooseFiles = NO; panel.canChooseDirectories = YES; panel.canCreateDirectories = YES;
  panel.message = @"Choose a folder for checked excerpts. Copies and pitch annotations will be saved together.";
  if (_exportFolder) panel.directoryURL = [NSURL fileURLWithPath:_exportFolder];
  if ([panel runModal] == NSModalResponseOK) {
    _exportFolder = panel.URL.path;
    _status.stringValue = [NSString stringWithFormat:@"Checked excerpts will be exported to %@.", _exportFolder];
  }
}
- (BOOL)windowShouldClose:(NSWindow*)sender { return [self canLeave]; }
- (BOOL)prepareToClose { return [self canLeave]; }
- (void)windowWillClose:(NSNotification*)notification { [self stop:nil]; ++_generation; }
@end
