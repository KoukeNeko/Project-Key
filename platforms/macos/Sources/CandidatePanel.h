// The Direction A candidate window: an optional whole-sentence proof row
// above a grid of numbered candidates. Metrics and colors follow
// docs/design/candidate-window/a-full (round 02).
#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

// NSUserDefaults key for the window style: 0 dark, 1 Liquid Glass (macOS 26 and later).
FOUNDATION_EXPORT NSString *const PKAppearanceKey;
FOUNDATION_EXPORT BOOL PKGlassAvailable(void);  // C linkage, for SettingsForm.swift

@interface PKPanelContent : NSObject
@property(nonatomic, copy) NSArray<NSString *> *segments;
@property(nonatomic, copy) NSIndexSet *doubtful;
@property(nonatomic) NSInteger activeSegment;  // -1 when not choosing
@property(nonatomic, copy) NSString *pendingReading;
@property(nonatomic) NSUInteger caret;  // characters of the joined segments before the cursor
@property(nonatomic) BOOL showsProof;
@property(nonatomic, copy, nullable) NSString *keyHint;  // a key drawn as a keycap at the end of the proof row
@property(nonatomic, copy, nullable) NSString *replacedText;  // struck out before the proof text, with an arrow
// Visible rows of the candidate grid; nil hides the candidate area. Each row
// holds `columns` columns, and a long candidate spans several.
@property(nonatomic, copy, nullable) NSArray<NSArray<NSString *> *> *rows;
@property(nonatomic) NSInteger columns;      // grid columns per row, 6 to PKMaxGridColumns
@property(nonatomic) NSInteger selectedRow;  // index into rows; only this row carries the 1-9 labels
@property(nonatomic) NSInteger selection;    // index within the selected row
@property(nonatomic, copy, nullable) NSString *pageLabel;               // "1/3", nil for a single page
@property(nonatomic) BOOL expanded;  // several rows unfold; otherwise a page label means ↓ unfolds them
@end

extern const NSInteger PKMaxGridColumns;

@interface PKCandidatePanel : NSObject
+ (instancetype)shared;
// Grid columns `candidate` takes, 1 to PKMaxGridColumns.
+ (NSInteger)columnsSpannedBy:(NSString *)candidate;
// lineRect is the composition's first line in screen coordinates.
- (void)show:(PKPanelContent *)content below:(NSRect)lineRect;
// The Tab hint's own window, and placing it beside `panel` on the side away from the text.
+ (instancetype)hint;
- (void)show:(PKPanelContent *)content beside:(PKCandidatePanel *)panel;
- (void)hide;
@property(nonatomic, readonly, getter=isVisible) BOOL visible;
@property(nonatomic, readonly, nullable) PKPanelContent *content;
// A left click on the candidate at rows[row][index], or on the chevron (row -1). While nil, clicks go to the app.
@property(nonatomic, copy, nullable) void (^clicked)(NSInteger row, NSInteger index);
// Renders the window without showing it, at 2x, for design review.
+ (NSData *)pngForContent:(PKPanelContent *)content proofBelow:(BOOL)proofBelow;
@end

// The 中／英 badge shown under the caret when Shift switches modes; it fades out by itself.
@interface PKModeIndicator : NSObject
+ (instancetype)shared;
- (void)show:(NSString *)label below:(NSRect)lineRect;
// Moves a showing badge out of `rect`, the candidate window's frame.
- (void)avoid:(NSRect)rect;
@end

NS_ASSUME_NONNULL_END
