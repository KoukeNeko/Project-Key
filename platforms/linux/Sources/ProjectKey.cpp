// The Fcitx5 addon: each input context gets a Controller (core/engine), whose
// composition becomes the preedit. Its candidates, proof row and 中／英 badge go
// into Project Key's own windows (Panel.cpp), which look like the macOS and
// Windows ones: on X11, and on Wayland for apps that reach Fcitx5 through the
// compositor's input method (Wayland's text input). Where they cannot go at the
// caret, they go into Fcitx5's input panel and follow its theme.
#include <dbus_public.h>
#include <enchant.h>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>
#include <notifications_public.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cctype>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef PROJECTKEY_ADVANCED_MODEL
#include "Microphone.h"
#include "SpeechModel.h"
#endif
#include "Controller.h"
#include "Update.h"
#include "Panel.h"
#include "../Settings/Rows.h"

namespace ProjectKey {

namespace {

constexpr char kConfigPath[] = "conf/projectkey.conf";
constexpr uint64_t kFirstUpdateCheckUs = 60000000;  // a minute after Fcitx5 starts

enum class ProofMode { Always, WhenDoubtful, Off };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ProofMode, N_("永遠"), N_("有可疑字時"), N_("關閉"));
enum class GridColumns { Six, Seven, Eight, Nine };  // Setting::kGridColumns: 6 to 9 columns
FCITX_CONFIG_ENUM_NAME_WITH_I18N(GridColumns, N_("6"), N_("7"), N_("8"), N_("9"));
enum class KeyboardLayout { Standard, ETen, Hsu, ETen26, IBM };  // Setting::kKeyboardLayout
FCITX_CONFIG_ENUM_NAME_WITH_I18N(KeyboardLayout, N_("大千"), N_("倚天"), N_("許氏"), N_("倚天 26 鍵"), N_("IBM"));
enum class CandidatePosition { AfterCursor, BeforeCursor };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(CandidatePosition, N_("游標後"), N_("游標前"));
// Setting::kAppearance, macOS's numbers: the dark window, a tint over the blurred windows behind it,
// the light window, and light or dark as the desktop is. The config keeps the names.
enum class Appearance { Dark, Blur, Light, Auto };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(Appearance, N_("深色"), N_("模糊"), N_("淺色"), N_("自動"));

// Switches to the newest of the installed models and those the settings windows downloaded.
bool UseNewestModels(Environment& env) {
  return UseNewestModels(env, PROJECTKEY_DATA_DIR, SettingsRows::DownloadedModels().string());
}

bool DefaultFlag(Setting setting) {
  return Settings::DefaultValue(setting) != 0;
}

// The same settings as macOS and Windows, under the same names, edited in
// Fcitx5's configuration tool. The defaults are the engine's.
FCITX_CONFIGURATION(
    Config,
    fcitx::OptionWithAnnotation<CandidatePosition, CandidatePositionI18NAnnotation> candidatePosition{
        this, "CandidatePosition", "選字位置",
        static_cast<CandidatePosition>(Settings::DefaultValue(Setting::kCandidatePosition))};
    fcitx::OptionWithAnnotation<GridColumns, GridColumnsI18NAnnotation> gridColumns{
        this, "GridColumns", "每列欄數", static_cast<GridColumns>(Settings::DefaultValue(Setting::kGridColumns))};
    fcitx::OptionWithAnnotation<Appearance, AppearanceI18NAnnotation> appearance{
        this, "Appearance", "候選視窗外觀", static_cast<Appearance>(Settings::DefaultValue(Setting::kAppearance))};
    fcitx::Option<bool> nextDoubtful{this, "NextDoubtful", "選完跳到下一個可疑處", DefaultFlag(Setting::kNextDoubtful)};
    fcitx::Option<bool> omitTones{this, "OmitTones", "省略聲調", DefaultFlag(Setting::kOmitTones)};
    fcitx::Option<bool> abbreviated{this, "Abbreviated", "簡拼", DefaultFlag(Setting::kAbbreviated)};
    fcitx::Option<bool> fuzzySounds{this, "FuzzySounds", "近似音", DefaultFlag(Setting::kFuzzySounds)};
    fcitx::Option<bool> associatedPhrases{this, "AssociatedPhrases", "聯想詞", DefaultFlag(Setting::kAssociatedPhrases)};
    fcitx::Option<bool> keypadPicks{this, "KeypadPicks", "數字鍵盤選字", DefaultFlag(Setting::kKeypadPicks)};
    fcitx::Option<bool> rememberChoices{this, "RememberChoices", "記住選過的字", DefaultFlag(Setting::kRememberChoices)};
    fcitx::OptionWithAnnotation<ProofMode, ProofModeI18NAnnotation> proofPreview{
        this, "ProofPreview", "輸入時的整句預覽", static_cast<ProofMode>(Settings::DefaultValue(Setting::kProofPreview))};
    fcitx::OptionWithAnnotation<ProofMode, ProofModeI18NAnnotation> choosingProof{
        this, "ChoosingProof", "選字時的整句預覽", static_cast<ProofMode>(Settings::DefaultValue(Setting::kChoosingProof))};
    fcitx::OptionWithAnnotation<KeyboardLayout, KeyboardLayoutI18NAnnotation> keyboardLayout{
        this, "KeyboardLayout", "鍵盤排列", static_cast<KeyboardLayout>(Settings::DefaultValue(Setting::kKeyboardLayout))};
    fcitx::Option<bool> capsLockToggle{this, "CapsLockToggle", "Caps Lock 切換中英", DefaultFlag(Setting::kCapsLockToggle)};
    fcitx::Option<bool> shiftToggle{this, "ShiftToggle", "單按 Shift 切換中英", DefaultFlag(Setting::kShiftToggle)};
    fcitx::Option<bool> fullWidthToggle{this, "FullWidthToggle", "Shift+Space 切換全形", DefaultFlag(Setting::kFullWidthToggle)};
    fcitx::Option<bool> englishToZhuyin{this, "EnglishToZhuyin", "Tab 英文轉注音", DefaultFlag(Setting::kEnglishToZhuyin)};
    fcitx::Option<bool> zhuyinToEnglish{this, "ZhuyinToEnglish", "Tab 注音轉英文", DefaultFlag(Setting::kZhuyinToEnglish)};
    fcitx::Option<bool> simplified{this, "Simplified", "簡體中文", DefaultFlag(Setting::kSimplified)};
    fcitx::Option<bool> simplifiedToggle{this, "SimplifiedToggle", "快捷鍵切換繁簡", DefaultFlag(Setting::kSimplifiedToggle)};
    // Ctrl+Shift+F, as Fcitx5's own switch (chttrans) has it.
    fcitx::Option<fcitx::Key, fcitx::KeyConstrain> simplifiedKey{
        this, "SimplifiedKey", "繁簡切換鍵", fcitx::Key("Control+Shift+F"), fcitx::KeyConstrain(fcitx::KeyConstrainFlag::AllowModifierLess)};
    fcitx::Option<bool> dictation{this, "Dictation", "語音輸入", DefaultFlag(Setting::kDictation)};
    fcitx::Option<bool> checkForUpdates{this, "CheckForUpdates", "自動檢查更新", DefaultFlag(Setting::kCheckForUpdates)};
    // Held to dictate: a modifier alone, or a key that types nothing (Settings/Gtk.cpp and Qt.cpp record it).
    fcitx::Option<fcitx::Key, fcitx::KeyConstrain> dictationKey{
        this, "DictationKey", "語音輸入鍵", fcitx::Key(FcitxKey_Alt_R),
        fcitx::KeyConstrain({fcitx::KeyConstrainFlag::AllowModifierLess, fcitx::KeyConstrainFlag::AllowModifierOnly})};
    // The proof mode Ctrl+Shift+P turns back on; -1 until it is first toggled.
    fcitx::HiddenOption<int> proofPreviewLastOn{this, "ProofPreviewLastOn", "", -1};
    fcitx::HiddenOption<int> choosingProofLastOn{this, "ChoosingProofLastOn", "", -1};);

class ConfigSettings : public Settings {
 public:
  explicit ConfigSettings(Config& config) : config_(config) {}

