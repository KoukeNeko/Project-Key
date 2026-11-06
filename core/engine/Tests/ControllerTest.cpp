// Drives the Controller with key events against a fake client, covering what a
// user does: type a sentence, fix a doubtful word, commit.
// Usage: ControllerTest <path to data.txt>
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <map>
#include <set>

#include "Controller.h"

using namespace ProjectKey;

namespace {

class MemorySettings : public Settings {
 public:
  std::optional<int> stored(Setting setting) const override {
    auto it = values_.find(setting);
    return it == values_.end() ? std::nullopt : std::optional<int>(it->second);
  }
  void set(Setting setting, int value) override { values_[setting] = value; }
  void remove(Setting setting) { values_.erase(setting); }

 private:
  std::map<Setting, int> values_;
};

class FakeClient : public Client {
 public:
  std::string committed, marked;
  bool panelVisible = false;
  PanelContent panel;
  std::string indicator;

  void insertText(const std::string& text) override {
    committed += text;
    marked.clear();
  }
  void setComposition(const Composition& composition) override {
    // The marked text takes the place of what it replaces (ASCII in these tests).
    if (composition.replaceBefore) committed.erase(committed.size() - composition.replaceBefore);
    marked = composition.text;
  }
  void showPanel(const PanelContent& content) override {
    panel = content;
    panelVisible = true;
  }
  void hidePanel() override { panelVisible = false; }
  std::optional<PanelContent> hint;  // the Tab hint's own window
  void showHint(const PanelContent& content) override { hint = content; }
  void hideHint() override { hint.reset(); }
  void showIndicator(const std::string& label) override { indicator = label; }
  void beep() override {}
  std::string textBeforeCaret() override { return committed; }
};

bool HasSuffix(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool Contains(const std::string& s, const std::string& part) {
  return s.find(part) != std::string::npos;
}

std::string Joined(const std::vector<std::string>& parts) {
  std::string out;
  for (const std::string& part : parts) out += part;
  return out;
}

KeyEvent Press(std::string characters, ProjectKey::Key key = ProjectKey::Key::kCharacter) {
  KeyEvent event;
  event.key = key;
  event.characters = std::move(characters);
  return event;
}

KeyEvent Special(ProjectKey::Key key) {
  return Press(key == ProjectKey::Key::kSpace ? " " : "", key);
}

KeyEvent Shifted(std::string characters) {
  KeyEvent event = Press(std::move(characters));
  event.shift = true;
  return event;
}

KeyEvent Caps(std::string characters, bool shift = false) {
  KeyEvent event = Press(std::move(characters));
  event.capsLock = true;
  event.shift = shift;
  return event;
}

void Type(Controller& controller, const std::string& keys) {
  for (char c : keys) {
    bool handled = controller.handleKey(c == ' ' ? Special(ProjectKey::Key::kSpace) : Press(std::string(1, c)));
    assert(handled);
  }
}

void TapShift(Controller& controller) {
  controller.handleShift(true);
  controller.handleShift(false);
}

void EscapeAll(Controller& controller, FakeClient& client) {
  while (!client.marked.empty()) controller.handleKey(Special(ProjectKey::Key::kEscape));
}

// About as wide as the candidate font: CJK takes a full em, ASCII half.
int Span(const std::string& candidate) {
  double width = 0;
  for (size_t i = 0; i < candidate.size(); i++) {
    unsigned char c = static_cast<unsigned char>(candidate[i]);
    if (c < 0x80) width += 0.5;
    else if ((c & 0xc0) != 0x80) width += 1;
  }
  return std::max(1, std::min(kGridColumns, static_cast<int>((width + 1.5) / 2.4) + 1));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <data.txt>\n", argv[0]);
    return 2;
  }
  // UTF-16 positions to UTF-8 bytes: ASCII 1 byte, 國 3 bytes, an emoji 4 bytes and 2 units.
  const std::string mixed = "a國😀b";
  assert(Utf8Bytes(mixed, 0) == 0 && Utf8Bytes(mixed, 1) == 1 && Utf8Bytes(mixed, 2) == 4);
  assert(Utf8Bytes(mixed, 4) == 8 && Utf8Bytes(mixed, 5) == 9 && Utf8Bytes(mixed, 99) == mixed.size());
  assert(Utf8Bytes(mixed, Utf16Length(mixed)) == mixed.size());

  MemorySettings settings;
  settings.set(Setting::kAbbreviated, 1);  // most of these tests type toneless syllables in a row
  Environment env;
  env.lm = std::make_shared<Lexicon>();
  env.lm->loadLanguageModel(argv[1]);
  assert(env.lm->isDataModelLoaded());
  env.userModel = ProjectKey::NewUserModel();
  env.settings = &settings;
  env.isEnglish = [](const std::string& text) { return text == "hello" || text == "hello world" || text == "ja"; };
  env.columnsSpannedBy = Span;
  FakeClient client;
  Controller controller(env, client);

  // 我們把舊的程式重構成模組: the engine guesses 城市 and nothing is committed yet.
  Type(controller, "ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3");
  assert(client.committed.empty());
  assert(Contains(client.marked, "城市"));

  // Space opens candidates on the doubtful 城市; 2 picks the second one, 程式.
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  assert(controller.handleKey(Press("2")));
  assert(Contains(client.marked, "的程式"));

  // Enter commits the sentence as shown.
  assert(controller.handleKey(Special(ProjectKey::Key::kReturn)));
  assert(Contains(client.committed, "的程式重") && client.marked.empty());

  // Enter commits what is shown, except the syllable still open without a tone, which
  // goes as its Zhuyin: a lone symbol (ㄏ after 我) as itself.
  size_t sent = client.committed.size();
  Type(controller, "ji3c");
  assert(controller.handleKey(Special(ProjectKey::Key::kReturn)));
  assert(client.committed.substr(sent) == "我ㄏ");
  assert(client.marked.empty());
  // The lone symbol itself is offered by the first row (沒救ㄌ); picking it commits it.
  Type(controller, "x");
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)));
  const std::vector<std::string>& lone = controller.candidates();
  auto symbol = std::find(lone.begin(), lone.end(), "ㄌ");
  assert(symbol != lone.end() && symbol - lone.begin() <= static_cast<long>(client.panel.rows[0].size()));
  controller.choose(static_cast<int>(symbol - lone.begin()));
  assert(HasSuffix(client.committed, "ㄌ") && client.marked.empty());
  // Whole syllables without tones: the ones a later key closed as their words, the open one as Zhuyin.
  Type(controller, "sucl");
  assert(controller.handleKey(Special(ProjectKey::Key::kReturn)));
  assert(HasSuffix(client.committed, "你ㄏㄠ") && client.marked.empty());

  // The open syllable shows as its Zhuyin, the closed one before it as its word. The guess
  // (你好) is marked in the proof row and in the row without digits that holds the words
  // there, and Down opens all of them.
  Type(controller, "sucl");
  assert(client.marked == "你ㄏㄠ");
  const PanelContent& typing = client.panel;
  assert(client.panelVisible && typing.showsProof && typing.segments[typing.activeSegment] == "你好");
  assert(typing.selectedRow == -1 && typing.rows.size() == 1 && !typing.rows[0].empty());
  assert(typing.selection >= 0 && typing.rows[0][typing.selection] == "你好");
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)));
  assert(client.panel.selectedRow == 0 && client.marked == "你好");
  EscapeAll(controller, client);
  // First symbols: ㄨㄇ shows 我ㄇ. Tab would make the keys English (ja), which the proof
  // row shows instead; the row still marks the guess 我們.
  Type(controller, "ja");
  assert(client.marked == "我ㄇ" && typing.replacedText == "我ㄇ" && typing.segments[0] == "ja");
  assert(typing.selectedRow == -1 && typing.rows[0][typing.selection] == "我們");
  // Looking at the candidates and coming back changes nothing, so the hint is still there.
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)) && client.panel.selectedRow == 0);
  assert(controller.handleKey(Special(ProjectKey::Key::kEscape)));
  assert(typing.replacedText == "我ㄇ" && typing.segments[0] == "ja");
  // Backspace among the candidates only closes them, as Escape does.
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)) && client.panel.selectedRow == 0);
  assert(controller.handleKey(Special(ProjectKey::Key::kBackspace)));
  assert(typing.replacedText == "我ㄇ" && typing.segments[0] == "ja");
  EscapeAll(controller, client);
  // Where the hint has a window of its own, the panel keeps the guess in its proof row.
  env.hintWindow = true;
  Type(controller, "ja");
  assert(client.hint && client.hint->replacedText == "我ㄇ" && client.hint->segments[0] == "ja");
  assert(typing.segments[typing.activeSegment] == "我們" && typing.rows[0][typing.selection] == "我們");
  EscapeAll(controller, client);
  assert(!client.hint);
  env.hintWindow = false;
  // Down offers candidates while a syllable without a tone is still open; its Zhuyin has no
  // tone mark. Back from the candidates, the syllable is still open: ㄠ makes it ㄋㄧㄠ.
  Type(controller, "su");
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)));
  const std::vector<std::string>& open = controller.candidates();
  assert(std::find(open.begin(), open.end(), "ㄋㄧ") != open.end() && std::find(open.begin(), open.end(), "你") != open.end());
  assert(controller.handleKey(Special(ProjectKey::Key::kEscape)));
  Type(controller, "l");
  assert(client.marked == "ㄋㄧㄠ" && client.panel.pendingReading.empty());
  EscapeAll(controller, client);
  size_t before = client.committed.size();

  // Typed twice, the sentence has two doubtful 城市 (choices not remembered, so
  // fixing the first leaves the second): picking moves on to the second, or with
  // NextDoubtful off, ends choosing.
  settings.set(Setting::kRememberChoices, 0);
  for (int nextDoubtful : {1, 0}) {
    settings.set(Setting::kNextDoubtful, nextDoubtful);
    Type(controller, "ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3");
    assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
    assert(controller.handleKey(Press("2")));
    assert(client.panel.rows.empty() == !nextDoubtful);
    EscapeAll(controller, client);
  }
  settings.remove(Setting::kNextDoubtful);
  settings.remove(Setting::kRememberChoices);

  // Punctuation without Shift: ' and \ both give 、, [ and ] the corner brackets.
  Type(controller, "'\\[]");
  assert(client.marked == "、、「」");
  assert(controller.handleKey(Special(ProjectKey::Key::kEscape)));
  assert(client.marked.empty());

  // Tone 1 via space, shifted punctuation, Backspace, Esc.
  Type(controller, "rup wu0 ");
  assert(controller.handleKey(Shifted("<")));
  assert(client.marked == "今天，");
  assert(controller.handleKey(Special(ProjectKey::Key::kBackspace)));
  assert(client.marked == "今天");
  assert(controller.handleKey(Special(ProjectKey::Key::kEscape)));
  assert(client.marked.empty() && client.committed.size() == before);

  // With the cursor inside the phrase 今天, Space offers the syllables from
  // the cursor on, so its second character can be replaced alone.
  Type(controller, "rup wu0 ");
  assert(controller.handleKey(Special(ProjectKey::Key::kHome)));
  assert(controller.handleKey(Special(ProjectKey::Key::kRight)));
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  std::string second = client.panel.rows[0][1];
  assert(Utf16Length(second) == 1);
  assert(controller.handleKey(Press("2")));
  assert(HasSuffix(client.marked, second) && Utf16Length(client.marked) == 2);
  EscapeAll(controller, client);

  // Moved to the end, where nothing follows the cursor, Space offers the last character.
  Type(controller, "rup wu0 ");
  assert(controller.handleKey(Special(ProjectKey::Key::kHome)));
  assert(controller.handleKey(Special(ProjectKey::Key::kEnd)));
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  assert(Utf16Length(client.panel.rows[0][0]) == 1);
  EscapeAll(controller, client);

  // Set to the word before the cursor (macOS), the same cursor offers the whole 今天.
  settings.set(Setting::kCandidatePosition, 1);
  Type(controller, "rup wu0 ");
  assert(controller.handleKey(Special(ProjectKey::Key::kHome)));
  assert(controller.handleKey(Special(ProjectKey::Key::kRight)));
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  assert(Utf16Length(client.panel.rows[0][0]) == 2);
  EscapeAll(controller, client);
  settings.remove(Setting::kCandidatePosition);

  // Arrows move the cursor between syllables; typing and Forward Delete act there.
  Type(controller, "rup wu0 ");
  settings.set(Setting::kProofPreview, 0);  // always show the proof row
  assert(controller.handleKey(Special(ProjectKey::Key::kLeft)));
  assert(client.panel.caret == 1);
  Type(controller, "c");
  assert(client.panel.caret == 2 && client.panel.pendingReading.empty());
  Type(controller, "l3");
  assert(client.panel.caret == 2);
  assert(client.marked == "今好天");
  assert(controller.handleKey(Special(ProjectKey::Key::kForwardDelete)));
  assert(client.marked == "今好");
  assert(controller.handleKey(Special(ProjectKey::Key::kHome)));
  assert(controller.handleKey(Special(ProjectKey::Key::kForwardDelete)));
  assert(client.marked == "好");
  assert(controller.handleKey(Special(ProjectKey::Key::kEscape)));
  settings.remove(Setting::kProofPreview);

  // ⌃⇧P turns the proof row off and back on.
  Type(controller, "rup wu0 ");
  settings.set(Setting::kProofPreview, 0);
  KeyEvent toggle = Press("\x10");
  toggle.control = toggle.shift = toggle.isP = true;
  assert(controller.handleKey(toggle) && settings.get(Setting::kProofPreview) == 2 && !client.panelVisible);
  assert(controller.handleKey(toggle) && settings.get(Setting::kProofPreview) == 0 && client.panelVisible);
  settings.remove(Setting::kProofPreview);
  settings.remove(Setting::kProofPreviewLastOn);
  EscapeAll(controller, client);

  // A touch grid picks any candidate, past the row the panel shows.
  Type(controller, "rup wu0 ");
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  assert(controller.candidates().size() > client.panel.rows[0].size());
  std::string rare = controller.candidates().back();
  controller.choose(static_cast<int>(controller.candidates().size()) - 1);
  assert(Contains(client.marked, rare));
  EscapeAll(controller, client);

  // The Zhuyin itself is a candidate after the first row; picking it commits it in place of the word.
  settings.set(Setting::kCandidatePosition, 1);
  Type(controller, "rup wu0 ji3");
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  const std::vector<std::string>& offered = controller.candidates();
  auto zhuyin = std::find(offered.begin(), offered.end(), "ㄨㄛˇ");
  assert(zhuyin != offered.end() && zhuyin - offered.begin() >= static_cast<long>(client.panel.rows[0].size()));
  controller.choose(static_cast<int>(zhuyin - offered.begin()));
  assert(client.marked.empty() && HasSuffix(client.committed, "今天ㄨㄛˇ"));
  settings.remove(Setting::kCandidatePosition);
  before = client.committed.size();

  // A Zhuyin wider than the first row's last column starts the next row instead of leaving that
  // column empty (ㄐㄧˋㄓㄨˋ after one-column words). Columns as on macOS, where one holds two characters.
  auto columnsOf = [](const std::string& candidate) {
    int characters = 0;
    for (unsigned char c : candidate) characters += (c & 0xc0) != 0x80;
    return std::max(1, (characters + 1) / 2);
  };
  env.columnsSpannedBy = columnsOf;
  Type(controller, "ru45j4");
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)));
  int columns = 0;
  for (const std::string& candidate : client.panel.rows[0]) columns += columnsOf(candidate);
  assert(columns == kGridColumns && client.panel.rows[1][0] == "ㄐㄧˋㄓㄨˋ");
  EscapeAll(controller, client);
  env.columnsSpannedBy = Span;

  // Down expands the rows into a grid; Up walks back up, folds the grid on
  // its first row, and closes the candidates after that.
  Type(controller, "rup wu0 ");
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  assert(client.panel.rows.size() == 1);
  for (const auto& row : client.panel.rows) {
    int columns = 0;
    for (const std::string& candidate : row) columns += Span(candidate);
    assert(columns <= kGridColumns);
  }
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)));
  assert(client.panel.rows.size() > 1 && client.panel.selectedRow == 1 && client.panel.pageLabel.rfind("2/", 0) == 0);
  assert(controller.handleKey(Special(ProjectKey::Key::kUp)));
  assert(client.panel.rows.size() > 1 && client.panel.selectedRow == 0);
  assert(controller.handleKey(Special(ProjectKey::Key::kUp)));
  assert(client.panel.rows.size() == 1);
  assert(controller.handleKey(Special(ProjectKey::Key::kUp)));
  assert((!client.panelVisible || client.panel.rows.empty()) && client.marked == "今天");

  // In the grid, number keys pick from the selection's row.
  assert(controller.handleKey(Special(ProjectKey::Key::kSpace)));
  assert(controller.handleKey(Special(ProjectKey::Key::kDown)));
  std::string secondRowLast = client.panel.rows[1].back();  // the first is the Zhuyin, which commits
  assert(controller.handleKey(Press(std::to_string(client.panel.rows[1].size()))));
  assert(client.marked.rfind(secondRowLast, 0) == 0);
  assert(controller.handleKey(Special(ProjectKey::Key::kEscape)));
  assert(client.marked.empty() && client.committed.size() == before);

  // With nothing composed, editing keys belong to the app.
  assert(!controller.handleKey(Special(ProjectKey::Key::kBackspace)));
  assert(!controller.handleKey(Special(ProjectKey::Key::kReturn)));

  // A shortcut commits the composition first and passes through.
  // (Not checking for 今: the grid test above taught the user model another ㄐㄧㄣ.)
  size_t committed = Utf16Length(client.committed);
  Type(controller, "rup ");
  KeyEvent copy = Press("c");
  copy.control = true;
  assert(!controller.handleKey(copy));
  assert(Utf16Length(client.committed) == committed + 1 && client.marked.empty());

  // A character that is neither a reading key nor punctuation commits the
  // composition and follows it.
  Type(controller, "rup ");
  assert(controller.handleKey(Shifted("A")));
  assert(HasSuffix(client.committed, "A") && Utf16Length(client.committed) == committed + 3 && client.marked.empty());

  // Tapping Shift switches to English: keys pass through; tapping again switches back.
  TapShift(controller);
  assert(controller.englishMode() && client.indicator == "A");
  assert(!controller.handleKey(Press("r")));
  TapShift(controller);
  assert(!controller.englishMode() && client.indicator == "ㄅ");
  assert(controller.handleKey(Press("r")));
  EscapeAll(controller, client);

  // Shift with another key in between is a capital, not a switch.
  controller.handleShift(true);
  controller.handleKey(Shifted("<"));
  controller.handleShift(false);
  assert(!controller.englishMode());
  EscapeAll(controller, client);

  // Zhuyin typed in English mode: Tab turns the whole syllables at
  // the end (ji3cl3, 我好) into a composition and leaves the English alone.
  TapShift(controller);
  for (std::string key : {"h", "e", "l", "l", "o", " ", "j", "i", "3", "c", "l", "3"}) {
    assert(!controller.handleKey(key == " " ? Special(ProjectKey::Key::kSpace) : Press(key)));
    client.committed += key;  // what the app does with a key passed through
    // No preview for plain English; with a tone key in it, the preview shows the result and Tab.
    if (key == "o") assert(!client.panelVisible);
  }
  assert(client.panelVisible && client.panel.keyHint == "Tab");
  assert(Joined(client.panel.segments) == "我好" && client.panel.replacedText == "ji3cl3");
  assert(controller.handleKey(Special(ProjectKey::Key::kTab)));
  assert(HasSuffix(client.committed, "hello ") && client.marked == "我好");
  assert(!client.panelVisible || client.panel.keyHint.empty());
  assert(controller.handleKey(Press("r")));  // back in 中

  // The reverse: English typed in 中 shows what Tab turns it into,
  // and Tab puts the English in place of the composition and switches to 英.
  EscapeAll(controller, client);
  Type(controller, "hello");
  assert(client.panelVisible && client.panel.keyHint == "Tab");
  assert(client.panel.segments[0] == "hello" && client.panel.replacedText == client.marked);
  assert(controller.handleKey(Special(ProjectKey::Key::kTab)));
  assert(HasSuffix(client.committed, "hello") && client.marked.empty());
  assert(!controller.handleKey(Press("r")));  // now in 英
  // A lone Shift only switches: the English stays.
  for (std::string key : {"j", "i", "3"}) controller.handleKey(Press(key)), client.committed += key;
  TapShift(controller);
  assert(HasSuffix(client.committed, "ji3") && client.marked.empty() && !controller.englishMode());
  EscapeAll(controller, client);
  // Turned off in settings: no preview, and Tab opens candidates as usual.
  settings.set(Setting::kZhuyinToEnglish, 0);
  Type(controller, "hello");
  assert(!client.panelVisible || client.panel.keyHint.empty());
  assert(controller.handleKey(Special(ProjectKey::Key::kTab)) && !client.marked.empty());
  EscapeAll(controller, client);
  settings.remove(Setting::kZhuyinToEnglish);
  // Real Zhuyin is left alone, even when it spells something: "su " is ㄋㄧ as well as "su".
  Type(controller, "su ");
  assert(!client.panelVisible || client.panel.keyHint.empty());
  EscapeAll(controller, client);

  // The English must still be right before the caret: if the app moved it, Tab is a Tab.
  TapShift(controller);
  for (std::string key : {"c", "l", "3"}) controller.handleKey(Press(key));  // passed through, but the app typed elsewhere
  assert(!controller.handleKey(Special(ProjectKey::Key::kTab)));
  assert(client.marked.empty() && controller.englishMode());
  // After plain words that only spell syllables without a tone key ("su "), Tab is a Tab too.
  for (std::string key : {"s", "u", " "}) controller.handleKey(key == " " ? Special(ProjectKey::Key::kSpace) : Press(key)), client.committed += key;
  assert(!controller.handleKey(Special(ProjectKey::Key::kTab)));
  assert(client.marked.empty() && controller.englishMode());
  TapShift(controller);

  // Keys a Shift ended by committing are not behind the next composition: "hello" in 中,
  // Shift, ji3 and Tab in 英 make 我, with no offer to turn it into "hello", and the next
  // Tab opens the candidates instead of typing it.
  Type(controller, "hello");
  TapShift(controller);
  assert(controller.englishMode() && client.marked.empty());
  for (std::string key : {"j", "i", "3"}) controller.handleKey(Press(key)), client.committed += key;
  assert(controller.handleKey(Special(ProjectKey::Key::kTab)));
  assert(client.marked == "我" && (!client.panelVisible || client.panel.keyHint.empty()));
  size_t beforeTab = client.committed.size();
  assert(controller.handleKey(Special(ProjectKey::Key::kTab)));
  assert(client.marked == "我" && client.committed.size() == beforeTab && !controller.englishMode());
  EscapeAll(controller, client);

  // Off by default, Caps Lock leaves 中/英 alone.
  controller.handleCapsLock();
  assert(!controller.englishMode());
  // With its setting on, Caps Lock switches 中/英 on each press; the lock it leaves on does
  // not capitalize.
  settings.set(Setting::kCapsLockToggle, 1);
  controller.handleCapsLock();
  size_t beforeCaps = client.committed.size();
  assert(controller.handleKey(Caps("R")));
  assert(HasSuffix(client.committed, "r") && client.committed.size() == beforeCaps + 1);
  assert(!controller.handleKey(Caps("R", true)));
  controller.handleCapsLock();
  assert(controller.handleKey(Press("r")) && !client.marked.empty());  // back in 中
  EscapeAll(controller, client);
  // Back in 中 with the lock on (after Shift switched), letters are still Zhuyin keys.
  controller.handleCapsLock();
  TapShift(controller);
  assert(controller.handleKey(Caps("R")) && controller.isTypingSyllable());
  EscapeAll(controller, client);
  controller.handleCapsLock();
  TapShift(controller);  // leave it in 中
  assert(!controller.englishMode());
  settings.remove(Setting::kCapsLockToggle);

  // Losing focus commits; the app ending the composition drops it.
  Type(controller, "rup ");
  controller.deactivate();
  assert(client.marked.empty() && !controller.isComposing());
  Type(controller, "rup ");
  controller.reset();
  assert(!controller.isComposing());

  // What the user typed: a commit teaches env.personal while choices are remembered, and
  // clearing the choices forgets it too.
  env.personal = std::make_shared<PersonalModel>();
  client.marked.clear();  // reset() above left the app's marked text to the app
  Type(controller, "ji3ap7");
  assert(controller.handleKey(Special(ProjectKey::Key::kReturn)) && !env.personal->empty());
  ClearChoices(env);
  assert(env.personal->empty());
  settings.set(Setting::kRememberChoices, 0);
  Type(controller, "ji3ap7");
  assert(controller.handleKey(Special(ProjectKey::Key::kReturn)) && env.personal->empty());
  settings.remove(Setting::kRememberChoices);

  // 簡拼 off, the default: Zhuyin keys edit the open syllable, as in an ordinary Zhuyin input method.
  settings.remove(Setting::kAbbreviated);
  Type(controller, "ja");  // ㄨ then ㄇ is ㄇㄨ, not 我ㄇ
  assert(client.marked == "ㄇㄨ");
  EscapeAll(controller, client);
  Type(controller, "1q");  // ㄅ then ㄆ is ㄆ
  assert(client.marked == "ㄆ");
  EscapeAll(controller, client);
  Type(controller, "suc");  // ㄋㄧ then ㄏ is ㄏㄧ
  assert(client.marked == "ㄏㄧ");
  EscapeAll(controller, client);
  settings.set(Setting::kAbbreviated, 1);
  Type(controller, "ja");
  assert(client.marked == "我ㄇ");
  EscapeAll(controller, client);

  std::printf("ControllerTest passed\n");
  return 0;
}
