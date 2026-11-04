// The settings window opened from the input menu, and the defaults it edits.
// Also the Objective-C header SettingsForm.swift imports.
#import <Cocoa/Cocoa.h>

#import "CandidatePanel.h"

NS_ASSUME_NONNULL_BEGIN

FOUNDATION_EXPORT NSString *const PKProofPreviewKey;     // 0 always, 1 when something is doubtful, 2 off
FOUNDATION_EXPORT NSString *const PKAdjacentKeysKey;     // match readings one key off
FOUNDATION_EXPORT NSString *const PKRememberChoicesKey;  // the user override model
FOUNDATION_EXPORT NSString *const PKShiftToggleKey;      // a lone Shift switches 中/英

@interface PKSettingsWindow : NSObject
+ (void)registerDefaults;
+ (void)showWithClearChoices:(void (^)(void))clearChoices;
@end

NS_ASSUME_NONNULL_END