  std::optional<int> stored(Setting setting) const override {
    switch (setting) {
      case Setting::kProofPreview: return static_cast<int>(*config_.proofPreview);
      case Setting::kChoosingProof: return static_cast<int>(*config_.choosingProof);
      case Setting::kProofPreviewLastOn: return LastOn(*config_.proofPreviewLastOn);
      case Setting::kChoosingProofLastOn: return LastOn(*config_.choosingProofLastOn);
      case Setting::kAbbreviated: return *config_.abbreviated;
      case Setting::kOmitTones: return *config_.omitTones;
      case Setting::kFuzzySounds: return *config_.fuzzySounds;
      case Setting::kAssociatedPhrases: return *config_.associatedPhrases;
      case Setting::kKeypadPicks: return *config_.keypadPicks;
      case Setting::kRememberChoices: return *config_.rememberChoices;
      case Setting::kShiftToggle: return *config_.shiftToggle;
      case Setting::kFullWidthToggle: return *config_.fullWidthToggle;
      case Setting::kCapsLockToggle: return *config_.capsLockToggle;
      case Setting::kEnglishToZhuyin: return *config_.englishToZhuyin;
      case Setting::kZhuyinToEnglish: return *config_.zhuyinToEnglish;
      case Setting::kCandidatePosition: return static_cast<int>(*config_.candidatePosition);
      case Setting::kNextDoubtful: return *config_.nextDoubtful;
      case Setting::kAppearance: return static_cast<int>(*config_.appearance);
      case Setting::kGridColumns: return static_cast<int>(*config_.gridColumns);
      case Setting::kKeyboardLayout: return static_cast<int>(*config_.keyboardLayout);
      case Setting::kDictation: return *config_.dictation;
      case Setting::kSimplified: return *config_.simplified;
      case Setting::kSimplifiedToggle: return *config_.simplifiedToggle;
      case Setting::kCheckForUpdates: return *config_.checkForUpdates;
    }
    return std::nullopt;
  }

  // The engine sets only the proof modes, from Ctrl+Shift+P, and 簡體中文 from its switch.
  void set(Setting setting, int value) override {
    switch (setting) {
      case Setting::kSimplified: config_.simplified.setValue(value != 0); break;
      case Setting::kProofPreview: config_.proofPreview.setValue(static_cast<ProofMode>(value)); break;
      case Setting::kChoosingProof: config_.choosingProof.setValue(static_cast<ProofMode>(value)); break;
      case Setting::kProofPreviewLastOn: config_.proofPreviewLastOn.setValue(value); break;
      case Setting::kChoosingProofLastOn: config_.choosingProofLastOn.setValue(value); break;
      default: return;
    }
    fcitx::safeSaveAsIni(config_, kConfigPath);
  }

 private:
  // The proof mode the switch turns back on: 永遠 or 有可疑字時; anything else (a file edited by hand) is unset.
  static std::optional<int> LastOn(int value) {
    return value == 0 || value == 1 ? std::optional<int>(value) : std::nullopt;  // ProofMode::kProofAlways, kProofWhenDoubtful
  }
  Config& config_;
};

// Grid columns a candidate takes without Project Key's window to measure it
// in: two Han characters to a column, as the candidate window packs them.
int Span(const std::string& candidate) {
  double width = 0;
  for (unsigned char c : candidate) {
    if (c < 0x80) width += 0.5;
    else if ((c & 0xc0) != 0x80) width += 1;
  }
  return std::clamp(static_cast<int>((width + 1.9) / 2), 1, kMaxGridColumns);
}

// English spelling, for the Tab conversion of English typed in 中: every word
// of the keys must be a word in the en_US dictionary.
class Spelling {
 public:
  Spelling() : broker_(enchant_broker_init()) {
    if (broker_) dict_ = enchant_broker_request_dict(broker_, "en_US");
  }
  ~Spelling() {
    if (dict_) enchant_broker_free_dict(broker_, dict_);
    if (broker_) enchant_broker_free(broker_);
  }
  bool ok() const { return dict_ != nullptr; }
  bool isEnglish(const std::string& text) const {
    if (!dict_) return false;
    bool any = false;
    for (size_t start = 0; start <= text.size();) {
      size_t end = std::min(text.find(' ', start), text.size());
      if (end > start) {
        // Hunspell knows abbreviations in capitals only (CD, OK); macOS's checker takes cd and ok too.
        std::string word = text.substr(start, end - start), upper = word;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto known = [this](const std::string& w) { return enchant_dict_check(dict_, w.data(), static_cast<ssize_t>(w.size())) == 0; };
        if (!known(word) && !known(upper)) return false;
        any = true;
      }
      start = end + 1;
    }
    return any;
  }

