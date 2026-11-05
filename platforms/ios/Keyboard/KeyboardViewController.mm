// The iOS keyboard: Gboard's Zhuyin layout (Dachen keys in rows of ten, with
// the tone keys on the number row) drawn in the system keyboard's colors over
// its own background, driving the shared Controller. A key sends the Dachen
// key it stands on, as a hardware keyboard would.
#import <UIKit/UIKit.h>

#include <memory>
#include <optional>

#include "Controller.h"

using ProjectKey::Key;

@interface PKKeyboardViewController : UIInputViewController
- (void)reloadCandidates;
@end

namespace {

NSString *String(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}

std::string Std(NSString *s) {
  return s.UTF8String ?: "";
}

class DefaultsSettings : public ProjectKey::Settings {
 public:
  std::optional<int> stored(ProjectKey::Setting setting) const override {
    // Candidates are for the word just typed, as on the system keyboard.
    if (setting == ProjectKey::Setting::kCandidatePosition) return 1;
    NSString *name = @(Name(setting));
    if (![NSUserDefaults.standardUserDefaults objectForKey:name]) return std::nullopt;
    return static_cast<int>([NSUserDefaults.standardUserDefaults integerForKey:name]);
  }
  void set(ProjectKey::Setting setting, int value) override {
    [NSUserDefaults.standardUserDefaults setInteger:value forKey:@(Name(setting))];
  }
};

// Loaded once per extension process; the system relaunches the process often.
ProjectKey::Environment &SharedEnvironment() {
  static ProjectKey::Environment *environment = [] {
    auto *env = new ProjectKey::Environment();
    env->lm = std::make_shared<McBopomofo::McBopomofoLM>();
    NSString *data = [[NSBundle bundleForClass:PKKeyboardViewController.class] pathForResource:@"data" ofType:@"txt"];
    env->lm->loadLanguageModel(data.fileSystemRepresentation);
    if (!env->lm->isDataModelLoaded()) NSLog(@"Project Key: cannot load %@", data);
    env->userModel = std::make_shared<McBopomofo::UserOverrideModel>(500, 5400.0);
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

// The ?123 page: typed as they are, after what is composed.
NSArray<NSArray<NSString *> *> *SymbolRows() {
  return @[
    @[ @"1", @"2", @"3", @"4", @"5", @"6", @"7", @"8", @"9", @"0" ],
    @[ @"＠", @"＃", @"＄", @"％", @"＆", @"－", @"＋", @"（", @"）", @"／" ],
    @[ @"＊", @"「", @"」", @"：", @"；", @"、", @"～", @"『", @"』", @"＝" ],
    @[ @"，", @"。", @"？", @"！", @"…", @"—", @"《", @"》", @"〈", @"〉" ],
  ];
}

constexpr CGFloat kBarHeight = 44, kKeyHeight = 46, kKeyGap = 6, kSideInset = 3, kTopInset = 2, kBottomInset = 2;
// A row gap is this share of a row's pitch, so the rows keep their shape at any height.
constexpr CGFloat kRowGapShare = 0.12;
constexpr CGFloat kExpandWidth = 44, kGridRowHeight = 44;
constexpr int kGridColumns = 6;

// The system keyboard's key colors: character keys light, the others a step darker.
UIColor *KeyColor(BOOL function) {
  return [UIColor colorWithDynamicProvider:^UIColor *(UITraitCollection *traits) {
    if (traits.userInterfaceStyle == UIUserInterfaceStyleDark) {
      return [UIColor colorWithWhite:1 alpha:function ? 0.12 : 0.30];
    }
    return function ? [UIColor colorWithRed:0.67 green:0.70 blue:0.74 alpha:1] : UIColor.whiteColor;
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
@end

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
}

- (void)pressUp {
  self.backgroundColor = KeyColor(self.function);
}

@end

// The system keyboard's background, and its click sound.
@interface PKKeyboardView : UIInputView <UIInputViewAudioFeedback>
@end

@implementation PKKeyboardView
- (BOOL)enableInputClicksWhenVisible {
  return YES;
}
@end

@implementation PKKeyboardViewController {
  std::unique_ptr<ClientAdapter> _client;
  std::unique_ptr<ProjectKey::Controller> _controller;
  UIScrollView *_bar, *_grid;
  UIButton *_expandKey;
  BOOL _expanded;  // the candidates spread over the keys
  NSArray<NSArray<PKKeyButton *> *> *_zhuyinRows, *_symbolRows;
  PKKeyButton *_pageKey, *_commaKey, *_globeKey, *_spaceKey, *_erKey, *_deleteKey, *_returnKey;
  BOOL _symbols;
  NSTimer *_deleteRepeat;
}

- (void)loadView {
  self.inputView = [[PKKeyboardView alloc] initWithFrame:CGRectZero inputViewStyle:UIInputViewStyleKeyboard];
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
    if (@available(iOS 26.0, *)) {
      // The default edge blur would cover the candidates near the edges.
      scroll.topEdgeEffect.hidden = YES;
      scroll.bottomEdgeEffect.hidden = YES;
      scroll.leftEdgeEffect.hidden = YES;
      scroll.rightEdgeEffect.hidden = YES;
    }
  }
  [self.view addSubview:_bar];

  NSMutableArray *zhuyin = [NSMutableArray array];
  for (const auto &row : kRows) {
    NSMutableArray *keys = [NSMutableArray array];
    for (const KeySpec &spec : row) {
      PKKeyButton *key = [PKKeyButton keyWithTitle:spec.label symbol:nil function:NO];
      key.keys = spec.keys;
      key.hint = spec.hint;
      [key addTarget:self action:@selector(typeZhuyin:) forControlEvents:UIControlEventTouchUpInside];
      [self addLongPress:key];
      [keys addObject:key];
      [self.view addSubview:key];
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
      [key addTarget:self action:@selector(typeSymbol:) forControlEvents:UIControlEventTouchUpInside];
      key.hidden = YES;
      [keys addObject:key];
      [self.view addSubview:key];
    }
    [symbols addObject:keys];
  }
  _symbolRows = symbols;

  _pageKey = [PKKeyButton keyWithTitle:@"?123" symbol:nil function:YES];
  [_pageKey addTarget:self action:@selector(switchPage) forControlEvents:UIControlEventTouchUpInside];
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
  [_erKey addTarget:self action:@selector(typeZhuyin:) forControlEvents:UIControlEventTouchUpInside];
  _deleteKey = [PKKeyButton keyWithTitle:@"" symbol:@"delete.left" function:YES];
  [_deleteKey addTarget:self action:@selector(startDelete) forControlEvents:UIControlEventTouchDown];
  [_deleteKey addTarget:self action:@selector(stopDelete) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
  _returnKey = [PKKeyButton keyWithTitle:@"" symbol:@"return.left" function:YES];
  [_returnKey addTarget:self action:@selector(typeReturn) forControlEvents:UIControlEventTouchUpInside];
  for (PKKeyButton *key in @[ _pageKey, _commaKey, _globeKey, _spaceKey, _erKey, _deleteKey, _returnKey ]) {
    [key addTarget:self action:@selector(click) forControlEvents:UIControlEventTouchDown];
    [self.view addSubview:key];
  }
  for (NSArray<PKKeyButton *> *row in [_zhuyinRows arrayByAddingObjectsFromArray:_symbolRows]) {
    for (PKKeyButton *key in row) [key addTarget:self action:@selector(click) forControlEvents:UIControlEventTouchDown];
  }

  [self.view addSubview:_grid];
  _expandKey = [UIButton buttonWithType:UIButtonTypeSystem];
  _expandKey.tintColor = UIColor.labelColor;
  [_expandKey addTarget:self action:@selector(toggleGrid) forControlEvents:UIControlEventTouchUpInside];
  [self.view addSubview:_expandKey];

  // The system takes the keyboard's height from what its content needs, not from a
  // constraint on the view itself, so an invisible column asks for it.
  CGFloat pitch = kKeyHeight / (1 - kRowGapShare);
  UIView *strut = [[UIView alloc] init];
  strut.translatesAutoresizingMaskIntoConstraints = NO;
  strut.userInteractionEnabled = NO;
  [self.view insertSubview:strut atIndex:0];
  NSLayoutConstraint *height = [strut.heightAnchor constraintEqualToConstant:kBarHeight + kTopInset + (5 - kRowGapShare) * pitch + kBottomInset];
  height.priority = UILayoutPriorityRequired - 1;  // a height the system insists on still wins
  [NSLayoutConstraint activateConstraints:@[
    height, [strut.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [strut.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    [strut.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor], [strut.widthAnchor constraintEqualToConstant:0]
  ]];

  [self reloadCandidates];
}

- (void)viewWillDisappear:(BOOL)animated {
  [super viewWillDisappear:animated];
  _controller->deactivate();
}

- (void)textDidChange:(id<UITextInput>)textInput {
  [super textDidChange:textInput];
  [self updateReturnKey];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  CGFloat width = CGRectGetWidth(self.view.bounds);
  CGFloat unit = (width - 2 * kSideInset - 9 * kKeyGap) / 10;
  // Rows share the height the system gave, which can differ from the one asked for.
  CGFloat pitch = (CGRectGetHeight(self.view.bounds) - kBarHeight - kTopInset - kBottomInset) / (5 - kRowGapShare);
  CGFloat rowGap = round(pitch * kRowGapShare), keyHeight = pitch - rowGap;
  _bar.frame = CGRectMake(0, 0, width - kExpandWidth, kBarHeight);
  _expandKey.frame = CGRectMake(width - kExpandWidth, 0, kExpandWidth, kBarHeight);
  _grid.frame = self.view.bounds;
  if (_expanded) [self reloadCandidates];
  CGFloat y = kBarHeight + kTopInset;
  for (NSUInteger r = 0; r < 4; r++) {
    for (NSUInteger c = 0; c < 10; c++) {
      CGRect frame = CGRectMake(kSideInset + c * (unit + kKeyGap), y, unit, keyHeight);
      _zhuyinRows[r][c].frame = frame;
      _symbolRows[r][c].frame = frame;
    }
    y += pitch;
  }
  // Bottom row in key widths; the space bar takes what is left.
  BOOL globe = self.needsInputModeSwitchKey;
  _globeKey.hidden = !globe;
  NSArray<PKKeyButton *> *bottom = globe ? @[ _pageKey, _commaKey, _globeKey, _spaceKey, _erKey, _deleteKey, _returnKey ]
                                         : @[ _pageKey, _commaKey, _spaceKey, _erKey, _deleteKey, _returnKey ];
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
  [self updateReturnKey];
}

#pragma mark - Keys

- (void)click {
  [UIDevice.currentDevice playInputClick];
}

- (BOOL)choosing {
  const auto &panel = _controller->panel();
  return panel && !panel->rows.empty();
}

// Like Gboard, the bar lists candidates as soon as a syllable is complete.
// Down opens them, and does nothing while a syllable is unfinished.
- (void)showCandidates {
  if (_controller->isComposing() && ![self choosing]) _controller->handleKey(Press("", Key::kDown));
}

- (void)send:(const ProjectKey::KeyEvent &)event {
  // The candidates are always open, so the keys act as when composing; Space alone pages them.
  if (event.key != Key::kSpace) {
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
  [self send:Press("<", Key::kCharacter, true)];
}

- (void)typeSpace {
  [self send:Press(" ", Key::kSpace)];
}

- (void)typeReturn {
  [self send:Press("", Key::kReturn)];
}

- (void)typeSymbol:(PKKeyButton *)key {
  _controller->deactivate();
  [self.textDocumentProxy insertText:key.keys];
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
  if (key == _commaKey) {
    [self send:Press(">", Key::kCharacter, true)];  // Shift+. is 。
    return;
  }
  _controller->deactivate();
  [self.textDocumentProxy insertText:key.hint];
  [self reloadCandidates];
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
  for (NSArray<PKKeyButton *> *row in _zhuyinRows) for (PKKeyButton *key in row) key.hidden = _symbols;
  for (NSArray<PKKeyButton *> *row in _symbolRows) for (PKKeyButton *key in row) key.hidden = !_symbols;
  [_pageKey setTitle:_symbols ? @"ㄅㄆㄇ" : @"?123" forState:UIControlStateNormal];
  [self reloadCandidates];
}

- (void)updateReturnKey {
  // Like the system keyboard: the app's own return label, blue when it is an action.
  UIReturnKeyType type = self.textDocumentProxy.returnKeyType;
  BOOL action = type != UIReturnKeyDefault && !_controller->isComposing();
  _returnKey.backgroundColor = action ? UIColor.systemBlueColor : KeyColor(YES);
  _returnKey.tintColor = action ? UIColor.whiteColor : UIColor.labelColor;
}

#pragma mark - Candidates

// While choosing: the candidate row the Controller shows, tapped to pick, and
// ⌄ to spread all the candidates over the keys. While composing: the
// sentence, tapped to commit.
- (void)reloadCandidates {
  if (!_bar) return;
  for (UIView *view in _bar.subviews) [view removeFromSuperview];
  NSMutableArray<UIButton *> *buttons = [NSMutableArray array];
  const auto &panel = _controller->panel();
  BOOL choosing = [self choosing];
  if (choosing) {
    const auto &row = panel->rows[panel->selectedRow];
    for (size_t i = 0; i < row.size(); i++) {
      UIButton *button = [self candidate:String(row[i]) selected:static_cast<int>(i) == panel->selection];
      button.tag = static_cast<NSInteger>(i);
      [button addTarget:self action:@selector(pickCandidate:) forControlEvents:UIControlEventTouchUpInside];
      [buttons addObject:button];
    }
  } else if (!_client->marked.empty()) {
    UIButton *button = [self candidate:String(_client->marked) selected:NO];
    [button addTarget:self action:@selector(typeReturn) forControlEvents:UIControlEventTouchUpInside];
    [buttons addObject:button];
  }
  // Inset so the gray box stays inside the keyboard's rounded corner.
  CGFloat x = 10;
  for (UIButton *button in buttons) {
    [button sizeToFit];
    CGFloat w = MAX(40, CGRectGetWidth(button.bounds) + 20);
    button.frame = CGRectMake(x, 6, w, kBarHeight - 12);
    [_bar addSubview:button];
    x += w + 4;
  }
  _bar.contentSize = CGSizeMake(x, kBarHeight);

  BOOL more = choosing && !panel->pageLabel.empty();
  if (!more) _expanded = NO;
  _expandKey.hidden = !more;
  [_expandKey setImage:[UIImage systemImageNamed:_expanded ? @"chevron.up" : @"chevron.down"] forState:UIControlStateNormal];
  [self fillGrid];
  _grid.hidden = !_expanded;
  for (UIView *view in self.view.subviews) {
    if (view != _grid && view != _expandKey) view.alpha = _expanded ? 0 : 1;
  }
  [self updateReturnKey];
}

// Like the system keyboard: six columns with lines between the rows, the
// first row leaving room for ⌃, a long word taking more than one column.
- (void)fillGrid {
  for (UIView *view in _grid.subviews) [view removeFromSuperview];
  if (!_expanded) return;
  CGFloat width = CGRectGetWidth(self.view.bounds), cell = width / kGridColumns;
  const auto &candidates = _controller->candidates();
  int row = 0, column = 0;
  for (size_t i = 0; i < candidates.size(); i++) {
    UIButton *button = [self candidate:String(candidates[i]) selected:NO];
    [button sizeToFit];
    int span = MIN(kGridColumns, static_cast<int>(ceil((CGRectGetWidth(button.bounds) + 16) / cell)));
    if (column + span > (row == 0 ? kGridColumns - 1 : kGridColumns)) {
      row++;
      column = 0;
    }
    button.frame = CGRectMake(column * cell, row * kGridRowHeight, span * cell, kGridRowHeight);
    button.tag = static_cast<NSInteger>(i);
    [button addTarget:self action:@selector(pickFromGrid:) forControlEvents:UIControlEventTouchUpInside];
    [_grid addSubview:button];
    column += span;
  }
  CGFloat line = 1 / self.traitCollection.displayScale;
  for (int r = 1; r <= row; r++) {
    UIView *separator = [[UIView alloc] initWithFrame:CGRectMake(0, r * kGridRowHeight - line, width, line)];
    separator.backgroundColor = UIColor.separatorColor;
    [_grid addSubview:separator];
  }
  _grid.contentSize = CGSizeMake(width, (row + 1) * kGridRowHeight);
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
  char digit = static_cast<char>('1' + button.tag);
  _controller->handleKey(Press(std::string(1, digit)));
  [self showCandidates];
  [self reloadCandidates];
}

- (void)pickFromGrid:(UIButton *)button {
  [self click];
  _expanded = NO;
  _controller->choose(static_cast<int>(button.tag));
  [self showCandidates];
  [self reloadCandidates];
}

- (void)toggleGrid {
  [self click];
  _expanded = !_expanded;
  _grid.contentOffset = CGPointZero;
  [self reloadCandidates];
}

@end
