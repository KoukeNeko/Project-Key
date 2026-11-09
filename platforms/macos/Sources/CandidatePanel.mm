#import "CandidatePanel.h"

#import <QuartzCore/QuartzCore.h>

#include "PanelMetrics.h"

NSString *const PKAppearanceKey = @"Appearance";
const NSInteger PKGridColumns = 6;

BOOL PKGlassAvailable(void) {
  if (@available(macOS 26.0, *)) return YES;
  return NO;
}

@implementation PKPanelContent
@end

namespace {

using namespace ProjectKey::Metrics;

// Design tokens from the prototype's dark theme.
NSColor *Hex(uint32_t rgb, CGFloat alpha = 1) {
  return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xff) / 255.0
                             green:((rgb >> 8) & 0xff) / 255.0
                              blue:(rgb & 0xff) / 255.0
                             alpha:alpha];
}
NSColor *const kBackground = Hex(Colors::kBackground);
NSColor *const kLine = Hex(Colors::kLine);
NSColor *const kText = Hex(Colors::kText);
NSColor *const kMuted = Hex(Colors::kMuted);
NSColor *const kAccent = Hex(Colors::kAccent);
NSColor *const kAccentSoft = Hex(Colors::kAccentSoft);
NSColor *const kDoubtMark = Hex(Colors::kAccentSoft, Colors::kDoubtMarkAlpha);
NSColor *const kEdge = Hex(0x000000, 0.5);

// macOS corners; the other metrics are PanelMetrics.h's, shared with Windows and Linux.
constexpr CGFloat kRadius = 14, kPillRadius = 8;
NSString *const kReplacedArrow = @" → ";
constexpr NSTimeInterval kSlideDuration = kSlideMs / 1000.0, kResizeDuration = kResizeMs / 1000.0,
                         kWindowFadeDuration = kWindowFadeMs / 1000.0;

BOOL ReducesMotion() {
  return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
}

// Ease-out cubic.
CGFloat Ease(CGFloat t) {
  return 1 - pow(1 - t, 3);
}

BOOL UsesGlass() {
  return PKGlassAvailable() && [NSUserDefaults.standardUserDefaults integerForKey:PKAppearanceKey] == 1;
}

// Wraps `content` in a Liquid Glass view, or returns it as is for the dark style.
NSView *Container(NSView *content, BOOL glass, CGFloat radius) {
  if (@available(macOS 26.0, *)) {
    if (glass) {
      NSGlassEffectView *view = [[NSGlassEffectView alloc] init];
      view.cornerRadius = radius;
      view.contentView = content;
      return view;
    }
  }
  return content;
}

NSFont *TextFont(CGFloat size) {
  return [NSFont fontWithName:@"PingFang TC" size:size] ?: [NSFont systemFontOfSize:size];
}

NSFont *CandidateFont() {
  static NSFont *font = TextFont(20);
  return font;
}

NSFont *LabelFont() {
  static NSFont *font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightMedium];
  return font;
}

CGFloat WidthOf(NSString *string, NSFont *font) {
  return ceil([string sizeWithAttributes:@{NSFontAttributeName : font}].width);
}

// A cell's natural width: side padding, label, gap, text, side padding.
CGFloat CellWidth(NSString *candidate) {
  return kCellSide + WidthOf(@"6", LabelFont()) + kLabelGap + WidthOf(candidate, CandidateFont()) + kCellSide;
}

// Every column fits a two-character word; longer words span more columns.
CGFloat ColumnWidth() {
  static CGFloat width = CellWidth(@"國國");
  return width;
}

