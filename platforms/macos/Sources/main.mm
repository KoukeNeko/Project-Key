#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

#import "CandidatePanel.h"

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    NSBundle *bundle = NSBundle.mainBundle;
    // `ProjectKey --register` makes the installed bundle known to the system,
    // so it appears under System Settings > Keyboard > Input Sources.
    if (argc > 1 && strcmp(argv[1], "--register") == 0) {
      OSStatus status = TISRegisterInputSource((__bridge CFURLRef)bundle.bundleURL);
      if (status != noErr) fprintf(stderr, "TISRegisterInputSource failed: %d\n", (int)status);
      return status == noErr ? 0 : 1;
    }
    // `ProjectKey --preview <dir>` writes the candidate window states as PNGs.
    if (argc > 2 && strcmp(argv[1], "--preview") == 0) {
      NSString *dir = @(argv[2]);
      PKPanelContent *content = [[PKPanelContent alloc] init];
      content.segments = @[ @"我們", @"把", @"舊的", @"程式", @"重構", @"成", @"模組" ];
      content.doubtful = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(3, 2)];
      content.pendingReading = @"";
      content.caret = 12;
      content.showsProof = YES;
      content.activeSegment = 3;
      content.candidates = @[ @"程式", @"城市", @"成事", @"乘勢", @"呈示", @"程", @"城" ];
      content.selection = 0;
      content.pageLabel = @"1/9";
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"default.png"] atomically:YES];
      [[PKCandidatePanel pngForContent:content proofBelow:YES] writeToFile:[dir stringByAppendingPathComponent:@"flip.png"] atomically:YES];
      content.showsProof = NO;
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"proof-off.png"] atomically:YES];
      content.showsProof = YES;
      content.candidates = nil;
      content.activeSegment = -1;
      content.segments = @[ @"我們", @"把", @"舊的", @"程式", @"重構", @"成" ];
      content.pendingReading = @"ㄇㄛˊ";
      content.caret = 10;
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"composing.png"] atomically:YES];
      content.caret = 4;  // inside 舊的
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"cursor-reading.png"] atomically:YES];
      content.pendingReading = @"";
      content.caret = 3;
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"cursor.png"] atomically:YES];
      return 0;
    }
    NSString *connection = bundle.infoDictionary[@"InputMethodConnectionName"];
    IMKServer *server = [[IMKServer alloc] initWithName:connection bundleIdentifier:bundle.bundleIdentifier];
    if (!server) {
      NSLog(@"Project Key: cannot start the input method server %@", connection);
      return 1;
    }
    [NSApplication sharedApplication];
    [NSApp run];
  }
  return 0;
}
