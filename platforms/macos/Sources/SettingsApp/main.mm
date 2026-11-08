// Project Key 設定.app: the input method's settings window as an app of its own, for when Project
// Key is not in the menu bar. It edits the input method's defaults domain, which the input method
// reads at its next composition, and tells it about what it keeps in memory with distributed
// notifications, as the Windows settings app tells the engine server through its pipe.
#import <Cocoa/Cocoa.h>

#import "ProjectKey-Swift.h"
#import "SettingsWindow.h"
#include "Models.h"

namespace {

std::filesystem::path ModelsDirectory() {
  return ProjectKey::ModelsDirectory(PKInputMethodBundle().resourcePath.fileSystemRepresentation,
                                     [PKSupportDirectory() URLByAppendingPathComponent:@"models"].fileSystemRepresentation);
}

void Post(NSString *name) {
  [NSDistributedNotificationCenter.defaultCenter postNotificationName:name object:nil userInfo:nil deliverImmediately:YES];
}

}  // namespace

// The models the input method loads: the newest of its own and the downloaded ones.
@interface PKInstalledModels : NSObject <PKModels>
@end

@implementation PKInstalledModels
- (NSString *)version {
  return @(ProjectKey::ModelsVersion(ModelsDirectory()).c_str());
}
- (BOOL)loaded {
  std::error_code error;
  return std::filesystem::exists(ModelsDirectory() / "bigram.bin", error) && std::filesystem::exists(ModelsDirectory() / "chars.bin", error);
}
- (NSURL *)downloads {
  return [PKSupportDirectory() URLByAppendingPathComponent:@"models" isDirectory:YES];
}
// A running input method switches to them now; one started later picks them up anyway.
- (BOOL)reload {
  Post(PKReloadModelsNotification);
  return YES;
}
@end

void PKShowSettings(void) {
  [PKSettingsWindow showWithClearChoices:^{
    NSURL *personal = [PKSupportDirectory() URLByAppendingPathComponent:@"personal.txt"];
    NSError *error = nil;
    if (![NSFileManager.defaultManager removeItemAtURL:personal error:&error] && !(error.domain == NSCocoaErrorDomain && error.code == NSFileNoSuchFileError)) {
      NSLog(@"Project Key: cannot remove %@: %@", personal.path, error);
      return;
    }
    Post(PKClearChoicesNotification);  // the input method also forgets the choices it holds in memory
  } models:[[PKInstalledModels alloc] init]];
}

@interface PKSettingsApp : NSObject <NSApplicationDelegate>
@end

@implementation PKSettingsApp
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  return YES;
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible {
  if (!visible) PKShowSettings();
  return YES;
}
@end

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    NSApplication *app = NSApplication.sharedApplication;
    static PKSettingsApp *delegate = [[PKSettingsApp alloc] init];
    app.delegate = delegate;
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"關閉視窗" action:@selector(performClose:) keyEquivalent:@"w"];
    [appMenu addItemWithTitle:@"結束 Project Key 設定" action:@selector(terminate:) keyEquivalent:@"q"];
    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    appItem.submenu = appMenu;
    app.mainMenu = [[NSMenu alloc] init];
    [app.mainMenu addItem:appItem];
    if (!PKInputMethodBundle()) {
      NSAlert *alert = [[NSAlert alloc] init];
      alert.messageText = @"找不到 Project Key";
      alert.informativeText = @"請先安裝 Project Key 輸入法。";
      [alert runModal];
      return 1;
    }
    [PKSettingsWindow registerDefaults];
    PKShowSettings();
    [app run];
  }
  return 0;
}
