#import <Carbon/Carbon.h>
#import <InputMethodKit/InputMethodKit.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>

#import "CandidatePanel.h"
#import "SettingsWindow.h"
#include "Composer.h"

using ProjectKey::Composer;
using ProjectKey::Segment;

namespace {

constexpr NSInteger kExpandedRows = 5;  // rows the expanded grid shows at once

// Same three choices as the prototype's 整句預覽 setting.
enum ProofMode : NSInteger { kProofAlways = 0, kProofWhenDoubtful = 1, kProofOff = 2 };

std::shared_ptr<McBopomofo::McBopomofoLM> SharedLanguageModel() {
  static std::shared_ptr<McBopomofo::McBopomofoLM> lm = [] {
    auto model = std::make_shared<McBopomofo::McBopomofoLM>();
    NSString *path = [NSBundle.mainBundle pathForResource:@"data" ofType:@"txt"];
    model->loadLanguageModel(path.fileSystemRepresentation);
    if (!model->isDataModelLoaded()) NSLog(@"Project Key: cannot load %@", path);
    return model;
  }();
  return lm;
}

// Choices the user made, shared by every input session. Kept in memory only,
// with McBopomofo's capacity and 1.5-hour half-life.
McBopomofo::UserOverrideModel NewUserModel() {
  return McBopomofo::UserOverrideModel(500, 5400.0);
}

std::shared_ptr<McBopomofo::UserOverrideModel> SharedUserModel() {
  static auto model = std::make_shared<McBopomofo::UserOverrideModel>(NewUserModel());
  return model;
}

std::shared_ptr<const ProjectKey::Bigram> SharedBigram() {
  static std::shared_ptr<const ProjectKey::Bigram> bigram = []() -> std::shared_ptr<const ProjectKey::Bigram> {
    NSString *path = [NSBundle.mainBundle pathForResource:@"bigram" ofType:@"bin"];
    if (!path) return nullptr;
    auto model = std::make_shared<ProjectKey::Bigram>();
    if (!model->load(path.fileSystemRepresentation)) {
      NSLog(@"Project Key: cannot load %@", path);
      return nullptr;
    }
    return model;
  }();
  return bigram;
}

NSString *String(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
}

// One switch for all sessions, like a keyboard's own layout toggle.
BOOL gEnglishMode = NO;

}  // namespace

@interface PKInputController : IMKInputController
@end

@implementation PKInputController {
  std::unique_ptr<Composer> _composer;
  BOOL _choosing;
  size_t _activeStart;  // start reading of the segment being chosen
  std::vector<std::string> _candidates;
  NSInteger _selection;
  // Candidates packed into grid rows of PKGridColumns columns: where each row
  // starts, and the column each candidate starts at.
  std::vector<NSInteger> _rowStarts;
  std::vector<NSInteger> _columns;
  BOOL _expanded;          // several rows shown; collapsed shows only the selection's row
  NSInteger _firstRow;     // first grid row on screen
  BOOL _shiftAlone;
  BOOL _adjacentKeys, _rememberChoices;  // the settings _composer was made with
  // What English mode let through since the last other key, so a lone Shift
  // can turn Zhuyin typed in the wrong mode back into a composition.
  NSMutableString *_englishRun;
  // The keys behind the composition while it is only typed syllables, so a
  // lone Shift can turn English typed in the wrong mode back into English.
  std::string _zhuyinKeys;
  BOOL _zhuyinEdited;  // another key changed the composition apart from _zhuyinKeys
  // Text the next marked text replaces: the English a lone Shift converted.
  NSRange _replacing;
}

- (instancetype)initWithServer:(IMKServer *)server delegate:(id)delegate client:(id)client {
  if ((self = [super initWithServer:server delegate:delegate client:client])) {
    [self makeComposer];
  }
  return self;
}

// Without an IMK server or client; ControllerTest drives the controller this way.
- (instancetype)init {
  if ((self = [super init])) {
    [self makeComposer];
  }
  return self;
}

