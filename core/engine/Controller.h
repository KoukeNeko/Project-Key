// The input method's key handling, shared by both platforms: the macOS
// PKInputController and the Windows engine server each feed it key events and
// carry out what it does to the app through Client.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Composer.h"

namespace ProjectKey {

// Grid columns per candidate row, as on macOS.
constexpr int kGridColumns = 6;

// What the candidate window shows; mirrors the macOS PKPanelContent. Strings
// are UTF-8; caret and ranges count UTF-16 code units, as TSF does.
struct PanelContent {
  std::vector<std::string> segments;
  std::vector<bool> doubtful;   // per segment
  int activeSegment = -1;       // -1 when not choosing
  std::string pendingReading;
  uint32_t caret = 0;           // UTF-16 units of the joined segments before the cursor
  bool showsProof = false;
  std::string keyHint;          // a key drawn as a keycap at the end of the proof row; empty for none
  std::string replacedText;     // struck out before the proof text, with an arrow; empty for none
  // Visible rows of the candidate grid; empty hides the candidate area.
  std::vector<std::vector<std::string>> rows;
  int selectedRow = 0;          // only this row carries the 1-6 labels; -1 while typing, none does
  int selection = 0;            // index within the selected row; while typing, the guess in row 0 (-1 for none)
  std::string pageLabel;        // "1/3", empty for a single page
};

// The marked text in the document.
struct Composition {
  std::string text;             // empty ends the composition
  uint32_t caret = 0;           // UTF-16 units
  uint32_t activeStart = 0, activeLength = 0;  // the segment being chosen; length 0 for none
  uint32_t replaceBefore = 0;   // UTF-16 units before the caret this composition takes the place of
};

class Client {
 public:
  virtual ~Client() = default;
  // Inserts committed text in place of the composition, ending it.
  virtual void insertText(const std::string& text) = 0;
  virtual void setComposition(const Composition& composition) = 0;
  virtual void showPanel(const PanelContent& content) = 0;
  virtual void hidePanel() = 0;
  // The 中／英 badge under the caret.
  virtual void showIndicator(const std::string& label) = 0;
  virtual void beep() = 0;
  // Up to a few dozen characters before the caret; empty when unknown.
  virtual std::string textBeforeCaret() = 0;
  // The Tab hint in a window of its own beside the candidate window, on platforms that set
  // Environment::hintWindow; elsewhere the hint takes the panel's proof row.
  virtual void showHint(const PanelContent&) {}
  virtual void hideHint() {}
};

// Settings, stored by the platform (the registry on Windows). Same names as
// the macOS NSUserDefaults keys.
enum class Setting {
  kProofPreview,        // proof row while typing: 0 always, 1 when doubtful, 2 off
  kChoosingProof,       // proof row while choosing, same values
  kProofPreviewLastOn,  // the mode ⌃⇧P turns back on; absent until first toggled
  kChoosingProofLastOn,
  kAdjacentKeys,        // match readings one key off
  kRememberChoices,     // the user override model
  kShiftToggle,         // a lone Shift switches 中/英
  kCapsLockToggle,      // Caps Lock switches 中/英 and letters stay lowercase
  kEnglishToZhuyin,     // that Tab turns Zhuyin typed in 英 into a composition
  kZhuyinToEnglish,     // and English typed in 中 back into English
  kAppearance,          // candidate window: 0 dark, 1 Acrylic, 2 Mica, 3 Mica Alt
  kCandidatePosition,   // candidates for the word: 0 after the cursor (Windows), 1 before it (macOS)
  kNextDoubtful,        // picking a candidate moves on to the next doubtful word
  kKeyboardLayout,      // 0 Daqian Zhuyin, 1 Hanyu Pinyin
};

class Settings {
 public:
  virtual ~Settings() = default;
  virtual std::optional<int> stored(Setting setting) const = 0;
  virtual void set(Setting setting, int value) = 0;
  int get(Setting setting) const { return stored(setting).value_or(DefaultValue(setting)); }
  bool flag(Setting setting) const { return get(setting) != 0; }
  static int DefaultValue(Setting setting);
  static const char* Name(Setting setting);
};

// The remembered choices, empty, with McBopomofo's capacity and 1.5-hour half-life;
// every platform starts from it and clears to it.
inline std::shared_ptr<McBopomofo::UserOverrideModel> NewUserModel() {
  return std::make_shared<McBopomofo::UserOverrideModel>(500, 5400.0);
}

// What every session shares: the models, settings and the 中/英 switch.
struct Environment {
  std::shared_ptr<Lexicon> lm;
  std::shared_ptr<const Bigram> bigram;
  std::shared_ptr<const CharModel> chars;
  std::shared_ptr<McBopomofo::UserOverrideModel> userModel;  // replaced by value to clear it
  Settings* settings = nullptr;
  // Whether text (letters and spaces) is correctly spelled English.
  std::function<bool(const std::string&)> isEnglish;
  // Grid columns a candidate takes, 1 to kGridColumns.
  std::function<int(const std::string&)> columnsSpannedBy;
  bool englishMode = false;  // one switch for all sessions, like a keyboard's own layout toggle
  std::string tabKeycap = "Tab";  // the key the conversion previews end with
  // macOS lets the app type a key the input method returns; TSF does not
  // deliver a key once its test phase handed it on, so there the input method types it.
  bool appTypesUnhandledKeys = false;
  // While a syllable is open, the Tab hint goes to Client::showHint and the panel keeps
  // the guess in its proof row.
  bool hintWindow = false;
};

enum class Key {
  kCharacter,  // anything else; see KeyEvent::characters
  kSpace, kReturn, kTab, kBackspace, kEscape, kLeft, kRight, kUp, kDown,
  kHome, kEnd, kPageUp, kPageDown, kForwardDelete,
};

struct KeyEvent {
  Key key = Key::kCharacter;
  std::string characters;  // what the key types in a US layout with Shift and Caps Lock applied; empty for none
  bool shift = false, control = false, alt = false, win = false, capsLock = false;
  bool isP = false;  // the P key, for ⌃⇧P whatever it types
};

class Controller {
 public:
  Controller(Environment& environment, Client& client);

