// The iOS keyboard: Gboard's Zhuyin layout (Dachen keys in rows of ten, with
// the tone keys on the number row) drawn in the system keyboard's colors over
// its own background, driving the shared Controller. A key sends the Dachen
// key it stands on, as a hardware keyboard would.
#import <UIKit/UIKit.h>

#import "GIFPanel.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "Controller.h"

using ProjectKey::Key;

@interface PKKeyboardViewController : UIInputViewController <UIScrollViewDelegate, UICollectionViewDataSource, UICollectionViewDelegateFlowLayout>
- (void)reloadCandidates;
@end

namespace {

NSString *String(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}

std::string Std(NSString *s) {
  return s.UTF8String ?: "";
}

// The settings the app writes (App/ProjectKeyApp.swift), in the app group named in Info.plist.
// Without full access the keyboard cannot read the group, and the defaults serve.
NSUserDefaults *SharedDefaults() {
  static NSUserDefaults *defaults = [[NSUserDefaults alloc]
      initWithSuiteName:[NSBundle bundleForClass:PKKeyboardViewController.class].infoDictionary[@"PKAppGroup"]];
  return defaults;
}

// The app asks to clear the choices by raising this count; the keyboard clears them when it next shows.
NSString *const kClearChoicesRequestKey = @"ClearChoicesRequest";
NSString *const kClearedChoicesKey = @"ClearedChoices";  // the last request done, in the keyboard's own defaults
NSString *const kIOS18StyleKey = @"IOS18Style";
NSString *const kHapticsKey = @"Haptics";  // 0 off, 1 light, 2 medium, 3 heavy; light when unset
NSString *const kCharacterPreviewKey = @"CharacterPreview";  // on when unset
// The 顏文字 page's 自訂 section: one item a line. App/ProjectKeyApp.swift has the same default.
NSString *const kCustomSymbolsKey = @"CustomSymbols",
             *const kDefaultCustomSymbols = @"(´・ω・`)\n(╯°□°)╯︵ ┻━┻\n¯\\_(ツ)_/¯\n(｡•̀ᴗ-)✧\nಠ_ಠ";
// 最近 of the emoji and the 顏文字 pages, in the keyboard's own defaults, newest first.
NSString *const kRecentSymbolsKey = @"RecentSymbols", *const kRecentKaomojiKey = @"RecentKaomoji";
constexpr NSUInteger kRecentCount = 32;

// What covers the keyboard, opened from the bar's tools when nothing is typed.
enum class PanelPage : NSInteger { kNone, kEmoji, kGIF, kKaomoji, kSettings };

NSArray<NSString *> *Lines(NSString *key, NSString *fallback) {
  NSString *text = [SharedDefaults() stringForKey:key] ?: fallback;
  return text.length ? [text componentsSeparatedByString:@"\n"] : @[];
}

class DefaultsSettings : public ProjectKey::Settings {
 public:
  std::optional<int> stored(ProjectKey::Setting setting) const override {
    // Candidates are for the word just typed, as on the system keyboard.
    if (setting == ProjectKey::Setting::kCandidatePosition) return 1;
    NSString *name = @(Name(setting));
    if (![SharedDefaults() objectForKey:name]) {
      // A pick commits, so what follows it is offered, as on the system keyboard; and on a phone's
      // small keys, fewer of them: 簡拼.
      if (setting == ProjectKey::Setting::kAssociatedPhrases || setting == ProjectKey::Setting::kAbbreviated) return 1;
      return std::nullopt;
    }
    return static_cast<int>([SharedDefaults() integerForKey:name]);
  }
  void set(ProjectKey::Setting setting, int value) override {
    [SharedDefaults() setInteger:value forKey:@(Name(setting))];
  }
};

// Loaded once per extension process; the system relaunches the process often.
ProjectKey::Environment &SharedEnvironment() {
  static ProjectKey::Environment *environment = [] {
    auto *env = new ProjectKey::Environment();
    env->lm = std::make_shared<ProjectKey::Lexicon>();
    NSBundle *bundle = [NSBundle bundleForClass:PKKeyboardViewController.class];
    NSString *data = [bundle pathForResource:@"data" ofType:@"txt"];
    env->lm->loadLanguageModel(data.fileSystemRepresentation);
    if (!env->lm->isDataModelLoaded()) NSLog(@"Project Key: cannot load %@", data);
    NSString *associated = [bundle pathForResource:@"associated-phrases-v2" ofType:@"txt"];
    env->lm->loadAssociatedPhrasesV2(associated.fileSystemRepresentation);
    if (!env->lm->isAssociatedPhrasesV2Loaded()) NSLog(@"Project Key: cannot load %@", associated);
    if (NSString *emoji = [bundle pathForResource:@"emoji" ofType:@"txt"]) {
      env->lm->loadEmoji(emoji.fileSystemRepresentation);
    } else {
      NSLog(@"Project Key: no emoji.txt");
    }
    if (NSString *path = [bundle pathForResource:@"bigram" ofType:@"bin"]) {
      auto bigram = std::make_shared<ProjectKey::Bigram>();
      if (bigram->load(path.fileSystemRepresentation)) {
        env->bigram = bigram;
      } else {
        NSLog(@"Project Key: cannot load %@", path);
      }
    }
    if (NSString *path = [bundle pathForResource:@"chars" ofType:@"bin"]) {
      auto chars = std::make_shared<ProjectKey::CharModel>();
      if (chars->load(path.fileSystemRepresentation)) {
        env->chars = chars;
      } else {
        NSLog(@"Project Key: cannot load %@", path);
      }
    }
    env->userModel = ProjectKey::NewUserModel();
    // What the user typed, learned in the keyboard's own container on this device. The
    // system ends the extension at any time, so the model is saved after every commit.
    NSURL *support = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
                                                         inDomain:NSUserDomainMask
                                                appropriateForURL:nil
                                                           create:YES
                                                            error:nil];
    if (support) env->personalPath = [support URLByAppendingPathComponent:@"personal.txt"].fileSystemRepresentation;
    env->personal = std::make_shared<ProjectKey::PersonalModel>();
    if (!env->personalPath.empty()) env->personal->load(env->personalPath);
    static DefaultsSettings settings;
    env->settings = &settings;
    return env;
  }();
  return *environment;
}

class ClientAdapter : public ProjectKey::Client {
 public:
  __weak PKKeyboardViewController *owner;
  std::string marked;  // the composition as last shown

  id<UITextDocumentProxy> proxy() { return owner.textDocumentProxy; }

  void insertText(const std::string &text) override {
    // Marking the text and ending the mark puts it in place of the composition everywhere.
    [proxy() setMarkedText:String(text) selectedRange:NSMakeRange(text.empty() ? 0 : String(text).length, 0)];
    [proxy() unmarkText];
    marked.clear();
  }

  void setComposition(const ProjectKey::Composition &composition) override {
    for (uint32_t i = 0; i < composition.replaceBefore; i++) [proxy() deleteBackward];
    marked = composition.text;
    [proxy() setMarkedText:String(composition.text) selectedRange:NSMakeRange(composition.caret, 0)];
    if (composition.text.empty()) [proxy() unmarkText];
    [owner reloadCandidates];
  }

  void showPanel(const ProjectKey::PanelContent &) override { [owner reloadCandidates]; }
  void hidePanel() override { [owner reloadCandidates]; }
  void showIndicator(const std::string &) override {}
  void beep() override {}
  std::string textBeforeCaret() override { return Std(proxy().documentContextBeforeInput); }
};

ProjectKey::KeyEvent Press(const std::string &characters, Key key = Key::kCharacter, bool shift = false) {
  ProjectKey::KeyEvent event;
  event.key = key;
  event.characters = characters;
  event.shift = shift;
  return event;
}

// One key: what it shows, the Dachen key it sends, and what a long press types.
struct KeySpec {
  NSString *label;
  NSString *keys;
  NSString *hint;
};

const KeySpec kRows[4][10] = {
    {{@"ㄅ", @"1", @"1"}, {@"ㄉ", @"2", @"2"}, {@"ˇ", @"3", @"3"}, {@"ˋ", @"4", @"4"}, {@"ㄓ", @"5", @"5"},
     {@"ˊ", @"6", @"6"}, {@"˙", @"7", @"7"}, {@"ㄚ", @"8", @"8"}, {@"ㄞ", @"9", @"9"}, {@"ㄢ", @"0", @"0"}},
    {{@"ㄆ", @"q", @"q"}, {@"ㄊ", @"w", @"w"}, {@"ㄍ", @"e", @"e"}, {@"ㄐ", @"r", @"r"}, {@"ㄔ", @"t", @"t"},
     {@"ㄗ", @"y", @"y"}, {@"ㄧ", @"u", @"u"}, {@"ㄛ", @"i", @"i"}, {@"ㄟ", @"o", @"o"}, {@"ㄣ", @"p", @"p"}},
    {{@"ㄇ", @"a", @"a"}, {@"ㄋ", @"s", @"s"}, {@"ㄎ", @"d", @"d"}, {@"ㄑ", @"f", @"f"}, {@"ㄕ", @"g", @"g"},
     {@"ㄘ", @"h", @"h"}, {@"ㄨ", @"j", @"j"}, {@"ㄜ", @"k", @"k"}, {@"ㄠ", @"l", @"l"}, {@"ㄤ", @";", @"："}},
    {{@"ㄈ", @"z", @"z"}, {@"ㄌ", @"x", @"x"}, {@"ㄏ", @"c", @"c"}, {@"ㄒ", @"v", @"v"}, {@"ㄖ", @"b", @"b"},
     {@"ㄙ", @"n", @"n"}, {@"ㄩ", @"m", @"m"}, {@"ㄝ", @",", @"…"}, {@"ㄡ", @".", @"！"}, {@"ㄥ", @"/", @"？"}},
};

