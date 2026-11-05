// The Fcitx5 addon: each input context gets a Controller (core/engine), whose
// composition becomes the preedit and whose candidates and proof row go into
// Fcitx5's own input panel. Fcitx5 draws the panel, so its look follows the
// user's Fcitx5 theme rather than the macOS and Windows candidate window.
#include <enchant.h>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "Controller.h"

namespace ProjectKey {

namespace {

constexpr char kConfigPath[] = "conf/projectkey.conf";

enum class ProofMode { Always, WhenDoubtful, Off };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(ProofMode, N_("永遠"), N_("有可疑字時"), N_("關閉"));
enum class CandidatePosition { AfterCursor, BeforeCursor };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(CandidatePosition, N_("游標後"), N_("游標前"));

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
    fcitx::Option<bool> nextDoubtful{this, "NextDoubtful", "選完跳到下一個可疑處", DefaultFlag(Setting::kNextDoubtful)};
    fcitx::Option<bool> adjacentKeys{this, "AdjacentKeys", "鄰鍵修正", DefaultFlag(Setting::kAdjacentKeys)};
    fcitx::Option<bool> rememberChoices{this, "RememberChoices", "記住選過的字", DefaultFlag(Setting::kRememberChoices)};
    fcitx::OptionWithAnnotation<ProofMode, ProofModeI18NAnnotation> proofPreview{
        this, "ProofPreview", "輸入時的整句預覽", static_cast<ProofMode>(Settings::DefaultValue(Setting::kProofPreview))};
    fcitx::OptionWithAnnotation<ProofMode, ProofModeI18NAnnotation> choosingProof{
        this, "ChoosingProof", "選字時的整句預覽", static_cast<ProofMode>(Settings::DefaultValue(Setting::kChoosingProof))};
    fcitx::Option<bool> capsLockToggle{this, "CapsLockToggle", "Caps Lock 切換中英", DefaultFlag(Setting::kCapsLockToggle)};
    fcitx::Option<bool> shiftToggle{this, "ShiftToggle", "單按 Shift 切換中英", DefaultFlag(Setting::kShiftToggle)};
    fcitx::Option<bool> englishToZhuyin{this, "EnglishToZhuyin", "Tab 英文轉注音", DefaultFlag(Setting::kEnglishToZhuyin)};
    fcitx::Option<bool> zhuyinToEnglish{this, "ZhuyinToEnglish", "Tab 注音轉英文", DefaultFlag(Setting::kZhuyinToEnglish)};
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
      case Setting::kAdjacentKeys: return *config_.adjacentKeys;
      case Setting::kRememberChoices: return *config_.rememberChoices;
      case Setting::kShiftToggle: return *config_.shiftToggle;
      case Setting::kCapsLockToggle: return *config_.capsLockToggle;
      case Setting::kEnglishToZhuyin: return *config_.englishToZhuyin;
      case Setting::kZhuyinToEnglish: return *config_.zhuyinToEnglish;
      case Setting::kCandidatePosition: return static_cast<int>(*config_.candidatePosition);
      case Setting::kNextDoubtful: return *config_.nextDoubtful;
      case Setting::kAppearance: return std::nullopt;  // Fcitx5's theme draws the panel
    }
    return std::nullopt;
  }

  // The engine sets only the proof modes, from Ctrl+Shift+P.
  void set(Setting setting, int value) override {
    switch (setting) {
      case Setting::kProofPreview: config_.proofPreview.setValue(static_cast<ProofMode>(value)); break;
      case Setting::kChoosingProof: config_.choosingProof.setValue(static_cast<ProofMode>(value)); break;
      case Setting::kProofPreviewLastOn: config_.proofPreviewLastOn.setValue(value); break;
      case Setting::kChoosingProofLastOn: config_.choosingProofLastOn.setValue(value); break;
      default: return;
    }
    fcitx::safeSaveAsIni(config_, kConfigPath);
  }

 private:
  static std::optional<int> LastOn(int value) { return value < 0 ? std::nullopt : std::optional<int>(value); }
  Config& config_;
};

// Bytes of `text` that hold its first `units` UTF-16 code units: the engine
// counts in UTF-16, as TSF does, and Fcitx5 in UTF-8 bytes.
size_t BytesFor(const std::string& text, uint32_t units) {
  size_t i = 0;
  for (uint32_t n = 0; i < text.size() && n < units;) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : 4;
    n += length == 4 ? 2 : 1;
    i += length;
  }
  return std::min(i, text.size());
}