NSInteger Span(NSString *candidate) {
  NSInteger span = (NSInteger)ceil((CellWidth(candidate) + kCellGap) / (ColumnWidth() + kCellGap));
  return MAX(1, MIN(PKGridColumns, span));
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
@property(nonatomic) BOOL glass;  // on Liquid Glass: no fill, system label colors
- (NSSize)fittingSize;
// Moves the selection highlight to the content's selection, sliding there when animated.
- (void)moveSelectionAnimated:(BOOL)animated;
// Where the rows start, as drawn now.
- (CGFloat)shownRowTop;
// What a click at this point in the window lands on.
- (PanelHit)hitAt:(NSPoint)location;
// A change of shape, as the window moves and resizes to the new content: the rows move from
// rowTop to where the content puts them, and a proof row that comes, goes or changes sides
// fades out of its old place and into its new one. progress runs from 0 to 1.
- (void)morphFrom:(PKPanelContent *)old proofBelow:(BOOL)oldProofBelow rowTop:(CGFloat)rowTop;
@property(nonatomic) CGFloat progress;
@end

@implementation PKCandidateView {
  NSFont *_proofFont, *_candidateFont, *_labelFont, *_pageFont;
  NSRect _pillFrom, _pillTo;  // the highlight slides from one to the other, relative to the rows
  NSTimeInterval _slideStart;
  NSTimer *_slideTimer;
  PKPanelContent *_oldContent;  // what morphFrom: started from
  BOOL _oldProofBelow;
  CGFloat _oldRowTop;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _proofFont = TextFont(14);
    _candidateFont = CandidateFont();
    _labelFont = LabelFont();
    _pageFont = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
    _progress = 1;
  }
  return self;
}

- (void)morphFrom:(PKPanelContent *)old proofBelow:(BOOL)oldProofBelow rowTop:(CGFloat)rowTop {
  _oldContent = old;
  _oldProofBelow = oldProofBelow;
  _oldRowTop = rowTop;
  self.progress = 0;
}

- (void)setProgress:(CGFloat)progress {
  _progress = progress;
  if (progress >= 1) _oldContent = nil;
  self.needsDisplay = YES;
}

- (CGFloat)shownRowTop {
  return _oldRowTop + (self.rowTop - _oldRowTop) * _progress;
}

- (PanelHit)hitAt:(NSPoint)location {
  NSPoint point = [self convertPoint:location fromView:nil];
  std::vector<std::vector<int>> spans;
  for (NSArray<NSString *> *row in self.content.rows) {
    spans.emplace_back();
    for (NSString *word in row) spans.back().push_back(static_cast<int>(Span(word)));
  }
  return HitCandidates(spans, self.content.pageLabel != nil, point.x, point.y - self.shownRowTop, ColumnWidth(), static_cast<int>(PKGridColumns));
}

// Whether the proof row fades between two places rather than staying where it is.
- (BOOL)proofMoves {
  return _oldContent && (_oldContent.showsProof != self.content.showsProof || _oldProofBelow != self.proofBelow);
}

- (BOOL)isFlipped {
  return YES;
}

- (CGFloat)proofHeight {
  return self.content.showsProof ? kProofPad + kProofLine + kProofPad : 0;  // marks sit in the bottom pad
}

- (CGFloat)rowHeight {
  NSInteger rows = self.content.rows.count;
  if (!rows) return 0;
  CGFloat cell = kCellTop + kCellLine + kCellBottom;
  return kRowTop + rows * cell + (rows - 1) * kCellGap + kRowBottom;
}

- (CGFloat)widthOf:(NSString *)string font:(NSFont *)font {
  return WidthOf(string, font);
}

- (NSColor *)textColor {
  return self.glass ? NSColor.labelColor : kText;
}

- (NSColor *)mutedColor {
  return self.glass ? NSColor.secondaryLabelColor : kMuted;
}

- (NSSize)fittingSize {
  CGFloat width = 0;
  if (self.content.showsProof) {
    NSString *sentence = [[self.content.segments componentsJoinedByString:@""] stringByAppendingString:self.content.pendingReading];
    CGFloat key = self.content.keyHint ? kKeyGap + [self keyWidth] : 0;
    if (self.content.replacedText) key += [self widthOf:[self.content.replacedText stringByAppendingString:kReplacedArrow] font:_proofFont];
    width = MAX(width, kProofLeft + [self widthOf:sentence font:_proofFont] + key + kProofRight);
  }
  if (self.content.rows) {
    CGFloat row = kRowLeft + PKGridColumns * (ColumnWidth() + kCellGap) + self.tailWidth;
    width = MAX(width, row);
  }
  CGFloat separator = self.content.showsProof && self.content.rows ? 1 : 0;
  return NSMakeSize(width, self.proofHeight + separator + self.rowHeight);
}

