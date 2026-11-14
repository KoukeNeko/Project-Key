#import <Carbon/Carbon.h>
#import <InputMethodKit/InputMethodKit.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>

#import "CandidatePanel.h"
#import "SettingsWindow.h"
#ifdef PROJECTKEY_ADVANCED_MODEL
#include "AdvancedModel.h"
#endif
#include "Controller.h"
#include "Models.h"

using ProjectKey::Key;

@interface PKInputController : IMKInputController
- (void)clickCandidateRow:(NSInteger)row index:(NSInteger)index;  // row -1: the chevron
@end

namespace {

NSString *String(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}

NSString *OptionalString(const std::string &s) {
  return s.empty() ? nil : String(s);
}

PKPanelContent *Content(const ProjectKey::PanelContent &panel) {
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
  content.columns = panel.columns;
  content.selectedRow = panel.selectedRow;
  content.selection = panel.selection;
  content.pageLabel = OptionalString(panel.pageLabel);
  content.expanded = panel.expanded;
  content.excluding = panel.excluding;
  return content;
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

// Newer models the settings window downloads, beside personal.txt.
NSURL *DownloadedModels() {
  return [PKSupportDirectory() URLByAppendingPathComponent:@"models" isDirectory:YES];
}

// The advanced model the settings window downloads, beside the models.
NSURL *DownloadedAdvanced() {
  return [PKSupportDirectory() URLByAppendingPathComponent:@"advanced" isDirectory:YES];
}

// How long the typing pauses before the advanced model chooses the sentence.
constexpr NSTimeInterval kPauseSeconds = 0.3;

// The advanced model (core/engine/AdvancedModel.h): loaded and asked on a serial queue of its own,
// so typing never waits for it, with its answers handed back on the main thread. Built without
// llama.cpp, there is never one to load.
class AdvancedRanker {
 public:
  using Request = ProjectKey::Composer::RankingRequest;
  using Answer = std::function<void(const Request &, const std::vector<double> &)>;
  static AdvancedRanker &Shared() {
    static AdvancedRanker *ranker = new AdvancedRanker();
    return *ranker;
  }
  // Loads the newest download, or drops the model when there is none; env says which once done.
  void reload(ProjectKey::Environment *env) {
    dispatch_async(queue_, ^{
      bool loaded = load();
      dispatch_async(dispatch_get_main_queue(), ^{
        env->advancedRanking = loaded;
      });
    });
  }
  // Scores the request's sentences, unless one asked since (a key came) is waiting.
  void ask(Request request, Answer answer) {
    uint64_t ticket = ++latest_;
    auto job = std::make_shared<std::pair<Request, Answer>>(std::move(request), std::move(answer));
    dispatch_async(queue_, ^{
      if (ticket != latest_) return;
      std::vector<double> scores(job->first.sentences.size(), NAN);
#ifdef PROJECTKEY_ADVANCED_MODEL
      if (model_) scores = model_->log10Probabilities(job->first.sentences);
#endif
      dispatch_async(dispatch_get_main_queue(), ^{
        job->second(job->first, scores);
      });
    });
  }

 private:
  bool load() {
#ifdef PROJECTKEY_ADVANCED_MODEL
    model_.reset();  // a removed download's memory goes before a new one loads
    std::filesystem::path folder = DownloadedAdvanced().fileSystemRepresentation;
    std::filesystem::path dir = ProjectKey::NewestDownload(folder);
    if (dir.empty()) return false;
    auto model = std::make_unique<ProjectKey::AdvancedModel>();
    // All but one core: it runs only while the typing pauses.
    int threads = std::clamp(static_cast<int>(NSProcessInfo.processInfo.activeProcessorCount) - 1, 1, 8);
    if (!model->load((dir / ProjectKey::AdvancedModel::kFileName).string(), threads)) {
      NSLog(@"Project Key: cannot load the advanced model in %s", dir.c_str());
      return false;
    }
    model_ = std::move(model);
    // Older ones are left by an update, as UseNewestModels leaves older models; not one downloading.
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(folder, error)) {
      if (entry.path() != dir && entry.path().extension() != ".partial") std::filesystem::remove_all(entry.path(), error);
    }
    return true;
#else
    return false;
#endif
  }

  dispatch_queue_t queue_ = dispatch_queue_create("org.projectkey.inputmethod.advanced", DISPATCH_QUEUE_SERIAL);
  std::atomic<uint64_t> latest_{0};
#ifdef PROJECTKEY_ADVANCED_MODEL
  std::unique_ptr<ProjectKey::AdvancedModel> model_;  // only the queue touches it
#endif
};

// Switches env to the newest models. The tests run from a bare executable without a bundle and
// must not pick up the user's downloads.
bool UseNewestModels(ProjectKey::Environment &env) {
  std::string downloaded = NSBundle.mainBundle.bundleIdentifier ? DownloadedModels().fileSystemRepresentation : "";
  return ProjectKey::UseNewestModels(env, NSBundle.mainBundle.resourcePath.fileSystemRepresentation, downloaded);
}

// The models, settings and 中/英 switch every input session shares.
ProjectKey::Environment &SharedEnvironment() {
  static ProjectKey::Environment *environment = [] {
    [PKSettingsWindow registerDefaults];
    auto *env = new ProjectKey::Environment();
    if (!UseNewestModels(*env)) NSLog(@"Project Key: cannot load data.txt");
    env->userModel = ProjectKey::NewUserModel();
    // What the user typed, learned on this Mac only. Only the installed input method learns:
    // the tests run from a bare executable without a bundle and must not touch the user's file.
    if (NSBundle.mainBundle.bundleIdentifier) {
      NSURL *support = PKSupportDirectory();
      env->personalPath = [support URLByAppendingPathComponent:@"personal.txt"].fileSystemRepresentation;
      env->userPhrasesPath = [support URLByAppendingPathComponent:@"phrases.txt"].fileSystemRepresentation;
      env->personal = std::make_shared<ProjectKey::PersonalModel>();
      env->personal->load(env->personalPath);
      // From the settings app. Any app can post these, sandboxed ones too, so the choices are
      // cleared only once personal.txt is gone: removing it takes the user's own file access.
      NSDistributedNotificationCenter *center = NSDistributedNotificationCenter.defaultCenter;
      [center addObserverForName:PKClearChoicesNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
        if (![NSFileManager.defaultManager fileExistsAtPath:String(env->personalPath)]) ProjectKey::ClearChoices(*env);
      }];
      [center addObserverForName:PKReloadModelsNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
        if (!UseNewestModels(*env)) NSLog(@"Project Key: cannot load the downloaded models");
        AdvancedRanker::Shared().reload(env);
      }];
      AdvancedRanker::Shared().reload(env);
    }
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
    env->tabKeycap = "⇥";
    env->deleteKeycap = "⇧⌫";  // as well as ⌦, which a laptop only has as fn Delete
    env->backspaceKeycap = "⌫";
    env->shiftTabKeycap = "⇧⇥";
    env->appTypesUnhandledKeys = true;
    env->hintWindow = true;
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
    // The marked text takes the place of English Tab converted; apps
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
    // The English-to-Zhuyin offer, where it could not be carried out.
    if (!panel.keyHint.empty() && SharedEnvironment().englishMode && !replacesBeforeCaret()) {
      hidePanel();
      return;
    }
    __weak PKInputController *owner = controller_;  // the shared window answers to whichever context shows it
    PKCandidatePanel.shared.clicked = ^(NSInteger row, NSInteger index) {
      [owner clickCandidateRow:row index:index];
    };
    [PKCandidatePanel.shared show:Content(panel) below:lineRect()];
  }

  void hidePanel() override { [PKCandidatePanel.shared hide]; }

  void showHint(const ProjectKey::PanelContent &hint) override { [PKCandidatePanel.hint show:Content(hint) beside:PKCandidatePanel.shared]; }
  void hideHint() override { [PKCandidatePanel.hint hide]; }

  void showIndicator(const std::string &label) override { [PKModeIndicator.shared show:String(label) below:lineRect()]; }

  void beep() override { NSBeep(); }

  // Terminals show the shell's line but cannot take back what they already sent
  // to it, so English there cannot give way to a composition.
  // shortcut: a fixed list of terminals; add others as they turn up.
  bool replacesBeforeCaret() {
    static NSSet<NSString *> *terminals = [NSSet setWithArray:@[
      @"com.apple.Terminal", @"com.googlecode.iterm2", @"com.mitchellh.ghostty", @"net.kovidgoyal.kitty",
      @"org.alacritty", @"com.github.wez.wezterm", @"dev.warp.Warp-Stable"
    ]];
    return ![terminals containsObject:[client bundleIdentifier]];
  }

  std::string textBeforeCaret() override {
    if (!replacesBeforeCaret()) return "";
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
  key.numpad = flags & NSEventModifierFlagNumericPad;  // the arrows carry it too; only digits look at it
  return key;
}

}  // namespace

