#import "app/macos/annotation_window.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <sstream>

#include "core/annotation.h"
#include "platform/audio_file.h"
#include "tools/evaluation.h"
#include "gtest/gtest.h"

// Exercise the same actions as the buttons, without file-picker interaction.
@interface RSAnnotationWindow (TestActions)
- (void)saveLabels:(id)sender;
- (void)analyze:(id)sender;
- (void)edit:(id)sender;
- (void)reviewChanged:(id)sender;
- (void)exportExcerpt:(id)sender;
- (void)controlTextDidChange:(NSNotification*)notification;
@end

TEST(AnnotationWindow, LabelsAndCheckedExcerptRoundTripWithoutChangingSourceAudio) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp finishLaunching];
    NSString* root = [NSString stringWithUTF8String:std::getenv("TEST_TMPDIR")];
    NSString* folder = [root stringByAppendingPathComponent:@"source"];
    NSString* output = [root stringByAppendingPathComponent:@"reviewed"];
    [NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
    std::vector<float> tone(16000 * 12);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.2 * std::sin(2 * std::numbers::pi * 220 * i / 16000);
    const auto wav = raagasense::annotation::WavBytes(tone, 16000);
    NSData* original = [NSData dataWithBytes:wav.data() length:wav.size()];
    NSString* source = [folder stringByAppendingPathComponent:@"yaman.wav"];
    ASSERT_TRUE([original writeToFile:source atomically:YES]);
    NSString* labels = @"file,raag,sa_hz,singer,notes,sa_checked\r\nyaman.wav,Yaman,220,female,source note,yes\r\n";
    ASSERT_TRUE([labels writeToFile:[folder stringByAppendingPathComponent:@"labels.csv"] atomically:YES encoding:NSUTF8StringEncoding error:nil]);

    RSAnnotationWindow* controller = [[RSAnnotationWindow alloc] init];
    controller.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    [controller showWindow:nil];
    [controller openFolder:folder];
    NSTableView* table = [controller valueForKey:@"table"];
    ASSERT_EQ(table.numberOfRows, 1);
    ASSERT_EQ(table.selectedRow, 0);
    NSTextField* sa = [controller valueForKey:@"sa"];
    NSTextField* saNote = [controller valueForKey:@"saNote"];
    NSButton* saChecked = [controller valueForKey:@"saChecked"];
    EXPECT_TRUE([saNote.stringValue isEqualToString:@"A3 (+0¢)"]);
    const auto changeSa = [&](NSString* value) {
      sa.stringValue = value;
      [controller controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification object:sa]];
    };
    changeSa(@"261.625565");
    EXPECT_TRUE([saNote.stringValue isEqualToString:@"C4 (+0¢)"]);
    changeSa([NSString stringWithFormat:@"%.9f", 440 * std::exp2(23.0 / 1200)]);
    EXPECT_TRUE([saNote.stringValue isEqualToString:@"A4 (+23¢)"]);
    changeSa([NSString stringWithFormat:@"%.9f", 277.182631 * std::exp2(-17.0 / 1200)]);
    EXPECT_TRUE([saNote.stringValue isEqualToString:@"C♯4 (-17¢)"]);
    EXPECT_EQ(saChecked.state, NSControlStateValueOff);
    for (NSString* invalid : @[@"", @"0", @"-220", @"nan", @"220 Hz"]) {
      changeSa(invalid);
      EXPECT_TRUE([saNote.stringValue isEqualToString:@"—"]) << invalid.UTF8String;
    }
    changeSa(@"0");  // Let analysis supply an unverified Sa suggestion below.
    NSTextField* artist = [controller valueForKey:@"artist"];
    artist.stringValue = @"Artist A";
    [controller saveLabels:nil];
    ASSERT_EQ(table.selectedRow, 0);
    std::ifstream labelInput([[folder stringByAppendingPathComponent:@"labels.csv"] UTF8String]);
    std::vector<raagasense::evaluation::Label> parsedLabels;
    std::string error;
    ASSERT_TRUE(raagasense::evaluation::ReadLabels(labelInput, false, &parsedLabels, &error)) << error;
    ASSERT_EQ(parsedLabels.size(), 1u);
    EXPECT_EQ(parsedLabels[0].artist, "Artist A");

    NSTextField* start = [controller valueForKey:@"start"]; start.stringValue = @"2";
    NSPopUpButton* length = [controller valueForKey:@"length"]; [length selectItemWithTag:5];
    [controller analyze:nil];
    const auto deadline = [NSDate dateWithTimeIntervalSinceNow:15];
    while ([[controller valueForKey:@"loading"] boolValue] && deadline.timeIntervalSinceNow > 0) {
      [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
    ASSERT_FALSE([[controller valueForKey:@"loading"] boolValue]);
    NSTextField* status = [controller valueForKey:@"status"];
    EXPECT_TRUE([status.stringValue containsString:@"Listen to the recording"]) << status.stringValue.UTF8String;
    EXPECT_GT(sa.doubleValue, 0);
    EXPECT_FALSE([saNote.stringValue isEqualToString:@"—"]);
    EXPECT_EQ(saChecked.state, NSControlStateValueOff);
    changeSa(@"220");
    saChecked.state = NSControlStateValueOn;
    [controller saveLabels:nil];

    id plot = [controller valueForKey:@"plot"];
    [plot setValue:@0 forKey:@"selectionStart"]; [plot setValue:@1 forKey:@"selectionEnd"];
    NSButton* up = [NSButton buttonWithTitle:@"Up" target:nil action:nil]; up.tag = 0;
    [controller edit:up];
    [controller setValue:output forKey:@"exportFolder"];
    // Unreviewed drafts cannot be exported through the action.
    [controller exportExcerpt:nil];
    EXPECT_FALSE([NSFileManager.defaultManager fileExistsAtPath:output]);
    NSButton* reviewed = [controller valueForKey:@"reviewed"]; reviewed.state = NSControlStateValueOn;
    [controller reviewChanged:nil];
    NSButton* exportButton = [controller valueForKey:@"export"]; ASSERT_TRUE(exportButton.enabled);
    [controller exportExcerpt:nil];
    EXPECT_TRUE([status.stringValue containsString:@"Checked excerpt exported"]);
    NSString* recording = [output stringByAppendingPathComponent:@"recordings/yaman__2000-7000.wav"];
    auto audio = raagasense::ReadAudioFile(recording.UTF8String, &error);
    ASSERT_TRUE(audio) << error;
    EXPECT_EQ(audio->samples.size(), 5u * 16000);
    std::ifstream pitchInput([[output stringByAppendingPathComponent:@"corrected-pitch/yaman__2000-7000.wav.csv"] UTF8String]);
    std::vector<raagasense::PitchFrame> frames;
    ASSERT_TRUE(raagasense::evaluation::ReadPitchCsv(pitchInput, &frames, &error)) << error;
    ASSERT_FALSE(frames.empty());
    EXPECT_GE(frames.front().time, 0);
    EXPECT_LT(frames.back().time, 5);
    EXPECT_NEAR(frames.front().frequency, 440, 5);
    EXPECT_DOUBLE_EQ(frames.front().confidence, 1);
    std::ifstream exportedLabels([[output stringByAppendingPathComponent:@"recordings/labels.csv"] UTF8String]);
    ASSERT_TRUE(raagasense::evaluation::ReadLabels(exportedLabels, false, &parsedLabels, &error)) << error;
    ASSERT_EQ(parsedLabels.size(), 1u);
    EXPECT_EQ(parsedLabels[0].file, "yaman__2000-7000.wav");
    EXPECT_EQ(parsedLabels[0].artist, "Artist A");
    EXPECT_DOUBLE_EQ(parsedLabels[0].sa, 220);
    EXPECT_TRUE([original isEqualToData:[NSData dataWithContentsOfFile:source]]);

    [controller.window.contentView layoutSubtreeIfNeeded];
    [controller.window displayIfNeeded];
    const char* images = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    if (images) {
      NSView* view = controller.window.contentView;
      NSBitmapImageRep* bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
      [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
      NSString* image = [[NSString stringWithUTF8String:images] stringByAppendingPathComponent:@"annotation-window.png"];
      NSImage* flattened = [[NSImage alloc] initWithSize:view.bounds.size];
      [flattened lockFocus];
      [[NSColor colorWithSRGBRed:0.95 green:0.95 blue:0.95 alpha:1] setFill]; NSRectFill(view.bounds);
      NSImage* capture = [[NSImage alloc] initWithSize:view.bounds.size];
      [capture addRepresentation:bitmap];
      [capture drawInRect:view.bounds fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
      [flattened unlockFocus];
      NSBitmapImageRep* complete = [NSBitmapImageRep imageRepWithData:flattened.TIFFRepresentation];
      [[complete representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:image atomically:YES];
    }
    EXPECT_TRUE([controller prepareToClose]);
    NSString* empty = [root stringByAppendingPathComponent:@"empty"];
    [NSFileManager.defaultManager createDirectoryAtPath:empty withIntermediateDirectories:YES attributes:nil error:nil];
    [controller openFolder:empty];
    EXPECT_EQ(table.numberOfRows, 0);
    EXPECT_FALSE(exportButton.enabled);
    EXPECT_TRUE([saNote.stringValue isEqualToString:@"—"]);
    [controller.window close];
  }
}
