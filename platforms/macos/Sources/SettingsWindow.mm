#import "SettingsWindow.h"

#import "ProjectKey-Swift.h"

NSString *const PKProofPreviewKey = @"ProofPreview";
NSString *const PKChoosingProofKey = @"ChoosingProof";
NSString *const PKAbbreviatedKey = @"Abbreviated";
NSString *const PKRememberChoicesKey = @"RememberChoices";
NSString *const PKShiftToggleKey = @"ShiftToggle";
NSString *const PKCapsLockToggleKey = @"CapsLockToggle";
NSString *const PKEnglishToZhuyinKey = @"EnglishToZhuyin";
NSString *const PKZhuyinToEnglishKey = @"ZhuyinToEnglish";
NSString *const PKCandidatePositionKey = @"CandidatePosition";
NSString *const PKNextDoubtfulKey = @"NextDoubtful";
NSString *const PKKeypadPicksKey = @"KeypadPicks";

@implementation PKSettingsWindow

+ (void)registerDefaults {
  [NSUserDefaults.standardUserDefaults registerDefaults:@{
    PKAbbreviatedKey : @NO,
    PKRememberChoicesKey : @YES,
    PKShiftToggleKey : @YES,
    PKCapsLockToggleKey : @NO,
    PKEnglishToZhuyinKey : @YES,
    PKZhuyinToEnglishKey : @YES,
    PKAppearanceKey : @(PKGlassAvailable() ? 1 : 0),
    PKProofPreviewKey : @1,
    PKChoosingProofKey : @1,
    PKCandidatePositionKey : @1,  // before the cursor, as the system Zhuyin input does
    PKNextDoubtfulKey : @YES,
    PKKeypadPicksKey : @NO,
  }];
}

+ (void)showWithClearChoices:(void (^)(void))clearChoices models:(id<PKModels>)models {
  static NSWindow *window;
  // A new form each time: one opened again offers 清除 again and checks for an update again.
  NSView *form = [PKSettingsForm makeViewWithClearChoices:clearChoices models:models];
  if (!window) {
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, form.fittingSize.width, form.fittingSize.height)
                                         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                           backing:NSBackingStoreBuffered
                                             defer:YES];
    window.title = @"Project Key 設定";
    window.releasedWhenClosed = NO;
    [window center];
  }
  if (!window.visible) window.contentView = form;  // an open window keeps its state
  // The input method is a background app; it has to come forward for the window to take keys.
  [NSApp activateIgnoringOtherApps:YES];
  [window makeKeyAndOrderFront:nil];
}

@end
