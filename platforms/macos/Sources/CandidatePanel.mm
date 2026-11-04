#import "CandidatePanel.h"

@implementation PKPanelContent
@end

namespace {

// Design tokens from the prototype's dark theme.
NSColor *Hex(uint32_t rgb, CGFloat alpha = 1) {
  return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0
                             green:((rgb >> 8) & 0xff) / 255.0
                              blue:(rgb & 0xff) / 255.0
                             alpha:alpha];
}
NSColor *const kBackground = Hex(0x23282c);
NSColor *const kLine = Hex(0x343b40);
NSColor *const kText = Hex(0xe8ecee);
NSColor *const kMuted = Hex(0x8e9aa3);
NSColor *const kAccent = Hex(0x14806f);
NSColor *const kAccentSoft = Hex(0x3fb9a3);
NSColor *const kDoubtMark = Hex(0x3fb9a3, 0.7);
NSColor *const kEdge = Hex(0x000000, 0.5);

constexpr CGFloat kRadius = 14, kPillRadius = 8, kMinWidth = 460, kGapToText = 8, kAnchorInset = 16;
// Proof row: padding 10 16 8 19, 20px line, 2px marks 3px under the line, inset 1px per side.
constexpr CGFloat kProofTop = 10, kProofBottom = 8, kProofLeft = 19, kProofRight = 16, kProofLine = 20;
constexpr CGFloat kMarkGap = 3, kMarkHeight = 2, kMarkInset = 1;
// Candidate row: padding 6 6 6 8; cells padding 5 11 6, 25px line, 5px label gap, 2px apart.
constexpr CGFloat kRowTop = 6, kRowBottom = 6, kRowLeft = 8, kRowRight = 6;
constexpr CGFloat kCellTop = 5, kCellBottom = 6, kCellSide = 11, kCellLine = 25, kLabelGap = 5, kCellGap = 2;
constexpr CGFloat kPageLeft = 12, kPageRight = 10;

NSFont *TextFont(CGFloat size) {
  return [NSFont fontWithName:@"PingFang TC" size:size] ?: [NSFont systemFontOfSize:size];
}

NSDictionary *Attributes(NSFont *font, NSColor *color) {
  return @{NSFontAttributeName : font, NSForegroundColorAttributeName : color};
}

// Top of a single line of `font` centered in a line box starting at `top`.
CGFloat TextTop(NSFont *font, CGFloat top, CGFloat lineHeight) {
  return top + (lineHeight - (font.ascender - font.descender)) / 2;
}

}  // namespace

@interface PKCandidateView : NSView
@property(nonatomic, strong) PKPanelContent *content;
@property(nonatomic) BOOL proofBelow;  // window above the text: proof row last, next to the text
- (NSSize)fittingSize;
@end

@implementation PKCandidateView {
  NSFont *_proofFont, *_candidateFont, *_labelFont, *_pageFont;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _proofFont = TextFont(14);
    _candidateFont = TextFont(20);
    _labelFont = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightMedium];
    _pageFont = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}

- (CGFloat)proofHeight {
  return self.content.showsProof ? kProofTop + kProofLine + kProofBottom : 0;
}

- (CGFloat)rowHeight {
  return self.content.candidates ? kRowTop + kCellTop + kCellLine + kCellBottom + kRowBottom : 0;
}

- (CGFloat)widthOf:(NSString *)string font:(NSFont *)font {
  return ceil([string sizeWithAttributes:@{NSFontAttributeName : font}].width);
}

- (CGFloat)cellWidth:(NSUInteger)index {
  NSString *label = [NSString stringWithFormat:@"%lu", index + 1];
  return kCellSide + [self widthOf:label font:_labelFont] + kLabelGap +
         [self widthOf:self.content.candidates[index] font:_candidateFont] + kCellSide;
}

- (NSSize)fittingSize {
  CGFloat width = kMinWidth;
  if (self.content.showsProof) {
    NSString *sentence = [[self.content.segments componentsJoinedByString:@""] stringByAppendingString:self.content.pendingReading];
    width = MAX(width, kProofLeft + [self widthOf:sentence font:_proofFont] + kProofRight);
  }
  if (self.content.candidates) {
    CGFloat row = kRowLeft + kRowRight;
    for (NSUInteger i = 0; i < self.content.candidates.count; i++) row += [self cellWidth:i] + kCellGap;
    if (self.content.pageLabel) row += kPageLeft + [self widthOf:self.content.pageLabel font:_pageFont] + kPageRight;
    width = MAX(width, row);
  }
  CGFloat separator = self.content.showsProof && self.content.candidates ? 1 : 0;
  return NSMakeSize(width, self.proofHeight + separator + self.rowHeight);
}