// The first row's end: with more rows, the page label and a chevron, down while folded and
// up while unfolded. Every row keeps room for them and for the widest label (999/999, or
// wider for more rows), so the window keeps its width from typing to choosing, folded or not.
- (CGFloat)tailWidth {
  NSString *total = [self.content.pageLabel componentsSeparatedByString:@"/"].lastObject;
  if (total.length < 3) total = @"999";
  CGFloat label = [self widthOf:[NSString stringWithFormat:@"%@/%@", total, total] font:_pageFont];
  return kPageLeft + label + kChevronGap + kChevronWidth + kChevronInset;
}

- (CGFloat)rowTop {
  BOOL both = self.content.showsProof && self.content.rows;
  return self.proofBelow ? 0 : self.proofHeight + (both ? 1 : 0);
}

// The row with digits and the highlight; while typing (selectedRow -1), the one row, which looks
// the same though its digits type Zhuyin, so choosing from it changes nothing on screen.
- (NSInteger)labeledRow {
  return MAX(0, self.content.selectedRow);
}

- (NSRect)selectionRect {
  NSArray<NSArray<NSString *> *> *rows = self.content.rows;
  if (self.labeledRow >= (NSInteger)rows.count) return NSZeroRect;
  NSArray<NSString *> *row = rows[self.labeledRow];
  if (self.content.selection < 0 || self.content.selection >= (NSInteger)row.count) return NSZeroRect;
  CGFloat cellHeight = kCellTop + kCellLine + kCellBottom;
  NSInteger column = 0;
  for (NSInteger i = 0; i < self.content.selection; i++) column += Span(row[i]);
  NSInteger span = Span(row[self.content.selection]);
  return NSMakeRect(kRowLeft + column * (ColumnWidth() + kCellGap), kRowTop + self.labeledRow * (cellHeight + kCellGap),
                    span * ColumnWidth() + (span - 1) * kCellGap, cellHeight);
}

- (NSRect)shownSelectionRect {
  if (!_slideTimer) return _pillTo;
  CGFloat t = Ease(MIN(1, (NSDate.timeIntervalSinceReferenceDate - _slideStart) / kSlideDuration));
  return NSMakeRect(NSMinX(_pillFrom) + (NSMinX(_pillTo) - NSMinX(_pillFrom)) * t,
                    NSMinY(_pillFrom) + (NSMinY(_pillTo) - NSMinY(_pillFrom)) * t,
                    NSWidth(_pillFrom) + (NSWidth(_pillTo) - NSWidth(_pillFrom)) * t,
                    NSHeight(_pillFrom) + (NSHeight(_pillTo) - NSHeight(_pillFrom)) * t);
}

- (void)moveSelectionAnimated:(BOOL)animated {
  NSRect from = self.shownSelectionRect;
  NSRect to = self.selectionRect;
  [_slideTimer invalidate];
  _slideTimer = nil;
  _pillTo = to;
  if (animated && !NSIsEmptyRect(from) && !NSIsEmptyRect(to) && !NSEqualRects(from, to) && !ReducesMotion()) {
    _pillFrom = from;
    _slideStart = NSDate.timeIntervalSinceReferenceDate;
    __weak PKCandidateView *weakSelf = self;
    _slideTimer = [NSTimer timerWithTimeInterval:1.0 / 120 repeats:YES block:^(NSTimer *timer) {
      PKCandidateView *view = weakSelf;
      if (!view || NSDate.timeIntervalSinceReferenceDate - view->_slideStart >= kSlideDuration) {
        [timer invalidate];
        if (view) view->_slideTimer = nil;
      }
      view.needsDisplay = YES;
    }];
    [NSRunLoop.currentRunLoop addTimer:_slideTimer forMode:NSRunLoopCommonModes];
  }
  self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  NSRect bounds = self.bounds;
  if (!self.glass) {
    NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.25, 0.25) xRadius:kRadius yRadius:kRadius];
    [kBackground setFill];
    [shape fill];
    [kEdge setStroke];
    shape.lineWidth = 0.5;
    [shape stroke];
  }

  // The proof row hugs the edge next to the text (the bottom above it), so it stays put while
  // the window resizes; when it comes, goes or changes sides, it fades between its places.
  BOOL moves = self.proofMoves;
  if (moves && _oldContent.showsProof) [self drawProofOf:_oldContent below:_oldProofBelow alpha:1 - _progress];
  if (self.content.showsProof) [self drawProofOf:self.content below:self.proofBelow alpha:moves ? _progress : 1];
  if (self.content.rows) {
    // While the window grows above the text, rows laid out for the final size
    // would run over the pinned proof row; keep them above it.
    [NSGraphicsContext saveGraphicsState];
    if (!moves && self.proofBelow && self.content.showsProof) {
      NSRectClip(NSMakeRect(0, 0, NSWidth(bounds), NSHeight(bounds) - self.proofHeight - 1));
    }
    [self drawCandidatesAt:self.shownRowTop width:NSWidth(bounds)];
    [NSGraphicsContext restoreGraphicsState];
  }
}