 private:
  EnchantBroker* broker_ = nullptr;
  EnchantDict* dict_ = nullptr;
};

// Takes Fcitx5's announcement of the input method switched to out of its panel (the overlay message
// since 5.1.22, the panel's top line before) and returns its text.
template <typename InputPanel>
std::string TakeAnnouncement(InputPanel& panel) {
  if constexpr (requires { panel.setOverlayMessage(fcitx::Text()); }) {
    std::string text = panel.overlayMessage().toString();
    panel.setOverlayMessage(fcitx::Text());
    return text;
  } else {
    std::string text = panel.auxUp().toString();
    panel.setAuxUp(fcitx::Text());
    return text;
  }
}

}  // namespace

class Session;
// The session the engine is working for, set around each call into its controller:
// candidates packed into rows for Project Key's window are measured as the window
// draws them, for Fcitx5's panel as the text it shows (Span).
const Session* gActive = nullptr;
struct Active {
  explicit Active(const Session* session) : previous(std::exchange(gActive, session)) {}
  ~Active() { gActive = previous; }
  Active(const Active&) = delete;
  Active& operator=(const Active&) = delete;
  const Session* previous;
};

// One input context's composition: what the Controller does to the app goes
// into the context's preedit, commit and panel.
class Session : public fcitx::InputContextProperty, public Client {
 public:
  Session(Environment& environment, fcitx::Instance* instance, Panel& panel, fcitx::InputContext& ic)
      : instance_(instance), panel_(panel), ic_(ic), controller(environment, *this) {}
  ~Session() override { panel_.hide(ic_); }

  bool ownPanel() const { return panel_.shows(ic_); }
  // The app moved the caret, as it does after each preedit change.
  void caretMoved() {
    if (ownPanel()) panel_.follow(ic_, anchor());
  }
  // Where the composition starts, as on the other platforms, so the panel
  // stays put while it grows; the caret when there is none or it wrapped.
  fcitx::Rect anchor() const {
    const fcitx::Rect& caret = ic_.cursorRect();
    return start_ && start_->top() == caret.top() && start_->left() <= caret.left() ? *start_ : caret;
  }

  void insertText(const std::string& text) override {
    clearPreedit();
    if (!text.empty()) ic_.commitString(text);
  }

