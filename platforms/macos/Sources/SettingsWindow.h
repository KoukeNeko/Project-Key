// The settings window opened from the input menu, and the defaults it edits.
// Also the Objective-C header SettingsForm.swift imports.
#import <Cocoa/Cocoa.h>

#import "CandidatePanel.h"

NS_ASSUME_NONNULL_BEGIN

// Proof row modes: 0 always, 1 when something is doubtful, 2 off.
FOUNDATION_EXPORT NSString *const PKProofPreviewKey;     // the bubble beside the text while typing
FOUNDATION_EXPORT NSString *const PKChoosingProofKey;    // the row under the candidates while choosing
FOUNDATION_EXPORT NSString *const PKAbbreviatedKey;      // 簡拼: a lone symbol stands for a syllable
FOUNDATION_EXPORT NSString *const PKOmitTonesKey;        // a key in the place a whole syllable ends in starts the next
FOUNDATION_EXPORT NSString *const PKRememberChoicesKey;  // the user override model
FOUNDATION_EXPORT NSString *const PKShiftToggleKey;      // a lone Shift switches 中/英
FOUNDATION_EXPORT NSString *const PKFullWidthToggleKey;  // ⇧ Space switches full-width
FOUNDATION_EXPORT NSString *const PKCapsLockToggleKey;   // Caps Lock switches 中/英 and letters stay lowercase
FOUNDATION_EXPORT NSString *const PKEnglishToZhuyinKey;  // that Shift turns Zhuyin typed in 英 into a composition
FOUNDATION_EXPORT NSString *const PKZhuyinToEnglishKey;  // and English typed in 中 back into English
FOUNDATION_EXPORT NSString *const PKCandidatePositionKey;  // candidates for the word: 0 after the cursor, 1 before it
FOUNDATION_EXPORT NSString *const PKNextDoubtfulKey;       // picking a candidate moves on to the next doubtful word
FOUNDATION_EXPORT NSString *const PKKeypadPicksKey;        // the keypad's digits pick from the row shown while typing
FOUNDATION_EXPORT NSString *const PKGridColumnsKey;        // candidates per row: 0 to 3 for 6 to 9

// The dictionary and context models the engine uses, and where the settings window puts a
// newer release for it (core/engine/Models.h).
@protocol PKModels <NSObject>
@property(readonly, copy) NSString *version;  // models-version.txt of the models in use; empty when they have none
@property(readonly) BOOL loaded;              // bigram.bin and chars.bin are in use
@property(readonly, copy) NSURL *downloads;
- (BOOL)reload;  // switches to the newest release in downloads; NO when it cannot be loaded
@end

// The input method's bundle identifier (Resources/Info.plist). The settings app
// (Sources/SettingsApp) has its own, so the system never takes it for a second input method.
FOUNDATION_EXPORT NSString *const PKInputMethodBundleID;
// What the settings app posts to the input method, as distributed notifications.
FOUNDATION_EXPORT NSString *const PKClearChoicesNotification;  // after removing personal.txt
FOUNDATION_EXPORT NSString *const PKReloadModelsNotification;  // after downloading models
// The input method's defaults: the standard ones in the input method, its domain in the settings app.
FOUNDATION_EXPORT NSUserDefaults *PKDefaults(void);
// The input method's bundle: this one, or the installed one in the settings app (nil when not installed).
FOUNDATION_EXPORT NSBundle *_Nullable PKInputMethodBundle(void);
// ~/Library/Application Support/ProjectKey: personal.txt, phrases.txt and the downloaded models.
FOUNDATION_EXPORT NSURL *PKSupportDirectory(void);

// Opens the settings window, as the input menu's 設定… does (InputController.mm), or at the
// settings app's launch (SettingsApp/main.mm).
FOUNDATION_EXPORT void PKShowSettings(void);
// Opens the user's words (Lexicon::loadUserPhrases) in their text editor, made first when missing;
// as the input menu's 編輯自訂詞… does. Nothing without a place to keep them (outside the app bundle).
FOUNDATION_EXPORT void PKEditUserPhrases(void);

@interface PKSettingsWindow : NSObject
+ (void)registerDefaults;
+ (void)showWithClearChoices:(void (^)(void))clearChoices models:(id<PKModels>)models;
@end

NS_ASSUME_NONNULL_END