// The engine's models, for the settings window.
@interface PKEngineModels : NSObject <PKModels>
@end

@implementation PKEngineModels

- (NSString *)version {
  return String(ProjectKey::ModelsVersion(ProjectKey::ModelsDirectory(NSBundle.mainBundle.resourcePath.fileSystemRepresentation,
                                                                     DownloadedModels().fileSystemRepresentation)));
}

- (BOOL)loaded {
  return SharedEnvironment().bigram && SharedEnvironment().chars;
}

- (NSURL *)downloads {
  return DownloadedModels();
}

- (BOOL)reload {
  return UseNewestModels(SharedEnvironment());
}

- (BOOL)advancedSupported {
#ifdef PROJECTKEY_ADVANCED_MODEL
  return YES;
#else
  return NO;
#endif
}

- (NSString *)advancedVersion {
  std::filesystem::path dir = ProjectKey::NewestDownload(DownloadedAdvanced().fileSystemRepresentation);
  return dir.empty() ? @"" : String(ProjectKey::ModelsVersion(dir));
}

- (NSURL *)advancedDownloads {
  return DownloadedAdvanced();
}

- (void)reloadAdvanced {
  AdvancedRanker::Shared().reload(&SharedEnvironment());
}

@end

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

- (void)activateServer:(id)sender {
  [super activateServer:sender];
  _client->client = sender;
  _controller->showMode();
}