  void setComposition(const Composition& composition) override {
    // Zhuyin typed in 英 becomes the composition in place of what the app shows.
    if (composition.replaceBefore && ic_.capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText)) {
      ic_.deleteSurroundingText(-static_cast<int>(composition.replaceBefore), composition.replaceBefore);
    }
    if (composition.text.empty()) {
      clearPreedit();
      return;
    }
    // The app has not drawn the preedit yet, so its caret is where the composition starts.
    if (!start_) start_ = ic_.cursorRect();
    bool inApp = ic_.capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
    fcitx::TextFormatFlags normal = inApp ? fcitx::TextFormatFlag::Underline : fcitx::TextFormatFlag::NoFlag;
    const std::string& text = composition.text;
    size_t activeStart = Utf8Bytes(text, composition.activeStart);
    size_t activeEnd = Utf8Bytes(text, composition.activeStart + composition.activeLength);
    fcitx::Text preedit;
    preedit.append(text.substr(0, activeStart), normal);
    if (activeEnd > activeStart) preedit.append(text.substr(activeStart, activeEnd - activeStart), fcitx::TextFormatFlag::HighLight);
    preedit.append(text.substr(activeEnd), normal);
    preedit.setCursor(static_cast<int>(Utf8Bytes(text, composition.caret)));
    if (inApp) {
      ic_.inputPanel().setClientPreedit(preedit);
    } else {
      ic_.inputPanel().setPreedit(preedit);
    }
    ic_.updatePreedit();
    ic_.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  }

  // Project Key's window, or else Fcitx5's panel: the proof row and page label on
  // its top line, then the selection's row as the candidate list, or the
  // expanded grid's rows as lines of text.
  void showPanel(const PanelContent& content) override {
    if (ownPanel()) {
      panel_.show(ic_, content, anchor());
      return;
    }
    shown_ = content;
    showInFcitx(content);
  }

  // The Tab hint beside Project Key's window. Fcitx5's panel has one top line, so
  // there the hint takes it over the candidates shown, as without a hint window.
  void showHint(const PanelContent& hint) override {
    if (ownPanel()) {
      panel_.showHint(ic_, hint);
      return;
    }
    PanelContent merged = hint;
    if (shown_) {
      merged.rows = shown_->rows;
      merged.selectedRow = shown_->selectedRow;
      merged.selection = shown_->selection;
      merged.pageLabel = shown_->pageLabel;
    }
    showInFcitx(merged);
  }
  // In Fcitx5's panel the next showPanel or hidePanel, which always follows, replaces the hint.
  void hideHint() override { panel_.hideHint(); }

  void showInFcitx(const PanelContent& content) {
    fcitx::InputPanel& panel = ic_.inputPanel();
    fcitx::Text aux;
    if (!content.replacedText.empty()) {
      aux.append(content.replacedText, fcitx::TextFormatFlag::Strike);
      aux.append(" → ");
    }
    if (content.showsProof) {
      uint32_t at = 0;
      bool readingShown = content.pendingReading.empty();
      auto reading = [&] {
        if (!readingShown) aux.append(content.pendingReading, fcitx::TextFormatFlag::Underline);
        readingShown = true;
      };
      for (size_t i = 0; i < content.segments.size(); i++) {
        const std::string& segment = content.segments[i];
        // The word being chosen in the theme's highlight (the accent color, as the selected
        // cell; Fcitx5 has one highlight style), a doubtful one underlined.
        fcitx::TextFormatFlags format = fcitx::TextFormatFlag::NoFlag;
        if (static_cast<int>(i) == content.activeSegment) {
          format = fcitx::TextFormatFlag::HighLight;
        } else if (i < content.doubtful.size() && content.doubtful[i]) {
          format = fcitx::TextFormatFlag::Underline;
        }
        uint32_t length = Utf16Length(segment);
        if (content.caret == at) reading();
        if (content.caret > at && content.caret < at + length) {
          size_t split = Utf8Bytes(segment, content.caret - at);
          aux.append(segment.substr(0, split), format);
          reading();
          aux.append(segment.substr(split), format);
        } else {
          aux.append(segment, format);
        }
        at += length;
      }
      reading();
    }
    if (!content.keyHint.empty()) aux.append("  [" + content.keyHint + "]");
    // The page label ends the top line: Fcitx5 puts its second line (aux down) between it and the candidates.
    if (!content.pageLabel.empty()) aux.append((aux.empty() ? "" : "   ") + content.pageLabel);
    panel.setAuxUp(aux);
    panel.setAuxDown(fcitx::Text());

    if (content.rows.empty()) {
      panel.setCandidateList(nullptr);
    } else if (content.rows.size() > 1 && content.selectedRow >= 0) {
      // The expanded grid. Fcitx5's candidate list is one row or one column, so each
      // grid row is one candidate of a column. Fcitx5's own panel shows the selected
      // cell in the selection's colors, as the candidate window does; a panel the app
      // draws (GNOME on Wayland) takes each candidate as plain text, so there the
      // selected row takes the selection's colors and brackets mark the cell.
      bool plain = ic_.capabilityFlags().test(fcitx::CapabilityFlag::ClientSideInputPanel);
      auto list = std::make_unique<fcitx::CommonCandidateList>();
      list->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
      list->setPageSize(static_cast<int>(content.rows.size()));
      for (size_t r = 0; r < content.rows.size(); r++) list->append<GridRow>(RowText(content, r, plain));
      list->setLabels(std::vector<std::string>(content.rows.size(), ""));
      if (plain) list->setGlobalCursorIndex(content.selectedRow);
      panel.setCandidateList(std::move(list));
    } else {
      // The row shown while typing (selectedRow -1) has no digits: those keys type Zhuyin.
      bool typing = content.selectedRow < 0;
      const std::vector<std::string>& row = content.rows[typing ? 0 : content.selectedRow];
      auto list = std::make_unique<fcitx::CommonCandidateList>();
      list->setLayoutHint(fcitx::CandidateLayoutHint::Horizontal);
      list->setPageSize(static_cast<int>(row.size()));
      std::vector<std::string> labels;
      for (size_t i = 0; i < row.size(); i++) {
        labels.push_back(typing ? "" : std::to_string(i + 1) + " ");
        list->append<Word>(this, row[i], typing ? 0 : content.selectedRow, static_cast<int>(i));
      }
      list->setLabels(labels);
      if (!typing) list->setGlobalCursorIndex(content.selection);
      panel.setCandidateList(std::move(list));
    }
    ic_.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  }

  void hidePanel() override {
    panel_.hide(ic_);
    shown_.reset();
    fcitx::InputPanel& panel = ic_.inputPanel();
    panel.setAuxUp(fcitx::Text());
    panel.setAuxDown(fcitx::Text());
    panel.setCandidateList(nullptr);
    ic_.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  }

  // Without Project Key's window, Fcitx5's own input method popup stands in
  // for the badge; it shows the engine's subMode, which reads the same mode.
  void showIndicator(const std::string& label) override {
    if (ownPanel()) {
      panel_.showBadge(ic_, label, anchor());
      return;
    }
    instance_->showInputMethodInformation(&ic_);
    ic_.updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
  }

  void beep() override {}

  std::string textBeforeCaret() override {
    if (!ic_.capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText) || !ic_.surroundingText().isValid()) return {};
    const std::string& text = ic_.surroundingText().text();
    unsigned int cursor = ic_.surroundingText().cursor();  // in characters
    size_t end = fcitx::utf8::ncharByteLength(text.begin(), cursor);
    size_t start = cursor > 64 ? fcitx::utf8::ncharByteLength(text.begin(), cursor - 64) : 0;
    return text.substr(start, end - start);
  }

  void clearPreedit() {
    start_.reset();
    ic_.inputPanel().setClientPreedit(fcitx::Text());
    ic_.inputPanel().setPreedit(fcitx::Text());
    ic_.updatePreedit();
  }

 private:
  // A row of the expanded grid as text, its columns lined up as the candidate
  // window's: each cell is a label slot (a digit on the selected row, a figure
  // space elsewhere), an en space and two ideographic widths per column for the
  // word, between ideographic spaces. The selected cell is in the selection's
  // colors, or between brackets where the panel takes plain text.
  static fcitx::Text RowText(const PanelContent& content, size_t r, bool plain) {
    static const std::string kFigure = " ", kEn = " ", kIdeographic = "　";
    bool labeled = static_cast<int>(r) == content.selectedRow;
    fcitx::Text text;
    for (size_t i = 0; i < content.rows[r].size(); i++) {
      const std::string& word = content.rows[r][i];
      int columns = Span(word);
      // Widths in halves of an ideographic width: ASCII is one, anything else two (as Span counts).
      int halves = 0;
      for (unsigned char c : word) {
        if (c < 0x80) halves += 1;
        else if ((c & 0xc0) != 0x80) halves += 2;
      }
      bool on = labeled && static_cast<int>(i) == content.selection;
      std::string cell = (labeled ? std::to_string(i + 1) : kFigure) + kEn + word;
      for (int pad = std::max(0, 4 * columns - halves); pad > 0; pad -= pad >= 2 ? 2 : 1) cell += pad >= 2 ? kIdeographic : kEn;
      for (int extra = 1; extra < columns; extra++) cell += kIdeographic + kFigure + kEn + kIdeographic;  // the columns it spans past the first
      text.append(on && plain ? "［" : kIdeographic);
      text.append(cell, on && !plain ? fcitx::TextFormatFlag::HighLight : fcitx::TextFormatFlag::NoFlag);
      text.append(on && plain ? "］" : kIdeographic);
    }
    return text;
  }

  // A row of the expanded grid in Fcitx5's panel; the keys choose within it, as in the candidate window.
  class GridRow : public fcitx::CandidateWord {
   public:
    explicit GridRow(fcitx::Text text) : fcitx::CandidateWord(std::move(text)) {}
    void select(fcitx::InputContext*) const override {}
  };
  // A candidate clicked in the panel, picked as a click in Project Key's window picks it.
  class Word : public fcitx::CandidateWord {
   public:
    Word(Session* session, const std::string& text, int row, int index)
        : fcitx::CandidateWord(fcitx::Text(text)), session_(session), row_(row), index_(index) {}
    void select(fcitx::InputContext*) const override {
      Active active(session_);
      session_->controller.clickCandidate(row_, index_);
    }

   private:
    Session* session_;
    int row_, index_;
  };

  fcitx::Instance* instance_;
  Panel& panel_;
  fcitx::InputContext& ic_;
  std::optional<PanelContent> shown_;  // in Fcitx5's panel, for a hint to join
  std::optional<fcitx::Rect> start_;  // the caret when the composition began

 public:
  Controller controller;  // after the members it calls back into
};