// The English page, typed as they are; its last row starts with Shift.
NSArray<NSArray<NSString *> *> *EnglishRows() {
  return @[
    @[ @"1", @"2", @"3", @"4", @"5", @"6", @"7", @"8", @"9", @"0" ],
    @[ @"q", @"w", @"e", @"r", @"t", @"y", @"u", @"i", @"o", @"p" ],
    @[ @"a", @"s", @"d", @"f", @"g", @"h", @"j", @"k", @"l", @"'" ],
    @[ @"", @"z", @"x", @"c", @"v", @"b", @"n", @"m", @"?", @"!" ],
  ];
}

// The ?123 page: typed as they are, after what is composed.
NSArray<NSArray<NSString *> *> *SymbolRows() {
  return @[
    @[ @"1", @"2", @"3", @"4", @"5", @"6", @"7", @"8", @"9", @"0" ],
    @[ @"＠", @"＃", @"＄", @"％", @"＆", @"－", @"＋", @"（", @"）", @"／" ],
    @[ @"＊", @"「", @"」", @"：", @"；", @"、", @"～", @"『", @"』", @"＝" ],
    @[ @"，", @"。", @"？", @"！", @"…", @"—", @"《", @"》", @"〈", @"〉" ],
  ];
}

constexpr CGFloat kKeyHeight = 46, kKeyGap = 6, kSideInset = 3, kTopInset = 2, kBottomInset = 2;
// A row gap is this share of a row's pitch, so the rows keep their shape at any height.
constexpr CGFloat kRowGapShare = 0.12;
// The candidate bar takes one row of keys: a key's height and the gap below it, with a candidate's box,
// shorter than a key, at the top. After a switch from Apple's English keyboard, iOS 26 and 27 add a strip
// of about 17pt above the extension (FB21449121), and the bar's rest below the box matches it.
constexpr CGFloat kBarHeight = kKeyHeight / (1 - kRowGapShare), kChipHeight = 34;
// The iOS 18 style (kIOS18StyleKey) leaves the strip be: a shorter bar, the box in its middle.
constexpr CGFloat kShortBarHeight = 40;
constexpr CGFloat kExpandWidth = 44, kGridRowHeight = 44;
using ProjectKey::kGridColumns;  // the engine packs rows to the same width
// Candidate buttons are tagged from here with their index, so viewWithTag: never finds the view itself (tag 0).
constexpr NSInteger kCandidateTag = 1;

// The system keyboard's key colors: character keys light, the others a step darker.
UIColor *KeyColor(BOOL function) {
  return [UIColor colorWithDynamicProvider:^UIColor *(UITraitCollection *traits) {
    if (traits.userInterfaceStyle == UIUserInterfaceStyleDark) {
      return [UIColor colorWithWhite:1 alpha:function ? 0.12 : 0.30];
    }
    return function ? [UIColor colorWithRed:0.67 green:0.70 blue:0.74 alpha:1] : UIColor.whiteColor;
  }];
}

// A character key's color drawn over the keyboard's background, for what floats over other keys.
UIColor *OpaqueKeyColor() {
  return [UIColor colorWithDynamicProvider:^UIColor *(UITraitCollection *traits) {
    return traits.userInterfaceStyle == UIUserInterfaceStyleDark ? [UIColor colorWithWhite:0.35 alpha:1] : UIColor.whiteColor;
  }];
}

UIColor *PressedColor(BOOL function) {
  return KeyColor(!function);
}

}  // namespace

// A key drawn as the system draws one: a rounded face with a hairline shadow below.
@interface PKKeyButton : UIButton
@property(nonatomic) BOOL function;
@property(nonatomic, copy) NSString *keys;
@property(nonatomic, copy) NSString *hint;
@property(nonatomic, strong) UILabel *hintLabel;
@property(nonatomic) BOOL previews;  // shows what it types above the finger, as the system's letter keys do
@property(nonatomic, strong) UILabel *preview;
@property(nonatomic, weak) PKKeyButton *tracked;  // the key under the finger, which may have slid off this one
@end

// The character key a finger is on. As on the system keyboard, it types when the next key goes
// down rather than on release, so two thumbs typing fast keep the order and show one preview.
static __weak PKKeyButton *gHeld;
static BOOL gShowsPreview = YES;  // kCharacterPreviewKey

@implementation PKKeyButton

+ (instancetype)keyWithTitle:(NSString *)title symbol:(NSString *)symbol function:(BOOL)function {
  PKKeyButton *key = [PKKeyButton buttonWithType:UIButtonTypeCustom];
  key.function = function;
  key.layer.cornerRadius = 8.5;
  key.layer.cornerCurve = kCACornerCurveContinuous;
  key.layer.shadowColor = UIColor.blackColor.CGColor;
  key.layer.shadowOffset = CGSizeMake(0, 1);
  key.layer.shadowRadius = 0;
  key.layer.shadowOpacity = 0.3;
  key.backgroundColor = KeyColor(function);
  [key setTitleColor:UIColor.labelColor forState:UIControlStateNormal];
  key.titleLabel.font = [UIFont systemFontOfSize:title.length > 2 ? 16 : 22];
  if (symbol) {
    UIImageSymbolConfiguration *style = [UIImageSymbolConfiguration configurationWithPointSize:19 weight:UIImageSymbolWeightRegular];
    [key setImage:[UIImage systemImageNamed:symbol withConfiguration:style] forState:UIControlStateNormal];
    key.tintColor = UIColor.labelColor;
  } else {
    [key setTitle:title forState:UIControlStateNormal];
  }
  [key addTarget:key action:@selector(pressDown) forControlEvents:UIControlEventTouchDown];
  [key addTarget:key action:@selector(pressUp) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
  return key;
}

- (void)setHint:(NSString *)hint {
  _hint = [hint copy];
  if (!self.hintLabel) {
    self.hintLabel = [[UILabel alloc] init];
    self.hintLabel.font = [UIFont systemFontOfSize:10];
    self.hintLabel.textColor = UIColor.secondaryLabelColor;
    [self addSubview:self.hintLabel];
  }
  self.hintLabel.text = hint;
}

- (void)layoutSubviews {
  [super layoutSubviews];
  [self.hintLabel sizeToFit];
  CGSize size = self.hintLabel.bounds.size;
  self.hintLabel.frame = CGRectMake(CGRectGetWidth(self.bounds) - size.width - 4, 2, size.width, size.height);
  self.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:self.bounds cornerRadius:self.layer.cornerRadius].CGPath;
}

- (void)pressDown {
  self.backgroundColor = PressedColor(self.function);
  if (!self.previews || !gShowsPreview) return;
  if (!self.preview) {
    self.preview = [[UILabel alloc] init];
    self.preview.font = [UIFont systemFontOfSize:32];
    self.preview.textAlignment = NSTextAlignmentCenter;
    self.preview.textColor = UIColor.labelColor;
    self.preview.layer.cornerRadius = 10;
    self.preview.layer.cornerCurve = kCACornerCurveContinuous;
    self.preview.layer.shadowColor = UIColor.blackColor.CGColor;
    self.preview.layer.shadowOffset = CGSizeMake(0, 1);
    self.preview.layer.shadowRadius = 2;
    self.preview.layer.shadowOpacity = 0.3;
    self.preview.userInteractionEnabled = NO;
  }
  self.preview.text = [self titleForState:UIControlStateNormal];
  // On the layer, which the corners round; a label's own background ignores them.
  self.preview.layer.backgroundColor = [OpaqueKeyColor() resolvedColorWithTraitCollection:self.traitCollection].CGColor;
  // Above the key, within the keyboard; the top row's reaches over the candidate bar.
  CGRect key = self.frame;
  CGFloat width = CGRectGetWidth(key) + 16, height = CGRectGetHeight(key) + 4;
  CGFloat x = MIN(MAX(CGRectGetMidX(key) - width / 2, 0), CGRectGetWidth(self.superview.bounds) - width);
  self.preview.frame = CGRectMake(x, MAX(0, CGRectGetMinY(key) - height), width, height);
  [self.superview addSubview:self.preview];
}

- (void)pressUp {
  self.backgroundColor = KeyColor(self.function);
  [self.preview removeFromSuperview];
}

// Character keys track the finger themselves: the press follows it across the
// character keys and the key under it on release is the one typed.
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  [gHeld commit];
  if (!self.previews) return [super touchesBegan:touches withEvent:event];
  gHeld = self;
  self.tracked = self;
  [self sendActionsForControlEvents:UIControlEventTouchDown];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (!self.previews) return [super touchesMoved:touches withEvent:event];
  if (gHeld != self) return;
  CGPoint point = [touches.anyObject locationInView:self.superview];
  for (UIView *view in self.superview.subviews) {
    if (![view isKindOfClass:PKKeyButton.class] || view.hidden || view.alpha < 0.01) continue;
    PKKeyButton *key = (PKKeyButton *)view;
    if (!key.previews || key == self.tracked || !CGRectContainsPoint(key.frame, point)) continue;
    [self.tracked pressUp];
    self.tracked = key;
    [key pressDown];
    break;
  }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (!self.previews) return [super touchesEnded:touches withEvent:event];
  if (gHeld == self) [self commit];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (!self.previews) return [super touchesCancelled:touches withEvent:event];
  if (gHeld != self) return;
  gHeld = nil;
  [self.tracked pressUp];
}