// A proof row, with the line between it and the rows, at the edge next to the text.
- (void)drawProofOf:(PKPanelContent *)content below:(BOOL)below alpha:(CGFloat)alpha {
  CGFloat height = kProofPad + kProofLine + kProofPad;
  CGFloat top = below ? NSHeight(self.bounds) - height : 0;
  CGContextRef context = NSGraphicsContext.currentContext.CGContext;
  CGContextSaveGState(context);
  CGContextSetAlpha(context, alpha);
  CGContextBeginTransparencyLayer(context, NULL);
  if (content.rows) {
    [(self.glass ? NSColor.separatorColor : kLine) setFill];
    NSRectFill(NSMakeRect(0, below ? top - 1 : height, NSWidth(self.bounds), 1));
  }
  // drawProofAt: draws self.content; an old proof row fading out is drawn from the old content.
  PKPanelContent *shown = _content;
  _content = content;
  [self drawProofAt:top];
  _content = shown;
  CGContextEndTransparencyLayer(context);
  CGContextRestoreGState(context);
}

- (CGFloat)keyWidth {
  return MAX(kKeyHeight, [self widthOf:self.content.keyHint font:_labelFont] + 2 * kKeyPadding);
}

- (void)drawKeyHintAt:(CGFloat)lineTop {
  CGFloat width = [self keyWidth];
  NSRect cap = NSMakeRect(NSWidth(self.bounds) - kProofRight - width, lineTop + (kProofLine - kKeyHeight) / 2, width, kKeyHeight);
  // A key: a face with a hairline edge, raised on a darker lip that shows below it.
  NSBezierPath *face = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(cap, 0.25, 0.25) xRadius:kKeyRadius yRadius:kKeyRadius];
  [NSGraphicsContext saveGraphicsState];
  NSBezierPath *outside = [NSBezierPath bezierPathWithRect:self.bounds];
  [outside appendBezierPath:face];
  outside.windingRule = NSWindingRuleEvenOdd;
  [outside addClip];
  [[NSColor.blackColor colorWithAlphaComponent:0.35] setFill];
  [[NSBezierPath bezierPathWithRoundedRect:NSOffsetRect(cap, 0, kKeyLip) xRadius:kKeyRadius yRadius:kKeyRadius] fill];
  [NSGraphicsContext restoreGraphicsState];
  [[self.mutedColor colorWithAlphaComponent:0.16] setFill];
  [face fill];
  face.lineWidth = 0.5;
  [[self.mutedColor colorWithAlphaComponent:0.35] setStroke];
  [face stroke];
  if ([self.content.keyHint isEqualToString:@"⇥"]) {  // the system's own Tab symbol
    NSImageSymbolConfiguration *style = [[NSImageSymbolConfiguration configurationWithPointSize:_labelFont.pointSize weight:NSFontWeightMedium]
        configurationByApplyingConfiguration:[NSImageSymbolConfiguration configurationWithPaletteColors:@[ self.mutedColor ]]];
    NSImage *symbol = [[NSImage imageWithSystemSymbolName:@"arrow.right.to.line" accessibilityDescription:@"Tab"] imageWithSymbolConfiguration:style];
    NSSize size = symbol.size;
    NSRect frame = NSMakeRect(NSMidX(cap) - size.width / 2, NSMidY(cap) - size.height / 2, size.width, size.height);
    [symbol drawInRect:frame fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    return;
  }
  NSDictionary *attributes = Attributes(_labelFont, self.mutedColor);
  // Centered on its ink, not its line box: a symbol such as ⇥ sits high in the line.
  NSRect ink = [self.content.keyHint boundingRectWithSize:NSZeroSize options:NSStringDrawingUsesDeviceMetrics attributes:attributes];
  CGFloat baseline = NSMidY(cap) + NSMinY(ink) + NSHeight(ink) / 2;
  [self.content.keyHint drawAtPoint:NSMakePoint(NSMidX(cap) - NSMinX(ink) - NSWidth(ink) / 2, baseline - _labelFont.ascender) withAttributes:attributes];
}