  // A key press; true when the input method takes it, false when the app should.
  bool handleKey(const KeyEvent& event);
  // Shift pressed or released: a press and release with no key in between switches 中/英.
  void handleShift(bool down);
  // Caps Lock pressed: each press switches 中/英.
  void handleCapsLock();
  // Another modifier (Control, Alt, Windows) pressed; it ends a lone Shift.
  void handleOtherModifier();
  // The 中/英 button in the taskbar: commits and switches modes.
  void toggleMode();
  // Focus left the document: commit what is composed.
  void deactivate();
  // The app ended the composition itself: forget it without committing.
  void reset();

  bool isComposing() const { return !composer_->isEmpty(); }
  // A syllable is still being typed: Space gives it tone 1.
  bool isTypingSyllable() const { return composer_->hasPendingReading(); }
  bool englishMode() const { return env_.englishMode; }
  // The candidate window's last content, if shown; for tests.
  const std::optional<PanelContent>& panel() const { return panel_; }
  // While choosing, every candidate, and picking one by its index there: for a
  // touch screen grid that shows them all, past the rows the panel holds.
  const std::vector<std::string>& candidates() const { return candidates_; }
  void choose(int index);

  static constexpr int kExpandedRows = 5;  // rows the expanded grid shows at once

 private:
  enum ProofMode { kProofAlways = 0, kProofWhenDoubtful = 1, kProofOff = 2 };

  void makeComposer();
  Setting proofKey() const;
  ProofMode proofMode() const;
  void toggleProof();
  bool proofVisible() const;

  void forgetEnglishRun();
  void trackEnglish(const KeyEvent& event);
  std::string zhuyinAtEnd(size_t* trailingSpaces) const;
  void typeKeys(const std::string& keys);
  void showEnglishHint();
  void trackZhuyin(const KeyEvent& event);
  std::optional<std::string> englishTyped() const;
  bool convertZhuyinKeys();
  bool convertEnglishRun();
  void switchMode(bool english);

  bool handleComposing(const KeyEvent& event);
  bool handleChoosing(const KeyEvent& event);
  int rowOf(int index) const;
  int rowEnd(int row) const;
  int indexInRow(int row, int column) const;
  size_t defaultSegment() const;
  // The segment holding the syllable still open without a tone, while its word shows.
  std::optional<size_t> openSegment() const;
  int span(const std::string& candidate) const;
  // How many of words, from the first, fit one grid row.
  size_t firstRowSize(const std::vector<std::string>& words) const;
  std::optional<size_t> doubtfulAfter(size_t start, bool backwards) const;
  void startChoosingAt(size_t start);
  void pick(int index);

  void commit();
  void refresh();
  void showPanel(const PanelContent& content);
  void hidePanel();

  Environment& env_;
  Client& client_;
  std::unique_ptr<Composer> composer_;
  bool choosing_ = false;
  size_t activeStart_ = 0;  // start reading of the segment being chosen
  std::vector<std::string> candidates_;
  int readingIndex_ = -1;  // where the syllables' Zhuyin sits in candidates_; -1 for none
  int selection_ = 0;
  // Candidates packed into grid rows of kGridColumns columns: where each row
  // starts, and the column each candidate starts at.
  std::vector<int> rowStarts_;
  std::vector<int> columns_;
  bool expanded_ = false;  // several rows shown; collapsed shows only the selection's row
  int firstRow_ = 0;       // first grid row on screen
  bool shiftAlone_ = false;
  bool cursorMoved_ = false;  // arrows moved the cursor since the last typed key
  std::string shown_;  // the composition as last shown, pending reading included
  bool adjacentKeys_ = false, rememberChoices_ = false, pinyin_ = false;  // the settings composer_ was made with
  // What English mode let through since the last other key, so Tab
  // can turn Zhuyin typed in the wrong mode back into a composition.
  std::string englishRun_;
  // The keys behind the composition while it is only typed syllables, so
  // Tab can turn English typed in the wrong mode back into English.
  std::string zhuyinKeys_;
  bool zhuyinEdited_ = false;  // another key changed the composition apart from zhuyinKeys_
  uint32_t replacing_ = 0;     // UTF-16 units before the caret the next composition replaces
  std::optional<PanelContent> panel_;
};

// UTF-16 length of UTF-8 text.
uint32_t Utf16Length(const std::string& utf8);
// Bytes of UTF-8 text that hold its first `units` UTF-16 code units, for
// platforms that count in bytes (Fcitx5, Pango); all of it when it is shorter.
size_t Utf8Bytes(const std::string& utf8, uint32_t units);

}  // namespace ProjectKey