- (void)commit {
  gHeld = nil;
  for (UIGestureRecognizer *press in self.gestureRecognizers) {  // a finger left down must not long-press too
    press.enabled = NO;
    press.enabled = YES;
  }
  // The target registered for touch-up also takes the press off.
  [self.tracked sendActionsForControlEvents:UIControlEventTouchUpInside];
}

@end

// The keys, over the whole keyboard and above the candidate bar so the top row's previews
// show over it; a touch on no key passes to the bar.
@interface PKKeysView : UIView
@end

@implementation PKKeysView
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
  UIView *hit = [super hitTest:point withEvent:event];
  return hit == self ? nil : hit;
}
@end

// The system keyboard's background, and its click sound.
@interface PKKeyboardView : UIInputView <UIInputViewAudioFeedback>
@property(nonatomic, weak) UIView *keys;
@end

@implementation PKKeyboardView
- (BOOL)enableInputClicksWhenVisible {
  return YES;
}

// A touch in the gap between keys goes to the nearest key, as on the system keyboard, rather than nowhere.
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
  UIView *hit = [super hitTest:point withEvent:event];
  if (hit != self || self.keys.hidden || self.keys.alpha < 0.01) return hit;
  point = [self convertPoint:point toView:self.keys];
  constexpr CGFloat kReach = 8;  // a gap is 6pt wide; farther than this is no key's
  PKKeyButton *nearest = nil;
  CGFloat best = kReach * kReach;
  for (UIView *view in self.keys.subviews) {
    if (![view isKindOfClass:PKKeyButton.class] || view.hidden || view.alpha < 0.01) continue;
    CGRect frame = view.frame;
    CGFloat dx = MAX(0, MAX(CGRectGetMinX(frame) - point.x, point.x - CGRectGetMaxX(frame)));
    CGFloat dy = MAX(0, MAX(CGRectGetMinY(frame) - point.y, point.y - CGRectGetMaxY(frame)));
    if (dx * dx + dy * dy <= best) {
      best = dx * dx + dy * dy;
      nearest = (PKKeyButton *)view;
    }
  }
  return nearest ?: hit;
}
@end

// One item of the emoji panel: an emoji large, a longer item (顏文字, a phrase) at text size.
@interface PKSymbolCell : UICollectionViewCell
@property(nonatomic, readonly) UILabel *label;
@end

static UIFont *SymbolFont(NSString *symbol) {
  BOOL single = [symbol rangeOfComposedCharacterSequenceAtIndex:0].length == symbol.length;
  return [UIFont systemFontOfSize:single ? 30 : 17];
}

@implementation PKSymbolCell
- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _label = [[UILabel alloc] init];
    _label.textAlignment = NSTextAlignmentCenter;
    [self.contentView addSubview:_label];
    UIView *pressed = [[UIView alloc] init];
    pressed.backgroundColor = UIColor.tertiarySystemFillColor;
    pressed.layer.cornerRadius = 8;
    pressed.layer.cornerCurve = kCACornerCurveContinuous;
    self.selectedBackgroundView = pressed;
  }
  return self;
}

- (void)layoutSubviews {
  [super layoutSubviews];
  _label.frame = self.contentView.bounds;
}
@end

// Rows start at the left, as on the system's emoji keyboard, rather than spread to both edges.
@interface PKLeftAlignedLayout : UICollectionViewFlowLayout
@end

@implementation PKLeftAlignedLayout
- (NSArray<UICollectionViewLayoutAttributes *> *)layoutAttributesForElementsInRect:(CGRect)rect {
  NSArray<UICollectionViewLayoutAttributes *> *attributes =
      [[NSArray alloc] initWithArray:[super layoutAttributesForElementsInRect:rect] copyItems:YES];
  CGFloat x = 0, row = -1;
  for (UICollectionViewLayoutAttributes *item in attributes) {
    if (item.representedElementCategory != UICollectionElementCategoryCell) continue;
    CGRect frame = item.frame;
    if (fabs(CGRectGetMinY(frame) - row) > 1) {
      row = CGRectGetMinY(frame);
      x = self.sectionInset.left;
    }
    frame.origin.x = x;
    item.frame = frame;
    x += CGRectGetWidth(frame) + self.minimumInteritemSpacing;
  }
  return attributes;
}
@end

// A section's title in the emoji and 顏文字 pages.
@interface PKSectionHeader : UICollectionReusableView
@property(nonatomic, readonly) UILabel *label;
@end

@implementation PKSectionHeader
- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _label = [[UILabel alloc] init];
    _label.font = [UIFont systemFontOfSize:12];
    _label.textColor = UIColor.secondaryLabelColor;
    [self addSubview:_label];
  }
  return self;
}

- (void)layoutSubviews {
  [super layoutSubviews];
  _label.frame = CGRectInset(self.bounds, 12, 0);
}
@end

@implementation PKKeyboardViewController {
  std::unique_ptr<ClientAdapter> _client;
  std::unique_ptr<ProjectKey::Controller> _controller;
  UIScrollView *_bar, *_grid;
  UIView *_keys;  // masked while the grid unfolds over it
  UIButton *_expandKey;
  BOOL _expanded;  // the candidates spread over the keys
  BOOL _gridShown;  // what the views last showed of _expanded
  BOOL _sliding;  // the grid opening or folding
  CGFloat _barHeight, _chipTop;  // by the style, kIOS18StyleKey
  NSLayoutConstraint *_height;
  // How far the bar and the grid are filled: a lone symbol has over a thousand candidates,
  // too many to make at once, so they are made as the view scrolls toward them.
  size_t _barCount, _gridCount;
  CGFloat _barX, _gridX;
  int _gridRow, _gridColumn;
  NSArray<NSArray<PKKeyButton *> *> *_zhuyinRows, *_englishRows, *_symbolRows;
  PKKeyButton *_pageKey, *_modeKey, *_commaKey, *_globeKey, *_spaceKey, *_erKey, *_deleteKey, *_returnKey, *_shiftKey;
  BOOL _english;  // the letters page in place of Zhuyin's
  BOOL _shift;    // the next letter is a capital
  BOOL _symbols;
  NSTimer *_deleteRepeat;
  UIImpactFeedbackGenerator *_haptics;  // nil when off (kHapticsKey)
  NSArray<NSString *> *_customSymbols;  // kCustomSymbolsKey
  UIButton *_hideKey;  // in ⌄'s place when nothing is typed
  // The panel over the whole keyboard, showing _page. The emoji and 顏文字 pages scroll
  // through their sections, which the icons above the bottom row jump to.
  PanelPage _page;
  UIView *_panel;
  UICollectionView *_symbolGrid;
  NSArray<NSString *> *_sectionTitles;
  NSArray<NSArray<NSString *> *> *_sections;
  UIStackView *_categories, *_pageSwitch;
  PKGIFPanel *_gifPanel;
  UIView *_settingsPanel;
  UILabel *_settingsWarning;
  UISegmentedControl *_hapticsControl;
  NSArray<UISwitch *> *_settingSwitches;  // SettingSwitches()' order
  PKKeyButton *_panelBackKey, *_panelDeleteKey;
}