- (void)deactivateServer:(id)sender {
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(paused) object:nil];
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
  if (!SharedEnvironment().userPhrasesPath.empty()) {
    [menu addItemWithTitle:@"編輯自訂詞…" action:@selector(editUserPhrases:) keyEquivalent:@""];
    [menu addItemWithTitle:@"編輯排除詞…" action:@selector(editExcludedPhrases:) keyEquivalent:@""];
  }
  return menu;
}

- (void)editUserPhrases:(id)sender {
  PKEditUserPhrases();
}

- (void)editExcludedPhrases:(id)sender {
  PKEditExcludedPhrases();
}

- (void)showSettings:(id)sender {
  PKShowSettings();
}

- (void)clickCandidateRow:(NSInteger)row index:(NSInteger)index {
  if (row < 0) {
    _controller->toggleGrid();
  } else {
    _controller->clickCandidate(static_cast<int>(row), static_cast<int>(index));
  }
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
  BOOL handled = _controller->handleKey(KeyEventFrom(event));
  [self waitForPause];
  return handled;
}

// After each key the pause starts again; once it lasts, the advanced model chooses
// (Controller::rankingRequest), and its answer is dropped when a key came since.
- (void)waitForPause {
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(paused) object:nil];
  if (SharedEnvironment().advancedRanking) [self performSelector:@selector(paused) withObject:nil afterDelay:kPauseSeconds];
}

- (void)paused {
  std::optional<ProjectKey::Composer::RankingRequest> request = _controller->rankingRequest();
  if (!request) return;
  __weak PKInputController *weakSelf = self;
  AdvancedRanker::Shared().ask(std::move(*request), [weakSelf](const ProjectKey::Composer::RankingRequest &asked, const std::vector<double> &scores) {
    PKInputController *strongSelf = weakSelf;
    if (strongSelf) strongSelf->_controller->applyRanking(asked, scores);
  });
}

@end

void PKShowSettings(void) {
  [PKSettingsWindow showWithClearChoices:^{
    ProjectKey::ClearChoices(SharedEnvironment());
  } models:[[PKEngineModels alloc] init]];
}
