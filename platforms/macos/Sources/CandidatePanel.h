// The Direction A candidate window: an optional whole-sentence proof row
// above a single row of numbered candidates. Metrics and colors follow
// docs/design/candidate-window/a-full (round 02).
#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface PKPanelContent : NSObject
@property(nonatomic, copy) NSArray<NSString *> *segments;
@property(nonatomic, copy) NSIndexSet *doubtful;
@property(nonatomic) NSInteger activeSegment;  // -1 when not choosing
@property(nonatomic, copy) NSString *pendingReading;
@property(nonatomic) NSUInteger caret;  // characters of the joined segments before the cursor
@property(nonatomic) BOOL showsProof;
@property(nonatomic, copy, nullable) NSArray<NSString *> *candidates;  // this page only; nil hides the row
@property(nonatomic) NSInteger selection;                             // index within the page
@property(nonatomic, copy, nullable) NSString *pageLabel;               // "1/3", nil for a single page
@end

@interface PKCandidatePanel : NSObject
+ (instancetype)shared;
// lineRect is the composition's first line in screen coordinates.
- (void)show:(PKPanelContent *)content below:(NSRect)lineRect;
- (void)hide;
@property(nonatomic, readonly, getter=isVisible) BOOL visible;
@property(nonatomic, readonly, nullable) PKPanelContent *content;
// Renders the window without showing it, at 2x, for design review.
+ (NSData *)pngForContent:(PKPanelContent *)content proofBelow:(BOOL)proofBelow;
@end

// The 中／英 badge shown under the caret when Shift switches modes; it fades out by itself.
@interface PKModeIndicator : NSObject
+ (instancetype)shared;
- (void)show:(NSString *)label below:(NSRect)lineRect;
@end

NS_ASSUME_NONNULL_END