- (void)drawProofAt:(CGFloat)top {
  CGFloat x = kProofLeft;
  // The line stays centered; marks sit in the space below it, so they come and
  // go without moving the text.
  CGFloat lineTop = top + kProofPad;
  CGFloat textTop = TextTop(_proofFont, lineTop, kProofLine);
  CGFloat markTop = lineTop + kProofLine + kMarkGap;
  if (self.content.keyHint) [self drawKeyHintAt:lineTop];
  NSDictionary *muted = Attributes(_proofFont, self.mutedColor);
  if (self.content.replacedText) {
    NSMutableDictionary *struck = [muted mutableCopy];
    struck[NSStrikethroughStyleAttributeName] = @(NSUnderlineStyleSingle);
    [self.content.replacedText drawAtPoint:NSMakePoint(x, textTop) withAttributes:struck];
    x += [self widthOf:self.content.replacedText font:_proofFont];
    [kReplacedArrow drawAtPoint:NSMakePoint(x, textTop) withAttributes:muted];
    x += [self widthOf:kReplacedArrow font:_proofFont];
  }
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
      NSRectFill(NSMakeRect(*at, lineTop + 1, 1, kProofLine - 2));
    }
  };
  if (self.content.caret == 0) drawCursor(&x);
  for (NSUInteger i = 0; i < self.content.segments.count; i++) {
    NSString *segment = self.content.segments[i];
    BOOL active = (NSInteger)i == self.content.activeSegment;
    NSDictionary *attributes = Attributes(_proofFont, active ? kAccentSoft : self.mutedColor);
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
  CGFloat cellHeight = kCellTop + kCellLine + kCellBottom;
  NSRect pill = NSOffsetRect(self.shownSelectionRect, 0, top);
  if (!NSIsEmptyRect(pill)) {
    BOOL chosen = self.content.selectedRow >= 0;
    [(chosen ? kAccent : [self.textColor colorWithAlphaComponent:Colors::kGuessAlpha]) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:pill xRadius:kPillRadius yRadius:kPillRadius] fill];
  }
  for (NSUInteger r = 0; r < self.content.rows.count; r++) {
    CGFloat cellTop = top + kRowTop + r * (cellHeight + kCellGap);
    CGFloat textTop = TextTop(_candidateFont, cellTop + kCellTop, kCellLine);
    CGFloat labelTop = textTop + _candidateFont.ascender - _labelFont.ascender;  // share the baseline
    BOOL labeled = (NSInteger)r == self.labeledRow;
    NSInteger column = 0;
    NSArray<NSString *> *row = self.content.rows[r];
    for (NSUInteger i = 0; i < row.count; i++) {
      NSInteger span = Span(row[i]);
      CGFloat x = kRowLeft + column * (ColumnWidth() + kCellGap);
      column += span;
      BOOL on = labeled && self.content.selectedRow >= 0 && (NSInteger)i == self.content.selection;  // on the accent
      NSString *label = [NSString stringWithFormat:@"%lu", i + 1];
      if (labeled) {
        [label drawAtPoint:NSMakePoint(x + kCellSide, labelTop)
            withAttributes:Attributes(_labelFont, on ? [NSColor colorWithWhite:1 alpha:0.72] : self.mutedColor)];
      }
      CGFloat textX = x + kCellSide + [self widthOf:@"6" font:_labelFont] + kLabelGap;
      NSColor *color = on ? NSColor.whiteColor : self.textColor;
      [row[i] drawAtPoint:NSMakePoint(textX, textTop) withAttributes:Attributes(_candidateFont, color)];
    }
  }
  CGFloat cellTop = top + kRowTop;  // the chevron and page label sit on the first row
  if (self.content.pageLabel) {
    CGFloat x = width - kChevronInset - kChevronWidth, middle = top + kChevronMiddle;
    CGFloat tip = self.content.expanded ? -kChevronHeight / 2 : kChevronHeight / 2;  // up or down
    NSBezierPath *chevron = [NSBezierPath bezierPath];
    [chevron moveToPoint:NSMakePoint(x, middle - tip)];
    [chevron lineToPoint:NSMakePoint(x + kChevronWidth / 2, middle + tip)];
    [chevron lineToPoint:NSMakePoint(x + kChevronWidth, middle - tip)];
    chevron.lineWidth = kChevronLine;
    chevron.lineCapStyle = NSLineCapStyleRound;
    chevron.lineJoinStyle = NSLineJoinStyleRound;
    [self.mutedColor setStroke];
    [chevron stroke];
    CGFloat labelWidth = [self widthOf:self.content.pageLabel font:_pageFont];
    CGFloat pageTop = TextTop(_pageFont, cellTop + kCellTop, kCellLine);
    [self.content.pageLabel drawAtPoint:NSMakePoint(x - kChevronGap - labelWidth, pageTop)
                         withAttributes:Attributes(_pageFont, self.mutedColor)];
  }
}

