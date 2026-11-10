#import "SettingsWindow.h"

#import "ProjectKey-Swift.h"

NSString *const PKProofPreviewKey = @"ProofPreview";
NSString *const PKChoosingProofKey = @"ChoosingProof";
NSString *const PKAbbreviatedKey = @"Abbreviated";
NSString *const PKOmitTonesKey = @"OmitTones";
NSString *const PKRememberChoicesKey = @"RememberChoices";
NSString *const PKShiftToggleKey = @"ShiftToggle";
NSString *const PKFullWidthToggleKey = @"FullWidthToggle";
NSString *const PKCapsLockToggleKey = @"CapsLockToggle";
NSString *const PKEnglishToZhuyinKey = @"EnglishToZhuyin";
NSString *const PKZhuyinToEnglishKey = @"ZhuyinToEnglish";
NSString *const PKCandidatePositionKey = @"CandidatePosition";
NSString *const PKNextDoubtfulKey = @"NextDoubtful";
NSString *const PKKeypadPicksKey = @"KeypadPicks";
NSString *const PKGridColumnsKey = @"GridColumns";
NSString *const PKInputMethodBundleID = @"org.projectkey.inputmethod.ProjectKey";
NSString *const PKClearChoicesNotification = @"org.projectkey.inputmethod.ProjectKey.ClearChoices";
NSString *const PKReloadModelsNotification = @"org.projectkey.inputmethod.ProjectKey.ReloadModels";

// The settings app is the bundled process that is not the input method; the tests have no bundle.
static BOOL InSettingsApp(void) {
  NSString *identifier = NSBundle.mainBundle.bundleIdentifier;
  return identifier && ![identifier isEqualToString:PKInputMethodBundleID];
}

NSUserDefaults *PKDefaults(void) {
  static NSUserDefaults *defaults;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    defaults = InSettingsApp() ? [[NSUserDefaults alloc] initWithSuiteName:PKInputMethodBundleID] : NSUserDefaults.standardUserDefaults;
  });
  return defaults;
}

NSBundle *PKInputMethodBundle(void) {
  if (!InSettingsApp()) return NSBundle.mainBundle;
  // Where install.sh and the Homebrew cask put it.
  NSString *installed = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Input Methods/ProjectKey.app"];
  return [NSBundle bundleWithPath:installed];
}

NSURL *PKSupportDirectory(void) {
  NSURL *support = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
  return [support URLByAppendingPathComponent:@"ProjectKey" isDirectory:YES];
}

// Opens the user's words in their text editor; the next composition after a save reads them.
// The tests have no bundle and must not touch the user's file.
void PKEditUserPhrases(void) {
  if (!NSBundle.mainBundle.bundleIdentifier) return;
  NSString *path = [PKSupportDirectory() URLByAppendingPathComponent:@"phrases.txt"].path;
  NSFileManager *files = NSFileManager.defaultManager;
  if (![files fileExistsAtPath:path]) {
    [files createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    NSError *error = nil;
    if (![@"# 每行一個詞：詞、空白、注音\n# 注音的音節以 - 連接，中間不留空白\n# 一聲不標；其他聲調寫在音節後，輕聲寫 ˙（ㄉㄜ˙）\n# 例：小鍵 ㄒㄧㄠˇ-ㄐㄧㄢˋ\n# 存檔後，下一次打字就會用到；# 開頭的行不會讀入\n" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
      NSLog(@"Project Key: cannot create %@: %@", path, error);
      return;
    }
  }
  [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:path]];
}

@implementation PKSettingsWindow

+ (void)registerDefaults {
  [PKDefaults() registerDefaults:@{
    PKAbbreviatedKey : @NO,
    PKOmitTonesKey : @NO,
    PKRememberChoicesKey : @YES,
    PKShiftToggleKey : @YES,
    PKFullWidthToggleKey : @YES,
    PKCapsLockToggleKey : @NO,
    PKEnglishToZhuyinKey : @YES,
    PKZhuyinToEnglishKey : @YES,
    PKAppearanceKey : @(PKGlassAvailable() ? 1 : 0),
    PKProofPreviewKey : @1,
    PKChoosingProofKey : @1,
    PKCandidatePositionKey : @1,  // before the cursor, as the system Zhuyin input does
    PKNextDoubtfulKey : @YES,
    PKKeypadPicksKey : @NO,
    PKGridColumnsKey : @0,
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