- (void)drawRect:(NSRect)dirtyRect {
  NSRect bounds = self.bounds;
  NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.25, 0.25) xRadius:kRadius yRadius:kRadius];
  [kBackground setFill];
  [shape fill];
  [kEdge setStroke];
  shape.lineWidth = 0.5;
  [shape stroke];

  BOOL both = self.content.showsProof && self.content.candidates;
  CGFloat proofTop = self.proofBelow ? self.rowHeight + (both ? 1 : 0) : 0;
  CGFloat rowTop = self.proofBelow ? 0 : self.proofHeight + (both ? 1 : 0);
  if (both) {
    [kLine setFill];
    NSRectFill(NSMakeRect(0, self.proofBelow ? self.rowHeight : self.proofHeight, NSWidth(bounds), 1));
  }
  if (self.content.showsProof) [self drawProofAt:proofTop];
  if (self.content.candidates) [self drawCandidatesAt:rowTop width:NSWidth(bounds)];
}

- (void)drawProofAt:(CGFloat)top {
  CGFloat x = kProofLeft;
  CGFloat textTop = TextTop(_proofFont, top + kProofTop, kProofLine);
  CGFloat markTop = top + kProofTop + kProofLine + kMarkGap;
  NSDictionary *muted = Attributes(_proofFont, kMuted);
  NSUInteger length = [self.content.segments componentsJoinedByString:@""].length;
  NSUInteger start = 0;
  // The pending reading, or a caret line when there is none, sits at the cursor.
  void (^drawCursor)(CGFloat *) = ^(CGFloat *at) {
    NSString *reading = self.content.pendingReading;
    if (reading.length) {
      [reading drawAtPoint:NSMakePoint(*at, textTop) withAttributes:muted];
      *at += [self widthOf:reading font:self->_proofFont];
    } else if (self.content.caret < length && self.content.activeSegment < 0) {
      [kAccentSoft setFill];
      NSRectFill(NSMakeRect(*at, top + kProofTop + 1, 1, kProofLine - 2));
    }
  };
  if (self.content.caret == 0) drawCursor(&x);
  for (NSUInteger i = 0; i < self.content.segments.count; i++) {
    NSString *segment = self.content.segments[i];
    BOOL active = (NSInteger)i == self.content.activeSegment;
    NSDictionary *attributes = Attributes(_proofFont, active ? kAccentSoft : kMuted);
    CGFloat left = x;
    NSUInteger split = self.content.caret > start && self.content.caret < start + segment.length ? self.content.caret - start : 0;
    if (split) {
      NSString *head = [segment substringToIndex:split];
      [head drawAtPoint:NSMakePoint(x, textTop) withAttributes:attributes];
      x += [self widthOf:head font:_proofFont];
      drawCursor(&x);
      NSString *tail = [segment substringFromIndex:split];
      [tail drawAtPoint:NSMakePoint(x, textTop) withAttributes:attributes];
      x += [self widthOf:tail font:_proofFont];
    } else {
      [segment drawAtPoint:NSMakePoint(x, textTop) withAttributes:attributes];
      x += [self widthOf:segment font:_proofFont];
    }
    NSColor *mark = active ? kAccentSoft : [self.content.doubtful containsIndex:i] ? kDoubtMark : nil;
    if (mark) {
      [mark setFill];
      NSRect line = NSMakeRect(left + kMarkInset, markTop, x - left - 2 * kMarkInset, kMarkHeight);
      [[NSBezierPath bezierPathWithRoundedRect:line xRadius:1 yRadius:1] fill];
    }
    start += segment.length;
    if (self.content.caret == start && start > 0) drawCursor(&x);
  }
}