// Setting::kCheckForUpdates: GitHub's latest release, fetched by curl on a thread of its own so Fcitx5
// never waits on the network; a newer one is handed back on Fcitx5's thread.
// shortcut: needs the curl command, which the install instructions use too; link libcurl if a desktop lacks it.
class UpdateCheck {
 public:
  explicit UpdateCheck(fcitx::EventLoop& loop) { dispatcher_.attach(&loop); }
  ~UpdateCheck() {
    if (thread_.joinable()) thread_.join();
    dispatcher_.detach();
  }
  // found(version) once the release is newer; nothing when it is not, or the check failed. A check
  // still going is left to finish.
  void run(std::function<void(const std::string&)> found) {
    if (running_) return;
    if (thread_.joinable()) thread_.join();
    running_ = true;
    thread_ = std::thread([this, found = std::move(found)] {
      std::string release;
      std::string command = std::string("curl -fsSL --max-time 20 -H 'User-Agent: ProjectKey' ") + kLatestRelease + " 2>/dev/null";
      if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[4096];
        for (size_t n; (n = fread(buffer, 1, sizeof buffer, pipe)) > 0;) release.append(buffer, n);
        pclose(pipe);
      }
      std::optional<std::string> newer = NewerRelease(release, PROJECTKEY_VERSION);
      dispatcher_.schedule([this, newer, found] {
        running_ = false;
        if (newer) found(*newer);
      });
    });
  }

 private:
  fcitx::EventDispatcher dispatcher_;
  std::thread thread_;
  bool running_ = false;  // on Fcitx5's thread only
};

// Dictation (Setting::kDictation): the microphone records on Fcitx5's thread while the dictation key is
// held, and the speech model (core/engine/SpeechModel.h) the settings windows download transcribes
// on a thread of its own; what was said goes back on Fcitx5's thread. Built without llama.cpp,
// there is never a model, and the dictation key stays an ordinary key.
class Dictation {
 public:
  using Answer = std::function<void(const std::string&)>;
  explicit Dictation(fcitx::EventLoop& loop) {
    dispatcher_.attach(&loop);
    thread_ = std::thread([this] { run(); });
  }
  ~Dictation() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
    dispatcher_.detach();
  }
  // Fcitx5's thread only, as the answer of reload says.
  bool loaded() const { return sampleRate_ > 0; }
  // Loads the newest download, or drops the model when there is none.
  void reload() {
    std::lock_guard<std::mutex> lock(mutex_);
    reload_ = true;
    wake_.notify_all();
  }
  // false when the microphone cannot be opened.
  bool record() {
#ifdef PROJECTKEY_ADVANCED_MODEL
    return loaded() && microphone_.start(sampleRate_);
#else
    return false;
#endif
  }
  // Stops recording; with answer, transcribes what was recorded, biased toward words.
  void finish(std::vector<std::string> words, Answer answer) {
#ifdef PROJECTKEY_ADVANCED_MODEL
    std::vector<float> samples = microphone_.stop();
    if (!answer) return;
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back({std::move(samples), std::move(words), std::move(answer)});
    wake_.notify_all();
#endif
  }

 private:
  struct Job {
    std::vector<float> samples;
    std::vector<std::string> words;
    Answer answer;
  };
  void run() {
    for (;;) {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [&] { return stopping_ || reload_ || !jobs_.empty(); });
      if (stopping_) return;
      if (reload_) {
        reload_ = false;
        lock.unlock();
        int rate = load();
        dispatcher_.schedule([this, rate] { sampleRate_ = rate; });
        continue;
      }
      Job job = std::move(jobs_.front());
      jobs_.erase(jobs_.begin());
      lock.unlock();
      std::string text;
#ifdef PROJECTKEY_ADVANCED_MODEL
      if (model_) text = model_->transcribe(job.samples, job.words);
#endif
      dispatcher_.schedule([answer = std::move(job.answer), text = std::move(text)] { answer(text); });
    }
  }
  // The model's sample rate, 0 for none.
  int load() {
#ifdef PROJECTKEY_ADVANCED_MODEL
    model_.reset();
    std::filesystem::path dir = NewestDownload(SettingsRows::DownloadedSpeech());
    if (dir.empty()) return 0;
    auto model = std::make_unique<SpeechModel>();
    int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 8);
    if (!model->load(dir.string(), threads)) {
      FCITX_ERROR() << "Project Key: cannot load the speech model in " << dir;
      return 0;
    }
    model_ = std::move(model);
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(SettingsRows::DownloadedSpeech(), error)) {
      if (entry.path() != dir && entry.path().extension() != ".partial") std::filesystem::remove_all(entry.path(), error);
    }
    return model_->sampleRate();
#else
    return 0;
#endif
  }

  fcitx::EventDispatcher dispatcher_;
  int sampleRate_ = 0;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false, reload_ = false;
  std::vector<Job> jobs_;
#ifdef PROJECTKEY_ADVANCED_MODEL
  Microphone microphone_;                  // Fcitx5's thread
  std::unique_ptr<SpeechModel> model_;  // the thread's
#endif
  std::thread thread_;  // last: it runs on the members above
};

// What the settings windows (Settings/Qt.cpp, Settings/Gtk.cpp) ask of the engine over D-Bus,
// at /projectkey on Fcitx5's own bus name: forgetting the choices, which live
// only in this process, switching to models they downloaded, and every setting's default.
class SettingsObject : public fcitx::dbus::ObjectVTable<SettingsObject> {
 public:
  SettingsObject(Environment& environment, Config& config, std::function<void()> reloadAdvanced)
      : environment_(environment), config_(config), reloadAdvanced_(std::move(reloadAdvanced)) {}
  void clearChoices() { ClearChoices(environment_); }
  void resetSettings() {
    fcitx::RawConfig defaults;
    Config().save(defaults);
    config_.load(defaults);
    fcitx::safeSaveAsIni(config_, kConfigPath);
  }
  // Also after the advanced model was downloaded or removed.
  void reloadModels() {
    if (!UseNewestModels(environment_)) FCITX_ERROR() << "Project Key: cannot load the downloaded models";
    reloadAdvanced_();
  }

 private:
  Environment& environment_;
  Config& config_;
  std::function<void()> reloadAdvanced_;
  FCITX_OBJECT_VTABLE_METHOD(clearChoices, "ClearChoices", "", "");
  FCITX_OBJECT_VTABLE_METHOD(resetSettings, "ResetSettings", "", "");
  FCITX_OBJECT_VTABLE_METHOD(reloadModels, "ReloadModels", "", "");
};