// Grid columns a candidate takes: two Han characters to a column, as the
// candidate window on the other platforms packs them.
int Span(const std::string& candidate) {
  double width = 0;
  for (unsigned char c : candidate) {
    if (c < 0x80) width += 0.5;
    else if ((c & 0xc0) != 0x80) width += 1;
  }
  return std::clamp(static_cast<int>((width + 1.9) / 2), 1, kGridColumns);
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
  bool isEnglish(const std::string& text) const {
    if (!dict_) return false;
    bool any = false;
    for (size_t start = 0; start <= text.size();) {
      size_t end = std::min(text.find(' ', start), text.size());
      if (end > start) {
        if (enchant_dict_check(dict_, text.data() + start, static_cast<ssize_t>(end - start)) != 0) return false;
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

}  // namespace

// One input context's composition: what the Controller does to the app goes
// into the context's preedit, commit and input panel.
class Session : public fcitx::InputContextProperty, public Client {
 public:
  Session(Environment& environment, fcitx::Instance* instance, fcitx::InputContext& ic)
      : instance_(instance), ic_(ic), controller(environment, *this) {}

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
    bool inApp = ic_.capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
    fcitx::TextFormatFlags normal = inApp ? fcitx::TextFormatFlag::Underline : fcitx::TextFormatFlag::NoFlag;
    const std::string& text = composition.text;
    size_t activeStart = BytesFor(text, composition.activeStart);
    size_t activeEnd = BytesFor(text, composition.activeStart + composition.activeLength);
    fcitx::Text preedit;
    preedit.append(text.substr(0, activeStart), normal);
    if (activeEnd > activeStart) preedit.append(text.substr(activeStart, activeEnd - activeStart), fcitx::TextFormatFlag::HighLight);
    preedit.append(text.substr(activeEnd), normal);
    preedit.setCursor(static_cast<int>(BytesFor(text, composition.caret)));
    if (inApp) {
      ic_.inputPanel().setClientPreedit(preedit);
    } else {
      ic_.inputPanel().setPreedit(preedit);
    }
    ic_.updatePreedit();
    ic_.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  }

  // The proof row goes above the candidates, the selection's row of the grid
  // into the candidate list, and the page label below it.
  void showPanel(const PanelContent& content) override {
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
        fcitx::TextFormatFlags format = static_cast<int>(i) == content.activeSegment ? fcitx::TextFormatFlag::HighLight
                                        : i < content.doubtful.size() && content.doubtful[i] ? fcitx::TextFormatFlag::Underline
                                                                                              : fcitx::TextFormatFlag::NoFlag;
        uint32_t length = Utf16Length(segment);
        if (content.caret == at) reading();
        if (content.caret > at && content.caret < at + length) {
          size_t split = BytesFor(segment, content.caret - at);
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
    panel.setAuxUp(aux);

    if (content.rows.empty()) {
      panel.setCandidateList(nullptr);
      panel.setAuxDown(fcitx::Text());
    } else {
      const std::vector<std::string>& row = content.rows[content.selectedRow];
      auto list = std::make_unique<fcitx::CommonCandidateList>();
      list->setLayoutHint(fcitx::CandidateLayoutHint::Horizontal);
      list->setPageSize(static_cast<int>(row.size()));
      std::vector<std::string> labels;
      for (size_t i = 0; i < row.size(); i++) {
        labels.push_back(std::to_string(i + 1) + " ");
        list->append<Word>(this, row, static_cast<int>(i));
      }
      list->setLabels(labels);
      list->setGlobalCursorIndex(content.selection);
      panel.setCandidateList(std::move(list));
      panel.setAuxDown(fcitx::Text(content.pageLabel));
    }
    ic_.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  }

  void hidePanel() override {
    fcitx::InputPanel& panel = ic_.inputPanel();
    panel.setAuxUp(fcitx::Text());
    panel.setAuxDown(fcitx::Text());
    panel.setCandidateList(nullptr);
    ic_.updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  }

  // Fcitx5's own input method popup stands in for the 中／英 badge; it shows
  // the engine's subMode, which reads the mode the label names.
  void showIndicator(const std::string&) override {
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
    ic_.inputPanel().setClientPreedit(fcitx::Text());
    ic_.inputPanel().setPreedit(fcitx::Text());
    ic_.updatePreedit();
  }

 private:
  // A candidate clicked in the panel: its row's cells sit together in the
  // controller's candidates, so the row's place there gives the index to pick.
  class Word : public fcitx::CandidateWord {
   public:
    Word(Session* session, const std::vector<std::string>& row, int index)
        : fcitx::CandidateWord(fcitx::Text(row[index])), session_(session), row_(row), index_(index) {}
    void select(fcitx::InputContext*) const override {
      const std::vector<std::string>& all = session_->controller.candidates();
      auto found = std::search(all.begin(), all.end(), row_.begin(), row_.end());
      if (found != all.end()) session_->controller.choose(static_cast<int>(found - all.begin()) + index_);
    }

   private:
    Session* session_;
    std::vector<std::string> row_;
    int index_;
  };

  fcitx::Instance* instance_;
  fcitx::InputContext& ic_;

 public:
  Controller controller;  // after the members it calls back into
};

class Engine : public fcitx::InputMethodEngineV2 {
 public:
  explicit Engine(fcitx::Instance* instance)
      : instance_(instance), settings_(config_), factory_([this](fcitx::InputContext& ic) {
          return new Session(environment_, instance_, ic);
        }) {
    fcitx::readAsIni(config_, kConfigPath);
    std::string dir = PROJECTKEY_DATA_DIR;
    environment_.lm = std::make_shared<Lexicon>();
    environment_.lm->loadLanguageModel((dir + "/data.txt").c_str());
    if (!environment_.lm->isDataModelLoaded()) FCITX_ERROR() << "Project Key: cannot load " << dir << "/data.txt";
    // The context ranking model from tools/bigram-trainer; without it, unigrams only.
    auto bigram = std::make_shared<Bigram>();
    if (bigram->load(dir + "/bigram.bin")) environment_.bigram = bigram;
    // Choices the user made, shared by every context; in memory only, as on the other platforms.
    environment_.userModel = std::make_shared<McBopomofo::UserOverrideModel>(500, 5400.0);
    environment_.settings = &settings_;
    environment_.isEnglish = [this](const std::string& text) { return spelling_.isEnglish(text); };
    environment_.columnsSpannedBy = Span;
    environment_.appTypesUnhandledKeys = true;  // a key the engine does not filter reaches the app
    instance_->inputContextManager().registerProperty("projectkey", &factory_);
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
          event.inputContext()->propertyFor(&factory_)->controller.handleShift(!event.isRelease());
          event.filter();  // the app still gets the key
        });
  }

  void keyEvent(const fcitx::InputMethodEntry&, fcitx::KeyEvent& event) override {
    Session* session = event.inputContext()->propertyFor(&factory_);
    Controller& controller = session->controller;
    fcitx::Key key = event.rawKey();
    fcitx::KeySym sym = key.sym();
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
  }

  // Focus left the field, or the app reset it: drop the composition, as apps
  // differ in what they do with a preedit then. Any other reset (switching
  // input methods) commits it.
  void reset(const fcitx::InputMethodEntry&, fcitx::InputContextEvent& event) override {
    Session* session = event.inputContext()->propertyFor(&factory_);
    if (event.type() == fcitx::EventType::InputContextFocusOut || event.type() == fcitx::EventType::InputContextReset) {
      session->controller.reset();
      session->clearPreedit();
    } else {
      session->controller.deactivate();
    }
  }

  std::string subMode(const fcitx::InputMethodEntry&, fcitx::InputContext&) override {
    return environment_.englishMode ? "英" : "中";
  }
  std::string subModeLabelImpl(const fcitx::InputMethodEntry&, fcitx::InputContext&) override {
    return environment_.englishMode ? "A" : "ㄅ";
  }

  const fcitx::Configuration* getConfig() const override { return &config_; }
  void setConfig(const fcitx::RawConfig& raw) override {
    config_.load(raw, true);
    fcitx::safeSaveAsIni(config_, kConfigPath);
  }
  void reloadConfig() override { fcitx::readAsIni(config_, kConfigPath); }

 private:
  fcitx::Instance* instance_;
  Config config_;
  ConfigSettings settings_;
  Spelling spelling_;
  Environment environment_;
  fcitx::FactoryFor<Session> factory_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> shiftWatcher_;
};

class Factory : public fcitx::AddonFactory {
  fcitx::AddonInstance* create(fcitx::AddonManager* manager) override { return new Engine(manager->instance()); }
};

}  // namespace ProjectKey

FCITX_ADDON_FACTORY(ProjectKey::Factory);
