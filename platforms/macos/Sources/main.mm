#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

#import "CandidatePanel.h"
#import "ProjectKey-Swift.h"

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
      content.rows = @[ @[ @"程式", @"城市", @"成事", @"乘勢", @"呈示", @"程" ] ];
      content.selectedRow = 0;
      content.selection = 0;
      content.pageLabel = @"1/9";
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"default.png"] atomically:YES];
      [[PKCandidatePanel pngForContent:content proofBelow:YES] writeToFile:[dir stringByAppendingPathComponent:@"flip.png"] atomically:YES];
      content.showsProof = NO;
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"proof-off.png"] atomically:YES];
      content.showsProof = YES;
      content.rows = @[ @[ @"程式", @"城市", @"成事", @"乘勢", @"呈示", @"程" ], @[ @"程式設計", @"城市", @"成事", @"乘", @"承" ],
                        @[ @"程式語言設計", @"城", @"成", @"乘" ], @[ @"誠", @"呈", @"盛", @"橙", @"懲", @"澄" ] ];
      content.selectedRow = 1;
      content.selection = 1;
      content.pageLabel = @"2/9";
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"expanded.png"] atomically:YES];
      content.rows = nil;
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
      content.segments = @[ @"促進" ];
      content.doubtful = [NSIndexSet indexSet];
      content.caret = 2;
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"plain.png"] atomically:YES];
      content.segments = @[ @"我好" ];
      content.caret = 2;
      content.keyHint = @"⇥";
      content.replacedText = @"ji3cl3";
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"hint.png"] atomically:YES];
      // Typing a syllable without a tone: the guess marked in the sentence, the other words below.
      content.segments = @[ @"好像", @"有", @"衝突" ];
      content.activeSegment = 2;
      content.caret = 5;
      content.keyHint = nil;
      content.replacedText = nil;
      content.rows = @[ @[ @"傳統", @"春天", @"常態", @"出土", @"出頭" ] ];
      content.selectedRow = -1;
      content.selection = -1;
      content.pageLabel = nil;
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"typing.png"] atomically:YES];
      content.segments = @[ @"ja" ];
      content.activeSegment = -1;
      content.caret = 2;
      content.keyHint = @"⇥";
      content.replacedText = @"我ㄇ";
      content.rows = @[ @[ @"我們", @"我沒", @"外面", @"文明" ] ];
      [[PKCandidatePanel pngForContent:content proofBelow:NO] writeToFile:[dir stringByAppendingPathComponent:@"typing-hint.png"] atomically:YES];
      NSView *form = [PKSettingsForm makeViewWithClearChoices:^{
      }];
      NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, form.fittingSize.width, form.fittingSize.height)
                                                     styleMask:NSWindowStyleMaskBorderless
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
      window.contentView = form;
      [form layoutSubtreeIfNeeded];
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.3]];
      NSBitmapImageRep *rep = [form bitmapImageRepForCachingDisplayInRect:form.bounds];
      [form cacheDisplayInRect:form.bounds toBitmapImageRep:rep];
      [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[dir stringByAppendingPathComponent:@"settings.png"] atomically:YES];
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