@end

// Never the key window, so it hears of a click here rather than through its views' responder chain.
@interface PKClickablePanel : NSPanel
@property(nonatomic, copy) void (^released)(NSPoint location);
@end

@implementation PKClickablePanel

- (void)sendEvent:(NSEvent *)event {
  if (event.type == NSEventTypeLeftMouseUp && self.released) self.released(event.locationInWindow);
  [super sendEvent:event];
}

@end

@implementation PKCandidatePanel {
  NSPanel *_panel;
  PKCandidateView *_view;
  NSRect _target;  // where the last show put the window, before any resize animation ends
  BOOL _hiding;     // fading out; a show before it ends fades back in
  NSUInteger _fades;  // counts fades, so a fade-out that ends after a show does not order the window out
  NSTimer *_morphTimer;  // moves and resizes the window to _target, with the view's progress
}

+ (instancetype)shared {
  static PKCandidatePanel *shared;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    shared = [[PKCandidatePanel alloc] init];
  });
  return shared;
}

+ (instancetype)hint {
  static PKCandidatePanel *hint;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    hint = [[PKCandidatePanel alloc] init];
  });
  return hint;
}

- (void)setClicked:(void (^)(NSInteger, NSInteger))clicked {
  _clicked = [clicked copy];
  _panel.ignoresMouseEvents = !clicked;
}

- (void)releasedAt:(NSPoint)location {
  if (_hiding || !self.clicked) return;
  PanelHit hit = [_view hitAt:location];
  if (hit.kind == PanelHit::kCandidate) self.clicked(hit.row, hit.index);
  if (hit.kind == PanelHit::kChevron) self.clicked(-1, -1);
}

- (instancetype)init {
  if ((self = [super init])) {
    PKClickablePanel *clickable = [PKClickablePanel alloc];
    __weak PKCandidatePanel *weakSelf = self;
    _panel = clickable = [clickable initWithContentRect:NSZeroRect
                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                          backing:NSBackingStoreBuffered
                                            defer:YES];
    _panel.level = NSPopUpMenuWindowLevel + 1;
    _panel.opaque = NO;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.hasShadow = YES;
    _panel.ignoresMouseEvents = YES;
    clickable.released = ^(NSPoint location) {
      [weakSelf releasedAt:location];
    };
    // "Hide Others" in the app being typed into hides this background process too.
    _panel.canHide = NO;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    _view = [[PKCandidateView alloc] initWithFrame:NSZeroRect];
  }
  return self;
}

- (void)useCurrentAppearance {
  BOOL glass = UsesGlass();
  if (glass != _view.glass || !_panel.contentView) {
    _view.glass = glass;
    [_view removeFromSuperview];
    _panel.contentView = Container(_view, glass, kRadius);
  }
}