// Settings take effect on the next composition.
- (void)makeComposer {
  if (!_composer) _replacing = NSMakeRange(NSNotFound, 0);
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    [PKSettingsWindow registerDefaults];
  });
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  BOOL adjacentKeys = [defaults boolForKey:PKAdjacentKeysKey], rememberChoices = [defaults boolForKey:PKRememberChoicesKey];
  if (_composer && adjacentKeys == _adjacentKeys && rememberChoices == _rememberChoices) return;
  _adjacentKeys = adjacentKeys;
  _rememberChoices = rememberChoices;
  _composer = std::make_unique<Composer>(SharedLanguageModel(), SharedBigram(), rememberChoices ? SharedUserModel() : nullptr,
                                         Composer::kContextWeight, Composer::kTrigramWeight,
                                         adjacentKeys ? Composer::kAdjacentKeyScore : -INFINITY);
}

- (NSUInteger)recognizedEvents:(id)sender {
  return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}

- (void)deactivateServer:(id)sender {
  [self forgetEnglishRun];
  [self commitTo:sender];
  [super deactivateServer:sender];
}

- (void)commitComposition:(id)sender {
  [self commitTo:sender];
}

#pragma mark - Proof preview setting

// While choosing, the candidate window's own setting applies; otherwise the bubble's.
- (NSString *)proofKey {
  return _choosing ? PKChoosingProofKey : PKProofPreviewKey;
}

- (ProofMode)proofMode {
  return (ProofMode)[NSUserDefaults.standardUserDefaults integerForKey:self.proofKey];
}

- (void)toggleProof {
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  NSString *lastOnKey = [self.proofKey stringByAppendingString:@"LastOn"];
  ProofMode mode = self.proofMode;
  if (mode != kProofOff) [defaults setInteger:mode forKey:lastOnKey];
  // Never toggled before: back on means 永遠, since 有可疑字時 may still show nothing.
  ProofMode on = [defaults objectForKey:lastOnKey] ? (ProofMode)[defaults integerForKey:lastOnKey] : kProofAlways;
  [defaults setInteger:mode == kProofOff ? on : kProofOff forKey:self.proofKey];
}

- (BOOL)proofVisible {
  if (_composer->isEmpty()) return NO;
  switch (self.proofMode) {
    case kProofAlways:
      return YES;
    case kProofWhenDoubtful:
      for (const Segment &segment : _composer->segments()) {
        if (segment.doubtful) return YES;
      }
      return NO;
    case kProofOff:
      return NO;
  }
  return NO;
}

- (NSMenu *)menu {
  NSMenu *menu = [[NSMenu alloc] init];
  [menu addItemWithTitle:@"設定…" action:@selector(showSettings:) keyEquivalent:@""];
  return menu;
}

- (void)showSettings:(id)sender {
  [PKSettingsWindow showWithClearChoices:^{
    *SharedUserModel() = NewUserModel();
  }];
}

#pragma mark - Keys

