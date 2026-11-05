#import <Carbon/Carbon.h>
#import <InputMethodKit/InputMethodKit.h>

#include <memory>

#import "CandidatePanel.h"
#import "SettingsWindow.h"
#include "Controller.h"

using ProjectKey::Key;

@interface PKInputController : IMKInputController
@end

namespace {

NSString *String(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}

NSString *OptionalString(const std::string &s) {
  return s.empty() ? nil : String(s);
}

// Choices the user made, shared by every input session. Kept in memory only,
// with McBopomofo's capacity and 1.5-hour half-life.
McBopomofo::UserOverrideModel NewUserModel() {
  return McBopomofo::UserOverrideModel(500, 5400.0);
}

// Settings in NSUserDefaults, under the names Settings::Name gives.
class DefaultsSettings : public ProjectKey::Settings {
 public:
  std::optional<int> stored(ProjectKey::Setting setting) const override {
    NSString *name = @(Name(setting));
    if (![NSUserDefaults.standardUserDefaults objectForKey:name]) return std::nullopt;
    return static_cast<int>([NSUserDefaults.standardUserDefaults integerForKey:name]);
  }
  void set(ProjectKey::Setting setting, int value) override {
    [NSUserDefaults.standardUserDefaults setInteger:value forKey:@(Name(setting))];
  }
};

// The models, settings and 中/英 switch every input session shares.
ProjectKey::Environment &SharedEnvironment() {
  static ProjectKey::Environment *environment = [] {
    [PKSettingsWindow registerDefaults];
    auto *env = new ProjectKey::Environment();
    env->lm = std::make_shared<McBopomofo::McBopomofoLM>();
    NSString *data = [NSBundle.mainBundle pathForResource:@"data" ofType:@"txt"];
    env->lm->loadLanguageModel(data.fileSystemRepresentation);
    if (!env->lm->isDataModelLoaded()) NSLog(@"Project Key: cannot load %@", data);
    if (NSString *path = [NSBundle.mainBundle pathForResource:@"bigram" ofType:@"bin"]) {
      auto bigram = std::make_shared<ProjectKey::Bigram>();
      if (bigram->load(path.fileSystemRepresentation)) {
        env->bigram = bigram;
      } else {
        NSLog(@"Project Key: cannot load %@", path);
      }
    }
    env->userModel = std::make_shared<McBopomofo::UserOverrideModel>(NewUserModel());
    static DefaultsSettings settings;
    env->settings = &settings;
    env->isEnglish = [](const std::string &text) {
      NSRange misspelled = [NSSpellChecker.sharedSpellChecker checkSpellingOfString:String(text)
                                                                         startingAt:0
                                                                           language:@"en"
                                                                               wrap:NO
                                                             inSpellDocumentWithTag:0
                                                                          wordCount:NULL];
      return misspelled.location == NSNotFound;
    };
    env->columnsSpannedBy = [](const std::string &candidate) {
      return static_cast<int>([PKCandidatePanel columnsSpannedBy:String(candidate)]);
    };
    env->shiftKeycap = "⇧";
    env->appTypesUnhandledKeys = true;
    return env;
  }();
  return *environment;
}

// Carries out what the shared Controller does on the IMK client of the current event.
class ClientAdapter : public ProjectKey::Client {
 public:
  explicit ClientAdapter(PKInputController *controller) : controller_(controller) {}
  id client;