class Engine : public fcitx::InputMethodEngineV2 {
  // Defined before its use in the constructor, which needs its deduced type.
  FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());
  FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());

 public:
  explicit Engine(fcitx::Instance* instance)
      : instance_(instance), settings_(config_), panel_(instance, settings_), factory_([this](fcitx::InputContext& ic) {
          return new Session(environment_, instance_, panel_, ic);
        }) {
    fcitx::readAsIni(config_, kConfigPath);
    // The installed models, or newer ones the settings windows downloaded.
    if (!UseNewestModels(environment_)) FCITX_ERROR() << "Project Key: cannot load " << PROJECTKEY_DATA_DIR << "/data.txt";
    environment_.userModel = NewUserModel();
    // What the user typed, learned on this computer only.
    if (std::filesystem::path data = SettingsRows::UserData(); !data.empty()) {
      environment_.personalPath = (data / "personal.txt").string();
      environment_.userPhrasesPath = (data / "phrases.txt").string();  // edited from the settings windows
    }
    environment_.personal = std::make_shared<PersonalModel>();
    if (!environment_.personalPath.empty()) environment_.personal->load(environment_.personalPath);
    environment_.settings = &settings_;
    environment_.isEnglish = [this](const std::string& text) { return spelling_.isEnglish(text); };
    environment_.hintWindow = true;  // beside the candidate window, as on macOS and Windows (Session::showHint)
    environment_.columnsSpannedBy = [this](const std::string& candidate) {
      return gActive && gActive->ownPanel() ? panel_.span(candidate) : Span(candidate);
    };
    environment_.appTypesUnhandledKeys = true;  // a key the engine does not filter reaches the app
    instance_->inputContextManager().registerProperty("projectkey", &factory_);
    panel_.clicked = [this](fcitx::InputContext& ic, const Metrics::PanelHit& hit) {
      Session* session = ic.propertyFor(&factory_);
      Active active(session);
      if (hit.kind == Metrics::PanelHit::kChevron) {
        session->controller.toggleGrid();
      } else {
        session->controller.clickCandidate(hit.row, hit.index);
      }
    };
    // The settings window: from the Fcitx5 menu, as from the macOS input menu and the Windows language bar.
    settingsAction_.setShortText("Project Key 設定…");
    settingsAction_.connect<fcitx::SimpleAction::Activated>([](fcitx::InputContext*) { fcitx::startProcess({PROJECTKEY_SETTINGS}); });
    instance_->userInterfaceManager().registerAction("projectkey-settings", &settingsAction_);
    // The input method menu's switches, ticked as they are (syncMenu).
    simplifiedAction_.setShortText("簡體中文");
    simplifiedAction_.setCheckable(true);
    simplifiedAction_.connect<fcitx::SimpleAction::Activated>([this](fcitx::InputContext* ic) {
      if (!ic || instance_->inputMethodEngine(ic) != this) return;
      Session* session = ic->propertyFor(&factory_);
      Active active(session);
      session->controller.toggleSimplified();
      syncMenu(ic);
    });
    instance_->userInterfaceManager().registerAction("projectkey-simplified", &simplifiedAction_);
    fullWidthAction_.setShortText("全形");
    fullWidthAction_.setCheckable(true);
    fullWidthAction_.connect<fcitx::SimpleAction::Activated>([this](fcitx::InputContext* ic) {
      if (!ic || instance_->inputMethodEngine(ic) != this) return;
      Session* session = ic->propertyFor(&factory_);
      Active active(session);
      session->controller.toggleFullWidth();
      syncMenu(ic);
    });
    instance_->userInterfaceManager().registerAction("projectkey-fullwidth", &fullWidthAction_);
    // A newer release, found in the background (checkForUpdate), offered first in the menu; the settings
    // window's 關於 updates to it.
    updateAction_.connect<fcitx::SimpleAction::Activated>([](fcitx::InputContext*) { fcitx::startProcess({PROJECTKEY_SETTINGS}); });
    instance_->userInterfaceManager().registerAction("projectkey-update", &updateAction_);
    // Not at once: Fcitx5 starts with the session, often before the network is up.
    updateTimer_ = instance_->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + kFirstUpdateCheckUs, 0, [this](fcitx::EventSourceTime* timer, uint64_t) {
          checkForUpdate();
          timer->setNextInterval(std::chrono::duration_cast<std::chrono::microseconds>(kUpdateCheckInterval).count());
          timer->setOneShot();
          return true;
        });
    checkAction_.setShortText("檢查 Project Key 安裝");
    checkAction_.connect<fcitx::SimpleAction::Activated>([this](fcitx::InputContext* ic) { checkInstall(ic); });
    instance_->userInterfaceManager().registerAction("projectkey-check", &checkAction_);
    if (fcitx::AddonInstance* dbus = this->dbus()) {
      dbus->call<fcitx::IDBusModule::bus>()->addObjectVTable("/projectkey", "org.projectkey.Engine", settingsObject_);
    }
    // Fcitx5 takes Left Shift to switch input methods before an engine sees it;
    // while Project Key is on and Shift switches 中／英, Shift is its instead.
    shiftWatcher_ = instance_->watchEvent(
        fcitx::EventType::InputContextKeyEvent, fcitx::EventWatcherPhase::PreInputMethod, [this](fcitx::Event& e) {
          auto& event = static_cast<fcitx::KeyEvent&>(e);
          fcitx::KeySym sym = event.rawKey().sym();
          if ((sym != FcitxKey_Shift_L && sym != FcitxKey_Shift_R) || instance_->inputMethodEngine(event.inputContext()) != this ||
              !settings_.flag(Setting::kShiftToggle)) {
            return;
          }
          Session* session = event.inputContext()->propertyFor(&factory_);
          Active active(session);
          session->controller.handleShift(!event.isRelease());
          event.filter();  // the app still gets the key
        });
    caretWatcher_ = instance_->watchEvent(
        fcitx::EventType::InputContextCursorRectChanged, fcitx::EventWatcherPhase::Default, [this](fcitx::Event& e) {
          fcitx::InputContext* ic = static_cast<fcitx::InputContextEvent&>(e).inputContext();
          if (instance_->inputMethodEngine(ic) == this) ic->propertyFor(&factory_)->caretMoved();
        });
    // Fcitx5 announces the input method switched to in its own panel, which its theme draws away from
    // the caret on GNOME. Switching to or from Project Key where its window reaches, the announcement
    // is taken out before that panel is drawn (Fcitx5 sets it after this phase, in the same dispatch,
    // and draws later): the 中／英 badge shows Project Key (activate), and shows the text of any other.
    switchWatcher_ = instance_->watchEvent(
        fcitx::EventType::InputContextSwitchInputMethod, fcitx::EventWatcherPhase::PostInputMethod, [this](fcitx::Event& e) {
          auto& event = static_cast<fcitx::InputContextSwitchInputMethodEvent&>(e);
          fcitx::InputContext* ic = event.inputContext();
          bool to = instance_->inputMethodEngine(ic) == this;
          if ((!to && event.oldInputMethod() != SettingsRows::kInputMethod) || !panel_.shows(*ic)) return;
          announcing_ = ic;
          leaving_ = !to;
          announced_ = instance_->eventLoop().addDeferEvent([this](fcitx::EventSource*) {
            announcing_ = nullptr;
            announced_.reset();
            return true;
          });
        });
    reloadAdvanced();
    announcementWatcher_ = instance_->watchEvent(
        fcitx::EventType::InputContextUpdateUI, fcitx::EventWatcherPhase::PreInputMethod, [this](fcitx::Event& e) {
          auto& event = static_cast<fcitx::InputContextUpdateUIEvent&>(e);
          fcitx::InputContext* ic = event.inputContext();
          if (ic != announcing_ || event.component() != fcitx::UserInterfaceComponent::InputPanel) return;
          announcing_ = nullptr;
          std::string text = TakeAnnouncement(ic->inputPanel());
          if (leaving_ && !text.empty()) ic->propertyFor(&factory_)->showIndicator(text);
        });
  }

  void keyEvent(const fcitx::InputMethodEntry&, fcitx::KeyEvent& event) override {
    Session* session = event.inputContext()->propertyFor(&factory_);
    Active active(session);
    Controller& controller = session->controller;
    fcitx::Key key = event.rawKey();
    fcitx::KeySym sym = key.sym();
    if (!event.isRelease() && settings_.flag(Setting::kSimplifiedToggle) && event.key().check(*config_.simplifiedKey)) {
      controller.toggleSimplified();
      syncMenu(event.inputContext());
      event.filterAndAccept();
      return;
    }
    // While dictation is on and has its model, its key is Project Key's, pressed and released.
    if (sym == config_.dictationKey->sym() && dictation_.loaded() && settings_.flag(Setting::kDictation)) {
      dictationKey(event, controller);
      event.filterAndAccept();
      return;
    }
    switch (sym) {
      case FcitxKey_Shift_L:
      case FcitxKey_Shift_R:
        controller.handleShift(!event.isRelease());
        return;
      case FcitxKey_Caps_Lock:
        if (!event.isRelease()) controller.handleCapsLock();
        return;
      case FcitxKey_Control_L:
      case FcitxKey_Control_R:
      case FcitxKey_Alt_L:
      case FcitxKey_Alt_R:
      case FcitxKey_Super_L:
      case FcitxKey_Super_R:
      case FcitxKey_Meta_L:
      case FcitxKey_Meta_R:
      case FcitxKey_ISO_Level3_Shift:
        if (!event.isRelease()) controller.handleOtherModifier();
        return;
      default:
        break;
    }
    if (event.isRelease()) return;

    KeyEvent k;
    fcitx::KeyStates states = key.states();
    k.shift = states.test(fcitx::KeyState::Shift);
    k.control = states.test(fcitx::KeyState::Ctrl);
    k.alt = states.test(fcitx::KeyState::Alt);
    k.win = states.test(fcitx::KeyState::Super);
    k.capsLock = states.test(fcitx::KeyState::CapsLock);
    k.isP = sym == FcitxKey_p || sym == FcitxKey_P;
    k.numpad = sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9;
    switch (sym) {
      case FcitxKey_space: k.key = Key::kSpace; k.characters = " "; break;
      case FcitxKey_Return:
      case FcitxKey_KP_Enter: k.key = Key::kReturn; break;
      case FcitxKey_Tab:
      case FcitxKey_ISO_Left_Tab: k.key = Key::kTab; break;
      case FcitxKey_BackSpace: k.key = Key::kBackspace; break;
      case FcitxKey_Escape: k.key = Key::kEscape; break;
      case FcitxKey_Left:
      case FcitxKey_KP_Left: k.key = Key::kLeft; break;
      case FcitxKey_Right:
      case FcitxKey_KP_Right: k.key = Key::kRight; break;
      case FcitxKey_Up:
      case FcitxKey_KP_Up: k.key = Key::kUp; break;
      case FcitxKey_Down:
      case FcitxKey_KP_Down: k.key = Key::kDown; break;
      case FcitxKey_Home:
      case FcitxKey_KP_Home: k.key = Key::kHome; break;
      case FcitxKey_End:
      case FcitxKey_KP_End: k.key = Key::kEnd; break;
      case FcitxKey_Page_Up:
      case FcitxKey_KP_Page_Up: k.key = Key::kPageUp; break;
      case FcitxKey_Page_Down:
      case FcitxKey_KP_Page_Down: k.key = Key::kPageDown; break;
      case FcitxKey_Delete:
      case FcitxKey_KP_Delete: k.key = Key::kForwardDelete; break;
      default: k.characters = fcitx::Key::keySymToUTF8(sym); break;  // the layout has applied Shift and Caps Lock
    }
    if (controller.handleKey(k)) event.filterAndAccept();
    if (environment_.fullWidth != fullWidthAction_.isChecked(nullptr)) syncMenu(event.inputContext());  // Shift+Space
  }

  // Focus left the field, or the app reset it (a click in the text): the app
  // keeps the preedit as text itself (Qt does), so the engine only forgets it;
  // committing too would type it twice. Any other reset (switching input
  // methods) commits it.
  void reset(const fcitx::InputMethodEntry&, fcitx::InputContextEvent& event) override {
    Session* session = event.inputContext()->propertyFor(&factory_);
    if (event.type() == fcitx::EventType::InputContextFocusOut || event.type() == fcitx::EventType::InputContextReset) {
      session->controller.reset();
      session->clearPreedit();
    } else {
      session->controller.deactivate();
    }
  }

  void activate(const fcitx::InputMethodEntry&, fcitx::InputContextEvent& event) override {
    if (!newerRelease_.empty() && settings_.flag(Setting::kCheckForUpdates)) {
      event.inputContext()->statusArea().addAction(fcitx::StatusGroup::InputMethod, &updateAction_);
    }
    event.inputContext()->statusArea().addAction(fcitx::StatusGroup::InputMethod, &simplifiedAction_);
    event.inputContext()->statusArea().addAction(fcitx::StatusGroup::InputMethod, &fullWidthAction_);
    event.inputContext()->statusArea().addAction(fcitx::StatusGroup::InputMethod, &settingsAction_);
    event.inputContext()->statusArea().addAction(fcitx::StatusGroup::InputMethod, &checkAction_);
    Session* session = event.inputContext()->propertyFor(&factory_);
    Active active(session);
    session->controller.showMode();
    syncMenu(event.inputContext());
  }

  // COSMIC's panel tints a tray icon with its text color, which turns the colored icon into
  // a blank square: there it gets the one-color icon with the ㄅ cut out.
  std::string subModeIconImpl(const fcitx::InputMethodEntry&, fcitx::InputContext&) override {
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    return desktop && std::strstr(desktop, "COSMIC") ? "fcitx-projectkey-symbolic" : "";
  }

  std::string subMode(const fcitx::InputMethodEntry&, fcitx::InputContext&) override {
    return environment_.englishMode ? "英" : "中";
  }
  std::string subModeLabelImpl(const fcitx::InputMethodEntry&, fcitx::InputContext&) override {
    return ProjectKey::ModeLabel(environment_);
  }

  const fcitx::Configuration* getConfig() const override { return &config_; }
  void setConfig(const fcitx::RawConfig& raw) override {
    config_.load(raw, true);
    fcitx::safeSaveAsIni(config_, kConfigPath);
  }
  void reloadConfig() override { fcitx::readAsIni(config_, kConfigPath); }

 private:
  void syncMenu(fcitx::InputContext* ic) {
    simplifiedAction_.setChecked(settings_.flag(Setting::kSimplified));
    fullWidthAction_.setChecked(environment_.fullWidth);
    simplifiedAction_.update(ic);
    fullWidthAction_.update(ic);
  }
  void checkForUpdate() {
    if (!settings_.flag(Setting::kCheckForUpdates)) return;
    updates_.run([this](const std::string& version) {
      newerRelease_ = version;
      updateAction_.setShortText("更新到 " + version);
      // The menu of the app typed in now; others get the item as Project Key next turns on in them.
      fcitx::InputContext* ic = instance_->mostRecentInputContext();
      if (!ic || instance_->inputMethodEngine(ic) != this || !settings_.flag(Setting::kCheckForUpdates)) return;
      ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, &updateAction_);
      ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
    });
  }
  void reloadAdvanced() {
    dictation_.reload();
  }
  // The dictation key pressed or released: records while it is held, then types what was said in the
  // field it was said in.
  void dictationKey(fcitx::KeyEvent& event, Controller& controller) {
    if (!event.isRelease()) {
      if (controller.dictationKey(true) && !dictation_.record()) {
        controller.handleOtherModifier();  // takes the recording badge away
        notify("沒有可用的麥克風", "語音輸入需要麥克風。");
      }
      return;
    }
    if (!controller.dictationKey(false)) return dictation_.finish({}, nullptr);
    dictation_.finish(DictationWords(environment_), [this, ref = event.inputContext()->watch()](const std::string& text) {
      fcitx::InputContext* ic = ref.get();
      if (!ic || instance_->inputMethodEngine(ic) != this) return;
      Session* session = ic->propertyFor(&factory_);
      Active active(session);
      session->controller.insertDictation(text);
    });
  }
  void notify(const std::string& summary, const std::string& body) {
    if (fcitx::AddonInstance* notifications = this->notifications()) {
      notifications->call<fcitx::INotifications::sendNotification>("Project Key", 0, "fcitx-projectkey", summary, body,
                                                                   std::vector<std::string>{}, -1, nullptr, nullptr);
    }
  }

  // The Fcitx5 menu's 檢查 Project Key 安裝: what an install needs, as a notification. ic is
  // the app the menu was opened for, whose candidates may go to Fcitx5's panel instead.
  void checkInstall(fcitx::InputContext* ic) {
    std::vector<std::string> problems;
    if (!environment_.lm || !environment_.lm->isDataModelLoaded()) {
      problems.push_back(std::string("詞庫沒有載入（") + PROJECTKEY_DATA_DIR + "/data.txt）：請重新安裝");
    }
    if (!spelling_.ok()) problems.push_back("沒有英文字典（Debian／Ubuntu 的 hunspell-en-us）：不會出現 Tab 英文提示");
    if (access(PROJECTKEY_SETTINGS, X_OK) != 0) problems.push_back("找不到設定程式 " + std::string(PROJECTKEY_SETTINGS));
    for (std::string& problem : panel_.problems()) problems.push_back(std::move(problem));
    std::string notes;
    if (ic && panel_.ok() && !panel_.shows(*ic)) {
      notes = "這個 App 的候選字用 Fcitx5 的候選面板。";
      fcitx::RawConfig classicUi;
      fcitx::readAsIni(classicUi, "conf/classicui.conf");
      const std::string* theme = classicUi.valueByPath("Theme");
      if (!theme || *theme != "projectkey") {
        problems.push_back("Fcitx5 的主題不是 Project Key：在 Fcitx5 設定的「附加元件 → 經典使用者介面 → 主題」選 Project Key");
      }
    }
    std::string summary = problems.empty() ? "Project Key 已安裝完成" : "Project Key 安裝有 " + std::to_string(problems.size()) + " 個問題";
    std::string body;
    for (const std::string& problem : problems) body += "• " + problem + "\n";
    if (problems.empty()) body = "詞庫、英文字典、字型、候選字窗與設定程式都正常。\n";
    body += notes;
    FCITX_INFO() << summary << "\n" << body;
    if (fcitx::AddonInstance* notifications = this->notifications()) {
      notifications->call<fcitx::INotifications::sendNotification>("Project Key", 0, "fcitx-projectkey", summary, body,
                                                                   std::vector<std::string>{}, -1, nullptr, nullptr);
    }
  }

  fcitx::Instance* instance_;
  Config config_;
  ConfigSettings settings_;
  Spelling spelling_;
  Panel panel_;  // before the sessions that show it
  Environment environment_;
  fcitx::FactoryFor<Session> factory_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> shiftWatcher_, caretWatcher_, switchWatcher_, announcementWatcher_;
  fcitx::InputContext* announcing_ = nullptr;  // switched to or from Project Key, until Fcitx5 announces it
  bool leaving_ = false;                        // from it
  std::unique_ptr<fcitx::EventSource> announced_;
  fcitx::SimpleAction settingsAction_, checkAction_, simplifiedAction_, fullWidthAction_, updateAction_;
  std::string newerRelease_;  // the version updateAction_ offers, once found
  std::unique_ptr<fcitx::EventSourceTime> updateTimer_;
  Dictation dictation_{instance_->eventLoop()};
  UpdateCheck updates_{instance_->eventLoop()};
  SettingsObject settingsObject_{environment_, config_, [this] { reloadAdvanced(); }};
};

class Factory : public fcitx::AddonFactory {
  fcitx::AddonInstance* create(fcitx::AddonManager* manager) override { return new Engine(manager->instance()); }
};

}  // namespace ProjectKey

FCITX_ADDON_FACTORY(ProjectKey::Factory);