- (BOOL)handleEvent:(NSEvent *)event client:(id)client {
  NSEventModifierFlags flags = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  if (event.type == NSEventTypeFlagsChanged) {
    // Each Caps Lock press switches 中/英, like the system's own Caps Lock switch.
    if (event.keyCode == kVK_CapsLock && [defaults boolForKey:PKCapsLockToggleKey]) {
      _shiftAlone = NO;
      [self commitTo:client];
      [self forgetEnglishRun];
      gEnglishMode = !gEnglishMode;
      NSRect lineRect = NSZeroRect;
      [client attributesForCharacterIndex:0 lineHeightRectangle:&lineRect];
      [PKModeIndicator.shared show:gEnglishMode ? @"A" : @"ㄅ" below:lineRect];
      return NO;
    }
    // A Shift press and release with no key in between switches 中/英.
    if (event.keyCode == kVK_Shift || event.keyCode == kVK_RightShift) {
      if (![defaults boolForKey:PKShiftToggleKey]) {
        _shiftAlone = NO;
      } else if (flags & NSEventModifierFlagShift) {
        _shiftAlone = YES;
      } else if (_shiftAlone) {
        _shiftAlone = NO;
        if (!gEnglishMode && [self convertZhuyinKeys:client]) return NO;
        [self commitTo:client];
        if (gEnglishMode && [self convertEnglishRun:client]) return NO;
        [self forgetEnglishRun];
        gEnglishMode = !gEnglishMode;
        NSRect lineRect = NSZeroRect;
        [client attributesForCharacterIndex:0 lineHeightRectangle:&lineRect];
        [PKModeIndicator.shared show:gEnglishMode ? @"A" : @"ㄅ" below:lineRect];
      }
    } else {
      _shiftAlone = NO;
    }
    return NO;
  }
  if (event.type != NSEventTypeKeyDown) return NO;
  _shiftAlone = NO;
  if (_composer->isEmpty()) {
    [self makeComposer];
    // With both switches turned off there is no way back to 中.
    if (gEnglishMode && ![defaults boolForKey:PKShiftToggleKey] && ![defaults boolForKey:PKCapsLockToggleKey]) gEnglishMode = NO;
  }
  // Caps Lock is the switch, so the lock it leaves on does not capitalize: the
  // key acts as its lowercase letter (Shift still capitalizes).
  NSString *lowered = event.characters.lowercaseString;
  BOOL capsLetter = (flags & NSEventModifierFlagCapsLock) && !(flags & NSEventModifierFlagShift) &&
                    [defaults boolForKey:PKCapsLockToggleKey] && ![lowered isEqualToString:event.characters];
  if (capsLetter) {
    flags &= ~NSEventModifierFlagCapsLock;
    event = [NSEvent keyEventWithType:event.type
                             location:event.locationInWindow
                        modifierFlags:flags
                            timestamp:event.timestamp
                         windowNumber:event.windowNumber
                              context:nil
                           characters:lowered
          charactersIgnoringModifiers:lowered
                            isARepeat:event.isARepeat
                              keyCode:event.keyCode];
  }

  if (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption)) {
    [self forgetEnglishRun];
    BOOL proofToggle = (flags & NSEventModifierFlagControl) && (flags & NSEventModifierFlagShift) && event.keyCode == kVK_ANSI_P;
    if (proofToggle && !_composer->isEmpty()) {
      [self toggleProof];
      [self refresh:client];
      return YES;
    }
    [self commitTo:client];  // let the shortcut act on committed text
    return NO;
  }
  if (gEnglishMode) {
    [self trackEnglish:event flags:flags];
    // Passed through, the app would capitalize it; insert the lowercase letter instead.
    if (capsLetter) [client insertText:lowered replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
    [self showEnglishHint:client];
    return capsLetter;
  }
  [self trackZhuyin:event flags:flags];
  return _choosing ? [self handleChoosing:event flags:flags client:client] : [self handleComposing:event flags:flags client:client];
}

#pragma mark - English typed as Zhuyin

- (void)forgetEnglishRun {
  [_englishRun setString:@""];
  if (PKCandidatePanel.shared.content.keyHint) [PKCandidatePanel.shared hide];
}

- (void)trackEnglish:(NSEvent *)event flags:(NSEventModifierFlags)flags {
  if (!_englishRun) _englishRun = [NSMutableString string];
  NSString *characters = event.characters;
  unichar c = characters.length == 1 ? [characters characterAtIndex:0] : 0;
  if (event.keyCode == kVK_Delete) {
    if (_englishRun.length) [_englishRun deleteCharactersInRange:NSMakeRange(_englishRun.length - 1, 1)];
  } else if (!(flags & NSEventModifierFlagShift) && (c == ' ' || (c < 128 && _composer->isReadingKey((char)c)))) {
    [_englishRun appendString:characters];
  } else {
    [_englishRun setString:@""];
  }
}