- (void)loadView {
  self.inputView = [[PKKeyboardView alloc] initWithFrame:CGRectZero inputViewStyle:UIInputViewStyleKeyboard];
  // The system passes touches on fully transparent pixels to the app behind, so a
  // swipe that starts between candidates never arrived.
  self.inputView.backgroundColor = [UIColor colorWithWhite:0 alpha:0.001];
  self.inputView.clipsToBounds = YES;  // the scrolled grid's rows would show in the strip iOS adds above
  self.view = self.inputView;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  _client = std::make_unique<ClientAdapter>();
  _client->owner = self;
  _controller = std::make_unique<ProjectKey::Controller>(SharedEnvironment(), *_client);

  _bar = [[UIScrollView alloc] init];
  _grid = [[UIScrollView alloc] init];
  for (UIScrollView *scroll in @[ _bar, _grid ]) {
    scroll.showsHorizontalScrollIndicator = NO;
    scroll.showsVerticalScrollIndicator = NO;
    scroll.delegate = self;
    if (@available(iOS 26.0, *)) {
      // The default edge blur would cover the candidates near the edges.
      scroll.topEdgeEffect.hidden = YES;
      scroll.bottomEdgeEffect.hidden = YES;
      scroll.leftEdgeEffect.hidden = YES;
      scroll.rightEdgeEffect.hidden = YES;
    }
  }
  [self.view addSubview:_bar];
  _keys = [[PKKeysView alloc] init];
  ((PKKeyboardView *)self.view).keys = _keys;
  [self.view addSubview:_keys];

  NSMutableArray *zhuyin = [NSMutableArray array];
  for (const auto &row : kRows) {
    NSMutableArray *keys = [NSMutableArray array];
    for (const KeySpec &spec : row) {
      PKKeyButton *key = [PKKeyButton keyWithTitle:spec.label symbol:nil function:NO];
      key.keys = spec.keys;
      key.hint = spec.hint;
      key.previews = YES;
      [key addTarget:self action:@selector(typeZhuyin:) forControlEvents:UIControlEventTouchUpInside];
      [self addLongPress:key];
      [keys addObject:key];
      [_keys addSubview:key];
    }
    [zhuyin addObject:keys];
  }
  _zhuyinRows = zhuyin;

  NSMutableArray *symbols = [NSMutableArray array];
  for (NSArray<NSString *> *row in SymbolRows()) {
    NSMutableArray *keys = [NSMutableArray array];
    for (NSString *text in row) {
      PKKeyButton *key = [PKKeyButton keyWithTitle:text symbol:nil function:NO];
      key.keys = text;
      key.previews = YES;
      [key addTarget:self action:@selector(typeSymbol:) forControlEvents:UIControlEventTouchUpInside];
      key.hidden = YES;
      [keys addObject:key];
      [_keys addSubview:key];
    }
    [symbols addObject:keys];
  }
  _symbolRows = symbols;

  NSMutableArray *english = [NSMutableArray array];
  for (NSArray<NSString *> *row in EnglishRows()) {
    NSMutableArray *keys = [NSMutableArray array];
    for (NSString *text in row) {
      PKKeyButton *key;
      if (text.length) {
        key = [PKKeyButton keyWithTitle:text symbol:nil function:NO];
        key.keys = text;
        key.previews = YES;
        [key addTarget:self action:@selector(typeLetter:) forControlEvents:UIControlEventTouchUpInside];
      } else {
        key = _shiftKey = [PKKeyButton keyWithTitle:@"" symbol:@"shift" function:YES];
        [key addTarget:self action:@selector(toggleShift) forControlEvents:UIControlEventTouchUpInside];
      }
      key.hidden = YES;
      [keys addObject:key];
      [_keys addSubview:key];
    }
    [english addObject:keys];
  }
  _englishRows = english;

  _pageKey = [PKKeyButton keyWithTitle:@"?123" symbol:nil function:YES];
  [_pageKey addTarget:self action:@selector(switchPage) forControlEvents:UIControlEventTouchUpInside];
  _modeKey = [PKKeyButton keyWithTitle:@"ABC" symbol:nil function:YES];
  [_modeKey addTarget:self action:@selector(switchMode) forControlEvents:UIControlEventTouchUpInside];
  for (PKKeyButton *key in @[ _pageKey, _modeKey ]) {
    key.titleLabel.adjustsFontSizeToFitWidth = YES;  // ㄅㄆㄇ is wider than ?123
    key.titleLabel.minimumScaleFactor = 0.7;
  }
  _commaKey = [PKKeyButton keyWithTitle:@"，" symbol:nil function:YES];
  _commaKey.keys = @"<";  // Shift+, is the full-width comma
  _commaKey.hint = @"。";
  [_commaKey addTarget:self action:@selector(typeComma) forControlEvents:UIControlEventTouchUpInside];
  [self addLongPress:_commaKey];
  _globeKey = [PKKeyButton keyWithTitle:@"" symbol:@"globe" function:YES];
  [_globeKey addTarget:self action:@selector(handleInputModeListFromView:withEvent:) forControlEvents:UIControlEventAllTouchEvents];
  _spaceKey = [PKKeyButton keyWithTitle:@"注音" symbol:nil function:NO];
  _spaceKey.titleLabel.font = [UIFont systemFontOfSize:16];
  [_spaceKey addTarget:self action:@selector(typeSpace) forControlEvents:UIControlEventTouchUpInside];
  _erKey = [PKKeyButton keyWithTitle:@"ㄦ" symbol:nil function:NO];
  _erKey.keys = @"-";
  _erKey.previews = YES;
  [_erKey addTarget:self action:@selector(typeEr) forControlEvents:UIControlEventTouchUpInside];
  _deleteKey = [PKKeyButton keyWithTitle:@"" symbol:@"delete.left" function:YES];
  [_deleteKey addTarget:self action:@selector(startDelete) forControlEvents:UIControlEventTouchDown];
  [_deleteKey addTarget:self action:@selector(stopDelete) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
  _returnKey = [PKKeyButton keyWithTitle:@"" symbol:@"return.left" function:YES];
  [_returnKey addTarget:self action:@selector(typeReturn) forControlEvents:UIControlEventTouchUpInside];
  for (PKKeyButton *key in @[ _pageKey, _modeKey, _commaKey, _globeKey, _spaceKey, _erKey, _deleteKey, _returnKey ]) {
    [key addTarget:self action:@selector(click) forControlEvents:UIControlEventTouchDown];
    [_keys addSubview:key];
  }
  for (NSArray<PKKeyButton *> *row in [[_zhuyinRows arrayByAddingObjectsFromArray:_englishRows] arrayByAddingObjectsFromArray:_symbolRows]) {
    for (PKKeyButton *key in row) [key addTarget:self action:@selector(click) forControlEvents:UIControlEventTouchDown];
  }

  [self.view addSubview:_grid];
  _expandKey = [UIButton buttonWithType:UIButtonTypeSystem];
  _expandKey.tintColor = UIColor.labelColor;
  UIView *divider = [[UIView alloc] initWithFrame:CGRectMake(0, 6, 0.5, kChipHeight - 12)];  // between the row and ⌄
  divider.backgroundColor = UIColor.separatorColor;
  [_expandKey addSubview:divider];
  [_expandKey addTarget:self action:@selector(toggleGrid) forControlEvents:UIControlEventTouchUpInside];
  [self.view addSubview:_expandKey];

  _hideKey = [UIButton buttonWithType:UIButtonTypeSystem];
  [_hideKey setImage:[UIImage systemImageNamed:@"keyboard.chevron.compact.down"] forState:UIControlStateNormal];
  _hideKey.tintColor = UIColor.secondaryLabelColor;
  _hideKey.accessibilityLabel = @"隱藏鍵盤";
  [_hideKey addTarget:self action:@selector(hideKeyboard) forControlEvents:UIControlEventTouchUpInside];
  [self.view addSubview:_hideKey];

  // The panel: its page above, and the bottom row where the keys' is: ㄅㄆㄇ, the pages, delete.
  _panel = [[UIView alloc] init];
  _panel.hidden = YES;
  UICollectionViewFlowLayout *layout = [[PKLeftAlignedLayout alloc] init];
  layout.minimumInteritemSpacing = layout.minimumLineSpacing = 0;
  layout.sectionInset = UIEdgeInsetsMake(0, kSideInset, 6, kSideInset);
  _symbolGrid = [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:layout];
  _symbolGrid.backgroundColor = UIColor.clearColor;
  _symbolGrid.showsVerticalScrollIndicator = NO;
  _symbolGrid.dataSource = self;
  _symbolGrid.delegate = self;
  [_symbolGrid registerClass:PKSymbolCell.class forCellWithReuseIdentifier:@"symbol"];
  [_symbolGrid registerClass:PKSectionHeader.class forSupplementaryViewOfKind:UICollectionElementKindSectionHeader withReuseIdentifier:@"header"];
  if (@available(iOS 26.0, *)) {
    _symbolGrid.topEdgeEffect.hidden = _symbolGrid.bottomEdgeEffect.hidden = YES;
  }
  _categories = [[UIStackView alloc] init];
  _categories.distribution = UIStackViewDistributionFillEqually;

  _gifPanel = [[PKGIFPanel alloc] init];
  __weak PKKeyboardViewController *weakSelf = self;
  _gifPanel.onTap = ^{ [weakSelf click]; };

  _settingsPanel = [self makeSettingsPanel];

  _panelBackKey = [PKKeyButton keyWithTitle:@"ㄅㄆㄇ" symbol:nil function:YES];
  _panelBackKey.titleLabel.adjustsFontSizeToFitWidth = YES;  // as on ?123 and ABC
  _panelBackKey.titleLabel.minimumScaleFactor = 0.7;
  [_panelBackKey addTarget:self action:@selector(closePanel) forControlEvents:UIControlEventTouchUpInside];
  _pageSwitch = [[UIStackView alloc] init];
  _pageSwitch.spacing = 4;
  _pageSwitch.distribution = UIStackViewDistributionFillEqually;
  for (UIButton *page in @[ [self tool:@"face.smiling" title:nil label:@"表情符號" page:PanelPage::kEmoji],
                            [self tool:nil title:@"GIF" label:@"GIF" page:PanelPage::kGIF],
                            [self tool:nil title:@":-)" label:@"顏文字" page:PanelPage::kKaomoji] ]) {
    page.layer.cornerRadius = 15;
    page.layer.cornerCurve = kCACornerCurveContinuous;
    [_pageSwitch addArrangedSubview:page];
  }
  _panelDeleteKey = [PKKeyButton keyWithTitle:@"" symbol:@"delete.left" function:YES];
  [_panelDeleteKey addTarget:self action:@selector(click) forControlEvents:UIControlEventTouchDown];
  [_panelDeleteKey addTarget:self action:@selector(startDelete) forControlEvents:UIControlEventTouchDown];
  [_panelDeleteKey addTarget:self action:@selector(stopDelete) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
  for (UIView *view in @[ _symbolGrid, _categories, _gifPanel, _settingsPanel, _panelBackKey, _pageSwitch, _panelDeleteKey ]) {
    [_panel addSubview:view];
  }
  [self.view addSubview:_panel];

  // The system takes the keyboard's height from what its content needs, not from a
  // constraint on the view itself, so an invisible column asks for it (applyStyle).
  UIView *strut = [[UIView alloc] init];
  strut.translatesAutoresizingMaskIntoConstraints = NO;
  strut.userInteractionEnabled = NO;
  [self.view insertSubview:strut atIndex:0];
  _height = [strut.heightAnchor constraintEqualToConstant:0];
  _height.priority = UILayoutPriorityRequired - 1;  // a height the system insists on still wins
  [self applyStyle];
  [NSLayoutConstraint activateConstraints:@[
    _height, [strut.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [strut.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    [strut.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor], [strut.widthAnchor constraintEqualToConstant:0]
  ]];

  [self reloadCandidates];
  [self showGridAnimated:NO];  // folded
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  NSInteger request = [SharedDefaults() integerForKey:kClearChoicesRequestKey];
  if (request != [NSUserDefaults.standardUserDefaults integerForKey:kClearedChoicesKey]) {
    ProjectKey::ClearChoices(SharedEnvironment());
    [NSUserDefaults.standardUserDefaults setInteger:request forKey:kClearedChoicesKey];
  }
  NSUserDefaults *defaults = SharedDefaults();
  gShowsPreview = [defaults objectForKey:kCharacterPreviewKey] ? [defaults boolForKey:kCharacterPreviewKey] : YES;
  [self applyHaptics:[defaults objectForKey:kHapticsKey] ? [defaults integerForKey:kHapticsKey] : 1];
  _customSymbols = Lines(kCustomSymbolsKey, kDefaultCustomSymbols);
  [self applyStyle];
  [self.view setNeedsLayout];
  [self reloadCandidates];
}

- (void)applyStyle {
  BOOL ios18 = [SharedDefaults() boolForKey:kIOS18StyleKey];
  _barHeight = ios18 ? kShortBarHeight : kBarHeight;
  _chipTop = ios18 ? (kShortBarHeight + kTopInset - kChipHeight) / 2 : 0;
  _height.constant = _barHeight + kTopInset + (5 - kRowGapShare) * kKeyHeight / (1 - kRowGapShare) + kBottomInset;
}

- (void)viewWillDisappear:(BOOL)animated {
  [super viewWillDisappear:animated];
  _controller->deactivate();
  [self showPanel:PanelPage::kNone];
}

- (void)textDidChange:(id<UITextInput>)textInput {
  [super textDidChange:textInput];
  [self updateReturnKey];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  // Moving the grid asks for layout, which would set the grid's frame back mid-slide;
  // the slide lays out again when it ends.
  if (_sliding) return;
  CGFloat width = CGRectGetWidth(self.view.bounds);
  CGFloat unit = (width - 2 * kSideInset - 9 * kKeyGap) / 10;
  // Rows share the height the system gave, which can differ from the one asked for.
  CGFloat pitch = (CGRectGetHeight(self.view.bounds) - _barHeight - kTopInset - kBottomInset) / (5 - kRowGapShare);
  CGFloat rowGap = round(pitch * kRowGapShare), keyHeight = pitch - rowGap;
  _bar.frame = CGRectMake(0, 0, width - kExpandWidth, _barHeight);
  _expandKey.frame = _hideKey.frame = CGRectMake(width - kExpandWidth, _chipTop, kExpandWidth, kChipHeight);
  _grid.frame = _keys.frame = self.view.bounds;
  if (_expanded) [self reloadCandidates];
  CGFloat y = _barHeight + kTopInset;
  for (NSUInteger r = 0; r < 4; r++) {
    for (NSUInteger c = 0; c < 10; c++) {
      CGRect frame = CGRectMake(kSideInset + c * (unit + kKeyGap), y, unit, keyHeight);
      _zhuyinRows[r][c].frame = frame;
      _englishRows[r][c].frame = frame;
      _symbolRows[r][c].frame = frame;
    }
    y += pitch;
  }
  // Bottom row in key widths; the space bar takes what is left.
  BOOL globe = self.needsInputModeSwitchKey;
  _globeKey.hidden = !globe;
  NSArray<PKKeyButton *> *bottom = globe ? @[ _pageKey, _modeKey, _commaKey, _globeKey, _spaceKey, _erKey, _deleteKey, _returnKey ]
                                         : @[ _pageKey, _modeKey, _commaKey, _spaceKey, _erKey, _deleteKey, _returnKey ];
  NSDictionary<NSValue *, NSNumber *> *wide = @{[NSValue valueWithNonretainedObject:_pageKey] : @1.4, [NSValue valueWithNonretainedObject:_returnKey] : @1.4};
  CGFloat fixed = 0;
  for (PKKeyButton *key in bottom) {
    if (key != _spaceKey) fixed += unit * ([wide[[NSValue valueWithNonretainedObject:key]] doubleValue] ?: 1);
  }
  CGFloat space = width - 2 * kSideInset - fixed - (bottom.count - 1) * kKeyGap;
  CGFloat x = kSideInset;
  for (PKKeyButton *key in bottom) {
    CGFloat w = key == _spaceKey ? space : unit * ([wide[[NSValue valueWithNonretainedObject:key]] doubleValue] ?: 1);
    key.frame = CGRectMake(x, y, w, keyHeight);
    x += w + kKeyGap;
  }
  // The panel's bottom row stands where the keys' does: ㄅㄆㄇ, the tabs, delete.
  _panel.frame = self.view.bounds;
  CGFloat side = unit * 1.4, pageTop = y - rowGap;  // the pages end above the bottom row
  _panelBackKey.frame = CGRectMake(kSideInset, y, side, keyHeight);
  _panelDeleteKey.frame = CGRectMake(width - kSideInset - side, y, side, keyHeight);
  CGFloat switchWidth = 3 * 56 + 2 * 4, switchHeight = MIN(keyHeight, 30);
  _pageSwitch.frame = CGRectMake((width - switchWidth) / 2, y + (keyHeight - switchHeight) / 2, switchWidth, switchHeight);
  CGFloat categories = MIN(width - 2 * kSideInset, _categories.arrangedSubviews.count * 48.0);  // 顏文字 has two
  _categories.frame = CGRectMake(kSideInset, pageTop - 32, categories, 32);
  _symbolGrid.frame = CGRectMake(0, kTopInset, width, CGRectGetMinY(_categories.frame) - kTopInset);
  [_symbolGrid.collectionViewLayout invalidateLayout];
  _gifPanel.frame = CGRectMake(0, kTopInset, width, pageTop - kTopInset);
  _settingsPanel.frame = CGRectMake(0, kTopInset, width, pageTop - kTopInset);
  [self updateReturnKey];
}

#pragma mark - Keys

- (void)click {
  [UIDevice.currentDevice playInputClick];
  [_haptics impactOccurred];
  [_haptics prepare];  // keeps the next one prompt
}

- (BOOL)choosing {
  const auto &panel = _controller->panel();
  return panel && !panel->rows.empty() && panel->selectedRow >= 0;  // the row shown while typing is no choice
}

// Like Gboard, the bar lists candidates as soon as a syllable is complete.
// Down opens them, and does nothing while a syllable is unfinished.
- (void)showCandidates {
  if (_controller->isComposing() && ![self choosing]) _controller->handleKey(Press("", Key::kDown));
}

- (void)send:(const ProjectKey::KeyEvent &)event {
  // The candidates are always open, so the keys act as when composing; Space alone pages them,
  // unless a syllable is still being typed, which Space gives tone 1.
  if (event.key != Key::kSpace || _controller->isTypingSyllable()) {
    for (int i = 0; i < 2 && [self choosing]; i++) _controller->handleKey(Press("", Key::kEscape));
  }
  if (!_controller->handleKey(event)) {
    switch (event.key) {
      case Key::kSpace: [self.textDocumentProxy insertText:@" "]; break;
      case Key::kReturn: [self.textDocumentProxy insertText:@"\n"]; break;
      case Key::kBackspace: [self.textDocumentProxy deleteBackward]; break;
      default: [self.textDocumentProxy insertText:String(event.characters)]; break;
    }
  }
  [self showCandidates];
  [self reloadCandidates];
}

- (void)typeZhuyin:(PKKeyButton *)key {
  [self send:Press(Std(key.keys))];
}

- (void)typeComma {
  if (_english) {
    [self typeText:@","];
  } else {
    [self send:Press("<", Key::kCharacter, true)];
  }
}

- (void)typeEr {
  if (_english) {
    [self typeText:@"."];
  } else {
    [self typeZhuyin:_erKey];
  }
}

- (void)typeLetter:(PKKeyButton *)key {
  [self typeText:_shift ? key.keys.uppercaseString : key.keys];
  if (_shift) [self toggleShift];
}

- (void)toggleShift {
  _shift = !_shift;
  [self updatePage];
}

// Space takes the candidate in the gray box, as a tap on it does; it gives tone 1 while a syllable is typed.
- (void)typeSpace {
  if ([self choosing] && !_controller->isTypingSyllable()) {
    _expanded = NO;
    _controller->chooseAndCommit(static_cast<int>([self selectedCandidate]));
    [self reloadCandidates];
    return;
  }
  [self send:Press(" ", Key::kSpace)];
}

- (void)typeReturn {
  [self send:Press("", Key::kReturn)];
}

- (void)typeSymbol:(PKKeyButton *)key {
  [self typeText:key.keys];
}

// Text as it is, after what is composed.
- (void)typeText:(NSString *)text {
  _controller->deactivate();
  [self.textDocumentProxy insertText:text];
  [self reloadCandidates];
}

- (void)addLongPress:(PKKeyButton *)key {
  UILongPressGestureRecognizer *press = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(longPress:)];
  press.minimumPressDuration = 0.35;
  press.cancelsTouchesInView = YES;
  [key addGestureRecognizer:press];
}

// A long press types the key's small label as it is, after what is composed.
- (void)longPress:(UILongPressGestureRecognizer *)press {
  if (press.state != UIGestureRecognizerStateBegan) return;
  PKKeyButton *key = (PKKeyButton *)press.view;
  [key pressUp];
  if (key == _commaKey && !_english) {
    [self send:Press(">", Key::kCharacter, true)];  // Shift+. is 。
    return;
  }
  [self typeText:key.hint];
}

- (void)startDelete {
  [self deleteOnce];
  _deleteRepeat = [NSTimer scheduledTimerWithTimeInterval:0.45 repeats:NO block:^(NSTimer *) {
    self->_deleteRepeat = [NSTimer scheduledTimerWithTimeInterval:0.08 target:self selector:@selector(deleteOnce) userInfo:nil repeats:YES];
  }];
}

- (void)deleteOnce {
  [self send:Press("", Key::kBackspace)];
}

- (void)stopDelete {
  [_deleteRepeat invalidate];
  _deleteRepeat = nil;
}

- (void)switchPage {
  _controller->deactivate();
  _symbols = !_symbols;
  [self updatePage];
  [self reloadCandidates];
}

// ABC and ㄅㄆㄇ switch between the letters and Zhuyin, from the symbols too.
- (void)switchMode {
  _controller->deactivate();
  _english = !_english;
  _symbols = _shift = NO;
  [self updatePage];
  [self reloadCandidates];
}

- (void)updatePage {
  for (NSArray<PKKeyButton *> *row in _zhuyinRows) for (PKKeyButton *key in row) key.hidden = _symbols || _english;
  for (NSArray<PKKeyButton *> *row in _englishRows) {
    for (PKKeyButton *key in row) {
      key.hidden = _symbols || !_english;
      if (key.keys) [key setTitle:_shift ? key.keys.uppercaseString : key.keys forState:UIControlStateNormal];
    }
  }
  for (NSArray<PKKeyButton *> *row in _symbolRows) for (PKKeyButton *key in row) key.hidden = !_symbols;
  UIImageSymbolConfiguration *style = [_shiftKey imageForState:UIControlStateNormal].symbolConfiguration;
  [_shiftKey setImage:[UIImage systemImageNamed:_shift ? @"shift.fill" : @"shift" withConfiguration:style] forState:UIControlStateNormal];
  NSString *letters = _english ? @"ABC" : @"ㄅㄆㄇ";
  [_pageKey setTitle:_symbols ? letters : @"?123" forState:UIControlStateNormal];
  [_modeKey setTitle:_english ? @"ㄅㄆㄇ" : @"ABC" forState:UIControlStateNormal];
  [_commaKey setTitle:_english ? @"," : @"，" forState:UIControlStateNormal];
  _commaKey.hint = _english ? @";" : @"。";
  [_erKey setTitle:_english ? @"." : @"ㄦ" forState:UIControlStateNormal];
  [_spaceKey setTitle:_english ? @"English" : @"注音" forState:UIControlStateNormal];
}

- (void)updateReturnKey {
  // Like the system keyboard: the app's own return label, blue when it is an action.
  UIReturnKeyType type = self.textDocumentProxy.returnKeyType;
  BOOL action = type != UIReturnKeyDefault && !_controller->isComposing();
  _returnKey.backgroundColor = action ? UIColor.systemBlueColor : KeyColor(YES);
  _returnKey.tintColor = action ? UIColor.whiteColor : UIColor.labelColor;
}

#pragma mark - Candidates

// Where the candidate the Controller selects is among all of them; they are distinct, so it is found by its text.
- (size_t)selectedCandidate {
  const auto &panel = _controller->panel();
  const auto &candidates = _controller->candidates();
  return std::find(candidates.begin(), candidates.end(), panel->rows[panel->selectedRow][panel->selection]) - candidates.begin();
}

// While choosing: every candidate in one row that scrolls sideways, tapped
// to pick, and ⌄ to spread them over the keys. While composing: the
// sentence, tapped to commit.
- (void)reloadCandidates {
  if (!_bar) return;
  for (UIView *view in _bar.subviews) [view removeFromSuperview];
  _barCount = 0;
  _barX = 10;  // inset so the gray box stays inside the keyboard's rounded corner
  _bar.contentSize = CGSizeZero;
  _bar.contentOffset = CGPointZero;
  const auto &panel = _controller->panel();
  BOOL choosing = [self choosing], idle = NO;
  if (choosing) {
    size_t selected = [self selectedCandidate];
    [self extendBarTo:2 * CGRectGetWidth(_bar.bounds) count:selected + 1];
    if (selected < _barCount) {  // the word as typed can be past the edge
      [_bar scrollRectToVisible:[_bar viewWithTag:kCandidateTag + static_cast<NSInteger>(selected)].frame animated:NO];
    }
  } else if (!_client->marked.empty()) {
    UIButton *button = [self candidate:String(_client->marked) selected:NO];
    [button addTarget:self action:@selector(typeReturn) forControlEvents:UIControlEventTouchUpInside];
    [self addToBar:button];
  } else if (panel && !panel->rows.empty()) {  // 聯想詞, after a commit
    for (size_t i = 0; i < panel->rows[0].size(); i++) {
      UIButton *button = [self candidate:String(panel->rows[0][i]) selected:NO];
      button.tag = kCandidateTag + static_cast<NSInteger>(i);
      [button addTarget:self action:@selector(pickAssociated:) forControlEvents:UIControlEventTouchUpInside];
      [self addToBar:button];
    }
  } else {
    [self fillIdleBar];
    idle = YES;
  }

  BOOL more = choosing && !panel->pageLabel.empty();
  if (!more) _expanded = NO;
  _expandKey.hidden = !more;
  _hideKey.hidden = !idle || _page != PanelPage::kNone;
  [self fillGrid];
  if (_expanded != _gridShown) [self showGridAnimated:NO];
  [self updateReturnKey];
}

// With nothing typed, on every page: the panel's tools, a step quieter than candidates, and
// hiding the keyboard in ⌄'s place.
- (void)fillIdleBar {
  for (UIButton *tool in @[ [self tool:@"face.smiling" title:nil label:@"表情符號" page:PanelPage::kEmoji],
                            [self tool:nil title:@"GIF" label:@"GIF" page:PanelPage::kGIF],
                            [self tool:nil title:@":-)" label:@"顏文字" page:PanelPage::kKaomoji],
                            [self tool:@"gearshape" title:nil label:@"設定" page:PanelPage::kSettings] ]) {
    [self addToBar:tool];
  }
}

- (UIButton *)tool:(NSString *)symbol title:(NSString *)title label:(NSString *)label page:(PanelPage)page {
  UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
  if (symbol) {
    UIImageSymbolConfiguration *size = [UIImageSymbolConfiguration configurationWithPointSize:19];
    [button setImage:[UIImage systemImageNamed:symbol withConfiguration:size] forState:UIControlStateNormal];
  } else {
    [button setTitle:title forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightBold];
  }
  button.accessibilityLabel = label;
  button.tintColor = UIColor.secondaryLabelColor;
  button.tag = static_cast<NSInteger>(page);
  [button addTarget:self action:@selector(openPanel:) forControlEvents:UIControlEventTouchUpInside];
  return button;
}

- (void)hideKeyboard {
  [self click];
  [self dismissKeyboard];
}

#pragma mark - Panel

- (void)openPanel:(UIButton *)tool {
  [self click];
  [self showPanel:static_cast<PanelPage>(tool.tag)];
}

- (void)closePanel {
  [self click];
  [self showPanel:PanelPage::kNone];
}

// Shown at once over the bar and the keys, which come back as they were when it closes.
- (void)showPanel:(PanelPage)page {
  if (page == _page) return;
  _page = page;
  BOOL open = page != PanelPage::kNone, symbols = page == PanelPage::kEmoji || page == PanelPage::kKaomoji;
  _panel.hidden = !open;
  _keys.hidden = _bar.hidden = open;
  _symbolGrid.hidden = _categories.hidden = !symbols;
  _gifPanel.hidden = page != PanelPage::kGIF;
  _settingsPanel.hidden = page != PanelPage::kSettings;
  _pageSwitch.hidden = _panelDeleteKey.hidden = page == PanelPage::kSettings;
  for (UIButton *button in _pageSwitch.arrangedSubviews) {
    BOOL selected = button.tag == static_cast<NSInteger>(page);
    button.backgroundColor = selected ? UIColor.tertiarySystemFillColor : nil;
    button.tintColor = selected ? UIColor.labelColor : UIColor.secondaryLabelColor;
  }
  if (symbols) [self loadSections];
  if (page == PanelPage::kGIF) {
    _gifPanel.hasFullAccess = self.hasFullAccess;
    [_gifPanel show];
  }
  if (page == PanelPage::kSettings) [self updateSettings];
  [self reloadCandidates];
}

- (NSString *)recentKey {
  return _page == PanelPage::kEmoji ? kRecentSymbolsKey : kRecentKaomojiKey;
}

// 最近, then the emoji table's groups, or the user's 自訂 on the 顏文字 page.
- (void)loadSections {
  NSArray<NSString *> *recent = [NSUserDefaults.standardUserDefaults stringArrayForKey:[self recentKey]] ?: @[];
  NSMutableArray<NSString *> *titles = [NSMutableArray arrayWithObject:@"最近"], *icons = [NSMutableArray arrayWithObject:@"clock"];
  NSMutableArray<NSArray<NSString *> *> *sections = [NSMutableArray arrayWithObject:recent];
  if (_page == PanelPage::kEmoji) {
    NSArray<NSString *> *groupIcons = @[ @"face.smiling", @"person", @"leaf", @"fork.knife", @"car", @"soccerball", @"lightbulb", @"heart", @"flag" ];
    auto &lm = *SharedEnvironment().lm;
    auto groups = lm.getUnigrams("_emoji_groups");
    for (size_t i = 0; i < groups.size(); i++) {
      [titles addObject:String(groups[i].value())];
      [icons addObject:i < groupIcons.count ? groupIcons[i] : @"circle"];
      NSMutableArray<NSString *> *emoji = [NSMutableArray array];
      for (const auto &unigram : lm.getUnigrams("_emoji_list_" + std::to_string(i))) [emoji addObject:String(unigram.value())];
      [sections addObject:emoji];
    }
  } else {
    [titles addObject:@"自訂"];
    [icons addObject:@"star"];
    [sections addObject:_customSymbols];
  }
  _sectionTitles = titles;
  _sections = sections;
  for (UIView *view in _categories.arrangedSubviews) [view removeFromSuperview];
  for (NSUInteger i = 0; i < icons.count; i++) {
    UIButton *category = [UIButton buttonWithType:UIButtonTypeSystem];
    UIImageSymbolConfiguration *size = [UIImageSymbolConfiguration configurationWithPointSize:16];
    [category setImage:[UIImage systemImageNamed:icons[i] withConfiguration:size] forState:UIControlStateNormal];
    category.accessibilityLabel = titles[i];
    category.tag = static_cast<NSInteger>(i);
    [category addTarget:self action:@selector(jumpToSection:) forControlEvents:UIControlEventTouchUpInside];
    [_categories addArrangedSubview:category];
  }
  [self.view setNeedsLayout];
  [_symbolGrid reloadData];
  _symbolGrid.contentOffset = CGPointZero;
  [self highlightCategory:recent.count ? 0 : 1];
}

- (void)highlightCategory:(NSInteger)section {
  for (UIButton *category in _categories.arrangedSubviews) {
    category.tintColor = category.tag == section ? UIColor.labelColor : UIColor.tertiaryLabelColor;
  }
}

- (void)jumpToSection:(UIButton *)category {
  [self click];
  [_symbolGrid layoutIfNeeded];
  NSIndexPath *first = [NSIndexPath indexPathForItem:0 inSection:category.tag];
  UICollectionViewLayoutAttributes *header =
      [_symbolGrid layoutAttributesForSupplementaryElementOfKind:UICollectionElementKindSectionHeader atIndexPath:first];
  CGFloat end = MAX(0, _symbolGrid.contentSize.height - CGRectGetHeight(_symbolGrid.bounds));
  [_symbolGrid setContentOffset:CGPointMake(0, MIN(end, CGRectGetMinY(header.frame))) animated:NO];
  [self highlightCategory:category.tag];
}

// While the finger scrolls, the icon of the section at the top lights.
- (void)followCategory {
  CGFloat top = _symbolGrid.contentOffset.y + 30;
  NSInteger section = -1;
  for (NSIndexPath *path in _symbolGrid.indexPathsForVisibleItems) {
    UICollectionViewLayoutAttributes *item = [_symbolGrid layoutAttributesForItemAtIndexPath:path];
    if (CGRectGetMaxY(item.frame) > top && (section < 0 || path.section < section)) section = path.section;
  }
  if (section >= 0) [self highlightCategory:section];
}

- (NSInteger)numberOfSectionsInCollectionView:(UICollectionView *)collectionView {
  return static_cast<NSInteger>(_sections.count);
}

- (NSInteger)collectionView:(UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section {
  return static_cast<NSInteger>(_sections[static_cast<NSUInteger>(section)].count);
}

- (NSString *)symbolAt:(NSIndexPath *)indexPath {
  return _sections[static_cast<NSUInteger>(indexPath.section)][static_cast<NSUInteger>(indexPath.item)];
}

- (UICollectionViewCell *)collectionView:(UICollectionView *)collectionView cellForItemAtIndexPath:(NSIndexPath *)indexPath {
  PKSymbolCell *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"symbol" forIndexPath:indexPath];
  NSString *symbol = [self symbolAt:indexPath];
  cell.label.text = symbol;
  cell.label.font = SymbolFont(symbol);
  return cell;
}

- (UICollectionReusableView *)collectionView:(UICollectionView *)collectionView viewForSupplementaryElementOfKind:(NSString *)kind atIndexPath:(NSIndexPath *)indexPath {
  PKSectionHeader *header = [collectionView dequeueReusableSupplementaryViewOfKind:kind withReuseIdentifier:@"header" forIndexPath:indexPath];
  header.label.text = _sectionTitles[static_cast<NSUInteger>(indexPath.section)];
  return header;
}

// An empty section, 最近 before anything is typed from the page, has no title either.
- (CGSize)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout *)layout referenceSizeForHeaderInSection:(NSInteger)section {
  return _sections[static_cast<NSUInteger>(section)].count ? CGSizeMake(CGRectGetWidth(collectionView.bounds), 26) : CGSizeZero;
}

// Eight emoji a row; a longer item takes the width it needs.
- (CGSize)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout *)layout sizeForItemAtIndexPath:(NSIndexPath *)indexPath {
  CGFloat width = CGRectGetWidth(collectionView.bounds) - 2 * kSideInset, cell = floor(width / 8);
  NSString *symbol = [self symbolAt:indexPath];
  if ([symbol rangeOfComposedCharacterSequenceAtIndex:0].length == symbol.length) return CGSizeMake(cell, 44);
  CGFloat text = ceil([symbol sizeWithAttributes:@{NSFontAttributeName : SymbolFont(symbol)}].width) + 20;
  return CGSizeMake(MIN(width, MAX(cell, text)), 44);
}

// Typed, and kept first among 最近, which shows it when the page next opens: the page
// stays as it is under the finger.
- (void)collectionView:(UICollectionView *)collectionView didSelectItemAtIndexPath:(NSIndexPath *)indexPath {
  [collectionView deselectItemAtIndexPath:indexPath animated:NO];
  [self click];
  NSString *symbol = [self symbolAt:indexPath];
  [self typeText:symbol];
  NSMutableArray<NSString *> *recent = [([NSUserDefaults.standardUserDefaults stringArrayForKey:[self recentKey]] ?: @[]) mutableCopy];
  [recent removeObject:symbol];
  [recent insertObject:symbol atIndex:0];
  if (recent.count > kRecentCount) [recent removeObjectsInRange:NSMakeRange(kRecentCount, recent.count - kRecentCount)];
  [NSUserDefaults.standardUserDefaults setObject:recent forKey:[self recentKey]];
}

#pragma mark - Settings

// The app's settings that matter while typing, set from the keyboard. Writing them needs full
// access, as reading them does.
- (UIView *)makeSettingsPanel {
  UIStackView *rows = [[UIStackView alloc] init];
  rows.axis = UILayoutConstraintAxisVertical;
  rows.layoutMargins = UIEdgeInsetsMake(0, 16, 0, 16);
  rows.layoutMarginsRelativeArrangement = YES;
  _settingsWarning = [[UILabel alloc] init];
  _settingsWarning.text = @"未開啟完整取用，設定無法儲存";
  _settingsWarning.font = [UIFont systemFontOfSize:13];
  _settingsWarning.textColor = UIColor.secondaryLabelColor;
  [_settingsWarning.heightAnchor constraintEqualToConstant:28].active = YES;
  [rows addArrangedSubview:_settingsWarning];
  _hapticsControl = [[UISegmentedControl alloc] initWithItems:@[ @"關閉", @"輕", @"中", @"重" ]];
  [_hapticsControl.widthAnchor constraintEqualToConstant:200].active = YES;
  [_hapticsControl addTarget:self action:@selector(changeHaptics) forControlEvents:UIControlEventValueChanged];
  [rows addArrangedSubview:[self settingRow:@"觸覺回饋" control:_hapticsControl]];
  NSMutableArray<UISwitch *> *switches = [NSMutableArray array];
  for (NSString *title in @[ @"字元預覽", @"聯想詞", @"簡拼", @"近似音" ]) {
    UISwitch *control = [[UISwitch alloc] init];
    control.tag = static_cast<NSInteger>(switches.count);
    [control addTarget:self action:@selector(changeSwitch:) forControlEvents:UIControlEventValueChanged];
    [switches addObject:control];
    [rows addArrangedSubview:[self settingRow:title control:control]];
  }
  _settingSwitches = switches;
  return rows;
}

- (UIView *)settingRow:(NSString *)title control:(UIControl *)control {
  UILabel *label = [[UILabel alloc] init];
  label.text = title;
  label.font = [UIFont systemFontOfSize:16];
  UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[ label, control ]];
  row.alignment = UIStackViewAlignmentCenter;
  [label setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
  [row.heightAnchor constraintEqualToConstant:44].active = YES;
  return row;
}

// The engine's settings behind 聯想詞, 簡拼 and 近似音, the switches after 字元預覽.
static const ProjectKey::Setting kSwitchSettings[] = {ProjectKey::Setting::kAssociatedPhrases, ProjectKey::Setting::kAbbreviated,
                                                      ProjectKey::Setting::kFuzzySounds};

- (void)updateSettings {
  BOOL access = self.hasFullAccess;
  _settingsWarning.hidden = access;
  NSUserDefaults *defaults = SharedDefaults();
  _hapticsControl.selectedSegmentIndex = [defaults objectForKey:kHapticsKey] ? [defaults integerForKey:kHapticsKey] : 1;
  _hapticsControl.enabled = access;
  _settingSwitches[0].on = gShowsPreview;
  for (NSUInteger i = 1; i < _settingSwitches.count; i++) {
    _settingSwitches[i].on = SharedEnvironment().settings->flag(kSwitchSettings[i - 1]);
  }
  for (UISwitch *control in _settingSwitches) control.enabled = access;
}

- (void)changeHaptics {
  NSInteger haptics = _hapticsControl.selectedSegmentIndex;
  [SharedDefaults() setInteger:haptics forKey:kHapticsKey];
  [self applyHaptics:haptics];
  [self click];  // how it now feels
}

- (void)changeSwitch:(UISwitch *)control {
  if (control.tag == 0) {
    gShowsPreview = control.on;
    [SharedDefaults() setBool:control.on forKey:kCharacterPreviewKey];
  } else {
    SharedEnvironment().settings->set(kSwitchSettings[control.tag - 1], control.on ? 1 : 0);
  }
}

- (void)applyHaptics:(NSInteger)haptics {
  const UIImpactFeedbackStyle styles[] = {UIImpactFeedbackStyleLight, UIImpactFeedbackStyleMedium, UIImpactFeedbackStyleHeavy};
  _haptics = haptics >= 1 && haptics <= 3 ? [[UIImpactFeedbackGenerator alloc] initWithStyle:styles[haptics - 1]] : nil;
  [_haptics prepare];
}

// Opens or folds the grid as _expanded says. Open, it covers the keys; it unfolds down from
// the bar, its first row, over keys that stay where they are, hidden above its edge, and
// folds back up the same way.
- (void)showGridAnimated:(BOOL)animated {
  BOOL open = _expanded;
  _gridShown = open;
  [_expandKey setImage:[UIImage systemImageNamed:open ? @"chevron.up" : @"chevron.down"] forState:UIControlStateNormal];
  CGRect full = self.view.bounds, folded = CGRectMake(0, 0, CGRectGetWidth(full), _barHeight);
  // What of the keys shows: below the grid's edge.
  CGRect keysFolded = CGRectMake(0, _barHeight, CGRectGetWidth(full), CGRectGetHeight(full) - _barHeight);
  CGRect keysCovered = CGRectMake(0, CGRectGetHeight(full), CGRectGetWidth(full), 0);
  void (^settle)(void) = ^{
    self->_grid.frame = full;
    self->_grid.hidden = !open;
    self->_bar.alpha = open ? 0 : 1;
    self->_keys.maskView = nil;
    self->_keys.alpha = open ? 0 : 1;
  };
  // A slide under way stops; its completion then reports unfinished.
  [_grid.layer removeAllAnimations];
  [_keys.maskView.layer removeAllAnimations];
  if (!animated) {
    settle();
    _sliding = NO;
    [self.view setNeedsLayout];
    return;
  }
  _sliding = YES;
  _grid.hidden = NO;
  _grid.frame = open ? folded : full;
  _bar.alpha = 0;  // the grid's first row stands in for it
  UIView *mask = [[UIView alloc] initWithFrame:open ? keysFolded : keysCovered];
  mask.backgroundColor = UIColor.blackColor;
  _keys.maskView = mask;
  _keys.alpha = 1;
  [UIView animateWithDuration:0.25 delay:0 options:UIViewAnimationOptionCurveEaseOut
    animations:^{
      self->_grid.frame = open ? full : folded;
      mask.frame = open ? keysCovered : keysFolded;
    }
    completion:^(BOOL finished) {
      if (!finished) return;  // stopped by the next opening or folding, which sets the views
      settle();
      self->_sliding = NO;
      [self.view setNeedsLayout];
    }];
}

// Adds candidates to the bar until it reaches `right` and holds the first `count`.
- (void)extendBarTo:(CGFloat)right count:(size_t)count {
  const auto &panel = _controller->panel();
  const std::string &current = panel->rows[panel->selectedRow][panel->selection];
  const auto &candidates = _controller->candidates();
  for (; _barCount < candidates.size() && (_barX < right || _barCount < count); _barCount++) {
    UIButton *button = [self candidate:String(candidates[_barCount]) selected:candidates[_barCount] == current];
    button.tag = kCandidateTag + static_cast<NSInteger>(_barCount);
    [button addTarget:self action:@selector(pickCandidate:) forControlEvents:UIControlEventTouchUpInside];
    [self addToBar:button];
  }
}

static CGFloat ChipWidth(UIButton *button) {
  [button sizeToFit];
  return MAX(40, CGRectGetWidth(button.bounds) + 20);
}

- (void)addToBar:(UIButton *)button {
  CGFloat w = ChipWidth(button);
  button.frame = CGRectMake(_barX, _chipTop, w, kChipHeight);
  [_bar addSubview:button];
  _barX += w + 4;
  _bar.contentSize = CGSizeMake(_barX + 6, _barHeight);
}

- (void)fillGrid {
  for (UIView *view in _grid.subviews) [view removeFromSuperview];
  _gridCount = 0;
  _gridRow = _gridColumn = 0;
  _gridX = 10;
  if (_expanded) [self extendGridTo:2 * CGRectGetHeight(self.view.bounds)];  // the grid itself may be mid-slide
}

// The first row is the bar as it was, so expanding and folding leave those
// candidates where they are; below it, like the system keyboard, six columns
// with lines between the rows, a long word taking more than one column, all left
// of the ⌃ column. Adds candidates until the grid's rows reach `bottom`.
- (void)extendGridTo:(CGFloat)bottom {
  CGFloat width = CGRectGetWidth(self.view.bounds), cell = (width - kExpandWidth) / kGridColumns;
  CGFloat line = 1 / self.traitCollection.displayScale;
  const auto &panel = _controller->panel();
  const std::string &current = panel->rows[panel->selectedRow][panel->selection];
  const auto &candidates = _controller->candidates();
  for (; _gridCount < candidates.size() && _barHeight + (_gridRow - 1) * kGridRowHeight < bottom; _gridCount++) {
    UIButton *button = [self candidate:String(candidates[_gridCount]) selected:candidates[_gridCount] == current];
    button.tag = kCandidateTag + static_cast<NSInteger>(_gridCount);
    [button addTarget:self action:@selector(pickCandidate:) forControlEvents:UIControlEventTouchUpInside];
    [_grid addSubview:button];
    CGFloat w = ChipWidth(button);
    if (_gridRow == 0 && _gridX + w <= width - kExpandWidth) {
      button.frame = CGRectMake(_gridX, _chipTop, w, kChipHeight);
      _gridX += w + 4;
      continue;
    }
    int span = MIN(kGridColumns, static_cast<int>(ceil((CGRectGetWidth(button.bounds) + 16) / cell)));
    if (_gridRow == 0 || _gridColumn + span > kGridColumns) {
      _gridRow++;
      _gridColumn = 0;
      CGFloat top = _barHeight + (_gridRow - 1) * kGridRowHeight;
      UIView *separator = [[UIView alloc] initWithFrame:CGRectMake(0, top - line, width - kExpandWidth, line)];
      separator.backgroundColor = UIColor.separatorColor;
      [_grid addSubview:separator];
    }
    button.frame = CGRectMake(_gridColumn * cell, _barHeight + (_gridRow - 1) * kGridRowHeight, span * cell, kGridRowHeight);
    _gridColumn += span;
  }
  _grid.contentSize = CGSizeMake(width, _barHeight + _gridRow * kGridRowHeight);
}

- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
  if (scrollView == _symbolGrid) {
    if (scrollView.isDragging || scrollView.isDecelerating) [self followCategory];  // a jump lights its own
    return;
  }
  if (![self choosing]) return;
  if (scrollView == _bar) {
    [self extendBarTo:_bar.contentOffset.x + 2 * CGRectGetWidth(_bar.bounds) count:0];
  } else if (_expanded) {
    [self extendGridTo:_grid.contentOffset.y + 2 * CGRectGetHeight(_grid.bounds)];
  }
}

// The selected candidate sits in a gray box, as the system keyboard marks the one Space takes.
- (UIButton *)candidate:(NSString *)title selected:(BOOL)selected {
  UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
  [button setTitle:title forState:UIControlStateNormal];
  button.titleLabel.font = [UIFont systemFontOfSize:20];
  button.tintColor = UIColor.labelColor;
  if (selected) {
    button.backgroundColor = UIColor.tertiarySystemFillColor;
    button.layer.cornerRadius = 8;
    button.layer.cornerCurve = kCACornerCurveContinuous;
  }
  return button;
}

- (void)pickCandidate:(UIButton *)button {
  [self click];
  _expanded = NO;
  _controller->chooseAndCommit(static_cast<int>(button.tag - kCandidateTag));
  [self reloadCandidates];
}

- (void)pickAssociated:(UIButton *)button {
  [self click];
  _controller->clickCandidate(0, static_cast<int>(button.tag - kCandidateTag));
  [self reloadCandidates];
}

- (void)toggleGrid {
  [self click];
  _expanded = !_expanded;
  if (_expanded) {
    _grid.contentOffset = CGPointZero;
    [self fillGrid];
  }  // folding keeps the grid's candidates to slide away
  [self showGridAnimated:YES];
}

@end
