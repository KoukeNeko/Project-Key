#import "SettingsWindow.h"

#import "ProjectKey-Swift.h"

NSString *const PKProofPreviewKey = @"ProofPreview";
NSString *const PKChoosingProofKey = @"ChoosingProof";
NSString *const PKAdjacentKeysKey = @"AdjacentKeys";
NSString *const PKRememberChoicesKey = @"RememberChoices";
NSString *const PKShiftToggleKey = @"ShiftToggle";
NSString *const PKEnglishToZhuyinKey = @"EnglishToZhuyin";
NSString *const PKZhuyinToEnglishKey = @"ZhuyinToEnglish";

@implementation PKSettingsWindow

+ (void)registerDefaults {
  [NSUserDefaults.standardUserDefaults registerDefaults:@{
    PKAdjacentKeysKey : @YES,
    PKRememberChoicesKey : @YES,
    PKShiftToggleKey : @YES,
    PKEnglishToZhuyinKey : @YES,
    PKZhuyinToEnglishKey : @YES,
    PKProofPreviewKey : @1,
    PKChoosingProofKey : @1,
  }];
}

+ (void)showWithClearChoices:(void (^)(void))clearChoices {
  static NSWindow *window;
  if (!window) {
    NSView *form = [PKSettingsForm makeViewWithClearChoices:clearChoices];
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, form.fittingSize.width, form.fittingSize.height)
                                         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                           backing:NSBackingStoreBuffered
                                             defer:YES];
    window.title = @"Project Key 設定";
    window.releasedWhenClosed = NO;
    window.contentView = form;
    [window center];
  }
  // The input method is a background app; it has to come forward for the window to take keys.
  [NSApp activateIgnoringOtherApps:YES];
  [window makeKeyAndOrderFront:nil];
}

@end