- (void)drawCandidatesAt:(CGFloat)top width:(CGFloat)width {
  CGFloat x = kRowLeft;
  CGFloat cellTop = top + kRowTop;
  CGFloat textTop = TextTop(_candidateFont, cellTop + kCellTop, kCellLine);
  CGFloat labelTop = textTop + _candidateFont.ascender - _labelFont.ascender;  // share the baseline
  for (NSUInteger i = 0; i < self.content.candidates.count; i++) {
    BOOL on = (NSInteger)i == self.content.selection;
    CGFloat cell = [self cellWidth:i];
    if (on) {
      [kAccent setFill];
      NSRect pill = NSMakeRect(x, cellTop, cell, kCellTop + kCellLine + kCellBottom);
      [[NSBezierPath bezierPathWithRoundedRect:pill xRadius:kPillRadius yRadius:kPillRadius] fill];
    }
    NSString *label = [NSString stringWithFormat:@"%lu", i + 1];
    [label drawAtPoint:NSMakePoint(x + kCellSide, labelTop)
        withAttributes:Attributes(_labelFont, on ? [NSColor colorWithWhite:1 alpha:0.72] : kMuted)];
    CGFloat textX = x + kCellSide + [self widthOf:label font:_labelFont] + kLabelGap;
    [self.content.candidates[i] drawAtPoint:NSMakePoint(textX, textTop) withAttributes:Attributes(_candidateFont, on ? NSColor.whiteColor : kText)];
    x += cell + kCellGap;
  }
  if (self.content.pageLabel) {
    CGFloat labelWidth = [self widthOf:self.content.pageLabel font:_pageFont];
    CGFloat pageTop = TextTop(_pageFont, cellTop + kCellTop, kCellLine);
    [self.content.pageLabel drawAtPoint:NSMakePoint(width - kRowRight - kPageRight - labelWidth, pageTop)
                         withAttributes:Attributes(_pageFont, kMuted)];
  }
}

@end

@implementation PKCandidatePanel {
  NSPanel *_panel;
  PKCandidateView *_view;
}

+ (instancetype)shared {
  static PKCandidatePanel *shared;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    shared = [[PKCandidatePanel alloc] init];
  });
  return shared;
}

- (instancetype)init {
  if ((self = [super init])) {
    _panel = [[NSPanel alloc] initWithContentRect:NSZeroRect
                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                          backing:NSBackingStoreBuffered
                                            defer:YES];
    _panel.level = NSPopUpMenuWindowLevel + 1;
    _panel.opaque = NO;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.hasShadow = YES;
    _panel.ignoresMouseEvents = YES;
    // "Hide Others" in the app being typed into hides this background process too.
    _panel.canHide = NO;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    _view = [[PKCandidateView alloc] initWithFrame:NSZeroRect];
    _panel.contentView = _view;
  }
  return self;
}

- (void)show:(PKPanelContent *)content below:(NSRect)lineRect {
  _view.content = content;
  NSSize size = [_view fittingSize];
  NSScreen *screen = NSScreen.mainScreen;
  for (NSScreen *candidate in NSScreen.screens) {
    if (NSPointInRect(lineRect.origin, candidate.frame)) screen = candidate;
  }
  NSRect visible = screen.visibleFrame;
  NSRect frame = NSMakeRect(NSMinX(lineRect) - kAnchorInset, NSMinY(lineRect) - kGapToText - size.height, size.width, size.height);
  _view.proofBelow = NSMinY(frame) < NSMinY(visible);
  if (_view.proofBelow) frame.origin.y = NSMaxY(lineRect) + kGapToText;
  frame.origin.x = MAX(NSMinX(visible), MIN(NSMinX(frame), NSMaxX(visible) - size.width));
  [_panel setFrame:frame display:NO];
  _view.needsDisplay = YES;
  [_panel orderFront:nil];
  [_panel invalidateShadow];
}

- (void)hide {
  [_panel orderOut:nil];
}

- (BOOL)isVisible {
  return _panel.visible;
}

- (PKPanelContent *)content {
  return _view.content;
}

+ (NSData *)pngForContent:(PKPanelContent *)content proofBelow:(BOOL)proofBelow {
  PKCandidateView *view = [[PKCandidateView alloc] initWithFrame:NSZeroRect];
  view.content = content;
  view.proofBelow = proofBelow;
  NSSize size = [view fittingSize];
  view.frame = NSMakeRect(0, 0, size.width, size.height);
  NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                  pixelsWide:(NSInteger)size.width * 2
                                                                  pixelsHigh:(NSInteger)size.height * 2
                                                               bitsPerSample:8
                                                             samplesPerPixel:4
                                                                    hasAlpha:YES
                                                                    isPlanar:NO
                                                              colorSpaceName:NSDeviceRGBColorSpace
                                                                 bytesPerRow:0
                                                                bitsPerPixel:0];
  rep.size = size;
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
  return [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
}

@end