- (void)show:(PKPanelContent *)content beside:(PKCandidatePanel *)panel {
  [self useCurrentAppearance];
  _view.content = content;
  NSSize size = [_view fittingSize];
  NSRect next = panel->_target;
  // The candidate window is above the text when its proof row is below.
  CGFloat y = panel->_view.proofBelow ? NSMaxY(next) + kHintGap : NSMinY(next) - kHintGap - size.height;
  _target = NSMakeRect(NSMinX(next), y, size.width, size.height);
  [_panel setFrame:_target display:NO];
  [_panel invalidateShadow];
  _view.needsDisplay = YES;
  [self orderFront];
}

- (void)orderFront {
  BOOL appearing = !self.isVisible;
  _hiding = NO;
  if (!appearing) {
    [_panel orderFront:nil];
    return;
  }
  _fades++;
  if (ReducesMotion()) {
    _panel.alphaValue = 1;
    [_panel orderFront:nil];
    return;
  }
  if (!_panel.visible) _panel.alphaValue = 0;  // else it is fading out, and turns back from where it is
  [_panel orderFront:nil];
  [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
    context.duration = kWindowFadeDuration;
    self->_panel.animator.alphaValue = 1;
  }];
}

- (void)show:(PKPanelContent *)content below:(NSRect)lineRect {
  [self useCurrentAppearance];
  // One window changing shape: it moves and resizes to the new content, across the text when it
  // changes sides, while the highlight slides, the rows move with it and the proof row fades
  // between its places.
  BOOL animate = self.isVisible && !_hiding && !ReducesMotion();
  NSRect from = _panel.frame;
  PKPanelContent *old = _view.content;
  BOOL oldProofBelow = _view.proofBelow;
  CGFloat oldRowTop = _view.shownRowTop;
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
  _target = frame;
  [PKModeIndicator.shared avoid:frame];  // both sit under the caret
  [_view moveSelectionAnimated:animate];
  [_morphTimer invalidate];
  _morphTimer = nil;
  if (animate) {
    [_view morphFrom:old proofBelow:oldProofBelow rowTop:oldRowTop];
    NSTimeInterval start = NSDate.timeIntervalSinceReferenceDate;
    __weak PKCandidatePanel *weakSelf = self;
    _morphTimer = [NSTimer timerWithTimeInterval:1.0 / 120 repeats:YES block:^(NSTimer *timer) {
      PKCandidatePanel *panel = weakSelf;
      CGFloat t = MIN(1, (NSDate.timeIntervalSinceReferenceDate - start) / kResizeDuration);
      if (!panel || t >= 1) [timer invalidate];
      if (!panel) return;
      CGFloat e = Ease(t);
      NSRect to = panel->_target;
      [panel->_panel setFrame:NSMakeRect(NSMinX(from) + (NSMinX(to) - NSMinX(from)) * e, NSMinY(from) + (NSMinY(to) - NSMinY(from)) * e,
                                         NSWidth(from) + (NSWidth(to) - NSWidth(from)) * e, NSHeight(from) + (NSHeight(to) - NSHeight(from)) * e)
                      display:YES];
      [panel->_panel invalidateShadow];
      panel->_view.progress = e;
      if (t >= 1) panel->_morphTimer = nil;
    }];
    [NSRunLoop.currentRunLoop addTimer:_morphTimer forMode:NSRunLoopCommonModes];
  } else {
    _view.progress = 1;
    [_panel setFrame:frame display:NO];
    [_panel invalidateShadow];
  }
  _view.needsDisplay = YES;
  [self orderFront];
}

- (void)hide {
  if (!self.isVisible) return;
  _hiding = YES;
  NSUInteger fade = ++_fades;
  if (ReducesMotion()) {
    [_panel orderOut:nil];
    return;
  }
  [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
    context.duration = kWindowFadeDuration;
    self->_panel.animator.alphaValue = 0;
  } completionHandler:^{
    if (self->_fades == fade) [self->_panel orderOut:nil];
  }];
}

- (NSRect)frame {
  return _panel.frame;
}

- (BOOL)isVisible {
  return _panel.visible && !_hiding;
}

- (PKPanelContent *)content {
  return _view.content;
}

+ (NSInteger)columnsSpannedBy:(NSString *)candidate {
  return Span(candidate);
}

