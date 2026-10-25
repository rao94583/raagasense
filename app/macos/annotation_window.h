// Optional metadata labeling and assisted review of short audio excerpts.
#import <AppKit/AppKit.h>

@interface RSAnnotationWindow : NSWindowController
- (instancetype)init;
- (void)openFolder:(NSString*)folder;
- (BOOL)prepareToClose;
@end
