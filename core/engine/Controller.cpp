#include "Controller.h"

#include <algorithm>
#include <cstring>

#include "Models.h"
#include "Utf8.h"

namespace ProjectKey {

namespace {

std::u16string ToUtf16(const std::string& s) {
  std::u16string out;
  for (size_t i = 0; i < s.size();) {
    char32_t cp = NextCodePoint(s, i);
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out += static_cast<char16_t>(0xd800 + (cp >> 10));
      out += static_cast<char16_t>(0xdc00 + (cp & 0x3ff));
    } else {
      out += static_cast<char16_t>(cp);
    }
  }
  return out;
}

std::string ToUtf8(const std::u16string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    char32_t cp = s[i];
    if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
      cp = 0x10000 + ((cp - 0xd800) << 10) + (s[++i] - 0xdc00);
    }
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xc0 | cp >> 6);
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xe0 | cp >> 12);
      out += static_cast<char>(0x80 | (cp >> 6 & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
      out += static_cast<char>(0xf0 | cp >> 18);
      out += static_cast<char>(0x80 | (cp >> 12 & 0x3f));
      out += static_cast<char>(0x80 | (cp >> 6 & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    }
  }
  return out;
}

std::string Lowercase(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

// The single ASCII character a key types, or 0.
char SingleCharacter(const KeyEvent& event) {
  return event.characters.size() == 1 && static_cast<unsigned char>(event.characters[0]) < 128 ? event.characters[0] : 0;
}

}  // namespace

uint32_t Utf16Length(const std::string& utf8) {
  return static_cast<uint32_t>(ToUtf16(utf8).size());
}

size_t Utf8Bytes(const std::string& utf8, uint32_t units) {
  size_t i = 0;
  for (uint32_t n = 0; i < utf8.size() && n < units;) {
    char32_t cp = NextCodePoint(utf8, i);
    n += cp >= 0x10000 ? 2 : 1;
  }
  return i;
}

int Settings::DefaultValue(Setting setting) {
  switch (setting) {
    case Setting::kProofPreview:
    case Setting::kChoosingProof:
      return 1;  // 有可疑字時
    case Setting::kAppearance:
      return 1;  // Acrylic; macOS registers its own default
    case Setting::kCapsLockToggle:
    case Setting::kAbbreviated:
    case Setting::kKeypadPicks:
    case Setting::kCandidatePosition:
    case Setting::kProofPreviewLastOn:
    case Setting::kChoosingProofLastOn:
      return 0;
    default:
      return 1;  // every switch is on by default
  }
}

const char* Settings::Name(Setting setting) {
  switch (setting) {
    case Setting::kProofPreview: return "ProofPreview";
    case Setting::kChoosingProof: return "ChoosingProof";
    case Setting::kProofPreviewLastOn: return "ProofPreviewLastOn";
    case Setting::kChoosingProofLastOn: return "ChoosingProofLastOn";
    case Setting::kAbbreviated: return "Abbreviated";
    case Setting::kRememberChoices: return "RememberChoices";
    case Setting::kShiftToggle: return "ShiftToggle";
    case Setting::kCapsLockToggle: return "CapsLockToggle";
    case Setting::kEnglishToZhuyin: return "EnglishToZhuyin";
    case Setting::kZhuyinToEnglish: return "ZhuyinToEnglish";
    case Setting::kAppearance: return "Appearance";
    case Setting::kCandidatePosition: return "CandidatePosition";
    case Setting::kNextDoubtful: return "NextDoubtful";
    case Setting::kKeypadPicks: return "KeypadPicks";
  }
  return "";
}

Controller::Controller(Environment& environment, Client& client, bool personal)
    : env_(environment), personal_(personal), client_(client) {
  makeComposer();
}

// Settings take effect on the next composition.
void Controller::makeComposer() {
  if (!composer_) replacing_ = 0;
  bool abbreviated = env_.settings->flag(Setting::kAbbreviated);
  bool rememberChoices = env_.settings->flag(Setting::kRememberChoices);
  ReloadUserPhrases(env_);
  // The composer keeps the words it looked up, so new user phrases need a new one.
  if (composer_ && abbreviated == abbreviated_ && rememberChoices == rememberChoices_ && env_.models == models_ &&
      env_.userPhrasesVersion == userPhrasesVersion_) {
    return;
  }
  userPhrasesVersion_ = env_.userPhrasesVersion;
  abbreviated_ = abbreviated;
  rememberChoices_ = rememberChoices;
  models_ = env_.models;
  composer_ = std::make_unique<Composer>(env_.lm, env_.bigram, rememberChoices ? env_.userModel : nullptr,
                                         Composer::kContextWeight, Composer::kTrigramWeight, env_.chars, Composer::kCharWeight,
                                         rememberChoices && personal_ ? env_.personal : nullptr, abbreviated);
}

void ClearChoices(Environment& env) {
  *env.userModel = *NewUserModel();
  if (!env.personal) return;
  env.personal->clear();
  if (!env.personalPath.empty()) env.personal->save(env.personalPath);
}

namespace {

// Paths come and go as UTF-8; a plain std::string would be read in the ANSI code page on Windows.
std::filesystem::path Path(const std::string& utf8) {
  return std::u8string(utf8.begin(), utf8.end());
}

std::string Utf8(const std::filesystem::path& path) {
  std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

}  // namespace

bool UseNewestModels(Environment& env, const std::string& bundled, const std::string& downloaded) {
  std::filesystem::path dir = ModelsDirectory(Path(bundled), Path(downloaded));
  auto lm = std::make_shared<Lexicon>();
  lm->loadLanguageModel(Utf8(dir / "data.txt").c_str());
  if (!lm->isDataModelLoaded()) return false;
  auto bigram = std::make_shared<Bigram>();
  auto chars = std::make_shared<CharModel>();
  env.lm = lm;
  env.bigram = bigram->load(Utf8(dir / "bigram.bin")) ? bigram : nullptr;
  env.chars = chars->load(Utf8(dir / "chars.bin")) ? chars : nullptr;
  ++env.models;
  env.userPhrasesTime.reset();  // the new lexicon has none yet
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(Path(downloaded), error)) {
    // Best effort: Windows keeps a release open while a composer still maps it; the next start removes it.
    if (entry.path() != dir && entry.path().extension() != ".partial") std::filesystem::remove_all(entry.path(), error);
  }
  return true;
}

void ReloadUserPhrases(Environment& env) {
  if (env.userPhrasesPath.empty()) return;
  std::error_code error;
  // A UTF-8 path, which Windows' narrow file calls would read in the ANSI code page.
  std::filesystem::path path(std::u8string(env.userPhrasesPath.begin(), env.userPhrasesPath.end()));
  auto written = std::filesystem::last_write_time(path, error);
  std::optional<std::filesystem::file_time_type> time;
  if (!error) time = written;
  if (time == env.userPhrasesTime) return;
  env.userPhrasesTime = time;
  env.lm->loadUserPhrases(env.userPhrasesPath);
  env.userPhrasesVersion++;
}

void Controller::learn(const std::string& committed) {
  if (!rememberChoices_ || !personal_ || !env_.personal) return;
  env_.personal->learn(committed);
  // shortcut: rewrites the whole file (at most PersonalModel::kMaxTriples lines) on every commit;
  // batch the writes if that ever shows up in typing latency.
  if (!env_.personalPath.empty()) env_.personal->save(env_.personalPath);
}

void Controller::deactivate() {
  forgetEnglishRun();
  commit();
}

void Controller::reset() {
  composer_->clear();
  choosing_ = false;
  zhuyinKeys_.clear();
  replacing_ = 0;
  hidePanel();
}

// MARK: Proof preview setting

// While choosing, the candidate window's own setting applies; otherwise the bubble's.
Setting Controller::proofKey() const {
  return choosing_ ? Setting::kChoosingProof : Setting::kProofPreview;
}

Controller::ProofMode Controller::proofMode() const {
  return static_cast<ProofMode>(env_.settings->get(proofKey()));
}

void Controller::toggleProof() {
  Setting lastOnKey = choosing_ ? Setting::kChoosingProofLastOn : Setting::kProofPreviewLastOn;
  ProofMode mode = proofMode();
  if (mode != kProofOff) env_.settings->set(lastOnKey, mode);
  // Never toggled before: back on means 永遠, since 有可疑字時 may still show nothing.
  std::optional<int> lastOn = env_.settings->stored(lastOnKey);
  int on = lastOn ? *lastOn : kProofAlways;
  env_.settings->set(proofKey(), mode == kProofOff ? on : kProofOff);
}

bool Controller::proofVisible() const {
  if (composer_->isEmpty()) return false;
  switch (proofMode()) {
    case kProofAlways:
      return true;
    case kProofWhenDoubtful:
      for (const Segment& segment : composer_->segments()) {
        if (segment.doubtful) return true;
      }
      return false;
    case kProofOff:
      return false;
  }
  return false;
}

// MARK: Keys

void Controller::switchMode(bool english) {
  env_.englishMode = english;
  client_.showIndicator(english ? "A" : "ㄅ");
}

void Controller::handleCapsLock() {
  // Each Caps Lock press switches 中/英, like the system's own Caps Lock switch.
  if (!env_.settings->flag(Setting::kCapsLockToggle)) {
    shiftAlone_ = false;
    return;
  }
  shiftAlone_ = false;
  commit();
  forgetEnglishRun();
  switchMode(!env_.englishMode);
}

void Controller::handleShift(bool down) {
  // A Shift press and release with no key in between switches 中/英.
  if (!env_.settings->flag(Setting::kShiftToggle)) {
    shiftAlone_ = false;
  } else if (down) {
    shiftAlone_ = true;
  } else if (shiftAlone_) {
    shiftAlone_ = false;
    toggleMode();
  }
}

void Controller::handleOtherModifier() {
  shiftAlone_ = false;
}

void Controller::toggleMode() {
  commit();
  forgetEnglishRun();
  switchMode(!env_.englishMode);
}

bool Controller::handleKey(const KeyEvent& original) {
  KeyEvent event = original;
  shiftAlone_ = false;
  typed_ = false;
  if (composer_->isEmpty()) {
    makeComposer();
    // With both switches turned off there is no way back to 中.
    if (env_.englishMode && !env_.settings->flag(Setting::kShiftToggle) && !env_.settings->flag(Setting::kCapsLockToggle)) {
      env_.englishMode = false;
    }
  }
  // Caps Lock is the switch, so the lock it leaves on does not capitalize: the
  // key acts as its lowercase letter (Shift still capitalizes).
  std::string lowered = Lowercase(event.characters);
  bool capsLetter = event.capsLock && !event.shift && env_.settings->flag(Setting::kCapsLockToggle) && lowered != event.characters;
  if (capsLetter) {
    event.capsLock = false;
    event.characters = lowered;
  }

  if (event.control || event.alt || event.win) {
    forgetEnglishRun();
    bool proofToggle = event.control && event.shift && event.isP;
    if (proofToggle && !composer_->isEmpty()) {
      toggleProof();
      refresh();
      return true;
    }
    commit();  // let the shortcut act on committed text
    return false;
  }
  bool plainTab = event.key == Key::kTab && !event.shift;
  if (env_.englishMode) {
    if (plainTab && convertEnglishRun()) return true;
    trackEnglish(event);
    // Passed through, the app would capitalize it; insert the lowercase letter instead.
    if (capsLetter) client_.insertText(lowered);
    showEnglishHint();
    return capsLetter;
  }
  if (plainTab && convertZhuyinKeys()) return true;
  trackZhuyin(event);
  return choosing_ ? handleChoosing(event) : handleComposing(event);
}

// MARK: English typed as Zhuyin

void Controller::forgetEnglishRun() {
  englishRun_.clear();
  if (panel_ && !panel_->keyHint.empty()) hidePanel();
}

void Controller::trackEnglish(const KeyEvent& event) {
  char c = SingleCharacter(event);
  if (event.key == Key::kBackspace) {
    if (!englishRun_.empty()) englishRun_.pop_back();
  } else if (!event.shift && (c == ' ' || (c && composer_->isReadingKey(c)))) {
    englishRun_ += event.characters;
  } else {
    englishRun_.clear();
  }
}

// The keys at the end of the English run that are whole Zhuyin syllables, and
// how many spaces follow them; empty when none are.
std::string Controller::zhuyinAtEnd(size_t* trailingSpaces) const {
  const std::string& run = englishRun_;
  size_t spaces = 0;
  while (spaces < run.size() && run[run.size() - 1 - spaces] == ' ') spaces++;
  if (trailingSpaces) *trailingSpaces = spaces;
  // Syllables end at a tone key, or at a space for the first tone (the last may have none); walk back
  // from the end while they are whole.
  std::string body = run.substr(0, run.size() - spaces);
  size_t start = body.size();
  while (start > 0) {
    size_t begin = start - 1;
    while (begin > 0 && !std::strchr(" 3467", body[begin - 1])) begin--;
    if (!composer_->isSyllableKeys(body.substr(begin, start - begin))) break;
    start = begin;
  }
  return body.substr(start);
}

void Controller::typeKeys(const std::string& keys) {
  for (char key : keys) {
    if (key == ' ') {
      composer_->completeWithFirstTone();
    } else {
      composer_->typeReadingKey(key);
    }
  }
}

// While English mode passes through Zhuyin, shows what Tab would turn
// it into. Only once a tone key is in it: plain words like "so " can happen to
// spell a first-tone syllable.
void Controller::showEnglishHint() {
  size_t trailingSpaces = 0;
  std::string keys = env_.settings->flag(Setting::kEnglishToZhuyin) ? zhuyinAtEnd(&trailingSpaces) : "";
  if (keys.find_first_of("3467") == std::string::npos) {
    if (panel_ && !panel_->keyHint.empty()) hidePanel();
    return;
  }
  typeKeys(keys);
  PanelContent content;
  content.segments = {composer_->text()};
  content.doubtful = {false};
  content.caret = Utf16Length(content.segments[0]);
  content.showsProof = true;
  content.keyHint = env_.tabKeycap;
  content.replacedText = keys + std::string(trailingSpaces, ' ');  // the English that goes
  composer_->clear();
  showPanel(content);
}

// MARK: Zhuyin that is English

void Controller::trackZhuyin(const KeyEvent& event) {
  if (composer_->isEmpty()) {
    zhuyinKeys_.clear();
    zhuyinEdited_ = false;
  }
  char c = SingleCharacter(event);
  bool typing = !choosing_ && !event.shift &&
                (event.key == Key::kSpace ? composer_->hasPendingReading() : c && composer_->isReadingKey(c));
  // Opening the candidates and moving among them changes nothing; any other key
  // (Backspace, arrows in the text, picking) edits the composition apart from the keys.
  bool browsing = false;
  switch (event.key) {
    case Key::kDown:
    case Key::kTab:
    case Key::kSpace:  // opens the candidates as Down does; with a reading pending it types
      browsing = choosing_ || composer_->pendingReading().empty();
      break;
    case Key::kUp:
    case Key::kLeft:
    case Key::kRight:
    case Key::kPageUp:
    case Key::kPageDown:
    case Key::kEscape:
    case Key::kBackspace:  // while choosing it only closes the candidates, as Escape does
      browsing = choosing_;
      break;
    default:
      break;
  }
  if (typing) {
    zhuyinKeys_ += c;
  } else if (!browsing) {
    zhuyinEdited_ = true;
  }
}

// The keys behind the composition as English when they spell words that are
// not themselves Zhuyin syllables; empty otherwise.
std::optional<std::string> Controller::englishTyped() const {
  if (zhuyinEdited_ || !env_.settings->flag(Setting::kZhuyinToEnglish)) return std::nullopt;
  const std::string& keys = zhuyinKeys_;
  for (char c : keys) {
    if (c != ' ' && (c < 'a' || c > 'z')) return std::nullopt;
  }
  size_t first = keys.find_first_not_of(' '), last = keys.find_last_not_of(' ');
  if (first == std::string::npos || last - first + 1 < 2) return std::nullopt;
  // "wu " (ㄊㄧ, first tone) or "su" (ㄋㄧ, no tone) is a syllable as well as a spelling the checker allows.
  bool syllables = true;
  for (size_t start = 0; start <= keys.size();) {
    size_t end = keys.find(' ', start);
    if (end == std::string::npos) end = keys.size();
    std::string word = keys.substr(start, end - start);
    if (!word.empty() && !composer_->isSyllableKeys(word + " ") && !composer_->isSyllableKeys(word)) syllables = false;
    start = end + 1;
  }
  if (syllables) return std::nullopt;
  if (!env_.isEnglish || !env_.isEnglish(keys)) return std::nullopt;
  return keys;
}

// Replaces a composition typed as English keys with that English and switches to 英.
bool Controller::convertZhuyinKeys() {
  std::optional<std::string> english = composer_->isEmpty() || choosing_ ? std::nullopt : englishTyped();
  if (!english) return false;
  client_.insertText(*english);  // takes the composition's place
  composer_->clear();
  zhuyinKeys_.clear();
  hidePanel();
  switchMode(true);
  return true;
}

// Replaces the end of the English run that is whole Zhuyin syllables with a
// composition of them and switches to 中. False when no tone key is there.
bool Controller::convertEnglishRun() {
  size_t trailingSpaces = 0;
  std::string keys = env_.settings->flag(Setting::kEnglishToZhuyin) ? zhuyinAtEnd(&trailingSpaces) : "";
  englishRun_.clear();
  // Only what the preview shows: after plain words like "so ", Tab is a Tab.
  if (keys.find_first_of("3467") == std::string::npos) return false;
  // The keys must still be right before the caret: the app may have moved it.
  std::string typed = keys + std::string(trailingSpaces, ' ');
  std::string before = client_.textBeforeCaret();
  if (before.size() < typed.size() || before.compare(before.size() - typed.size(), typed.size(), typed) != 0) return false;
  // The composition takes the English's place.
  replacing_ = static_cast<uint32_t>(typed.size());
  typeKeys(keys);
  // The keys of an earlier composition, ended by a commit that kept them, are not behind this one.
  zhuyinKeys_.clear();
  zhuyinEdited_ = true;
  env_.englishMode = false;
  refresh();
  client_.showIndicator("ㄅ");
  return true;
}

bool Controller::handleComposing(const KeyEvent& event) {
  bool empty = composer_->isEmpty();
  switch (event.key) {
    case Key::kSpace:
      if (composer_->hasPendingReading()) {
        if (composer_->completeWithFirstTone() == Composer::KeyResult::kInvalid) client_.beep();
        typed_ = true;
        refresh();
        return true;
      }
      if (empty) return false;
      startChoosingAt(defaultSegment());
      return true;
    case Key::kDown:
      if (empty) return false;
      if (composer_->pendingReading().empty()) startChoosingAt(openSegment().value_or(defaultSegment()));
      return true;
    case Key::kTab: {
      // The sentence as the proof row has it: guesses for syllables still shown as Zhuyin. Shift+Tab
      // when Tab offers English instead.
      if (empty) return false;
      composer_->endOpenSyllable();  // 為 for ㄨㄟ typed last without a tone, where 簡拼 is off
      std::string sentence = composer_->text() + composer_->pendingReading();
      client_.insertText(sentence);
      learn(sentence);
      composer_->clear();
      commit();
      return true;
    }
    case Key::kReturn:
      if (empty) return false;
      // The text as shown, except that a syllable still open without a tone goes as its
      // Zhuyin, which is a candidate of its own; Zhuyin that is no word yet goes as it is.
      composer_->showSymbols();
      refresh();
      client_.insertText(shown_);
      learn(shown_);
      composer_->clear();
      commit();
      return true;
    case Key::kBackspace:
      if (empty) return false;
      composer_->backspace();
      typed_ = !cursorMoved_;
      refresh();
      return true;
    case Key::kEscape:
      if (empty) return false;
      composer_->hasPendingReading() ? composer_->clearPendingReading() : composer_->clear();
      refresh();
      return true;
    case Key::kLeft:
    case Key::kRight:
    case Key::kHome:
    case Key::kEnd:
      if (empty) return false;
      // The cursor moves between whole syllables, so finish typing one first.
      if (composer_->pendingReading().empty()) {
        size_t cursor = composer_->cursor();
        switch (event.key) {
          case Key::kLeft: cursor = cursor > 0 ? cursor - 1 : 0; break;
          case Key::kRight: cursor++; break;
          case Key::kHome: cursor = 0; break;
          default: cursor = composer_->readingCount(); break;
        }
        composer_->setCursor(cursor);
        cursorMoved_ = true;
        refresh();
      }
      return true;
    case Key::kForwardDelete:
      if (empty) return false;
      if (composer_->pendingReading().empty() && composer_->deleteForward()) refresh();
      return true;
    case Key::kUp:
    case Key::kPageUp:
    case Key::kPageDown:
      return !empty;  // keep the app's caret still while composing
    default:
      break;
  }

  char key = SingleCharacter(event);
  if (key < 0x20 || key > 0x7e) return !empty;
  cursorMoved_ = false;
  typed_ = true;
  // The keypad's 1-6 pick from the row shown while typing, as its digits say; typing goes on.
  if (event.numpad && key >= '1' && key <= '6' && rowSegment_ && env_.settings->flag(Setting::kKeypadPicks)) {
    startChoosingAt(*rowSegment_);
    int index = key - '1';
    if (choosing_ && index < rowEnd(0)) {
      pick(index, /*moveOn=*/false);
    } else {
      client_.beep();
      choosing_ = false;
      refresh();
    }
    return true;
  }
  if (!event.shift && composer_->isReadingKey(key)) {
    if (empty) composer_->setTextBefore(client_.textBeforeCaret());
    if (composer_->typeReadingKey(key) == Composer::KeyResult::kInvalid) client_.beep();
    refresh();
    return true;
  }
  // Apple's Zhuyin keyboard prints 、 on the \ key; McBopomofo's table has it on '.
  if (key == '\\' && !event.shift) key = '\'';
  if (composer_->insertPunctuation(key)) {
    refresh();
    return true;
  }
  if (empty) return false;
  // Commit, then type the character after it.
  commit();
  if (env_.appTypesUnhandledKeys) return false;
  client_.insertText(event.characters);
  return true;
}

int Controller::rowOf(int index) const {
  return static_cast<int>(std::upper_bound(rowStarts_.begin(), rowStarts_.end(), index) - rowStarts_.begin()) - 1;
}

int Controller::rowEnd(int row) const {
  return row + 1 < static_cast<int>(rowStarts_.size()) ? rowStarts_[row + 1] : static_cast<int>(candidates_.size());
}

// The candidate of `row` under `column`, or the row's last one when it ends earlier.
int Controller::indexInRow(int row, int column) const {
  int index = rowStarts_[row];
  while (index + 1 < rowEnd(row) && columns_[index + 1] <= column) index++;
  return index;
}

bool Controller::handleChoosing(const KeyEvent& event) {
  int count = static_cast<int>(candidates_.size());
  int row = rowOf(selection_), rows = static_cast<int>(rowStarts_.size());
  int column = columns_[selection_];
  switch (event.key) {
    case Key::kLeft:
      selection_ = std::max(0, selection_ - 1);
      break;
    case Key::kRight:
      selection_ = std::min(count - 1, selection_ + 1);
      break;
    case Key::kDown:
      // Down opens the grid and moves down in the same column.
      if (row + 1 < rows) {
        expanded_ = true;
        selection_ = indexInRow(row + 1, column);
      }
      break;
    case Key::kUp:
      if (expanded_ && row > 0) {
        selection_ = indexInRow(row - 1, column);
      } else if (expanded_) {
        expanded_ = false;  // Up from the grid's first row folds it back
      } else if (row > 0) {
        selection_ = rowStarts_[row - 1];
      } else {
        choosing_ = false;  // past the first row, Up returns to the sentence
      }
      break;
    case Key::kSpace:
    case Key::kPageDown:
      if (expanded_) {
        selection_ = indexInRow(std::min(rows - 1, row + kExpandedRows), column);
      } else {
        selection_ = rowStarts_[(row + 1) % rows];
      }
      break;
    case Key::kPageUp:
      if (expanded_) {
        selection_ = indexInRow(std::max(0, row - kExpandedRows), column);
      } else {
        selection_ = rowStarts_[std::max(row - 1, 0)];  // wrapping back lands on the rarest row
      }
      break;
    case Key::kReturn:
      pick(selection_);
      return true;
    case Key::kTab: {
      std::optional<size_t> next = doubtfulAfter(activeStart_, event.shift);
      if (next) startChoosingAt(*next);
      return true;
    }
    case Key::kEscape:
      if (expanded_) {
        expanded_ = false;
      } else {
        choosing_ = false;
      }
      break;
    case Key::kBackspace:
      choosing_ = false;
      break;
    default: {
      char c = SingleCharacter(event);
      if (c >= '1' && c < '1' + kGridColumns) {
        int index = rowStarts_[row] + (c - '1');  // the selection's row carries the labels
        if (index < rowEnd(row)) pick(index);
        return true;
      }
      // Any other typing leaves the choice as it is and keeps composing.
      choosing_ = false;
      return handleComposing(event);
    }
  }
  refresh();
  return true;
}

// MARK: Choosing

// Before the cursor (macOS): the word ending at or holding the syllable before the cursor, the
// first word at the start. After it (Windows): the segment (or the rest of the phrase) right
// after the cursor, or, with the cursor at the end, the first doubtful segment so Space goes
// straight to the likely mistake; moved to the end by the user, the last character.
size_t Controller::defaultSegment() const {
  size_t cursor = composer_->cursor();
  if (env_.settings->get(Setting::kCandidatePosition) == 1) {
    for (const Segment& segment : composer_->segments()) {
      if (cursor <= segment.start + segment.length) return segment.start;
    }
    return composer_->segments().back().start;
  }
  if (cursor < composer_->readingCount()) {
    for (const Segment& segment : composer_->segments()) {
      if (cursor < segment.start + segment.length) return std::max(cursor, segment.start);
    }
  }
  if (cursorMoved_) return composer_->readingCount() - 1;
  for (const Segment& segment : composer_->segments()) {
    if (segment.doubtful) return segment.start;
  }
  return composer_->segments().back().start;
}

std::optional<size_t> Controller::openSegment() const {
  if (composer_->shownReading().empty()) return std::nullopt;
  return segmentBeforeCursor();
}

std::optional<size_t> Controller::segmentBeforeCursor() const {
  if (composer_->cursor() == 0) return std::nullopt;
  size_t before = composer_->cursor() - 1;
  for (const Segment& segment : composer_->segments()) {
    if (before >= segment.start && before < segment.start + segment.length) return segment.start;
  }
  return std::nullopt;
}

int Controller::span(const std::string& candidate) const {
  return std::clamp(env_.columnsSpannedBy ? env_.columnsSpannedBy(candidate) : 1, 1, kGridColumns);
}

size_t Controller::firstRowSize(const std::vector<std::string>& words) const {
  size_t count = 0;
  for (int used = 0; count < words.size() && used + span(words[count]) <= kGridColumns; count++) used += span(words[count]);
  return count;
}

// The next doubtful segment in reading order, wrapping around, excluding `start`.
std::optional<size_t> Controller::doubtfulAfter(size_t start, bool backwards) const {
  std::vector<size_t> doubtful;
  for (const Segment& segment : composer_->segments()) {
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

bool Controller::choosesFromEnd() const {
  return env_.settings->get(Setting::kCandidatePosition) == 1;
}

std::vector<std::string> Controller::choicesFor(const Segment& segment, int* readingIndex) const {
  std::vector<std::string> candidates = composer_->candidates(segment, choosesFromEnd());
  // The syllables' Zhuyin itself, as the system keyboards offer it: last in the first row when
  // all six columns hold one candidate each (iOS) and it fits that column, otherwise right after
  // the first row, which keeps the candidates' digits and leaves no column empty.
  *readingIndex = -1;
  if (candidates.empty() || segment.reading.rfind("_", 0) == 0) return candidates;  // punctuation readings start with _
  std::string zhuyin;
  for (char c : segment.reading) {
    if (c != '-' && c != Composer::kAnyTone) zhuyin += c;  // the grid's separator
  }
  if (std::find(candidates.begin(), candidates.end(), zhuyin) == candidates.end()) {
    int firstRow = static_cast<int>(firstRowSize(candidates));
    *readingIndex = firstRow < kGridColumns || span(zhuyin) > 1 ? firstRow : kGridColumns - 1;
    candidates.insert(candidates.begin() + *readingIndex, zhuyin);
  }
  return candidates;
}

void Controller::pack(const std::vector<std::string>& words, std::vector<int>* rowStarts, std::vector<int>* columns) const {
  rowStarts->clear();
  columns->clear();
  int column = kGridColumns;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    if (column + span(words[i]) > kGridColumns) {
      rowStarts->push_back(i);
      column = 0;
    }
    columns->push_back(column);
    column += span(words[i]);
  }
}

std::optional<Segment> Controller::activeSegment() const {
  std::optional<Segment> segment = composer_->segmentFrom(activeStart_);
  if (!segment || activeEnd_ >= segment->start + segment->length) return segment;
  size_t length = activeEnd_ - segment->start;
  size_t cut = 0;
  for (size_t i = 0; i < length; i++) cut = segment->reading.find('-', cut) + 1;  // the grid's separator
  // Its characters too when the phrase has one per syllable.
  std::u16string text = ToUtf16(segment->text);
  return Segment{text.size() == segment->length ? ToUtf8(text.substr(0, length)) : "", segment->reading.substr(0, cut - 1),
                 segment->start, length, false};
}

void Controller::startChoosingAt(size_t start) {
  activeStart_ = start;
  activeEnd_ = SIZE_MAX;
  std::optional<Segment> whole = composer_->segmentFrom(start);
  size_t cursor = composer_->cursor();
  // Choosing the word before the cursor, with the cursor inside a phrase, is choosing the
  // phrase's part before it: 打 of 打|字.
  if (choosesFromEnd() && whole && start < cursor && cursor < whole->start + whole->length) activeEnd_ = cursor;
  std::optional<Segment> segment = activeSegment();
  if (!segment) return;
  std::vector<std::string> candidates = choicesFor(*segment, &readingIndex_);
  if (candidates.empty()) return;
  choosing_ = true;
  expanded_ = false;
  firstRow_ = 0;
  candidates_ = std::move(candidates);
  pack(candidates_, &rowStarts_, &columns_);
  auto current = std::find(candidates_.begin(), candidates_.end(), segment->text);
  selection_ = current == candidates_.end() ? 0 : static_cast<int>(current - candidates_.begin());
  refresh();
}

void Controller::choose(int index) {
  if (choosing_ && index >= 0 && index < static_cast<int>(candidates_.size())) pick(index);
}

void Controller::pick(int index, bool moveOn) {
  zhuyinEdited_ = true;  // also when picked by a click, which passes no key
  std::optional<Segment> segment = activeSegment();
  if (segment && index == readingIndex_) {
    // The grid holds only words, so the Zhuyin goes in with the rest of the composition, ending it.
    std::string before, after;
    for (const Segment& other : composer_->segments()) {
      if (other.start + other.length <= segment->start) {
        before += other.text;
      } else if (other.start >= segment->start + segment->length) {
        after += other.text;
      } else {
        // A phrase the chosen part is inside keeps its characters before and after that part.
        // shortcut: a phrase without one character per syllable drops them; such phrases are rare.
        std::u16string text = ToUtf16(other.text);
        if (text.size() != other.length) continue;
        size_t end = segment->start + segment->length;
        if (other.start < segment->start) before += ToUtf8(text.substr(0, segment->start - other.start));
        if (other.start + other.length > end) after += ToUtf8(text.substr(end - other.start));
      }
    }
    client_.insertText(before + candidates_[index] + after);
    learn(before + candidates_[index] + after);
    composer_->clear();
    choosing_ = false;
    hidePanel();
    return;
  }
  if (segment) composer_->choose(*segment, candidates_[index], choosesFromEnd());
  std::optional<size_t> next;
  if (moveOn && env_.settings->flag(Setting::kNextDoubtful)) next = doubtfulAfter(activeStart_, false);
  if (next) {
    startChoosingAt(*next);
  } else {
    choosing_ = false;
    refresh();
  }
}

// MARK: Output

void Controller::showPanel(const PanelContent& content) {
  panel_ = content;
  client_.showPanel(content);
}

void Controller::hidePanel() {
  client_.hideHint();
  if (!panel_) return;
  panel_.reset();
  client_.hidePanel();
}

void Controller::commit() {
  if (!composer_->isEmpty()) {
    // A pending reading alone commits nothing but still ends the composition.
    client_.insertText(composer_->text());
    learn(composer_->text());
    composer_->clear();
  }
  choosing_ = false;
  hidePanel();
}

void Controller::refresh() {
  rowSegment_.reset();
  if (composer_->isEmpty()) {
    choosing_ = false;
    client_.setComposition(Composition{});
    hidePanel();
    return;
  }

  PanelContent content;
  std::u16string marked;
  bool hasActive = false;
  uint32_t activeStart = 0, activeLength = 0;
  size_t cursor = composer_->cursor();
  uint32_t caret = 0;
  // The syllable still open without a tone shows as its Zhuyin, so what is being typed
  // stays visible (and Enter sends it so); its word shows once the syllable closes, and
  // in the candidates. Not while choosing, where the text is what is chosen.
  std::u16string open = choosing_ ? u"" : ToUtf16(composer_->shownReading());
  uint32_t openGrowth = 0;
  // That word is a guess, so the proof row shows it marked in the sentence. With no syllable
  // open, the word just typed takes its place, so the panel stays up from one syllable to the
  // next instead of fading out at each tone.
  std::optional<size_t> typing = choosing_ ? std::nullopt : openSegment();
  if (!typing && !choosing_ && typed_) typing = segmentBeforeCursor();
  for (const Segment& segment : composer_->segments()) {
    std::u16string text = ToUtf16(segment.text);
    uint32_t length = static_cast<uint32_t>(text.size());
    size_t at = cursor - 1 - segment.start;  // the open syllable's place in this segment
    bool spliced = !open.empty() && cursor > segment.start && at < segment.length && length == segment.length;
    if (cursor >= segment.start + segment.length) {
      caret += length;
    } else if (cursor > segment.start) {
      // Inside a phrase: one character per syllable, except for odd phrases.
      caret += length == segment.length ? static_cast<uint32_t>(cursor - segment.start) : length;
    }
    if (choosing_ && activeStart_ >= segment.start && activeStart_ < segment.start + segment.length) {
      content.activeSegment = static_cast<int>(content.segments.size());
      hasActive = true;
      activeStart = static_cast<uint32_t>(marked.size());
      activeLength = length;
      // Only the part before the cursor, when that is what is chosen (one character per syllable).
      if (activeEnd_ < segment.start + segment.length && length == segment.length) {
        activeLength = static_cast<uint32_t>(activeEnd_ - segment.start);
      }
    }
    if (typing && segment.start == *typing) content.activeSegment = static_cast<int>(content.segments.size());
    content.segments.push_back(segment.text);
    content.doubtful.push_back(segment.doubtful);
    if (spliced) {
      text.replace(at, 1, open);
      openGrowth = static_cast<uint32_t>(open.size()) - 1;
      caret += openGrowth;  // the open syllable is always before the cursor
    }
    marked += text;
  }
  std::u16string reading = ToUtf16(composer_->pendingReading());
  marked.insert(caret, reading);
  caret += static_cast<uint32_t>(reading.size());
  if (hasActive && activeStart >= caret - reading.size()) activeStart += static_cast<uint32_t>(reading.size());
  std::string markedText = ToUtf8(marked);
  shown_ = markedText;
  client_.setComposition(Composition{markedText, caret, activeStart, hasActive ? activeLength : 0, replacing_});
  replacing_ = 0;

  // The words there in a row without digits (selectedRow -1): those keys still type
  // Zhuyin. selection marks the guess, which the renderers draw brighter than the rest.
  std::vector<std::string> guesses;
  int guessAt = -1;
  std::string guessPage;
  if (typing) {
    std::optional<Segment> segment = composer_->segmentFrom(*typing);
    if (segment && segment->reading[0] != '_') {  // punctuation keeps the proof row alone
      // The grid's first row, as choosing shows it, so a digit means the same word in both.
      int readingIndex;
      std::vector<std::string> all = choicesFor(*segment, &readingIndex);
      guesses.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(firstRowSize(all)));
      std::vector<int> rowStarts, columns;
      pack(all, &rowStarts, &columns);
      // Folded as when choosing: with more rows, the page label (drawn as a down chevron).
      if (rowStarts.size() > 1) guessPage = "1/" + std::to_string(rowStarts.size());
      auto at = std::find(guesses.begin(), guesses.end(), segment->text);
      if (at != guesses.end()) guessAt = static_cast<int>(at - guesses.begin());
    }
  }
  rowSegment_ = guesses.empty() ? std::nullopt : typing;
  auto showRow = [&](PanelContent& panel) {
    if (guesses.empty()) return;
    panel.rows = {guesses};
    panel.selectedRow = -1;
    panel.selection = guessAt;
    panel.pageLabel = guessPage;
  };

  std::optional<PanelContent> hint;  // what Tab would turn the keys into; ja is English and ㄨㄇ alike
  if (std::optional<std::string> english = choosing_ ? std::nullopt : englishTyped()) {
    hint.emplace();
    hint->segments = {*english};
    hint->doubtful = {false};
    hint->caret = Utf16Length(*english);
    hint->showsProof = true;
    hint->keyHint = env_.tabKeycap;
    hint->replacedText = markedText;
  }
  // Beside the panel, which goes on as without it; otherwise in the panel's proof row.
  bool hintApart = hint && typing && env_.hintWindow;
  if (!hintApart) client_.hideHint();
  if (hint && !hintApart) {
    showRow(*hint);
    showPanel(*hint);
    return;
  }
  // While typing, the row of guesses stands alone, as the grid does once chosen from; the
  // proof row joins it as the setting says (by default when something is doubtful).
  bool showsProof = proofVisible();
  if (!choosing_ && !showsProof && guesses.empty()) {
    hidePanel();
    return;
  }
  content.pendingReading = ToUtf8(reading);
  content.caret = caret - static_cast<uint32_t>(reading.size()) - openGrowth;
  content.showsProof = showsProof;
  if (showsProof && !choosing_) content.keyHint = hintApart ? env_.shiftTabKeycap : env_.tabKeycap;  // Tab sends it
  if (choosing_) {
    int row = rowOf(selection_), rows = static_cast<int>(rowStarts_.size());
    // Collapsed: the selection's row. Expanded: up to kExpandedRows rows, scrolled to the selection.
    int first = row, shown = 1;
    if (expanded_) {
      firstRow_ = std::max(std::min(firstRow_, row), row - kExpandedRows + 1);
      first = firstRow_;
      shown = std::min(kExpandedRows, rows - first);
    }
    for (int r = first; r < first + shown; r++) {
      std::vector<std::string> cells;
      for (int i = rowStarts_[r]; i < rowEnd(r); i++) cells.push_back(candidates_[i]);
      content.rows.push_back(std::move(cells));
    }
    content.selectedRow = row - first;
    content.selection = selection_ - rowStarts_[row];
    if (rows > 1) content.pageLabel = std::to_string(row + 1) + "/" + std::to_string(rows);
    content.expanded = expanded_;
  } else {
    showRow(content);
  }
  showPanel(content);
  if (hintApart) client_.showHint(*hint);  // placed by the panel just shown
}

}  // namespace ProjectKey