+ (NSData *)pngForContent:(PKPanelContent *)content proofBelow:(BOOL)proofBelow {
  PKCandidateView *view = [[PKCandidateView alloc] initWithFrame:NSZeroRect];
  view.content = content;
  view.proofBelow = proofBelow;
  [view moveSelectionAnimated:NO];
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

@interface PKCandidatePanel (Frame)
- (NSRect)frame;
@end

@implementation PKModeIndicator {
  NSPanel *_panel;
  NSView *_labelHost;
  NSTextField *_label;
  NSUInteger _generation;
  BOOL _glass;
}

+ (instancetype)shared {
  static PKModeIndicator *shared;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    shared = [[PKModeIndicator alloc] init];
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
    _panel.canHide = NO;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    _labelHost = [[NSView alloc] init];
    _labelHost.wantsLayer = YES;
    _labelHost.layer.cornerRadius = kPillRadius;
    _label = [NSTextField labelWithString:@""];
    _label.font = [NSFont fontWithName:@"PingFang TC Medium" size:15] ?: [NSFont systemFontOfSize:15 weight:NSFontWeightMedium];
    _label.alignment = NSTextAlignmentCenter;
    [_labelHost addSubview:_label];
  }
  return self;
}

// The badge's frame moved beside `rect` when it overlaps it: right of it, or
// left when the screen ends. Both hang from the text, so their tops already line up.
static NSRect Beside(NSRect badge, NSRect rect) {
  if (!NSIntersectsRect(badge, rect)) return badge;
  NSScreen *screen = NSScreen.mainScreen;
  for (NSScreen *candidate in NSScreen.screens) {
    if (NSIntersectsRect(rect, candidate.frame)) screen = candidate;
  }
  badge.origin.x = NSMaxX(rect) + kGapToText;
  if (NSMaxX(badge) > NSMaxX(screen.visibleFrame)) badge.origin.x = NSMinX(rect) - kGapToText - NSWidth(badge);
  return badge;
}

- (void)avoid:(NSRect)rect {
  if (_panel.visible) [_panel setFrame:Beside(_panel.frame, rect) display:NO];
}

- (void)show:(NSString *)label below:(NSRect)lineRect {
  constexpr CGFloat kSide = kBadgeSide, kHold = kBadgeHoldMs / 1000.0, kFadeIn = kBadgeFadeInMs / 1000.0, kFadeOut = kBadgeFadeOutMs / 1000.0;
  BOOL glass = UsesGlass();
  if (glass != _glass || !_panel.contentView) {
    _glass = glass;
    _labelHost.layer.backgroundColor = glass ? nil : kBackground.CGColor;
    _labelHost.layer.borderColor = glass ? nil : kEdge.CGColor;
    _labelHost.layer.borderWidth = glass ? 0 : 0.5;
    _label.textColor = glass ? NSColor.labelColor : kText;
    [_labelHost removeFromSuperview];
    _panel.contentView = Container(_labelHost, glass, kPillRadius);
  }
  _label.stringValue = label;
  [_label sizeToFit];
  CGFloat width = MAX(kSide, NSWidth(_label.frame) + 2 * kCellSide);
  _label.frame = NSMakeRect(0, (kSide - NSHeight(_label.frame)) / 2, width, NSHeight(_label.frame));
  NSRect frame = NSMakeRect(NSMinX(lineRect), NSMinY(lineRect) - kGapToText - kSide, width, kSide);
  NSScreen *screen = NSScreen.mainScreen;
  for (NSScreen *candidate in NSScreen.screens) {
    if (NSPointInRect(lineRect.origin, candidate.frame)) screen = candidate;
  }
  if (NSMinY(frame) < NSMinY(screen.visibleFrame)) frame.origin.y = NSMaxY(lineRect) + kGapToText;
  if (PKCandidatePanel.shared.visible) frame = Beside(frame, PKCandidatePanel.shared.frame);
  [_panel setFrame:frame display:YES];
  NSUInteger generation = ++_generation;
  _panel.alphaValue = 0;
  [_panel orderFront:nil];
  [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
    context.duration = kFadeIn;
    self->_panel.animator.alphaValue = 1;
  }];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((kFadeIn + kHold) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    if (generation != self->_generation) return;  // a newer switch restarted the badge
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
      context.duration = kFadeOut;
      self->_panel.animator.alphaValue = 0;
    } completionHandler:^{
      if (generation == self->_generation) [self->_panel orderOut:nil];
    }];
  });
}

@end