// The keys at the end of the English run that are whole Zhuyin syllables, and
// how many spaces follow them; empty when none are.
- (std::string)zhuyinAtEnd:(size_t *)trailingSpaces {
  std::string run = _englishRun.UTF8String ?: "";
  size_t spaces = 0;
  while (spaces < run.size() && run[run.size() - 1 - spaces] == ' ') spaces++;
  if (trailingSpaces) *trailingSpaces = spaces;
  // Syllables end at a tone key, or at a space for the first tone; walk back
  // from the end while they are whole.
  std::string body = run.substr(0, run.size() - spaces);
  size_t start = body.size();
  while (start > 0) {
    size_t begin = start - 1;
    while (begin > 0 && !std::strchr(" 3467", body[begin - 1])) begin--;
    if (!_composer->isSyllableKeys(body.substr(begin, start - begin))) break;
    start = begin;
  }
  return body.substr(start);
}

- (void)typeKeys:(const std::string &)keys {
  for (char key : keys) {
    if (key == ' ') {
      _composer->completeWithFirstTone();
    } else {
      _composer->typeReadingKey(key);
    }
  }
}

// While English mode passes through Zhuyin, shows what a lone Shift would turn
// it into. Only once a tone key is in it: plain words like "so " can happen to
// spell a first-tone syllable.
- (void)showEnglishHint:(id)client {
  size_t trailingSpaces = 0;
  std::string keys = [NSUserDefaults.standardUserDefaults boolForKey:PKEnglishToZhuyinKey] ? [self zhuyinAtEnd:&trailingSpaces] : "";
  if (keys.find_first_of("3467") == std::string::npos) {
    if (PKCandidatePanel.shared.content.keyHint) [PKCandidatePanel.shared hide];
    return;
  }
  [self typeKeys:keys];
  PKPanelContent *content = [[PKPanelContent alloc] init];
  content.segments = @[ String(_composer->text()) ];
  content.doubtful = [NSIndexSet indexSet];
  content.activeSegment = -1;
  content.pendingReading = @"";
  content.caret = content.segments.firstObject.length;
  content.showsProof = YES;
  content.keyHint = @"⇧";
  content.replacedText = String(keys + std::string(trailingSpaces, ' '));  // the English that goes
  _composer->clear();
  NSRect lineRect = NSZeroRect;
  [client attributesForCharacterIndex:0 lineHeightRectangle:&lineRect];
  [PKCandidatePanel.shared show:content below:lineRect];
}

// Replaces the end of the English run that is whole Zhuyin syllables with a
// composition of them and switches to 中. False when nothing there is Zhuyin.
#pragma mark - Zhuyin that is English

- (void)trackZhuyin:(NSEvent *)event flags:(NSEventModifierFlags)flags {
  if (_composer->isEmpty()) {
    _zhuyinKeys.clear();
    _zhuyinEdited = NO;
  }
  NSString *characters = event.characters;
  unichar c = characters.length == 1 ? [characters characterAtIndex:0] : 0;
  BOOL typing = !_choosing && !(flags & NSEventModifierFlagShift) &&
                (event.keyCode == kVK_Space ? _composer->hasPendingReading() : c < 128 && _composer->isReadingKey((char)c));
  // Any other key (Delete, arrows, choosing) edits the composition apart from the keys.
  if (typing) {
    _zhuyinKeys += (char)c;
  } else {
    _zhuyinEdited = YES;
  }
}