  void insertText(const std::string &text) override {
    [client insertText:String(text) replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
  }

  void setComposition(const ProjectKey::Composition &composition) override {
    NSString *text = String(composition.text);
    // The marked text takes the place of English a lone Shift converted; apps
    // honor a replacement range there more widely than an empty insertText.
    NSRange replacing = NSMakeRange(NSNotFound, 0);
    if (composition.replaceBefore) {
      NSRange selection = [client selectedRange];
      if (selection.location != NSNotFound && selection.location >= composition.replaceBefore) {
        replacing = NSMakeRange(selection.location - composition.replaceBefore, composition.replaceBefore);
      }
    }
    if (!text.length) {
      [client setMarkedText:@"" selectionRange:NSMakeRange(0, 0) replacementRange:replacing];
      return;
    }
    NSRange all = NSMakeRange(0, text.length);
    NSMutableAttributedString *marked = [[NSMutableAttributedString alloc] initWithString:text];
    [marked setAttributes:[controller_ markForStyle:kTSMHiliteConvertedText atRange:all] range:all];
    if (composition.activeLength) {
      NSRange active = NSMakeRange(composition.activeStart, composition.activeLength);
      [marked setAttributes:[controller_ markForStyle:kTSMHiliteSelectedConvertedText atRange:active] range:active];
    }
    [client setMarkedText:marked selectionRange:NSMakeRange(composition.caret, 0) replacementRange:replacing];
  }

  void showPanel(const ProjectKey::PanelContent &panel) override {
    PKPanelContent *content = [[PKPanelContent alloc] init];
    NSMutableArray<NSString *> *segments = [NSMutableArray array];
    NSMutableIndexSet *doubtful = [NSMutableIndexSet indexSet];
    for (size_t i = 0; i < panel.segments.size(); i++) {
      [segments addObject:String(panel.segments[i])];
      if (i < panel.doubtful.size() && panel.doubtful[i]) [doubtful addIndex:i];
    }
    content.segments = segments;
    content.doubtful = doubtful;
    content.activeSegment = panel.activeSegment;
    content.pendingReading = String(panel.pendingReading);
    content.caret = panel.caret;
    content.showsProof = panel.showsProof;
    content.keyHint = OptionalString(panel.keyHint);
    content.replacedText = OptionalString(panel.replacedText);
    if (!panel.rows.empty()) {
      NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
      for (const auto &row : panel.rows) {
        NSMutableArray<NSString *> *cells = [NSMutableArray array];
        for (const std::string &cell : row) [cells addObject:String(cell)];
        [rows addObject:cells];
      }
      content.rows = rows;
    }
    content.selectedRow = panel.selectedRow;
    content.selection = panel.selection;
    content.pageLabel = OptionalString(panel.pageLabel);
    [PKCandidatePanel.shared show:content below:lineRect()];
  }

  void hidePanel() override { [PKCandidatePanel.shared hide]; }

  void showIndicator(const std::string &label) override { [PKModeIndicator.shared show:String(label) below:lineRect()]; }

  void beep() override { NSBeep(); }

  std::string textBeforeCaret() override {
    NSRange selection = [client selectedRange];
    if (selection.location == NSNotFound) return "";
    NSUInteger start = selection.location > 64 ? selection.location - 64 : 0;
    NSString *text = [client attributedSubstringFromRange:NSMakeRange(start, selection.location - start)].string;
    // The range may start inside a surrogate pair, which has no UTF-8 form.
    if (text.length && CFStringIsSurrogateLowCharacter([text characterAtIndex:0])) text = [text substringFromIndex:1];
    return text.UTF8String ?: "";
  }

 private:
  NSRect lineRect() {
    NSRect rect = NSZeroRect;
    [client attributesForCharacterIndex:0 lineHeightRectangle:&rect];
    return rect;
  }

  __weak PKInputController *controller_;
};

// The key event as the shared Controller sees it.
ProjectKey::KeyEvent KeyEventFrom(NSEvent *event) {
  NSEventModifierFlags flags = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  ProjectKey::KeyEvent key;
  switch (event.keyCode) {
    case kVK_Space: key.key = Key::kSpace; break;
    case kVK_Return:
    case kVK_ANSI_KeypadEnter: key.key = Key::kReturn; break;
    case kVK_Tab: key.key = Key::kTab; break;
    case kVK_Delete: key.key = Key::kBackspace; break;
    case kVK_Escape: key.key = Key::kEscape; break;
    case kVK_LeftArrow: key.key = Key::kLeft; break;
    case kVK_RightArrow: key.key = Key::kRight; break;
    case kVK_UpArrow: key.key = Key::kUp; break;
    case kVK_DownArrow: key.key = Key::kDown; break;
    case kVK_Home: key.key = Key::kHome; break;
    case kVK_End: key.key = Key::kEnd; break;
    case kVK_PageUp: key.key = Key::kPageUp; break;
    case kVK_PageDown: key.key = Key::kPageDown; break;
    case kVK_ForwardDelete: key.key = Key::kForwardDelete; break;
    default: break;
  }
  key.characters = event.characters.UTF8String ?: "";
  key.shift = flags & NSEventModifierFlagShift;
  key.control = flags & NSEventModifierFlagControl;
  key.alt = flags & NSEventModifierFlagOption;
  key.win = flags & NSEventModifierFlagCommand;
  key.capsLock = flags & NSEventModifierFlagCapsLock;
  key.isP = event.keyCode == kVK_ANSI_P;
  return key;
}

}  // namespace

@implementation PKInputController {
  std::unique_ptr<ClientAdapter> _client;
  std::unique_ptr<ProjectKey::Controller> _controller;
}

- (instancetype)initWithServer:(IMKServer *)server delegate:(id)delegate client:(id)client {
  if ((self = [super initWithServer:server delegate:delegate client:client])) {
    [self makeController];
  }
  return self;
}

// Without an IMK server or client; ControllerTest drives the controller this way.
- (instancetype)init {
  if ((self = [super init])) {
    [self makeController];
  }
  return self;
}

- (void)makeController {
  _client = std::make_unique<ClientAdapter>(self);
  _controller = std::make_unique<ProjectKey::Controller>(SharedEnvironment(), *_client);
}

- (NSUInteger)recognizedEvents:(id)sender {
  return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}

- (void)deactivateServer:(id)sender {
  _client->client = sender;
  _controller->deactivate();
  [super deactivateServer:sender];
}

- (void)commitComposition:(id)sender {
  _client->client = sender;
  _controller->deactivate();
}

- (NSMenu *)menu {
  NSMenu *menu = [[NSMenu alloc] init];
  [menu addItemWithTitle:@"設定…" action:@selector(showSettings:) keyEquivalent:@""];
  return menu;
}

- (void)showSettings:(id)sender {
  [PKSettingsWindow showWithClearChoices:^{
    *SharedEnvironment().userModel = NewUserModel();
  }];
}

- (BOOL)handleEvent:(NSEvent *)event client:(id)client {
  _client->client = client;
  if (event.type == NSEventTypeFlagsChanged) {
    switch (event.keyCode) {
      case kVK_CapsLock: {
        // Each press changes the lock (on when pressed, off when released), and
        // the change arrives twice within milliseconds: switch once.
        static NSEventModifierFlags lastLock = 0;
        static NSTimeInterval lastAt = -1;
        NSEventModifierFlags lock = event.modifierFlags & NSEventModifierFlagCapsLock;
        BOOL repeat = lastAt >= 0 && lock == lastLock && event.timestamp - lastAt < 0.1;
        lastLock = lock;
        lastAt = event.timestamp;
        if (!repeat) _controller->handleCapsLock();
        break;
      }
      case kVK_Shift:
      case kVK_RightShift:
        _controller->handleShift(event.modifierFlags & NSEventModifierFlagShift);
        break;
      default:
        _controller->handleOtherModifier();
        break;
    }
    return NO;
  }
  if (event.type != NSEventTypeKeyDown) return NO;
  return _controller->handleKey(KeyEventFrom(event));
}

@end
