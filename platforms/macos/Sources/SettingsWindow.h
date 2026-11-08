// The settings window opened from the input menu, and the defaults it edits.
// Also the Objective-C header SettingsForm.swift imports.
#import <Cocoa/Cocoa.h>

#import "CandidatePanel.h"

NS_ASSUME_NONNULL_BEGIN

// Proof row modes: 0 always, 1 when something is doubtful, 2 off.
FOUNDATION_EXPORT NSString *const PKProofPreviewKey;     // the bubble beside the text while typing
FOUNDATION_EXPORT NSString *const PKChoosingProofKey;    // the row under the candidates while choosing
FOUNDATION_EXPORT NSString *const PKAbbreviatedKey;      // 簡拼: a lone symbol stands for a syllable
FOUNDATION_EXPORT NSString *const PKRememberChoicesKey;  // the user override model
FOUNDATION_EXPORT NSString *const PKShiftToggleKey;      // a lone Shift switches 中/英
FOUNDATION_EXPORT NSString *const PKCapsLockToggleKey;   // Caps Lock switches 中/英 and letters stay lowercase
FOUNDATION_EXPORT NSString *const PKEnglishToZhuyinKey;  // that Shift turns Zhuyin typed in 英 into a composition
FOUNDATION_EXPORT NSString *const PKZhuyinToEnglishKey;  // and English typed in 中 back into English
FOUNDATION_EXPORT NSString *const PKCandidatePositionKey;  // candidates for the word: 0 after the cursor, 1 before it
FOUNDATION_EXPORT NSString *const PKNextDoubtfulKey;       // picking a candidate moves on to the next doubtful word
FOUNDATION_EXPORT NSString *const PKKeypadPicksKey;        // the keypad's 1-6 pick from the row shown while typing

// The dictionary and context models the engine uses, and where the settings window puts a
// newer release for it (core/engine/Models.h).
@protocol PKModels <NSObject>
@property(readonly, copy) NSString *version;  // models-version.txt of the models in use; empty when they have none
@property(readonly) BOOL loaded;              // bigram.bin and chars.bin are in use
@property(readonly, copy) NSURL *downloads;
- (BOOL)reload;  // switches to the newest release in downloads; NO when it cannot be loaded
@end

// Opens the settings window, as the input menu's 設定… does (InputController.mm).
FOUNDATION_EXPORT void PKShowSettings(void);

@interface PKSettingsWindow : NSObject
+ (void)registerDefaults;
+ (void)showWithClearChoices:(void (^)(void))clearChoices models:(id<PKModels>)models;
@end

NS_ASSUME_NONNULL_END