// The keys behind the composition as English when they spell words that are
// not themselves Zhuyin syllables; nil otherwise.
- (NSString *)englishTyped {
  if (_zhuyinEdited || ![NSUserDefaults.standardUserDefaults boolForKey:PKZhuyinToEnglishKey]) return nil;
  NSString *keys = String(_zhuyinKeys);
  NSCharacterSet *notLetters = [[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyz "] invertedSet];
  if ([keys stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].length < 2 ||
      [keys rangeOfCharacterFromSet:notLetters].location != NSNotFound) {
    return nil;
  }
  // "su " or "wu " is a first-tone syllable as well as a spelling the checker allows.
  BOOL syllables = YES;
  for (NSString *word in [keys componentsSeparatedByString:@" "]) {
    if (word.length && !_composer->isSyllableKeys(std::string(word.UTF8String) + " ")) syllables = NO;
  }
  if (syllables) return nil;
  NSRange misspelled = [NSSpellChecker.sharedSpellChecker checkSpellingOfString:keys
                                                                     startingAt:0
                                                                       language:@"en"
                                                                           wrap:NO
                                                         inSpellDocumentWithTag:0
                                                                      wordCount:NULL];
  return misspelled.location == NSNotFound ? keys : nil;
}

// Replaces a composition typed as English keys with that English and switches to 英.
- (BOOL)convertZhuyinKeys:(id)client {
  NSString *english = _composer->isEmpty() || _choosing ? nil : [self englishTyped];
  if (!english) return NO;
  [client insertText:english replacementRange:NSMakeRange(NSNotFound, NSNotFound)];  // takes the marked text's place
  _composer->clear();
  _zhuyinKeys.clear();
  [PKCandidatePanel.shared hide];
  gEnglishMode = YES;
  NSRect lineRect = NSZeroRect;
  [client attributesForCharacterIndex:0 lineHeightRectangle:&lineRect];
  [PKModeIndicator.shared show:@"A" below:lineRect];
  return YES;
}

- (BOOL)convertEnglishRun:(id)client {
  size_t trailingSpaces = 0;
  std::string keys = [NSUserDefaults.standardUserDefaults boolForKey:PKEnglishToZhuyinKey] ? [self zhuyinAtEnd:&trailingSpaces] : "";
  [_englishRun setString:@""];
  if (keys.empty()) return NO;
  NSRange selection = [client selectedRange];
  NSUInteger length = keys.size() + trailingSpaces;
  if (selection.location == NSNotFound || selection.location < length) return NO;
  // The composition takes the English's place as marked text; apps honor a
  // replacement range there more widely than an empty insertText.
  _replacing = NSMakeRange(selection.location - length, length);
  [self typeKeys:keys];
  gEnglishMode = NO;
  [self refresh:client];
  NSRect lineRect = NSZeroRect;
  [client attributesForCharacterIndex:0 lineHeightRectangle:&lineRect];
  [PKModeIndicator.shared show:@"ㄅ" below:lineRect];
  return YES;
}

- (BOOL)handleComposing:(NSEvent *)event flags:(NSEventModifierFlags)flags client:(id)client {
  BOOL empty = _composer->isEmpty();
  switch (event.keyCode) {
    case kVK_Space:
      if (_composer->hasPendingReading()) {
        if (_composer->completeWithFirstTone() == Composer::KeyResult::kInvalid) NSBeep();
        [self refresh:client];
        return YES;
      }
      if (empty) return NO;
      [self startChoosingAt:[self defaultSegment] client:client];
      return YES;
    case kVK_DownArrow:
    case kVK_Tab:
      if (empty) return NO;
      if (!_composer->hasPendingReading()) [self startChoosingAt:[self defaultSegment] client:client];
      return YES;
    case kVK_Return:
    case kVK_ANSI_KeypadEnter:
      if (empty) return NO;
      [self commitTo:client];
      return YES;
    case kVK_Delete:
      if (empty) return NO;
      _composer->backspace();
      [self refresh:client];
      return YES;
    case kVK_Escape:
      if (empty) return NO;
      _composer->hasPendingReading() ? _composer->clearPendingReading() : _composer->clear();
      [self refresh:client];
      return YES;
    case kVK_LeftArrow:
    case kVK_RightArrow:
    case kVK_Home:
    case kVK_End:
      if (empty) return NO;
      // The cursor moves between whole syllables, so finish typing one first.
      if (!_composer->hasPendingReading()) {
        size_t cursor = _composer->cursor();
        switch (event.keyCode) {
          case kVK_LeftArrow: cursor = cursor > 0 ? cursor - 1 : 0; break;
          case kVK_RightArrow: cursor++; break;
          case kVK_Home: cursor = 0; break;
          default: cursor = _composer->readingCount(); break;
        }
        _composer->setCursor(cursor);
        [self refresh:client];
      }
      return YES;
    case kVK_ForwardDelete:
      if (empty) return NO;
      if (!_composer->hasPendingReading() && _composer->deleteForward()) [self refresh:client];
      return YES;
    case kVK_UpArrow:
      return !empty;  // keep the app's caret still while composing
  }

  NSString *characters = event.characters;
  if (characters.length != 1 || [characters characterAtIndex:0] > 0x7e || [characters characterAtIndex:0] < 0x20) {
    return !empty;
  }
  char key = (char)[characters characterAtIndex:0];
  if (!(flags & NSEventModifierFlagShift) && _composer->isReadingKey(key)) {
    if (_composer->typeReadingKey(key) == Composer::KeyResult::kInvalid) NSBeep();
    [self refresh:client];
    return YES;
  }
  // Apple's Zhuyin keyboard prints 、 on the \ key; McBopomofo's table has it on '.
  if (key == '\\' && !(flags & NSEventModifierFlagShift)) key = '\'';
  if (_composer->insertPunctuation(key)) {
    [self refresh:client];
    return YES;
  }
  [self commitTo:client];
  return NO;
}

- (NSInteger)rowOf:(NSInteger)index {
  return std::upper_bound(_rowStarts.begin(), _rowStarts.end(), index) - _rowStarts.begin() - 1;
}

- (NSInteger)rowEnd:(NSInteger)row {
  return row + 1 < (NSInteger)_rowStarts.size() ? _rowStarts[row + 1] : (NSInteger)_candidates.size();
}

// The candidate of `row` under `column`, or the row's last one when it ends earlier.
- (NSInteger)indexInRow:(NSInteger)row column:(NSInteger)column {
  NSInteger index = _rowStarts[row];
  while (index + 1 < [self rowEnd:row] && _columns[index + 1] <= column) index++;
  return index;
}

- (BOOL)handleChoosing:(NSEvent *)event flags:(NSEventModifierFlags)flags client:(id)client {
  NSInteger count = (NSInteger)_candidates.size();
  NSInteger row = [self rowOf:_selection], rows = (NSInteger)_rowStarts.size();
  NSInteger column = _columns[_selection];
  switch (event.keyCode) {
    case kVK_LeftArrow:
      _selection = MAX(0, _selection - 1);
      break;
    case kVK_RightArrow:
      _selection = MIN(count - 1, _selection + 1);
      break;
    case kVK_DownArrow:
      // Down opens the grid and moves down in the same column.
      if (row + 1 < rows) {
        _expanded = YES;
        _selection = [self indexInRow:row + 1 column:column];
      }
      break;
    case kVK_UpArrow:
      if (_expanded && row > 0) {
        _selection = [self indexInRow:row - 1 column:column];
      } else if (_expanded) {
        _expanded = NO;  // Up from the grid's first row folds it back
      } else if (row > 0) {
        _selection = _rowStarts[row - 1];
      } else {
        _choosing = NO;  // past the first row, Up returns to the sentence
      }
      break;
    case kVK_Space:
    case kVK_PageDown:
      if (_expanded) {
        _selection = [self indexInRow:MIN(rows - 1, row + kExpandedRows) column:column];
      } else {
        _selection = _rowStarts[(row + 1) % rows];
      }
      break;
    case kVK_PageUp:
      if (_expanded) {
        _selection = [self indexInRow:MAX(0, row - kExpandedRows) column:column];
      } else {
        _selection = _rowStarts[MAX(row - 1, 0)];  // wrapping back lands on the rarest row
      }
      break;
    case kVK_Return:
    case kVK_ANSI_KeypadEnter:
      [self pick:_selection client:client];
      return YES;
    case kVK_Tab: {
      std::optional<size_t> next = [self doubtfulAfter:_activeStart backwards:(flags & NSEventModifierFlagShift)];
      if (next) [self startChoosingAt:*next client:client];
      return YES;
    }
    case kVK_Escape:
      if (_expanded) {
        _expanded = NO;
      } else {
        _choosing = NO;
      }
      break;
    case kVK_Delete:
      _choosing = NO;
      break;
    default: {
      NSString *characters = event.characters;
      unichar c = characters.length == 1 ? [characters characterAtIndex:0] : 0;
      if (c >= '1' && c < '1' + PKGridColumns) {
        NSInteger index = _rowStarts[row] + (c - '1');  // the selection's row carries the labels
        if (index < [self rowEnd:row]) [self pick:index client:client];
        return YES;
      }
      // Any other typing leaves the choice as it is and keeps composing.
      _choosing = NO;
      return [self handleComposing:event flags:flags client:client];
    }
  }
  [self refresh:client];
  return YES;
}

#pragma mark - Choosing

- (std::optional<Segment>)segmentStartingAt:(size_t)start {
  return _composer->segmentFrom(start);
}

// The segment (or the rest of the phrase) right after the cursor, or, with the cursor at the end, the
// first doubtful segment so Space goes straight to the likely mistake.
- (size_t)defaultSegment {
  size_t cursor = _composer->cursor();
  if (cursor < _composer->readingCount()) {
    for (const Segment &segment : _composer->segments()) {
      if (cursor < segment.start + segment.length) return std::max(cursor, segment.start);
    }
  }
  for (const Segment &segment : _composer->segments()) {
    if (segment.doubtful) return segment.start;
  }
  return _composer->segments().back().start;
}

// The next doubtful segment in reading order, wrapping around, excluding `start`.
- (std::optional<size_t>)doubtfulAfter:(size_t)start backwards:(BOOL)backwards {
  std::vector<size_t> doubtful;
  for (const Segment &segment : _composer->segments()) {
    if (segment.doubtful && segment.start != start) doubtful.push_back(segment.start);
  }
  if (doubtful.empty()) return std::nullopt;
  if (backwards) {
    for (auto it = doubtful.rbegin(); it != doubtful.rend(); ++it) {
      if (*it < start) return *it;
    }
    return doubtful.back();
  }
  for (size_t candidate : doubtful) {
    if (candidate > start) return candidate;
  }
  return doubtful.front();
}

- (void)startChoosingAt:(size_t)start client:(id)client {
  std::optional<Segment> segment = [self segmentStartingAt:start];
  if (!segment) return;
  std::vector<std::string> candidates = _composer->candidates(*segment);
  if (candidates.empty()) return;
  _choosing = YES;
  _expanded = NO;
  _firstRow = 0;
  _activeStart = start;
  _candidates = std::move(candidates);
  _rowStarts.clear();
  _columns.clear();
  NSInteger column = PKGridColumns;
  for (NSInteger i = 0; i < (NSInteger)_candidates.size(); i++) {
    NSInteger span = [PKCandidatePanel columnsSpannedBy:String(_candidates[i])];
    if (column + span > PKGridColumns) {
      _rowStarts.push_back(i);
      column = 0;
    }
    _columns.push_back(column);
    column += span;
  }
  auto current = std::find(_candidates.begin(), _candidates.end(), segment->text);
  _selection = current == _candidates.end() ? 0 : current - _candidates.begin();
  [self refresh:client];
}

- (void)pick:(NSInteger)index client:(id)client {
  std::optional<Segment> segment = [self segmentStartingAt:_activeStart];
  if (segment) _composer->choose(*segment, _candidates[index]);
  std::optional<size_t> next = [self doubtfulAfter:_activeStart backwards:NO];
  if (next) {
    [self startChoosingAt:*next client:client];
  } else {
    _choosing = NO;
    [self refresh:client];
  }
}

#pragma mark - Output

- (void)commitTo:(id)client {
  if (!_composer->isEmpty()) {
    NSString *text = String(_composer->text());
    if (text.length) [client insertText:text replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
    _composer->clear();
  }
  _choosing = NO;
  [PKCandidatePanel.shared hide];
}

- (void)refresh:(id)client {
  if (_composer->isEmpty()) {
    _choosing = NO;
    [client setMarkedText:@"" selectionRange:NSMakeRange(0, 0) replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
    [PKCandidatePanel.shared hide];
    return;
  }

  NSMutableArray<NSString *> *segments = [NSMutableArray array];
  NSMutableIndexSet *doubtful = [NSMutableIndexSet indexSet];
  NSInteger active = -1;
  NSRange activeRange = NSMakeRange(NSNotFound, 0);
  NSMutableString *marked = [NSMutableString string];
  size_t cursor = _composer->cursor();
  NSUInteger caret = 0;
  for (const Segment &segment : _composer->segments()) {
    NSString *text = String(segment.text);
    if (cursor >= segment.start + segment.length) {
      caret += text.length;
    } else if (cursor > segment.start) {
      // Inside a phrase: one character per syllable, except for odd phrases.
      caret += text.length == segment.length ? cursor - segment.start : text.length;
    }
    if (segment.doubtful) [doubtful addIndex:segments.count];
    if (_choosing && _activeStart >= segment.start && _activeStart < segment.start + segment.length) {
      active = (NSInteger)segments.count;
      activeRange = NSMakeRange(marked.length, text.length);
    }
    [segments addObject:text];
    [marked appendString:text];
  }
  NSString *reading = String(_composer->pendingReading());
  [marked insertString:reading atIndex:caret];
  caret += reading.length;
  if (activeRange.location != NSNotFound && activeRange.location >= caret - reading.length) {
    activeRange.location += reading.length;
  }

  NSMutableAttributedString *attributed = [[NSMutableAttributedString alloc] initWithString:marked];
  [attributed setAttributes:[self markForStyle:kTSMHiliteConvertedText atRange:NSMakeRange(0, marked.length)]
                      range:NSMakeRange(0, marked.length)];
  if (activeRange.location != NSNotFound) {
    [attributed setAttributes:[self markForStyle:kTSMHiliteSelectedConvertedText atRange:activeRange] range:activeRange];
  }
  [client setMarkedText:attributed
         selectionRange:NSMakeRange(caret, 0)
       replacementRange:_replacing];
  _replacing = NSMakeRange(NSNotFound, 0);

  NSString *english = _choosing ? nil : [self englishTyped];
  if (english) {  // what a lone Shift would turn it into
    PKPanelContent *content = [[PKPanelContent alloc] init];
    content.segments = @[ english ];
    content.doubtful = [NSIndexSet indexSet];
    content.activeSegment = -1;
    content.pendingReading = @"";
    content.caret = english.length;
    content.showsProof = YES;
    content.keyHint = @"⇧";
    content.replacedText = marked;
    NSRect lineRect = NSZeroRect;
    [client attributesForCharacterIndex:0 lineHeightRectangle:&lineRect];
    [PKCandidatePanel.shared show:content below:lineRect];
    return;
  }
  BOOL showsProof = [self proofVisible];
  if (!_choosing && !showsProof) {
    [PKCandidatePanel.shared hide];
    return;
  }
  PKPanelContent *content = [[PKPanelContent alloc] init];
  content.segments = segments;
  content.doubtful = doubtful;
  content.activeSegment = active;
  content.pendingReading = reading;
  content.caret = caret - reading.length;
  content.showsProof = showsProof;
  if (_choosing) {
    NSInteger row = [self rowOf:_selection], rows = (NSInteger)_rowStarts.size();
    // Collapsed: the selection's row. Expanded: up to kExpandedRows rows, scrolled to the selection.
    NSInteger first = row, shown = 1;
    if (_expanded) {
      _firstRow = MAX(MIN(_firstRow, row), row - kExpandedRows + 1);
      first = _firstRow;
      shown = MIN(kExpandedRows, rows - first);
    }
    NSMutableArray<NSArray<NSString *> *> *visible = [NSMutableArray array];
    for (NSInteger r = first; r < first + shown; r++) {
      NSMutableArray<NSString *> *cells = [NSMutableArray array];
      for (NSInteger i = _rowStarts[r]; i < [self rowEnd:r]; i++) [cells addObject:String(_candidates[i])];
      [visible addObject:cells];
    }
    content.rows = visible;
    content.selectedRow = row - first;
    content.selection = _selection - _rowStarts[row];
    content.pageLabel = rows > 1 ? [NSString stringWithFormat:@"%ld/%ld", row + 1, rows] : nil;
  }
  NSRect lineRect = NSZeroRect;
  [client attributesForCharacterIndex:0 lineHeightRectangle:&lineRect];
  [PKCandidatePanel.shared show:content below:lineRect];
}

@end
